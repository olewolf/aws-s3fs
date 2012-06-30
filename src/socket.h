/**
 * \file socket.h
 * \brief Communication between the transfer queue and the filesystem frontend.
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

#ifndef __S3FS_SOCKET_H
#define __S3FS_SOCKET_H


#include <stdbool.h>
#include <sys/un.h>
#include <sys/socket.h>


bool CreateSocketPairProcess( int *socketFd, void (*child)( int socketFd ) );

bool CreateServerStreamSocket( const char *socketPath, int *socketFd,
			 struct sockaddr_un *socketAddress );
bool CreateServerDatagramSocket( const char *socketPath, int *socketFd,
			 struct sockaddr_un *socketAddress );
int SocketReceiveDatagramFromClient( int socketFd, char *buffer, size_t size,
				    struct ucred *credentials, int *fileHandle );
int SocketSendDatagramToClient( int socketFd, char *buffer, int bufferLength,
				int fileHandle );

void CreateClientStreamSocket( const char *socketPath, int *socketFd,
			 struct sockaddr_un *socketAddress );
void CreateClientDatagramSocket( const char *socketPathServer, int *socketFd,
				 struct sockaddr_un *socketAddressServer,
				 const char *socketPathClient,
				 struct sockaddr_un *socketAddressClient);
int SocketSendDatagramToServer( int socketFd, const char *buffer,
				int bufferLength );
int SocketReceiveDatagramFromServer( int socketFd, char *buffer, size_t size );



#endif /* __S3FS_SOCKET_H */
