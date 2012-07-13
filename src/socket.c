/**
 * \file socket.c
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


/* See http://www.lst.de/~okir/blackhats/node121.html */

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



/**
 * Create a server datagram unnamed socket pair and enable credentials so that
 * the server can receive the client's pid, uid, and gid. The function forks,
 * and the child continues from a function specified as a parameter.
 * @param socketPath [in] Name of the socket.
 * @param socketFd [out] Socket handle.
 * @param child [in] Function from which the child continues.
 * @return \a true on success, or \a false otherwise.
 */
bool
CreateSocketPairProcess( 
    int        *socketFd,
    void       (*child)( int socketFd )
			)
{
    int   socketPair[ 2 ];
    pid_t pid;
    bool  success = false;

    if( socketpair( PF_UNIX, SOCK_DGRAM, 0, socketPair ) < 0 )
    {
        fprintf( stderr, "Could not create socket\n" );
    }
    else
    {
        /* Spawn a child process. */
	if( (pid = fork( ) ) < 0 )
	{
	    fprintf( stderr, "Could not fork process\n" );
	}
	else if( pid == 0 )
	{
	    /* Child continues here. */
	    if( setuid( getuid( ) ) < 0 )
	    {
	        fprintf( stderr, "Unable to drop priviliges\n" );
	    }
	    else
	    {
	        /* Go the the specified child function and exit afterwards. */
                close( socketPair[ 1 ] );
		child( socketPair[ 0 ] );
		exit( 0 );
	    }
        }

	/* Parent continues here. */
        close( socketPair[ 0 ] );
        *socketFd = socketPair[ 1 ];
	success = true;
    }

    return( success );
}



/**
 * Create a named socket for the server and enable credentials so that the
 * server can receive the client's pid, uid, and gid.
 * @param socketPath [in] Name of the socket.
 * @param socketFd [out] File handle of the socket.
 * @param socketAddress [in/out] Memory area assigned to the socket.
 * @return \a true if the socket was created, or \a false otherwise.
 */
bool
CreateServerStreamSocket(
    const char         *socketPath,
    int                *socketFd,
    struct sockaddr_un *socketAddress
	                     )
{
    bool success       = false;
    int  credentialsOn = 1;

    /* Create a socket for listening. */
    memset( socketAddress, 0, sizeof( struct sockaddr_un ) );
    if( ( *socketFd = socket( AF_UNIX, SOCK_STREAM, 0 ) ) < 0 )
    {
        fprintf( stderr, "Cannot create socket.\n" );
    }
    else
    {
        /* Bind the socket to the assigned address. */
        socketAddress->sun_family = AF_UNIX;
		strncpy( socketAddress->sun_path, socketPath,
				 sizeof( socketAddress->sun_path ) /* = UNIX_PATH_MAX */ );
		unlink( socketPath );
		if( bind( *socketFd, socketAddress,
				  sizeof( struct sockaddr_un ) ) != 0 )
		{
			fprintf( stderr, "Cannot bind to socket.\n" );
		}
		else 
		{
			/* Change rw access to everyone. */
			if( chmod( socketPath, DEFFILEMODE ) < 0 )
			{
				fprintf( stderr, "Cannot set socket access permissions.\n" );
			}
			else
			{
				/* Listen on this socket. */
				if( listen( *socketFd, 5 ) != 0 )
				{
					fprintf( stderr, "Listener failed.\n" );
				}
				else
				{
					/* Enable (automatic) credentials passing as ancillary
					   data. This allows the server to identify the sender's
					   uid, gid, and pid. */
					setsockopt( *socketFd, SOL_SOCKET, SO_PASSCRED,
								&credentialsOn, sizeof( credentialsOn ) );

					/* Phew. */
					success = true;
				}
			}
		}
    }

    return( success );
}



/**
 * Create a named socket for the server and enable credentials so that the
 * server can receive the client's pid, uid, and gid.
 * @param socketPath [in] Name of the socket.
 * @param socketFd [out] File handle of the socket.
 * @param socketAddress [in/out] Memory area assigned to the socket.
 * @return \a true if the socket was created, or \a false otherwise.
 */
bool
CreateServerDatagramSocket(
    const char         *socketPath,
    int                *socketFd,
    struct sockaddr_un *socketAddress
			   )
{
    bool success       = false;
    int  credentialsOn = 1;


    /* Create a socket. */
    if( ( *socketFd = socket( AF_UNIX, SOCK_DGRAM, 0 ) ) < 0 )
    {
        fprintf( stderr, "Cannot create socket.\n" );
    }
    else
    {
        /* Bind the socket to the assigned address. */
        memset( socketAddress, 0, sizeof( struct sockaddr_un ) );
        socketAddress->sun_family = AF_UNIX;
	strncpy( socketAddress->sun_path, socketPath,
		 sizeof( socketAddress->sun_path ) /* = UNIX_PATH_MAX */ );
	if( bind( *socketFd, socketAddress,
		  sizeof( struct sockaddr_un ) ) != 0 )
	{
	    fprintf( stderr, "Cannot bind to socket.\n" );
	}
	else 
	{
	    /* Change rw access to everyone. */
	    if( chmod( socketPath, DEFFILEMODE ) < 0 )
	    {
		fprintf( stderr, "Cannot set socket access permissions.\n" );
	    }
	    else
	    {
	        /* Enable credentials passing as ancillary data. */
	        setsockopt( *socketFd, SOL_SOCKET, SO_PASSCRED,
			    &credentialsOn, sizeof( credentialsOn ) );

		/* Phew. */
		success = true;
	    }
	}
    }

    return( success );
}



/**
 * Receive socket client message and ancillary data (credentials and file
 * descriptors. The file descriptors are properly disposed of for now.
 * @param fd [in] File descriptor for the socket connection.
 * @param buffer [out] Destination buffer for the message.
 * @param size [in] Maximum length of the message.
 * @param credentials [out] Credentials of the client (pid, uid, gid).
 * @param fileHandle [out] File descriptor received from the client.
 * @return Number of bytes received, or \a -1 on failure.
 */
int
SocketReceiveDatagramFromClient(
    int          socketFd,
    char         *buffer,
    size_t       size,
    struct ucred *credentials,
    int          *fileHandle
			   )
{
    char           control[ 1024 ];
    struct msghdr  message;
    struct cmsghdr *cmessage;
    struct iovec   iovector;
    int            result;

    /* Receive plain message. */
    memset( &message, 0, sizeof( struct msghdr ) );
    iovector.iov_base      = buffer;
    iovector.iov_len       = size;
    message.msg_iov        = &iovector;
    message.msg_iovlen     = 1;
    message.msg_control    = control;
    message.msg_controllen = sizeof( control );

    if( recvmsg( socketFd, &message, 0 ) < 0 )
    {
	return( -1 );
    }

    /* Receive ancillary message. */
    result = -1;
    cmessage = CMSG_FIRSTHDR( &message );
    while( cmessage != NULL )
    {
        /* Store credentials. */
        if( ( cmessage->cmsg_level == SOL_SOCKET )
	    && ( cmessage->cmsg_type == SCM_CREDENTIALS ) )
	{
	    memcpy( credentials, CMSG_DATA( cmessage ),
		    sizeof( struct ucred ) );
	    result = iovector.iov_len;
	}
	/* Store file handle. */
	else if( ( cmessage->cmsg_level == SOL_SOCKET )
                 && ( cmessage->cmsg_type == SCM_RIGHTS ) )
	{
	    *fileHandle = *( (int*) CMSG_DATA( cmessage ) );
	    /*
	    dispose_fds( (int*) CMSG_DATA( cmessage ),
			 ( cmessage->cmsg_len - CMSG_LEN( 0 ) )
			 / sizeof( int ) );
	    */
	}
	cmessage = CMSG_NXTHDR( &message, cmessage );
    }

    return( result );
}



/**
 * Send a message, and optionally a filehandle, to a socket client.
 * @param socketFd [in] Socket file handle.
 * @param buffer [in] Message to send to the client.
 * @param bufferLength [in] Length of the message.
 * @param fileHandle [in] File handle to turn over to the client, or \a -1 if
 *        no file handle should be sent.
 * @return \a true if the message was sent, or \a false otherwise.
 */
int
SocketSendDatagramToClient(
    int  socketFd,
    char *buffer,
    int  bufferLength,
    int  fileHandle
			  )
{
    struct msghdr  message;
    struct cmsghdr *cmessage;
    struct iovec   iovector;
    char           control[ sizeof( struct cmsghdr ) + 10 ];
    int            status = true;

    /* Response with no ancillary data: */
    if( fileHandle < 0 )
    {
        if( write( socketFd, buffer, bufferLength ) < 0 )
	{
	    status = false;
	}
    }
    else
    {
	/* Response with file handle attached as ancillary data: */
	/* Compose response. */
	iovector.iov_base = buffer;
	iovector.iov_len  = bufferLength;
	memset( &message, 0, sizeof( struct msghdr ) );
	message.msg_iov        = &iovector;
	message.msg_iovlen     = 1;
	message.msg_control    = control;
	message.msg_controllen = sizeof( control );
	/* Attach file descriptor. */
	cmessage = CMSG_FIRSTHDR( &message );
	cmessage->cmsg_level = SOL_SOCKET;
	cmessage->cmsg_type = SCM_RIGHTS;
	cmessage->cmsg_len = CMSG_LEN( sizeof( int ) );
	*(int*) CMSG_DATA( cmessage ) = fileHandle;

	message.msg_controllen = cmessage->cmsg_len;
	if( sendmsg( socketFd, &message, 0 ) < 0 )
        {
	    fprintf( stderr, "Sendmsg failed\n" );
	    status = false;
	}
    }

    return( status );
}



/**
 * Create a socket for the client.
 * @param socketPath [in] Name of the socket.
 * @param socketFd [out] File handle of the socket.
 * @param socketAddress [in/out] Memory area assigned to the socket.
 * @return \a true if the socket was created, or \a false otherwise.
 */
void
CreateClientStreamSocket(
    const char         *socketPath,
    int                *socketFd,
    struct sockaddr_un *socketAddress
	                     )
{
    socklen_t addressLength = sizeof( struct sockaddr_un );

    memset( socketAddress, 0, addressLength );
    socketAddress->sun_family = AF_UNIX;
    strcpy( socketAddress->sun_path, socketPath );
    addressLength = strlen( socketAddress->sun_path )
                    + sizeof( socketAddress->sun_family );

    if( ( *socketFd = socket( AF_UNIX, SOCK_STREAM, 0 ) ) < 0 )
    {
        fprintf( stderr, "Error reating socket\n" );
        exit( 1 );
    }
    if( connect( *socketFd, socketAddress, addressLength ) < 0 )
    {
        fprintf( stderr, "Error connecting to socket.\n" );
        exit( 1 );
    }
}



/**
 * Create a socket for the client.
 * @param socketPath [in] Name of the socket.
 * @param socketFd [out] File handle of the socket.
 * @param socketAddress [in/out] Memory area assigned to the socket.
 * @return \a true if the socket was created, or \a false otherwise.
 */
void
CreateClientDatagramSocket(
    const char         *socketPathServer,
    int                *socketFd,
    struct sockaddr_un *socketAddressServer,
    const char         *socketPathClient,
    struct sockaddr_un *socketAddressClient
			   )
{

    if( ( *socketFd = socket( AF_UNIX, SOCK_DGRAM, 0 ) ) < 0 )
    {
        fprintf( stderr, "Error creating socket\n" );
        exit( 1 );
    }

    memset( socketAddressClient, 0, sizeof( struct sockaddr_un ) );
    socketAddressClient->sun_family = AF_UNIX;
    strcpy( socketAddressClient->sun_path, socketPathClient );
    if( bind( *socketFd, socketAddressClient,
	      sizeof( struct sockaddr_un ) ) < 0 )
    {
        fprintf( stderr, "Error binding to socket\n" );
        exit( 1 );
    }

    memset( socketAddressServer, 0, sizeof( struct sockaddr_un ) );
    socketAddressServer->sun_family = AF_UNIX;
    strcpy( socketAddressServer->sun_path, socketPathServer );

    /*
    bytes_sent = sendto(socket_fd, (char *) &integer_buffer, sizeof(int), 0,
                     (struct sockaddr *) &server_address, 
                     sizeof(struct sockaddr_un));

    address_length = sizeof(struct sockaddr_un);
    bytes_received = recvfrom(socket_fd, (char *) &integer_buffer, sizeof(int), 0, 
                           (struct sockaddr *) &(server_address),
                           &address_length);
    */

}



int
SocketSendDatagramToServer(
    int        socketFd,
    const char *buffer,
    int        bufferLength
			  )
{
    char           control[ sizeof( struct cmsghdr )
			    + sizeof( struct ucred ) + 1 ];
    struct msghdr  message;
    struct cmsghdr *cmessage;
    struct iovec   iovector;
    struct ucred   credentials;
    bool           success = true;

    /* Compose message. */
    iovector.iov_base = (char*) buffer;
    iovector.iov_len  = bufferLength;
    memset( &message, 0, sizeof( struct msghdr ) );
    message.msg_iov        = &iovector;
    message.msg_iovlen     = 1;
    message.msg_control    = control;
    message.msg_controllen = sizeof( control );
    /* Compose ancillary data: credentials. */
    credentials.pid = getpid( );
    credentials.uid = getuid( );
    credentials.gid = getgid( );
    cmessage = CMSG_FIRSTHDR( &message );
    cmessage->cmsg_level = SOL_SOCKET;
    cmessage->cmsg_type = SCM_CREDENTIALS;
    memcpy( CMSG_DATA( cmessage ), &credentials, sizeof( struct ucred ) );
    cmessage->cmsg_len = CMSG_LEN( sizeof( struct ucred ) );
    message.msg_controllen = cmessage->cmsg_len;

    /* Send message and ancillary data. */
    if( sendmsg( socketFd, &message, 0 ) < 0 )
    {
        fprintf( stderr, "Couldn't send message\n" );
	success = false;
    }

    return success;
}




/**
 * Receive socket message from the server, including any file handles that
 * might be sent as ancillary data.
 * @param fd [in] File descriptor for the socket connection.
 * @param buffer [out] Destination buffer for the message.
 * @param size [in] Maximum length of the message.
 * @param fileHandle [out] File handle received from the server, or \a -1
 *        if no file handle was received.
 * @return Number of bytes received, or \a -1 on failure.
 */
int
SocketReceiveDatagramFromServer(
    int          socketFd,
    char         *buffer,
    size_t       size
			       )
{
    char            control[ 1024 ];
    struct msghdr   message;
    struct cmsghdr  *cmessage;
    struct iovec    iovector;
    int             fileHandle;
    
    memset( &message, 0, sizeof( struct msghdr ) );
    iovector.iov_base      = buffer;
    iovector.iov_len       = size;
    message.msg_iov        = &iovector;
    message.msg_iovlen     = 1;
    message.msg_control    = control;
    message.msg_controllen = sizeof( control );

    if( recvmsg( socketFd, &message, 0 ) < 0 )
    {
        fprintf( stderr, "Recvmsg failed\n" );
    }
    else
    {
        /* Data is now in 'buffer'. */


        /* Read control data. */
        cmessage = CMSG_FIRSTHDR( &message );
        while( cmessage != NULL )
	{
	    if( ( cmessage->cmsg_level == SOL_SOCKET )
		&& ( cmessage->cmsg_type  == SCM_RIGHTS ) )
	    {
	        fileHandle = *(int*) CMSG_DATA( cmessage );
	    }
	    cmessage = CMSG_NXTHDR( &message, cmessage );
        }
    }

    return( fileHandle );
}



