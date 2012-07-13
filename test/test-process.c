/**
 * \file test-process.c
 * \brief .
 *
 * Copyright (C) 2012 Ole Wolf <wolf@blazingangles.com>
 *
 * This file is part of aws-s3fs.
 * 
 * aws-s3fs is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <config.h>
#include <string.h>
#include <unistd.h>
#include <socket.c>
#include <sys/stat.h>
#include "aws-s3fs.h"
#include "filecache.h"
#include "testfunctions.h"


#pragma GCC diagnostic ignored "-Wunused-parameter"

struct Configuration globalConfig;


static void test_Daemon( const char *param );


#define DISPATCHENTRY( x ) { #x, test_##x }
const struct dispatchTable dispatchTable[ ] =
{
	DISPATCHENTRY( Daemon ),

    { NULL, NULL }
};



/* Test that the file cache and the permissions grant modules work when
   daemonized. */
static void test_Daemon( const char *param )
{
    int                socketFd;
    struct sockaddr_un socketAddress;
    char buffer[ 100 ];
	struct stat        statInfo;

	/* Create a directory for the socket. */
	mkdir( CACHE_DIR, 0750 );
	unlink( SOCKET_NAME );

	/* Start the daemon. */
	system( "../../../src/aws-s3fs-queued &" );
	while( stat( SOCKET_NAME, &statInfo ) != 0 ) sleep( 1 );

	/* Create a client. */
    CreateClientStreamSocket( SOCKET_NAME, &socketFd, &socketAddress );
	printf( "Connected to socket %s\n", SOCKET_NAME );

    if( socketFd != -1 )
    {
		strcpy( buffer, "CONNECT bucket:12345678901234567890:"
				"1234567890123456789012345678901234567890" );
		write( socketFd, buffer, strlen( buffer ) );
		read( socketFd, buffer, 100 );
		printf( "Reply: %s\n", buffer );

		strcpy( buffer, "QUIT" );
		write( socketFd, buffer, strlen( buffer ) );
		printf( "Process terminated\n" );
	}
}
