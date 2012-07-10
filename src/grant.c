/**
 * \file grant.c
 * \brief Privileged operations for performing chown on files.
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
#include <stdlib.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include <malloc.h>
#include <assert.h>
#include "socket.h"
#include "filecache.h"


#define CACHE_FILES    CACHE_DIR "/files/"

#define COMPARESTRINGS( x, y ) \
    ( ( ( strlen( y ) < length ) && \
		( strncasecmp( x, y, strlen( y ) ) == 0 ) ) ? 0 : 1 )



/**
 * Initialize the Permission Grant module.
 * @param childPid [in] pid of the download queue; the permission grant module
 *        will accept socket communication only from a process with this pid.
 * @param socketHandle [in] Socket handle for communicating with the download
 *        queue.
 * @return Nothing.
 */
void
InitializePermissionsGrant(
	pid_t childPid,
	int   socketHandle
	                       )
{
	struct ucred credentials;
	size_t       length;
	char         request[ 100 ];
	int          fd; /* unused */


	while( 1 )
	{
		/* Wait for a request from the download queue. */
		length = SocketReceiveDatagramFromClient( socketHandle, request,
												  sizeof( request ),
												  &credentials, &fd );

		/* Validate the sender: it must have the pid of the download queue. */
		if( credentials.pid == childPid )
		{
			/* Process the request. */
			if( COMPARESTRINGS( request, "CHOWN " ) == 0 )
			{
			}
			else if( COMPARESTRINGS( request, "PUBLISH " ) == 0 )
			{
			}
			else if( COMPARESTRINGS( request, "DELETE " ) == 0 )
			{
			}
			else
			{
			}
		}
		/* Silently ignore the message if it was not sent from the download
		   queue. */
		else
		{
			/* Do nothing. */
		}
	}

}




#if 0
/**
 * Initialize the socket which is used to communicate with the file cache
 * module.
 * @return Nothing.
 */
void SetupCacheCommSocket(
    void
			  )
{
    int                socketFd;
    struct sockaddr_un socketAddressServer;

    int                connectionFd;
    struct sockaddr_un socketAddressClient;
    socklen_t          clientAddressLength = sizeof( struct sockaddr_un );

    char socketMessage[ 200 ];
    struct ucred credentials;
    socklen_t    credentialsLength = sizeof( struct ucred );


    CreateServerStreamSocket( SOCKET_NAME, &socketFd, &socketAddressServer );

    /* Wait for client connections and spawn a thread for each
       connection. */

    while( ( connectionFd = accept( socketFd, &socketAddressClient,
                                    &clientAddressLength ) ) > -1 )
    {
        getsockopt( connectionFd, SOL_SOCKET, SO_PEERCRED, &credentials,
		    &credentialsLength );
	printf( "Client PID: %d, UID: %d, GID: %d\n",
		credentials.pid, credentials.uid, credentials.gid );

        while( 1 )
	{

	    read( connectionFd, socketMessage, 200 );
	    printf( "Socket activity detected: \"%s\".\n", socketMessage );

	}
	close( connectionFd );
    }

    unlink( SOCKET_NAME );
}




void Client( void )
{
    int                socketFd;
    struct sockaddr_un socketAddress;

    char buffer[ 200 ];
    int  nBytes;

    CreateClientStreamSocket( SOCKET_NAME, &socketFd, &socketAddress );

    if( socketFd != -1 )
    {
        for( ; ; )
	{
	    printf( "Enter message: " );
	    fgets( buffer, 200, stdin );
	    printf( "Sending message...\n" );
	    nBytes = write( socketFd, buffer, strlen( buffer ) );

	    nBytes = read( socketFd, buffer, 200 );
	    printf( "Response: (%d) \"%s\"\n", nBytes, buffer );
	    if( strcmp( buffer, "END" ) == 0 )
	    {
		break;
	    }
	}
    }
}

#endif

