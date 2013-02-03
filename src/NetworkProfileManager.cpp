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

class NetworkingRequest
{
public:
	NetworkingRequest( NetworkingRequestType type, PlayerNumber pn, void *context ) :
		m_eType( type ), m_pn( pn ), m_pContext( context ) {};

	NetworkingRequestType m_eType;
	PlayerNumber m_pn;
	void *m_pContext;
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

		// GenesisDriver *m_pDriver;
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

		int iTestCounter;
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
		LOG->Info( "*** NETPROFMAN: downloading pass! (%d)", pass->m_uUniqueIdentifier );

		std::stringstream ss;
		ss << "http://" << g_sProfileServerURL.Get() << "/get_stats.php?id=" << pass->m_uUniqueIdentifier;

		CString resultString;
		curl_easy_setopt( m_pCurlHandle, CURLOPT_URL, ss.str().c_str() );
		curl_easy_setopt( m_pCurlHandle, CURLOPT_WRITEFUNCTION, callback_write_data );
		curl_easy_setopt( m_pCurlHandle, CURLOPT_WRITEDATA, &resultString );
		CURLcode success = curl_easy_perform( m_pCurlHandle );

		XNode xmlNode;
		PARSEINFO pi;
		xmlNode.Load( resultString, &pi );

		Profile *newProfile = new Profile();
		Profile::LoadResult result = newProfile->LoadStatsXmlFromNode( &xmlNode );

		LOG->Info( "*** Got profile with diff: %d", newProfile->m_LastDifficulty );

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

			if ( request.m_eType == NetRequestTypeDownloadProfile )
			{
				NetworkPass *pass = (NetworkPass *)request.m_pContext;
				Profile *profile = DownloadProfile( pass );
				request.m_pContext = profile;

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

		iTestCounter = 0;

		SetHeartbeat( 0.1f );
		StartThread();

		// initialize driver
	}

	void GenesisInputHandlerThread::DoHeartbeat()
	{
		iTestCounter++;

		NetworkPass *newPass = NULL;
		if ( iTestCounter == 35 )
		{
			newPass = new NetworkPass( 1 );
		}

		if ( newPass )
		{
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

				NPMLog( "Added network request" );

				m_State[p] = NETWORK_PASS_DOWNLOADING;
				SCREENMAN->RefreshCreditsMessages();
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

			if ( request.m_eType == NetRequestTypeDownloadProfile )
			{
				PlayerNumber pn = request.m_pn;

				m_DownloadedProfiles[pn] = (Profile *)request.m_pContext;
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




















