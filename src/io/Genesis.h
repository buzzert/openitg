#ifndef IO_GENESIS_H
#define IO_GENESIS_H

#include <stdint.h>
#include "USBDriver.h"

class GenesisScanner : public USBDriver
{
public:
	bool Open();
	
	// Read() blocks until the genesis reads in some data.
	bool Read( char **data );

	bool m_bInitialized;
};

#endif /* IO_GENESIS_H */