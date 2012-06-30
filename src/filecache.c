/**
 * \file filecache.c
 * \brief File cache server.
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

// See http://www.thomasstover.com/uds.html for passing credentials and file
// handles over a socket.

#include <config.h>
#include <stdlib.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include <malloc.h>
#include <sys/stat.h>
#include <assert.h>
#include <pthread.h>
#include <glib-2.0/glib.h>
#include <poll.h>
#include <errno.h>
#include "aws-s3fs.h"
#include "socket.h"
#include "filecache.h"



#define CACHE_INPROGRESS CACHE_FILES "unfinished/"


static pthread_t clientConnectionsListener;


struct CacheClientConnection
{
	int       connectionHandle;
	pthread_t thread;
	pid_t     pid;
	char      *bucket;
	char      keyId[ 21 ];
	char      secretKey[ 41 ];
};


static struct
{
	GRegex *keyIdAndSecretKey;
	GRegex *openOptions;
	GRegex *trimString;
} regexes;


static void CompileRegexes( void );
static void FreeRegexes( void );
static void* ReceiveRequests( void* );
static void* ClientConnectionsListener( void* );
static int CommandDispatcher( struct CacheClientConnection *clientConnection,
							  const char *message );

static int ClientConnects(	struct CacheClientConnection *clientConnection,
							const char *request );
static int ClientRequestsDownload(
	struct CacheClientConnection *clientConnection, const char *request );



/**
 * Send a message to a client via a socket connection. The message is sent
 * including its string-terminating null character.
 * @param connectionHandle [in] Connection handle for the socket.
 * @param message [in] Message to send.
 * @return \a true if the message was sent, or \a false if an error occurred.
 */
static bool
SendMessageToClient(
	int        connectionHandle,
	const char *message
	                )
{
	int  nBytes;
	bool status = true;

	nBytes = write( connectionHandle, message,
					strlen( message ) + sizeof( char ) );
	if( 0 <= nBytes )
	{
		status = false;
	}
	return( status );
}



/**
 * Initialize the file caching module.
 * @return Nothing.
 */
void
InitializeFileCache(
    void
	               )
{
    /* Create the files directory. Ignore any errors for now (such as that
       the directory already exists), because we only need to react to the
       fact that the local files won't be created unless the directory
       exists. */
    (void) mkdir( CACHE_FILES, S_IRWXU );
    (void) mkdir( CACHE_INPROGRESS, S_IRWXU );

	/* Initialize the database module. */
	InitializeFileCacheDatabase( );

	/* Compile regular expressions. */
	CompileRegexes( );

	/* Start the client connections listener thread. */
	if( pthread_create( &clientConnectionsListener, NULL,
						ClientConnectionsListener, NULL ) != 0 )
	{
		fprintf( stderr, "Couldn't start client connections listener thread" );
	}
}



/**
 * Shut down the file caching module.
 * @return Nothing.
 */
void
ShutdownFileCache(
    void
	             )
{
	ShutdownFileCacheDatabase( );
	FreeRegexes( );
}



/**
 * Read the entire pending message, regardless of length, and place it in
 * a buffer.
 * @param connectionHandle [in] Socket connection handle.
 * @return Message.
 */
static const char*
ReadEntireMessage(
    int connectionHandle
                  )
{
	char buffer[ 1024 ];
	int  nBytes;
	char *message    = malloc( 1 );
	int  messageSize = 0;

	struct pollfd fds = { 0, POLLIN, POLLIN };

	fds.fd = connectionHandle;

	/* Read partial message and expand the buffer as necessary until the
	   entire message is received. */
	while( 
		( 0 < ( nBytes = read( connectionHandle, buffer, sizeof( buffer ) ) ) )
		|| ( nBytes == sizeof( buffer ) ) )
	{
		message = realloc( message, messageSize + nBytes );
		memcpy( &message[ messageSize ], buffer, nBytes );
		messageSize += nBytes;
		/* Check if the entire buffer has been read, using poll( ) with
		   a timeout of 0 to avoid blocking. */
		if( poll( &fds, 1, 0 ) <= 0 )
		{
			break;
		}
	}
	printf( "Total message %d bytes\n", messageSize );

	/* Zero-terminate for good measure. */
	message[ messageSize ] = '\0';
	return( message );
}



/**
 * Thread that waits for requests and handles them appropriately. Each client
 * connection has its own thread to enable one thread to wait for a message
 * without blocking other threads.
 * @return Thread status (always 0 ).
 */
static void*
ReceiveRequests(
	void *data
                )
{
	struct CacheClientConnection *clientConnection = data;

	const char   *message;
	bool         end;
	struct ucred credentials;
	socklen_t    credentialsLength = sizeof( struct ucred );

	do
	{
		printf( "Waiting for message...\n" );
		message = ReadEntireMessage( clientConnection->connectionHandle );
        getsockopt( clientConnection->connectionHandle, SOL_SOCKET,
					SO_PEERCRED, &credentials, &credentialsLength );
		printf( "uid: %d, gid: %d, pid: %d\n", (int) credentials.uid,
				(int) credentials.gid, (int) credentials.pid );
		end = CommandDispatcher( clientConnection, message );
		free( (char*) message );
	} while( ! end );

	return( NULL );
}



/**
 * Invoke the appropriate function depending on the message received from the
 * cache client.
 * @param clientConnection [in] Client Information structure with information
 *        about the cache client.
 * @param message [in] Client message.
 * @return Nothing.
 */
static int
CommandDispatcher(
	struct CacheClientConnection *clientConnection,
	const char                   *message
	              )
{
	const char *command;
	int        (*commandFunction)( struct CacheClientConnection*, const char* );
	int        entry;
	bool       status = 0;

	/* List of commands and functions to handle the command. */
	const struct
	{
		const char *command;
		int        (*commandFunction)( struct CacheClientConnection *clientInfo,
									   const char *message );
	} dispatchTable[ ] =
		{
			{ "CACHE",   ClientRequestsDownload },
			{ "CONNECT", ClientConnects },
			{ NULL, NULL }
		};

	/* Find the command in the list and execute it. */
	entry = 0;
	while( ( command = dispatchTable[ entry ].command ) != NULL )
	{
		printf( "Trying command %s... ", command );
		if( strncasecmp( message, command, strlen( command ) ) == 0 )
		{
			printf( "executing command\n" );
			commandFunction = dispatchTable[ entry ].commandFunction;
			status = commandFunction( clientConnection,
									  &message[ strlen( command ) + 1 ] );
			break;
		}
		entry++;
	}
	if( command == NULL )
	{
		printf( "unknown command.\n" );
		SendMessageToClient( clientConnection->connectionHandle,
							 "ERROR: unknown command" );
	}

	return (status );
}











/**
 * Mark a file as dirty in the cache. This function must be called whenever
 * the local host writes to a file in the cache, and whenever the remote host
 * writes to one of its files and the file is cached.
 * 
 * @return Nothing.
 */










/**
 * Create a local filename for the S3 file and create a database entry
 * with the two file names and the S3 file attributes.
 * @param path [in] Full S3 pathname for the file.
 * @param uid [in] Ownership of the S3 file.
 * @param gid [in] Ownership of the S3 file.
 * @param permissions [in] Permissions for the S3 file.
 * @param mtime [in] Last modification time of the S3 file.
 * @param localfile [out] Filename of the local file.
 * @return ID for the database entry, or \a -1 if an error occurred.
 */
static sqlite3_int64
CreateLocalFile(
    const char *path,
    int        uid,
    int        gid,
    int        permissions,
    time_t     mtime,
    char       **localfile
	            )
{
	const char    *template = "XXXXXX";
    char          *localname;
    int           fileHd;
    sqlite3_int64 id;


    assert( path != NULL );

    /* Construct a template for the filename. */
    localname = malloc( strlen( CACHE_INPROGRESS ) + strlen( template )
						+ sizeof( char ) );
	strcpy( localname, CACHE_INPROGRESS );
	strcat( localname, template );

    /* Create and open a temporary file, and close it immediately in order to
	   create a uniquely named file. */
    fileHd = mkstemp( localname );
    close( fileHd );

    /* Keep the non-redundant part of the local filename. */
	*localfile = malloc( strlen( localname ) - strlen( template )
						 + sizeof( char ) );
    strcpy( *localfile,
			&localname[ strlen( localname ) - strlen( template ) ] );
    free( localname );

    /* Insert the filename combo into the database. */
	id = Query_CreateLocalFile( path, uid, gid, permissions,
								mtime, *localfile );

    return( id );
}



/**
 * Thread that listens to new connections and adds client connections as
 * they are detected. A new thread is initiated for each new connection
 * so that the process is able to wait for activity on them without blocking
 * the other threads.
 * @return Thread status (always 0 ).
 */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
void*
ClientConnectionsListener(
    void *dummy
	                      )
{
    int                socketFd;
    struct sockaddr_un socketAddressServer;

    int                connectionFd;
    struct sockaddr_un socketAddressClient;
    socklen_t          clientAddressLength = sizeof( struct sockaddr_un );

	struct CacheClientConnection *clientInfo;


    CreateServerStreamSocket( SOCKET_NAME, &socketFd, &socketAddressServer );
	printf( "Waiting for connections...\n" );

    /* Wait for client connections and start a new thread when a connection
	   is initiated. */
    while( ( connectionFd = accept( socketFd, &socketAddressClient,
                                    &clientAddressLength ) ) > -1 )
    {
		printf( "Connection established.\n" );

		/* Create new thread information structure and fill it with
		   partial data. */
		clientInfo = malloc( sizeof( struct CacheClientConnection ) );
		memset( clientInfo, 0, sizeof( struct CacheClientConnection ) );
		clientInfo->connectionHandle = connectionFd;
		/* Start a message receiver thread. */
		if( pthread_create( &clientInfo->thread, NULL,
							ReceiveRequests, clientInfo ) != 0 )
		{
			fprintf( stderr, "Couldn't start message receiver thread" );
		}
		else
		{
			printf( "New communications thread started.\n" );
		}
    }

	printf( "Exiting\n" );
    unlink( SOCKET_NAME );

	return( 0 );
}
#pragma GCC diagnostic pop





/**
 * The client sends a connect message passing the key ID and the secret key.
 * The CacheClientConnection structure for the client is updated with these
 * keys.
 * The server responds with CONNECTED or ERROR.
 * @param clientConnection [in/out] CacheClientConnection structure for the
 *        client.
 * @param request [in] Connection request parameters.
 * @return 0 on success, or \a -errno on failure.
 */
int
ClientConnects(
    struct CacheClientConnection *clientConnection,
	const char                   *request
	          )
{
	char       *keyId;
	char       *secretKey;
	GMatchInfo *matchInfo;
	int        status = -EINVAL;

	/* Grep the key and secret key. */
	g_regex_ref( regexes.keyIdAndSecretKey );
	if( g_regex_match( regexes.keyIdAndSecretKey, request, 0, &matchInfo ) )
	{
		keyId     = g_match_info_fetch( matchInfo, 1 );
		secretKey = g_match_info_fetch( matchInfo, 2 );
		g_match_info_free( matchInfo );

		/* Store the keys in the client connection info structure, and return
		   a success message if the keys have the correct length. */
		if( ( strlen( keyId ) == 20 ) && ( strlen( secretKey ) == 40 ) )
		{
			strncpy( clientConnection->keyId, keyId,
					 sizeof( clientConnection->keyId ) );
			strncpy( clientConnection->secretKey, secretKey,
					 sizeof( clientConnection->secretKey ) );
			SendMessageToClient( clientConnection->connectionHandle,
								 "CONNECTED" );
			status = 0;
		}
		else
		{
			status = -EKEYREJECTED;
		}
		g_free( keyId );
		g_free( secretKey );

	}
	g_regex_unref( regexes.keyIdAndSecretKey );

	if( status != 0 )
	{
		SendMessageToClient( clientConnection->connectionHandle,
							 "ERROR: unable to parse keys" );
	}

	return( status );
}



/**
 * The client sends a connect message passing the key ID and the secret key.
 * The CacheClientConnection structure for the client is updated with these
 * keys.
 * The server responds with CONNECTED or ERROR.
 * @param clientConnection [in/out] CacheClientConnection structure for the
 *        client.
 * @param request [in] Connection request parameters.
 * @return 0 on success, or \a -errno on failure.
 */
int
ClientRequestsCaching(
    struct CacheClientConnection *clientConnection,
	const char                   *request
	                 )
{
	int         status;

	GMatchInfo *matchInfo;
	gchar      *uidStr;
	gchar      *gidStr;
	gchar      *permissionsStr;
	gchar      *mtimeStr;
	gchar      *path;

	int        uid;
	int        gid;
	int        permissions;
	long long  mtime;
	char       *filename;
	char       *localfile;
	char       *reply;

	/* Extract uid, gid, permissions, mtime, and filename. */
	g_regex_ref( regexes.openOptions );
	if( g_regex_match( regexes.openOptions, request, 0, &matchInfo ) )
	{
		uidStr         = g_match_info_fetch( matchInfo, 1 );
		gidStr         = g_match_info_fetch( matchInfo, 2 );
		permissionsStr = g_match_info_fetch( matchInfo, 3 );
		mtimeStr       = g_match_info_fetch( matchInfo, 4 );
		path           = g_match_info_fetch( matchInfo, 5 );
		g_match_info_free( matchInfo );
		g_regex_unref( regexes.openOptions );
		/* Create numeric values for uid, gid, permissions, and mtime. */
		uid         = atoi( uidStr );
		gid         = atoi( gidStr );
		permissions = atoi( permissionsStr );
		mtime       = atoll( mtimeStr );
		g_free( uidStr );
		g_free( gidStr );
		g_free( permissionsStr );
		g_free( mtimeStr );
		/* Trim the filename. */
		g_regex_ref( regexes.trimString );
		filename = g_regex_replace( regexes.trimString, path,
									-1, 0, "", 0, NULL );
		g_free( path );

		/* Create a local file for the filename and return the name. */
		if( CreateLocalFile( filename, uid, gid, permissions,
							 mtime, &localfile ) )
		{
			reply = malloc( strlen( "CREATED " ) + sizeof( localfile )
							+ sizeof( char ) );
			sprintf( reply, "CREATED %s", localfile );
			SendMessageToClient( clientConnection->connectionHandle, reply );
			free( reply );
			status = 0;
		}
		else
		{
			status = -EIO;
		}
	}
	else
	{
		status = -EINVAL;
		SendMessageToClient( clientConnection->connectionHandle,
			"ERROR: cannot open file for caching" );
	}
	g_regex_unref( regexes.trimString );

	return( status );
}



#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
static int
ClientRequestsDownload(
    struct CacheClientConnection *clientConnection,
	const char                   *request
	                   )
{

	printf( "Download: %s\n", request );

	/* Determine the name of the local file. */
	

	/* Return if the file is already available in the cache directory. */


	/* Otherwise, enqueue for download. */

	SendClientMessage( clientConnection->connectionHandle, "OK" );
	return( 0 );
}
#pragma GCC diagnostic pop












/**
 * Compile regular expressions before use.
 * @return Nothing.
 */
static void
CompileRegexes(
    void
	           )
{
	/* Grep "keyid:secretkey" from the parameter string. */
	const char *keyIdAndSecretKey =
		"^\\s*([a-zA-Z0-9\\+/=]{20})\\s*:\\s*([a-zA-Z0-9\\+/=]{40})";

	/* Grep num:num:num:num:string */
	const char *openOptions =
		"([0-9]{1,5})\\s*:\\s*([0-9]{1,5})\\s*:\\s*([0-9]{1,3})\\s*:\\s*"
		"([0-9]{1,20})\\s*:\\s*(.+)";

    /* Replace leading and trailing spaces. */
	const char *trimString = "^[\\s]+|[\\s]+$";

	/* Compile regular expressions. */
    #define COMPILE_REGEX_JOINER( x, y ) x.y
    #define COMPILE_REGEX( regex ) COMPILE_REGEX_JOINER( regexes, regex ) = \
			g_regex_new( regex, G_REGEX_OPTIMIZE, G_REGEX_MATCH_NOTEMPTY, NULL )

	COMPILE_REGEX( keyIdAndSecretKey );
	COMPILE_REGEX( openOptions );
	COMPILE_REGEX( trimString );
}



static void
FreeRegexes(
	void
	        )
{
	g_regex_unref( regexes.keyIdAndSecretKey );
	g_regex_unref( regexes.trimString );
	g_regex_unref( regexes.openOptions );

}


