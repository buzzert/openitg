#ifndef NetworkProfileManager_H
#define NetworkProfileManager_H

#include "PlayerNumber.h"
#include "RageSound.h"
#include "Profile.h"
#include "GameConstantsAndTypes.h"


namespace NetworkProfileManagerThreads {
	class NetworkingThread;
	class GenesisInputHandlerThread;
}

typedef struct {
	XNode *statsNode;
	XNode *editableNode;
} PlayerBiscuit;

class NetworkPass
{
public:
	NetworkPass( CString sUniqueIdentifier ) : m_sUniqueIdentifier( sUniqueIdentifier ) {};
	CString m_sUniqueIdentifier;

	bool operator==(const NetworkPass &otherPass) { return m_sUniqueIdentifier == otherPass.m_sUniqueIdentifier; };
};

class NetworkProfileManager
{
public:
	NetworkProfileManager();
	~NetworkProfileManager();

	void Update();

	NetworkPassState GetPassState( PlayerNumber pn ) const { return m_State[pn]; }
	CString GetProfileDisplayString( PlayerNumber pn );

	bool LoadProfileForPlayerNumber( PlayerNumber pn, Profile &profile );
	bool SaveProfileForPlayerNumber( PlayerNumber pn, const Profile &profile );

protected:
	void ProcessNetworkPasses();
	void ProcessPendingProfiles();

	bool AssociateNetworkPass( PlayerNumber pn, NetworkPass *pass );
	bool DisassociateNetworkPass( PlayerNumber pn );

	// Threads
	NetworkProfileManagerThreads::NetworkingThread *m_pNetworkThread;
	NetworkProfileManagerThreads::GenesisInputHandlerThread *m_pInputHandler;

	NetworkPassState m_State[NUM_PLAYERS];
	NetworkPass* m_Passes[NUM_PLAYERS];

	PlayerBiscuit *m_DownloadedBiscuits[NUM_PLAYERS];
private:
	RageSound m_soundReady;
	RageSound m_soundDisconnect;
	RageSound m_soundScanned;
	RageSound m_soundSaved;
	RageSound m_soundTooLate;
};

extern NetworkProfileManager* NETPROFMAN;

#endif 