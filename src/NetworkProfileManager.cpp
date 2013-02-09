#include "global.h"

#include "NetworkProfileManager.h"

#include "RageLog.h"
#include "MessageManager.h"
#include "Foreach.h"
#include "RageUtil_WorkerThread.h"
#include "LuaManager.h"
#include "ThemeManager.h"
#include "ScreenManager.h"
#include "XmlFile.h"
#include "Preference.h"
#include "RageThreads.h"
#include "io/Genesis.h"

// libcurl
#include <curl/curl.h>
#include <curl/easy.h>

#include <queue>
#include <sstream>

static Preference<CString> g_sProfileServerURL( "ProfileServerURL", "10.0.1.39" );

static void NPMLog( CString message )
{
	LOG->Info( (CString("*** NETPROFMAN: ") += message) );
}

NetworkProfileManager *NETPROFMAN = NULL;

typedef enum {
	ProfileDataTypeGeneral,
	ProfileDataTypeStats,
		
	ProfileDataTypeSongScores,
	ProfileDataTypeCourseScores,
	ProfileDataTypeCategoryScores,
		
	ProfileDataTypeScreenshotData,
	ProfileDataTypeCalorieData,
		
	ProfileDataTypeRecentSongScores,
	ProfileDataTypeRecentCourseScores,
} ProfileDataType;

typedef enum {
	NetRequestTypeDownloadProfile,
	NetRequestTypeUploadProfile
} NetworkingRequestType;

typedef enum {
	NetResponseTypeProfile,
	NetResponseTypeError
} NetworkingResponseType;

class NetworkingRequest
{
public:
	NetworkingRequest( NetworkingRequestType type, PlayerNumber pn, void *requestData ) :
		m_eRequestType( type ), m_pn( pn ), m_pRequestData( requestData ) {};

	PlayerNumber GetPlayerNumber() const { return m_pn; };

	NetworkingRequestType GetRequestType() const { return m_eRequestType; };
	void SetRequestType( NetworkingRequestType t ) { m_eRequestType = t; };

	void* GetRequestData() const { return m_pRequestData; };
	void SetRequestData( void *requestData ) { m_pRequestData = requestData; };

	void* GetResponseData() const { return m_pResponseData; };
	NetworkingResponseType GetResponseType() const { return m_eResponseType; };

private:
	void SetResponseData( void *responseData ) { m_pResponseData = responseData; };
	void SetResponseType( NetworkingResponseType t ) { m_eResponseType = t; };

	NetworkingRequestType m_eRequestType;
	NetworkingResponseType m_eResponseType;

	PlayerNumber m_pn;

	void *m_pRequestData;
	void *m_pResponseData;

	friend class NetworkProfileManagerThreads::NetworkingThread;
};

namespace NetworkProfileManagerThreads {
	class NetworkingThread : public RageWorkerThread
	{
	public:
		NetworkingThread();
		~NetworkingThread();

		void AddNetworkRequest( const NetworkingRequest &request );
		bool GetCompletedNetworkRequests( std::queue<NetworkingRequest> &aOut );
		
	protected:
		void DoHeartbeat();
		void HandleRequest( int iRequest ) {};

	private:
		Profile* DownloadProfile( NetworkPass *pass );

		CURL *m_pCurlHandle;
		std::queue<NetworkingRequest> m_qPendingRequests;
		std::queue<NetworkingRequest> m_qCompletedRequests;

		RageMutex PendingRequestsMutex;
		RageMutex CompletedRequestsMutex;
	};


	class GenesisInputHandlerThread : public RageWorkerThread
	{
	public:
		GenesisInputHandlerThread();

		bool m_bPassesChanged;
		queue<NetworkPass *> m_qPassQueue;

	protected:
		void DoHeartbeat();
		void HandleRequest( int iRequest ) {};

		GenesisScanner *m_pDriver;
	};


	NetworkingThread::NetworkingThread() :
		RageWorkerThread( "NPM-NetworkingWorkerThread" ),
		PendingRequestsMutex( "PendingRequestsMutex" ),
		CompletedRequestsMutex( "CompletedRequestsMutex" )
	{
		m_pCurlHandle = curl_easy_init();

		SetHeartbeat( 0.1f );
		StartThread();
	}

	NetworkingThread::~NetworkingThread()
	{
		curl_easy_cleanup( m_pCurlHandle );
	}

	void NetworkingThread::AddNetworkRequest( const NetworkingRequest &request )
	{
		PendingRequestsMutex.Lock();
		m_qPendingRequests.push( request );
		PendingRequestsMutex.Unlock();
	}

	bool NetworkingThread::GetCompletedNetworkRequests( std::queue<NetworkingRequest> &aOut )
	{
		if ( m_qCompletedRequests.empty() )
			return false;

		CompletedRequestsMutex.Lock();

		// Copy, then clear
		aOut = m_qCompletedRequests;

		while ( !m_qCompletedRequests.empty() )
			m_qCompletedRequests.pop();

		CompletedRequestsMutex.Unlock();

		return true;
	}

	static size_t callback_write_data(void *buffer, size_t size, size_t num, void *userdata)
	{
		CString *pszXML = (CString *)userdata;
		(*pszXML) += CString( (const char *)buffer );
		return (size * num);
	}

	Profile* NetworkingThread::DownloadProfile( NetworkPass *pass )
	{
		std::stringstream ss;
		ss << "http://" << g_sProfileServerURL.Get();
		ss << "/smnetprof/get_stats.php?id=" << pass->m_sUniqueIdentifier;

		LOG->Warn("***** SS: %s", ss.str().c_str());

		CString resultString;
		curl_easy_setopt( m_pCurlHandle, CURLOPT_WRITEFUNCTION, callback_write_data );
		curl_easy_setopt( m_pCurlHandle, CURLOPT_WRITEDATA, &resultString );

		// get stats
		curl_easy_setopt( m_pCurlHandle, CURLOPT_URL, (ss.str() + "&type=stats").c_str() );
		CURLcode success = curl_easy_perform( m_pCurlHandle );

		// parse stats
		XNode xmlNode;
		PARSEINFO pi;
		xmlNode.Load( resultString, &pi );

		Profile *newProfile = new Profile();
		Profile::LoadResult result = newProfile->LoadStatsXmlFromNode( &xmlNode );

		// get editable
		resultString = "";
		curl_easy_setopt( m_pCurlHandle, CURLOPT_URL, (ss.str() + "&type=editable").c_str() );
		success = curl_easy_perform( m_pCurlHandle );

		// parse editable
		LOG->Warn("EDITABLE: %s", resultString.c_str());
		xmlNode.Load( resultString, &pi );

		if ( xmlNode.m_sName == "Editable" ) {
			int weight;
			CString displayName, highScoreName;

			xmlNode.GetChildValue( "DisplayName", displayName );
			xmlNode.GetChildValue( "LastUsedHighScoreName", highScoreName );
			xmlNode.GetChildValue( "WeightPounds", weight );

			LOG->Warn("****** NAME: %s", displayName.c_str());

			newProfile->m_sDisplayName = displayName;
			newProfile->m_sLastUsedHighScoreName = highScoreName;
			newProfile->m_iWeightPounds = weight;
		}

		return newProfile;
	}

	void NetworkingThread::DoHeartbeat()
	{
		if ( m_qPendingRequests.empty() )
			return;

		PendingRequestsMutex.Lock();
		while ( !m_qPendingRequests.empty() )
		{
			NetworkingRequest request = m_qPendingRequests.front();
			m_qPendingRequests.pop();

			if ( request.GetRequestType() == NetRequestTypeDownloadProfile )
			{
				NetworkPass *pass = (NetworkPass *)request.GetRequestData();

				Profile *profile = DownloadProfile( pass );
				request.SetResponseData( profile );
				request.SetResponseType( NetResponseTypeProfile );

				CompletedRequestsMutex.Lock();
				m_qCompletedRequests.push( request );
				CompletedRequestsMutex.Unlock();
			}
		}
		PendingRequestsMutex.Unlock();
	}


	/*** Genesis Input Handler ***/

	GenesisInputHandlerThread::GenesisInputHandlerThread() :
		RageWorkerThread( "GenesisInputHandlerThread" )
	{
		m_bPassesChanged = false;

		// initialize driver
		m_pDriver = new GenesisScanner();
		if ( !m_pDriver->Open() ) {
			LOG->Warn( "ERROR OPENING GENESIS SCANNER DEVICE! :(" );
			return;
		}

		SetHeartbeat( 0.1f );
		StartThread();
	}

	void GenesisInputHandlerThread::DoHeartbeat()
	{
		char *pDataIn;
		NetworkPass *newPass = NULL;
		while ( m_pDriver->Read( &pDataIn ) ) {
			CString passString = CString( pDataIn );

			LOG->Info( "*** GenesisInput: got new pass: %s", passString.c_str() );

			newPass = new NetworkPass( passString );
			m_bPassesChanged = true;
			m_qPassQueue.push( newPass );
		}
	}

}


NetworkProfileManager::NetworkProfileManager()
{
	FOREACH_PlayerNumber( p )
	{
		m_State[p] = NETWORK_PASS_ABSENT;
		m_Passes[p] = NULL;
	}

	m_pNetworkThread = new NetworkProfileManagerThreads::NetworkingThread();
	m_pInputHandler = new NetworkProfileManagerThreads::GenesisInputHandlerThread();

	// preload sounds
	m_soundReady.Load( THEME->GetPathS("MemoryCardManager", "ready"), true );
	m_soundDisconnect.Load( THEME->GetPathS("MemoryCardManager", "disconnect"), true );
}

void NetworkProfileManager::ProcessNetworkPasses()
{
	if ( !m_pInputHandler->m_bPassesChanged )
		return;

	NPMLog( "got pass from inputhandler" );

	while ( !m_pInputHandler->m_qPassQueue.empty() )
	{
		NetworkPass *newPass = m_pInputHandler->m_qPassQueue.front();
		m_pInputHandler->m_qPassQueue.pop();

		// Place pass into the first empty slot.
		FOREACH_PlayerNumber( p )
		{
			if ( m_Passes[p] == NULL )
			{
				m_Passes[p] = newPass;

				// Start downloading profile
				NetworkingRequest request( NetRequestTypeDownloadProfile, p, newPass );
				m_pNetworkThread->AddNetworkRequest( request );

				m_State[p] = NETWORK_PASS_DOWNLOADING;
				SCREENMAN->RefreshCreditsMessages();
				break;
			}
		}
	}

	m_pInputHandler->m_bPassesChanged = false;
}

void NetworkProfileManager::ProcessDownloadedProfiles()
{
	queue<NetworkingRequest> qCompletedRequests;
	if ( m_pNetworkThread->GetCompletedNetworkRequests( qCompletedRequests ) )
	{
		NPMLog( "Got downloaded profiles! ");

		while ( !qCompletedRequests.empty() )
		{
			NetworkingRequest request = qCompletedRequests.front();
			qCompletedRequests.pop();

			if ( request.GetResponseType() == NetResponseTypeProfile )
			{
				PlayerNumber pn = request.GetPlayerNumber();

				m_DownloadedProfiles[pn] = (Profile *)request.GetResponseData();
				m_State[pn] = NETWORK_PASS_READY;

				SCREENMAN->RefreshCreditsMessages();

				RageSoundParams params;
				params.m_bIsCriticalSound = true;
				m_soundReady.Play( &params );

				NPMLog( "Profile successfully loaded" );
			}
		}
	}
}

void NetworkProfileManager::Update()
{
	ProcessNetworkPasses();
	ProcessDownloadedProfiles();
}

bool NetworkProfileManager::LoadProfileForPlayerNumber( PlayerNumber pn, Profile &profile )
{
	if ( m_State[pn] == NETWORK_PASS_READY )
	{
		profile = *(m_DownloadedProfiles[pn]);
		return true;
	}

	return false;
}

