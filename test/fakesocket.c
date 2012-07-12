/**
 * \file fakesocket.c
 * \brief Simulate communication between the transfer queue and the filesystem
 *        for testing purposes frontend.
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
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/un.h>
#include "socket.h"


#pragma GCC diagnostic ignored "-Wunused-parameter"



bool
CreateSocketPairProcess( 
    int        *socketFd,
    void       (*child)( int socketFd )
			)
{
	bool success = false;

    return( success );
}



bool
CreateServerStreamSocket(
    const char         *socketPath,
    int                *socketFd,
    struct sockaddr_un *socketAddress
	                     )
{
    bool success       = false;

    return( success );
}



bool
CreateServerDatagramSocket(
    const char         *socketPath,
    int                *socketFd,
    struct sockaddr_un *socketAddress
			   )
{
    bool success       = false;

    return( success );
}



int
SocketReceiveDatagramFromClient(
    int          socketFd,
    char         *buffer,
    size_t       size,
    struct ucred *credentials,
    int          *fileHandle
			   )
{
    int            result = 0;

    return( result );
}



int
SocketSendDatagramToClient(
    int  socketFd,
    char *buffer,
    int  bufferLength,
    int  fileHandle
			  )
{
    int            status = true;

    return( status );
}



void
CreateClientStreamSocket(
    const char         *socketPath,
    int                *socketFd,
    struct sockaddr_un *socketAddress
	                     )
{
}



void
CreateClientDatagramSocket(
    const char         *socketPathServer,
    int                *socketFd,
    struct sockaddr_un *socketAddressServer,
    const char         *socketPathClient,
    struct sockaddr_un *socketAddressClient
			   )
{
}



int
SocketSendDatagramToServer(
    int        socketFd,
    const char *buffer,
    int        bufferLength
			  )
{
    bool           success = true;

    return( success );
}




int
SocketReceiveDatagramFromServer(
    int          socketFd,
    char         *buffer,
    size_t       size
			       )
{
    return( 0 );
}
