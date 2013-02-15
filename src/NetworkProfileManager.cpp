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
#include "GameState.h"

// libcurl
#include <curl/curl.h>
#include <curl/easy.h>

#include <queue>
#include <sstream>

#define PLAY_SOUND( sound ) \
	RageSoundParams params; \
	params.m_bIsCriticalSound = true; \
	sound.Play( &params );

static Preference<CString> g_sProfileServerURL( "ProfileServerURL", "10.0.1.39" );

static void NPMLog( CString message )
{
	LOG->Info( (CString("*** NETPROFMAN: ") += message) );
}

NetworkProfileManager *NETPROFMAN = NULL;

typedef enum {
	NetResponseStatusOK,
	NetResponseStatusError
} NetResponseStatusCodeType;

typedef enum {
	NetRequestTypeDownloadBiscuit,
	NetRequestTypeUploadProfile
} NetworkingRequestType;

typedef enum {
	NetResponseTypeBiscuit,
	NetResponseTypeStatusCode
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

	void* GetPayloadData() const { return m_pPayloadData; };
	void SetPayloadData( void *payloadData ) { m_pPayloadData = payloadData; };

private:
	void SetResponseData( void *responseData ) { m_pResponseData = responseData; };
	void SetResponseType( NetworkingResponseType t ) { m_eResponseType = t; };

	NetworkingRequestType m_eRequestType;
	NetworkingResponseType m_eResponseType;

	PlayerNumber m_pn;

	void *m_pRequestData;
	void *m_pResponseData;

	void *m_pPayloadData;

	friend class NetworkProfileManagerThreads::NetworkingThread;
};

namespace NetworkProfileManagerThreads {
	class NetworkingThread : public RageWorkerThread
	{
	public:
		NetworkingThread();
		~NetworkingThread();

		bool m_bShouldStop;
		void AddNetworkRequest( const NetworkingRequest &request );
		bool GetCompletedNetworkRequests( std::queue<NetworkingRequest> &aOut );
		
	protected:
		void DoHeartbeat();
		void HandleRequest( int iRequest ) {};

	private:
		PlayerBiscuit* DownloadPlayerBiscuit( NetworkPass *pass );
		bool UploadProfile( Profile *profile, NetworkPass *pass );
		
		std::queue<NetworkingRequest> m_qPendingRequests;
		std::queue<NetworkingRequest> m_qCompletedRequests;

		RageMutex PendingRequestsMutex;
		RageMutex CompletedRequestsMutex;
	};


	class GenesisInputHandlerThread : public RageWorkerThread
	{
	public:
		GenesisInputHandlerThread();

		bool m_bShouldStop;
		bool m_bPassesChanged;
		queue<NetworkPass *> m_qPassQueue;

	protected:
		void Reconnect();
		void DoHeartbeat();
		void HandleRequest( int iRequest ) {};

		GenesisScanner *m_pDriver;
	};


	NetworkingThread::NetworkingThread() :
		RageWorkerThread( "NPM-NetworkingWorkerThread" ),
		m_bShouldStop( false ),
		PendingRequestsMutex( "PendingRequestsMutex" ),
		CompletedRequestsMutex( "CompletedRequestsMutex" )
	{
		SetHeartbeat( 0.1f );
		StartThread();
	}

	NetworkingThread::~NetworkingThread()
	{
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

	static size_t callback_write_data( void *buffer, size_t size, size_t num, void *userdata )
	{
		CString *pszXML = (CString *)userdata;
		(*pszXML) += CString( (const char *)buffer );
		return (size * num);
	}

	PlayerBiscuit* NetworkingThread::DownloadPlayerBiscuit( NetworkPass *pass )
	{
		CURL *curlHandle = curl_easy_init();

		std::stringstream ss;
		ss << "http://" << g_sProfileServerURL.Get();
		ss << "/smnetprof/get_stats.php?id=" << pass->m_sUniqueIdentifier;

		CString resultString;
		curl_easy_setopt( curlHandle, CURLOPT_WRITEFUNCTION, callback_write_data );
		curl_easy_setopt( curlHandle, CURLOPT_WRITEDATA, &resultString );

		// NPMTODO: Checking, error handling
		PlayerBiscuit *biscuit = new PlayerBiscuit;

		// get stats
		curl_easy_setopt( curlHandle, CURLOPT_URL, (ss.str() + "&type=stats").c_str() );
		CURLcode success = curl_easy_perform( curlHandle );

		// parse stats
		XNode *statsXML = new XNode();
		PARSEINFO pi;
		statsXML->Load( resultString, &pi );

		biscuit->statsNode = statsXML;

		// get editable
		resultString = "";
		curl_easy_setopt( curlHandle, CURLOPT_URL, (ss.str() + "&type=editable").c_str() );
		success = curl_easy_perform( curlHandle );

		// parse editable
		XNode *editableXML = new XNode();
		editableXML->Load( resultString, &pi );

		biscuit->editableNode = editableXML;

		return biscuit;
	}

	bool NetworkingThread::UploadProfile( Profile *profile, NetworkPass *pass )
	{
		std::stringstream ss;
		ss << "http://" << g_sProfileServerURL.Get();
		ss << "/smnetprof/upload_stats.php";

		XNode *xml = profile->SaveStatsXmlCreateNode( false );
		CString stringValue = xml->GetXML();

		struct curl_httppost *formPost = NULL;
		struct curl_httppost *lastItem = NULL;
		struct curl_slist *headerList = NULL;
		static const char buf[] = "Expect:";

		CURL *curlHandle = curl_easy_init();
		curl_global_init( CURL_GLOBAL_ALL );

		curl_formadd( &formPost,
					  &lastItem,
					  CURLFORM_COPYNAME, "id",
					  CURLFORM_COPYCONTENTS, pass->m_sUniqueIdentifier.c_str(),
					  CURLFORM_END
		);

		curl_formadd( &formPost,
					  &lastItem,
					  CURLFORM_COPYNAME, "xmlfile",
					  CURLFORM_BUFFER, "stats",
					  CURLFORM_BUFFERPTR, stringValue.c_str(),
					  CURLFORM_END
		);

		headerList = curl_slist_append( headerList, buf );
		curl_easy_setopt( curlHandle, CURLOPT_URL, ss.str().c_str() );
		curl_easy_setopt( curlHandle, CURLOPT_HTTPHEADER, headerList );
		curl_easy_setopt( curlHandle, CURLOPT_HTTPPOST, formPost );

		CURLcode result = curl_easy_perform( curlHandle );
		if( result != CURLE_OK )
		{
			LOG->Warn("NETPROFMAN ERROR: uploading profile %s", curl_easy_strerror( result ));
		}

		curl_formfree( formPost );
		curl_slist_free_all( headerList );

		return result == CURLE_OK;
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

			if ( request.GetRequestType() == NetRequestTypeDownloadBiscuit )
			{
				NetworkPass *pass = (NetworkPass *)request.GetRequestData();

				PlayerBiscuit *biscuit = DownloadPlayerBiscuit( pass );
				request.SetResponseData( biscuit );
				request.SetResponseType( NetResponseTypeBiscuit );

				CompletedRequestsMutex.Lock();
				m_qCompletedRequests.push( request );
				CompletedRequestsMutex.Unlock();
			}
			else if ( request.GetRequestType() == NetRequestTypeUploadProfile )
			{
				NetworkPass *pass = (NetworkPass *)request.GetRequestData();
				Profile *profile = (Profile *)request.GetPayloadData();

				bool success = UploadProfile( profile, pass );
				//request.SetResponseData( success ? NetResponseStatusOK : NetResponseStatusError );
				request.SetResponseType( NetResponseTypeStatusCode );

				CompletedRequestsMutex.Lock();
				m_qCompletedRequests.push( request );
				CompletedRequestsMutex.Unlock();

			}
		}
		PendingRequestsMutex.Unlock();
	}


	/*** Genesis Input Handler ***/

	GenesisInputHandlerThread::GenesisInputHandlerThread() :
		RageWorkerThread( "GenesisInputHandlerThread" ),
		m_bShouldStop( false )
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

	void GenesisInputHandlerThread::Reconnect()
	{
		while ( !m_pDriver->Open() )
		{
			m_pDriver->Close();
			usleep( 1000 );
		}
	}

	void GenesisInputHandlerThread::DoHeartbeat()
	{
		char *pDataIn;
		NetworkPass *newPass = NULL;
		while ( true && !m_bShouldStop ) {
			while ( !m_pDriver->Read( &pDataIn ) )
				Reconnect();

			CString passString = CString( pDataIn );
			free( pDataIn );

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
	m_soundScanned.Load( THEME->GetPathS("NetworkProfileManager", "scanned"), true );
	m_soundSaved.Load( THEME->GetPathS("NetworkProfileManager", "saved"), true );
	m_soundTooLate.Load( THEME->GetPathS("MemoryCardManager","too late"), true );
}

NetworkProfileManager::~NetworkProfileManager()
{
	delete m_DownloadedBiscuits;
	delete m_Passes;

	if ( m_pNetworkThread )
	{
		m_pNetworkThread->m_bShouldStop = true;
		m_pNetworkThread->WaitForOneHeartbeat();
	}

	if ( m_pInputHandler )
	{
		m_pInputHandler->m_bShouldStop = true;
		m_pInputHandler->WaitForOneHeartbeat();
	}
	
}

bool NetworkProfileManager::AssociateNetworkPass( PlayerNumber pn, NetworkPass *pass )
{
	if ( m_Passes[pn] != NULL )
		return false;

	m_State[pn] = NETWORK_PASS_PRESENT;
	m_Passes[pn] = pass;
	return true;
}

bool NetworkProfileManager::DisassociateNetworkPass( PlayerNumber pn )
{
	m_State[pn] = NETWORK_PASS_ABSENT;

	delete m_Passes[pn];
	m_Passes[pn] = NULL;

	delete m_DownloadedBiscuits[pn];
	m_DownloadedBiscuits[pn] = NULL;

	return true;
}

void NetworkProfileManager::ProcessNetworkPasses()
{
	if ( !m_pInputHandler->m_bPassesChanged )
		return;

	NPMLog( "got pass from genesis reader" );

	m_pInputHandler->m_bPassesChanged = false;

	if ( !GAMESTATE->PlayersCanJoin() )
	{
		// too late. throw away the pass
		PLAY_SOUND( m_soundTooLate );
		m_pInputHandler->m_qPassQueue.pop();

		return;
	}

	while ( !m_pInputHandler->m_qPassQueue.empty() )
	{
		NetworkPass *newPass = m_pInputHandler->m_qPassQueue.front();
		m_pInputHandler->m_qPassQueue.pop();

		bool passAlreadyScanned = false;
		FOREACH_PlayerNumber( pn )
		{
			if ( m_Passes[pn] && *(m_Passes[pn]) == (*newPass) )
			{
				passAlreadyScanned = true;
				break;
			}
		}

		if ( passAlreadyScanned )
		{
			PLAY_SOUND( m_soundTooLate );
			break;
		}

		// Place pass into the first empty slot.
		bool passWasPlaced = false;
		FOREACH_PlayerNumber( p )
		{
			if ( m_Passes[p] == NULL && GAMESTATE->IsPlayerEnabled( p ) )
			{
				AssociateNetworkPass( p, newPass );

				// Start downloading profile
				NetworkingRequest request( NetRequestTypeDownloadBiscuit, p, newPass );
				m_pNetworkThread->AddNetworkRequest( request );

				PLAY_SOUND( m_soundScanned );

				m_State[p] = NETWORK_PASS_DOWNLOADING;
				SCREENMAN->RefreshCreditsMessages();

				passWasPlaced = true;
				break;
			}
		}

		if ( !passWasPlaced )
		{
			// Either too many players, no players joined, or it's not the right time.
			PLAY_SOUND( m_soundTooLate );
		}
	}
}

void NetworkProfileManager::ProcessPendingProfiles()
{
	queue<NetworkingRequest> qCompletedRequests;
	if ( m_pNetworkThread->GetCompletedNetworkRequests( qCompletedRequests ) )
	{
		while ( !qCompletedRequests.empty() )
		{
			NetworkingRequest request = qCompletedRequests.front();
			qCompletedRequests.pop();

			if ( request.GetResponseType() == NetResponseTypeBiscuit )
			{
				PlayerNumber pn = request.GetPlayerNumber();

				m_DownloadedBiscuits[pn] = (PlayerBiscuit *)request.GetResponseData();
				m_State[pn] = NETWORK_PASS_READY;

				SCREENMAN->RefreshCreditsMessages();

				PLAY_SOUND( m_soundReady );

				// do some credits stuff here soon!
				SCREENMAN->SystemMessage( ("Welcome " + GetProfileDisplayString( pn ) + "!") );

				NPMLog( "Biscuit was downloaded" );
			}
			else if ( request.GetRequestType() == NetRequestTypeUploadProfile )
			{
				PLAY_SOUND( m_soundSaved );

				PlayerNumber pn = request.GetPlayerNumber();
				DisassociateNetworkPass( pn );
				SCREENMAN->RefreshCreditsMessages();
			}
		}
	}
}

void NetworkProfileManager::Update()
{
	ProcessNetworkPasses();
	ProcessPendingProfiles();
}

CString NetworkProfileManager::GetProfileDisplayString( PlayerNumber pn )
{
	if ( m_State[pn] != NETWORK_PASS_ABSENT )
	{
		PlayerBiscuit *myBiscuit = m_DownloadedBiscuits[pn];
		if ( myBiscuit )
		{
			CString displayName;
			myBiscuit->editableNode->GetChildValue( "DisplayName", displayName );

			return displayName;
		}
	}

	return "Network Profile";
}

bool NetworkProfileManager::LoadProfileForPlayerNumber( PlayerNumber pn, Profile &profile )
{
	if ( m_State[pn] == NETWORK_PASS_READY )
	{
		PlayerBiscuit *myBiscuit = m_DownloadedBiscuits[pn];
		XNode *statsNode = myBiscuit->statsNode;;
		XNode *editableNode = myBiscuit->editableNode;

		// Load stats
		profile.LoadStatsXmlFromNode(statsNode);

		// Load editable
		int weight;
		CString displayName, highScoreName;

		editableNode->GetChildValue( "DisplayName", displayName );
		editableNode->GetChildValue( "LastUsedHighScoreName", highScoreName );
		editableNode->GetChildValue( "WeightPounds", weight );

		profile.m_sDisplayName = displayName;
		profile.m_sLastUsedHighScoreName = highScoreName;
		profile.m_iWeightPounds = weight;

		return true;
	}

	return false;
}

bool NetworkProfileManager::SaveProfileForPlayerNumber( PlayerNumber pn, const Profile &profile )
{
	if ( m_State[pn] != NETWORK_PASS_READY )
	{
		NPMLog( "Attempted to save profile that wasn't ready" );
		return false;
	}

	NPMLog( "Saving profile!" );
	NetworkingRequest saveRequest( NetRequestTypeUploadProfile, pn, m_Passes[pn] );
	saveRequest.SetPayloadData( new Profile( profile ) ); // XXX: Probably not the best way to do this...

	m_pNetworkThread->AddNetworkRequest( saveRequest );

	m_State[pn] = NETWORK_PASS_SAVING;
	SCREENMAN->RefreshCreditsMessages();

	return true; // NPMTODO: error handling
}
