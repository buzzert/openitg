//
//  RCButtonPacket.h
//


typedef enum {
	P1_MENULEFT = 0,
	P1_MENURIGHT,
	P1_SELECT,
    
	P2_MENULEFT,
	P2_MENURIGHT,
	P2_SELECT,
    
	NUM_BUTTONS
} ButtonIdentifier;

typedef enum {
    CommandTypeButton = 0,
    CommandTypeSystem,
} CommandType;

typedef enum {
    SystemCommandChangeVolume = 0,
    SystemCommandShutdown,
} SystemCommand;

typedef struct {
	ButtonIdentifier bi;
	bool state;
} button_command_t;

typedef struct {
    SystemCommand c;
    union {
        float   argument_f;
        int32_t argument_i32;
    };
} system_command_t;

typedef struct {
    CommandType type;
    union {
        button_command_t buttonCommand;
        system_command_t systemCommand;
    };
} command_packet_t;