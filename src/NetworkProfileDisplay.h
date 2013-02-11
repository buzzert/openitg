#ifndef NETWORK_PROF_DISPLAY_H
#define NETWORK_PROF_DISPLAY_H

#include "GameConstantsAndTypes.h"
#include "PlayerNumber.h"
#include "Sprite.h"
#include "ActorFrame.h"


class NetworkProfileDisplay : public ActorFrame
{
public:
	NetworkProfileDisplay();
	void Load( PlayerNumber pn );
	void Update( float fDelta );

protected:
	PlayerNumber 		m_PlayerNumber;
	NetworkPassState	m_LastSeenState;
	Sprite 				m_Sprites[NUM_NETWORK_PASS_STATES];
};

#endif /* NETWORK_PROF_DISPLAY_H */