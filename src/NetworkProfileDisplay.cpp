#include "global.h"
#include "NetworkProfileDisplay.h"
#include "ThemeManager.h"
#include "NetworkProfileManager.h"
#include "RageUtil.h"
#include "ActorUtil.h"
#include "RageLog.h"


NetworkProfileDisplay::NetworkProfileDisplay()
{
	m_PlayerNumber = PLAYER_INVALID;
	m_LastSeenState = NETWORK_PASS_ABSENT;
}

void NetworkProfileDisplay::Load( PlayerNumber pn )
{
	m_PlayerNumber = pn;

	for ( int i = 0; i < NUM_NETWORK_PASS_STATES; i++ )
	{
		NetworkPassState nps = (NetworkPassState)i;

		CString stateString = NetworkPassStateToString( nps );
		m_Sprites[i].SetName( ssprintf("%s%s", "NetworkProfileDisplay", stateString.c_str()) );
		m_Sprites[i].Load( THEME->GetPathG("NetworkProfileDisplay", stateString.c_str()) );
		m_Sprites[i].SetHidden( true );

		this->AddChild( &m_Sprites[i] );
	}
}

void NetworkProfileDisplay::Update( float fDelta )
{
	NetworkPassState newNps = NETPROFMAN->GetPassState( m_PlayerNumber );
	if( m_LastSeenState != newNps )
	{
		if( m_LastSeenState != NETWORK_PASS_INVALID && newNps != NETWORK_PASS_SAVING )
			m_Sprites[m_LastSeenState].SetHidden( true );

		m_LastSeenState = newNps;
		m_Sprites[m_LastSeenState].SetHidden( false );
		ActorUtil::OnCommand( m_Sprites[m_LastSeenState], "NetworkProfileDisplay" );
	}

	ActorFrame::Update( fDelta );
}
