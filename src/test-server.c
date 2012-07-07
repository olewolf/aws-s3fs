#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include "filecache.h"


int main( int argc, char **argv )
{
	if( argc != 1 )
	{
		printf( "Usage: %s \n", argv[ 0 ] );
		exit( 1 );
	}

	InitializeFileCache( );

	while( 1 ) sleep( 10 );

	exit( 0 );
}
