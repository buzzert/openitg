#ifndef NetworkProfileManager_H
#define NetworkProfileManager_H

#include "PlayerNumber.h"
#include "RageSound.h"
#include "Profile.h"

typedef enum {
	NETWORK_PASS_ABSENT,
	NETWORK_PASS_PRESENT,
	NETWORK_PASS_DOWNLOADING,
	NETWORK_PASS_SAVING,
	NETWORK_PASS_READY
} NetworkPassState;


namespace NetworkProfileManagerThreads {
	class NetworkingThread;
	class GenesisInputHandlerThread;
}

class NetworkPass
{
public:
	NetworkPass( uint64_t uUniqueIdentifier ) : m_uUniqueIdentifier( uUniqueIdentifier ) {};
	uint64_t m_uUniqueIdentifier;
};

class NetworkProfileManager
{
public:
	NetworkProfileManager();
	~NetworkProfileManager();

	void Update();

	NetworkPassState GetPassState( PlayerNumber pn ) const { return m_State[pn]; }
	bool LoadProfileForPlayerNumber( PlayerNumber pn, Profile &profile );
	
protected:
	void ProcessNetworkPasses();
	void ProcessDownloadedProfiles();

	// Threads
	NetworkProfileManagerThreads::NetworkingThread *m_pNetworkThread;
	NetworkProfileManagerThreads::GenesisInputHandlerThread *m_pInputHandler;

	NetworkPassState m_State[NUM_PLAYERS];
	NetworkPass* m_Passes[NUM_PLAYERS];

	Profile* m_DownloadedProfiles[NUM_PLAYERS];
private:
	RageSound m_soundReady;
	RageSound m_soundDisconnect;
};

extern NetworkProfileManager* NETPROFMAN;

#endif 