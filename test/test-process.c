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
#include <sys/wait.h>
#include <socket.c>
#include <sys/stat.h>
#include "aws-s3fs.h"
#include "filecache.h"
#include "testfunctions.h"


#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wunused-variable"

struct Configuration globalConfig;

extern void SendGrantMessage( int socketHandle, const char *privopRequest );

static void test_Daemon( const char *param );
static void test_LiveDownload( const char *param );


#define DISPATCHENTRY( x ) { #x, test_##x }
const struct dispatchTable dispatchTable[ ] =
{
	DISPATCHENTRY( Daemon ),
	DISPATCHENTRY( LiveDownload ),
    { NULL, NULL }
};


/* Dummy function. */
int ReadEntireMessage( int connectionHandle, char **clientMessage )
{
	return( 0 );
}



/* Test that the file cache and the permissions grant modules work when
   daemonized. */
static void test_Daemon( const char *param )
{
    int                socketFd;
    struct sockaddr_un socketAddress;
    char buffer[ 100 ];
	struct stat        statInfo;
	pid_t              pid;

	/* Create a directory for the socket. */
	mkdir( CACHE_DIR, 0750 );
	unlink( SOCKET_NAME );

	/* Start the daemon. */
	system( "../../aws-s3fs-queued &" );
	while( stat( SOCKET_NAME, &statInfo ) != 0 ) sleep( 1 );

	/* Test the file cache thread. */
    CreateClientStreamSocket( SOCKET_NAME, &socketFd, &socketAddress );
	printf( "Connected to socket %s\n", SOCKET_NAME );

    if( socketFd != -1 )
    {
		strcpy( buffer, "CONNECT bucket:1000:12345678901234567890:"
				"1234567890123456789012345678901234567890" );
		write( socketFd, buffer, strlen( buffer ) );
		read( socketFd, buffer, 100 );
		printf( "Reply: %s\n", buffer );

		strcpy( buffer, "QUIT" );
		write( socketFd, buffer, strlen( buffer ) );
		printf( "Process terminated\n" );
	}

	/* Start the daemon. */
	unlink( SOCKET_NAME );
	system( "../../aws-s3fs-queued &" );
	while( stat( SOCKET_NAME, &statInfo ) != 0 ) sleep( 1 );
    CreateClientStreamSocket( SOCKET_NAME, &socketFd, &socketAddress );

	/* There's (hopefully) no way to send a message to the grant process
	   outside of the file cache module because the grant process uses an
	   unnamed socket and requires all messages to be send from a process
	   with the id of the child process that was forked from the grant
	   process.  However, this test module happens to be linked with a module
	   that is capable of communicating with the grant process, so we can send
	   a test message even if it is guaranteed to receive with a "don't even
	   bother" reply via the "DEBUG" message. */
	strcpy( buffer, "DEBUG: expect a \"bugger-off\" reply" );
	write( socketFd, buffer, strlen( buffer ) );
	read( socketFd, buffer, 100 );
	printf( "Reply: %s\n", buffer );

	/* Terminate the server. */
	strcpy( buffer, "QUIT" );
	write( socketFd, buffer, strlen( buffer ) );
	printf( "Process terminated\n" );
}



void test_LiveDownload( const char *param )
{
	const char *hosts[ ] =
	{
		NULL,
		"s3-us-west-2",
		"s3-us-west-1",
		"s3-eu-west-1",
		"s3-ap-southeast-1",
		"s3-ap-northeast-1",
		"s3-sa-east-1"
	};
	struct stat        statInfo;
	int                socketFd;
	struct sockaddr_un socketAddress;

	char          url[ 200 ];
	char          request[ 200 ];
	char          reply[ 40 ];
	uid_t         uid;
	gid_t         gid;

	ReadLiveConfig( param );

	mkdir( CACHE_DIR, 0750 );
	mkdir( CACHE_FILES, 0755 );
	mkdir( CACHE_INPROGRESS, 0700 );

	/* Start the daemon. */
	unlink( SOCKET_NAME );
	system( "../../aws-s3fs-queued &" );
	while( stat( SOCKET_NAME, &statInfo ) != 0 ) sleep( 1 );
    CreateClientStreamSocket( SOCKET_NAME, &socketFd, &socketAddress );

	/* Connect to the daemon. */
	sprintf( request, "CONNECT %s:%d:%s:%s", globalConfig.bucketName,
			 getuid( ), globalConfig.keyId, globalConfig.secretKey );
	write( socketFd, request, strlen( request ) );
	read( socketFd, reply, sizeof( reply ) );
	if( strncmp( reply, "CONNECTED", strlen( "CONNECTED" ) ) != 0 )
	{
		printf( "Not connected\n" );
		exit( 1 );
	}

	/* Create local files and database entries. */
	if( globalConfig.region == US_STANDARD )
	{
		sprintf( url, "https://s3.amazonaws.com/%s/README",
				 globalConfig.bucketName );
	}
	else
	{
		sprintf( url, "https://%s.%s.amazonaws.com/README",
				 globalConfig.bucketName, hosts[ globalConfig.region ] );
	}
	uid = getuid( );
	gid = getgid( );
	sprintf( request, "CREATE %d:%d:%d:%d:%d:%d:100:%s",
			 (int) uid, (int) gid, 0750, (int) uid, (int) gid, 0640, url );
	write( socketFd, request, strlen( request ) );
	read( socketFd, reply, sizeof( reply ) );
	printf( "Reply: %s\n", reply );
	if( strncmp( reply, "CREATED", 7 ) != 0 )
	{
		printf( "Could not create local files\n" );
		exit( 1 );
	}
	sprintf( request, "ls -lgG -1 %s", CACHE_INPROGRESS );
	system( request );

	/* Request download of the file. */
	printf( "Requesting download of file %s\n", url );
	sprintf( request, "CACHE %s", url );
	write( socketFd, request, strlen( request ) );
	read( socketFd, reply, sizeof( reply ) );
	printf( "Reply: %s\n", reply );

	strcpy( request, "QUIT" );
	write( socketFd, request, strlen( request ) );
	read( socketFd, reply, sizeof( reply ) );
}

