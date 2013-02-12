#ifndef INPUT_HANDLER_REMOTECONTROL_H
#define INPUT_HANDLER_REMOTECONTROL_H

#include "InputHandler.h"
#include "RageThreads.h"
#include "RageTimer.h"

typedef enum {
	P1_MENULEFT = 0,
	P1_MENURIGHT,
	P1_SELECT,

	P2_MENULEFT,
	P2_MENURIGHT,
	P2_SELECT,

	NUM_BUTTONS
} ButtonIdentifier;

typedef struct {
	ButtonIdentifier bi;
	bool state;
} button_packet_t;

static const int SERVER_PORT = 3355;

class InputHandler_RemoteControl : public InputHandler
{
public:
	InputHandler_RemoteControl();
	~InputHandler_RemoteControl();

	void GetDevicesAndDescriptions( vector<InputDevice>& vDevicesOut, vector<CString>& vDescriptionsOut );

private:
	bool m_bShouldStop;

	int m_fdListeningSocket;
	int m_fdClientSocket;

	RageThread InputThread;

	button_packet_t *lastPacket;

	bool StartServer();

	void HandleInput();
	
	int InputThreadMain();
	static int InputThread_Start( void *data ) { return ((InputHandler_RemoteControl *) data)->InputThreadMain(); }
};

#endif /* INPUT_HANDLER_REMOTECONTROL_H */