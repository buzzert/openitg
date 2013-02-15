#include "global.h"

#include "RageLog.h"
#include "InputFilter.h"
#include "DiagnosticsUtil.h"
#include "arch/ArchHooks/ArchHooks.h"

// Linux specific for now
#include <sys/types.h> 
#include <sys/socket.h>
#include <netinet/in.h>

#include "InputHandler_RemoteControl.h"

#define DEVICE_NAME "RemoteControl"
#define DEVICE_INPUTDEVICE DEVICE_JOY10

REGISTER_INPUT_HANDLER( RemoteControl );

InputHandler_RemoteControl::InputHandler_RemoteControl() :
	m_bShouldStop(false)
{
	DiagnosticsUtil::SetInputType( DEVICE_NAME );
	
	InputThread.SetName( "RemoteControl I/O thread" );
	InputThread.Create( InputThread_Start, this );
}

InputHandler_RemoteControl::~InputHandler_RemoteControl()
{
	if ( InputThread.IsCreated() )
	{
		m_bShouldStop = true;
		InputThread.Wait();
	}

	close(m_fdClientSocket);
	close(m_fdListeningSocket);
}

bool InputHandler_RemoteControl::StartServer()
{
	struct sockaddr_in serverAddress, clientAddress;
	
	m_fdListeningSocket = socket( AF_INET, SOCK_STREAM, 0 );
	if ( m_fdListeningSocket < 0 )
	{
		LOG->Warn( "RemoteControl: Error opening listening socket" );
		return false;
	}

	memset( &serverAddress, 0, sizeof(serverAddress) );
	serverAddress.sin_family 	  = AF_INET;
	serverAddress.sin_addr.s_addr = INADDR_ANY;
	serverAddress.sin_port 		  = SERVER_PORT;

	if ( bind( m_fdListeningSocket, (const struct sockaddr *)&serverAddress, sizeof(serverAddress) ) < 0 )
	{
		LOG->Warn( "RemoteControl: Error binding socket" );
		return false;
	}

	listen( m_fdListeningSocket, 10 );

	size_t clientAddressSize = sizeof(clientAddress);
	m_fdClientSocket = accept(m_fdListeningSocket, (struct sockaddr *)&clientAddress, &clientAddressSize);
	LOG->Info("**** CONNECTED!");

	lastPacket = (command_packet_t *)calloc( sizeof(command_packet_t), 1 );

	return true;
}

void InputHandler_RemoteControl::GetDevicesAndDescriptions( vector<InputDevice>& vDevicesOut, vector<CString>& vDescriptionsOut )
{
	vDevicesOut.push_back( InputDevice(DEVICE_INPUTDEVICE) );
	vDescriptionsOut.push_back( DEVICE_NAME );
}

void InputHandler_RemoteControl::HandleInput()
{
	int iBytesRead = read( m_fdClientSocket, lastPacket, sizeof( command_packet_t ) );
	if ( iBytesRead <= 0 )
	{
		LOG->Warn( "Client was disconnected" );

		close( m_fdClientSocket );
		close( m_fdListeningSocket );
		StartServer();
		return;
	}

	if ( lastPacket->type == CommandTypeButton ) {
		button_command_t buttonCommand = lastPacket->buttonCommand;

		RageTimer now;
		DeviceInput deviceInput( DEVICE_INPUTDEVICE, JOY_1 + buttonCommand.bi );
		deviceInput.ts = now;

		ButtonPressed( deviceInput, buttonCommand.state );
	}
}

int InputHandler_RemoteControl::InputThreadMain()
{
	StartServer();

	while( !m_bShouldStop )
	{
		HandleInput();
	}
	
	return 0;
}
