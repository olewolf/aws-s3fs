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

			/* Acknowledge the receipt. */
			SocketSendDatagramToClient( socketHandle, "ACK", 4, -1 );
		}
		/* Ignore the message if it was not sent from the download
		   queue. */
		else
		{
			fprintf( stderr, "Warning: received socket message from an "
					 "unauthorized source (pid = %d).\n", credentials.pid );
			SocketSendDatagramToClient( socketHandle,
										"Not authorized", 15, -1 );
		}
	}
}


