#include "Genesis.h"

#include "global.h"
#include "RageLog.h"
#include "arch/USB/USBDriver_Impl.h"

// For ETIMEDOUT
#include <errno.h>

static const unsigned char keymap[] = {
	  0,   0,   0,   0, 'a', 'b', 'c', 'd', 'e', 'f',
	'g', 'h', 'i', 'j', 'k', 'l', 'm', 'n', 'o', 'p',
	'q', 'r', 's', 't', 'u', 'v', 'w', 'x', 'y', 'z',
	'1', '2', '3', '4', '5', '6', '7', '8', '9', '0',
	'\n',  0,   0, '\t', ' ', '-', '='
};

static const unsigned char shifted_keymap[] = {
	  0,   0,   0,   0, 'A', 'B', 'C', 'D', 'E', 'F',
	'G', 'H', 'I', 'J', 'K', 'L', 'M', 'N', 'O', 'P',
	'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X', 'Y', 'Z',
	'!', '@', '#', '$', '%', '^', '&', '*', '(', ')',
	'\n',  0,   0, '\t', ' ', '_', '+'
};

static const int INTERFACE_NUM = 0;
static const uint16_t GENESIS_VENDORID  = 0x0c2e;
static const uint16_t GENESIS_PRODUCTID = 0x0206;
static const uint16_t PACKET_SIZE = 16;
static const uint16_t ENDPOINT_ADDRESS = 0x81;

static const unsigned REQ_TIMEOUT = 5000; // 5 seconds

static const unsigned int MOD_SHIFT = (1 << 1);

static char _scancodeToChar( unsigned int modifier, unsigned int scancode ) {
	char character = '\0';
	if ( scancode > 0 && scancode < sizeof( keymap ) ) {
		if (modifier & MOD_SHIFT) {
			character = shifted_keymap[scancode];
		} else {
			character = keymap[scancode];
		}
	}
	return character;
}

bool GenesisScanner::Open()
{
	if ( OpenInternal( GENESIS_VENDORID, GENESIS_PRODUCTID ) )
	{
		m_bInitialized = true;
		return true;
	}
	else
	{
		m_bInitialized = false;
		LOG->Warn("Could not open a connection to the Genesis device!");
	}

	return false;
}

bool GenesisScanner::Read( char **pData )
{
	int iResult = -1;
	CString stringData;
	char *rawData = (char *)calloc( PACKET_SIZE, 1 );
	while ( true )
	{
		iResult = m_pDriver->InterruptRead( ENDPOINT_ADDRESS, rawData, PACKET_SIZE, REQ_TIMEOUT );
		if ( iResult > 0 && rawData[2] > 0 )
		{
			unsigned int modifier = rawData[0];
			unsigned int scancode = rawData[2];

			char character = _scancodeToChar( modifier, scancode );

			if ( character == '\n' )
			{
				(*pData) = (char *)malloc( stringData.length() + 1 );
				std::strcpy( *pData, stringData.c_str() );

				break;
			}

			stringData += character;
		}
		else if ( iResult == -ETIMEDOUT )
		{
			continue;
		}
		else if ( iResult < 0 )
		{
			LOG->Warn("GENESIS IO ERROR (%d)!", iResult);
			break;
		}
	}

	free( rawData );
	
	return (iResult >= 0);
}
