#define _GNU_SOURCE

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include "socket.h"
#include "aws-s3fs.h"

#undef SOCKET_NAME
#define SOCKET_NAME "temp/aws-s3fs.sock"


int main( int argc, char **argv )
{
	if( argc != 1 )
	{
		printf( "Usage: %s\n", argv[ 0 ] );
		exit( 1 );
	}

    int                socketFd;
    struct sockaddr_un socketAddress;

    char buffer[ 4096 ];
    int  nBytes;

	printf( "Connecting to socket %s\n", SOCKET_NAME );
    CreateClientStreamSocket( SOCKET_NAME, &socketFd, &socketAddress );

    if( socketFd != -1 )
    {
        for( ; ; )
		{
			printf( "Enter message: " );
			fgets( buffer, 4096, stdin );
			printf( "Sending message...\n" );
			nBytes = write( socketFd, buffer, strlen( buffer ) );

			nBytes = read( socketFd, buffer, 4096 );
			printf( "Response: (%d) \"%s\"\n", nBytes, buffer );
			if( strcmp( buffer, "END" ) == 0 )
			{
				break;
			}
		}
    }
}
