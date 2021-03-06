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


#include <config.h>
#include <stdlib.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include <malloc.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <assert.h>
#include <pthread.h>
#include <glib-2.0/glib.h>
#include <poll.h>
#include <errno.h>
#include "aws-s3fs.h"
#include "socket.h"
#include "filecache.h"


#ifdef AUTOTEST
#pragma GCC diagnostic ignored "-Wunused-parameter"
#endif

#define MEGABYTES ( 1024 * 1024 );
#define GIGABYTES ( 1024 * MEGABYTES );
#define TERABYTES ( 1024 * GIGABYTES );


static pthread_t clientConnectionsListener;

/* Used as a constant for blocking the SIGPIPE signal. */
static sigset_t sigpipeMask;

/* The grant module is called from this file only for testing purposes. */
int testSocket;
extern int SendGrantMessage( int socketHandle, const char *privopRequest,
							 char *reply, int replyBufferLength );


struct CacheClientConnection
{
	int       connectionHandle;
	pthread_t thread;
	pid_t     pid;
	uid_t     uid;
	gid_t     gid;
	char      *bucket;
	char      keyId[ 21 ];
	char      secretKey[ 41 ];
};


struct RegularExpressions regexes;


STATIC void CompileRegexes( void );
static void FreeRegexes( void );
STATIC void *ReceiveRequests( void* );
static void *ClientConnectionsListener( void* );
#ifdef AUTOTEST_SKIP_COMMUNICATIONS
extern int ReadEntireMessage( int connectionHandle, char **clientMessage );
#else
static int ReadEntireMessage( int connectionHandle, char **clientMessage );
#endif
STATIC int CommandDispatcher( struct CacheClientConnection *clientConnection,
							  const char *message );
STATIC char *TrimString( char *original );

STATIC int ClientConnects(
	struct CacheClientConnection *clientConnection, const char *request );
STATIC int ClientRequestsCreate(
	struct CacheClientConnection *clientConnection, const char *request );
STATIC int ClientDisconnects(
	struct CacheClientConnection *clientConnection, const char *request );
STATIC int
ClientRequestsLocalFilename(
	struct CacheClientConnection *clientConnection, const char *request );
STATIC int
ClientRequestsShutdown(
	struct CacheClientConnection *clientConnection, const char *request );
static int
ClientRequestsDebugMessage(
	struct CacheClientConnection *clientConnection, const char *request );
static int ClientRequestsDownload(
	struct CacheClientConnection *clientConnection, const char *request );
static int ClientRequestsFileClose(
	struct CacheClientConnection *clientConnection, const char *request );



/**
 * Block the SIGPIPE signal that may be generated on write( ).  The problem
 * is that the signal is delivered synchronously, meaning it must be handled
 * by an action handler that is set on a per-process level; it is also
 * delivered only to the thread that performs the write.  Hence, blocking it
 * and testing for errno == EPIPE is the best approach.
 * @param wasPending [out] \a true if a SIGPIPE signal was pending prior to
 *        blocking it; \a false otherwise.
 * @param wasBlocked [out] \a true if the SIGPIPE signal was already blocked
 *        when we blocked it.
 * @return Nothing.
 * Test: none.
 */
void
BlockSigpipeSignal( 
	bool *wasPending,
	bool *wasBlocked
	               )
{
	sigset_t                     pendingSignal;
	sigset_t                     sigpipeBlocked;

	/* Do nothing if a SIGPIPE signal is already pending, because any new
	   SIGPIPE will be merged with the pending SIGPIPE. */
	sigpending( &pendingSignal );
    *wasPending = sigismember( &pendingSignal, SIGPIPE );
	/* Otherwise, block the signal for this thread. */
	if( ! *wasPending )
    {
		pthread_sigmask( SIG_BLOCK, &sigpipeMask, &sigpipeBlocked );
		*wasBlocked = ! sigismember( &sigpipeBlocked, SIGPIPE );
    }
}



/**
 * Restore the SIGPIPE signal to its previous state.
 * @param wasPending [in] \a true if a SIGPIPE signal was pending prior to
 *        blocking it; \a false otherwise.
 * @param wasBlocked [in] \a true if the SIGPIPE signal was already blocked
 *        when we blocked it.
 * @return Nothing.
 * Test: none.
 */
void
RestoreSigpipeSignal( 
	bool wasPending,
	bool wasBlocked
	                 )
{
	sigset_t                     pendingSignal;
	static const struct timespec zeroWait = { 0, 0 };
	int                          res;

	/* Do nothing if a SIGPIPE signal was already pending, because any new
	   SIGPIPE generated during the write becomes merged with the pending
	   signal.
	   Otherwise we must react to any SIGPIPE generated by our write( ):
	   sigwait( ) it to clear its pending status and thus ignore it. */
    if( ! wasPending )
    {
		sigpending( &pendingSignal );
		if( sigismember( &pendingSignal, SIGPIPE ) )
        {
			/* The SIGPIPE may have been sent from a malicious user to the
			   process and delivered to another thread before we've had the
			   chance to react on it.  Circumvent this situation in the wait. */
			do
			{
				res = sigtimedwait( &sigpipeMask, NULL, &zeroWait );
			} while( ( res == -1 ) && ( errno == EINTR ) );
        }
		/* If the signal was already blocked prior to our blocking, unblock
		   our blocking. */
		if( wasBlocked )
		{
			pthread_sigmask( SIG_UNBLOCK, &sigpipeMask, NULL );
		}
    }
}



/**
 * Send a message to a client via a socket connection.  The message is sent
 * including its string-terminating null character.
 * @param connectionHandle [in] Connection handle for the socket.
 * @param message [in] Message to send.
 * @return >= 0 if the message was sent, or \a -errno if an error occurred.
 * Test: none.
 */
static int
SendMessageToClient(
	int        connectionHandle,
	const char *message
	                )
{
	int status;
	bool wasPending;
	bool wasBlocked;

	/* Block the SIGPIPE signal if the client happens to have disconnected. */
	BlockSigpipeSignal( &wasPending, &wasBlocked );
	/* Send the message to the client. */
#ifdef AUTOTEST_SKIP_COMMUNICATIONS
	status = strlen( message ) + sizeof( char );
#else
	status = write( connectionHandle, message,
					strlen( message ) + sizeof( char ) );
#endif
#ifdef AUTOTEST
	printf( "Sent: \"%s\"\n", message );
#endif
	/* Restore the SIGPIPE signal action. */
	RestoreSigpipeSignal( wasPending, wasBlocked );
	return( status );
}



/**
 * Initialize the file caching module.
 * @return Nothing.
 * Test: none.
 */
void
InitializeFileCache(
	void
	                )
{
	/* Initialize the sigpipeMask constant. */
	sigemptyset( &sigpipeMask );
	sigaddset( &sigpipeMask, SIGPIPE );

#if 0
	sigset_t signalMask;
	sigset_t noSignals;
	struct sigaction action = {
		.sa_sigaction = SignalHandler,
		.sa_flags     = SA_SIGINFO | SA_RESTART
	};

	/* Set signal handler to avoid SIGPIPE termination during writes to
	   disconnected clients. The signal handler is set per-process; the
	   blocking of the SIGPIPE is set by the threads (more specifically,
	   around writes to clients). */
	sigemptyset( &signalMask );
	sigaddset( &signalMask, SIGPIPE );
	sigemptyset( &noSignals );
	memcpy( action.sa_mask, noSignals, sizeof( sigset_t ) );
	res = sigaction( signalMask, &action, NULL );
	if( res != 0 )
	{
		fprintf( stderr, "Cannot set SIGPIPE handler\n" );
		exit( 1 );
	}
#endif

    /* Create the files directory.  Ignore any errors for now (such as that
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
 * Test: none.
 */
void
ShutdownFileCache(
    void
	             )
{
	ShutdownFileCacheDatabase( );
	FreeRegexes( );
	ShutdownDownloadQueue( );
#if 0
	static const struct sigaction action = { .sa_handler = SIG_DFL };
	sigaction( signalMask, &action, NULL);
#endif
}



/**
 * Read the entire pending message and place it in a buffer.
 * @param connectionHandle [in] Socket connection handle.
 * @param clientMessage [out] Message received from the client.
 * @return Number of bytes read.
 * Test: none (however, it is used in so frequently that defects would cause
 *       several unit tests to report errors).
 */
/* During one of the automated unit tests, this function is replaced by a
   simulation. */
#ifndef AUTOTEST_SKIP_COMMUNICATIONS
static int
ReadEntireMessage(
    int  connectionHandle,
	char **clientMessage
                  )
{
	char buffer[ 256 ];
	int  nBytes;
	char *message     = NULL;
	int  messageSize  = 0;
	struct pollfd fds = { connectionHandle, POLLIN, 0 };
	int  pollRes;


	/* Read partial message and expand the buffer as necessary until the
	   entire message is received. */
	while( 0 < ( nBytes = recv( connectionHandle, buffer,
								sizeof( buffer ), 0 ) ) )
	{
		if( message == NULL )
		{
			message = malloc( nBytes + sizeof( char ) );
		}
		else
		{
			message = realloc( message, messageSize + nBytes + sizeof( char ) );
		}

		memcpy( &message[ messageSize ], buffer, nBytes );
		messageSize += nBytes;
		/* Check if the entire buffer has been read, using poll( ) with
		   a timeout of 0 to avoid blocking. */
		pollRes = poll( &fds, 1, 0 );
		if( ( pollRes <= 0 ) || ( ( fds.revents & POLLHUP ) == POLLHUP ) )
		{
			break;
		}
	}
	/* Check if the client has disconnected and return an empty string
	   and -ENOTCONN. */
	if( nBytes == 0 )
	{
		(void) poll( &fds, 1, 0 );
		if( ( fds.revents & POLLHUP ) == POLLHUP )
		{
			if( message != NULL )
			{
				message[ 0 ] = '\0';
			}
			messageSize = -ENOTCONN;
		}
	}
	else
	{
		/* Zero-terminate for good measure. */
		message[ messageSize ] = '\0';
	}

	*clientMessage = message;
	return( messageSize );
}
#endif /* AUTOTEST_SKIP_COMMUNICATIONS */



/**
 * Thread that waits for requests and handles them appropriately.  Each client
 * connection gets its own thread so that one thread may wait for a message
 * without blocking other threads.
 * @param data [in] Thread context: the client connection.
 * @return Thread status (always 0 ).
 * Test: unit test (test-filecache.c).
 */
STATIC void*
ReceiveRequests(
	void *data
                )
{
	struct CacheClientConnection *clientConnection = data;

	char         *message;
	int          status;
	struct ucred credentials;
	socklen_t    credentialsLength = sizeof( struct ucred );
	bool         connected         = true;

	while( connected )
	{
#ifdef AUTOTEST
		printf( "Waiting for message...\n" );
#endif

		status = ReadEntireMessage( clientConnection->connectionHandle,
									&message );
		if( 0 <= status )
		{
			getsockopt( clientConnection->connectionHandle, SOL_SOCKET,
						SO_PEERCRED, &credentials, &credentialsLength );
			clientConnection->uid = credentials.uid;
			clientConnection->gid = credentials.gid;
			clientConnection->pid = credentials.pid;
			CommandDispatcher( clientConnection, message );
			free( message );
		}
		else
		{
			connected = false;
		}
		if( status == 0 )
		{
			printf( "Exiting\n" );
			exit( 1 );
		}
	}
	printf( "Lost connection, connection thread exiting\n" );
	pthread_exit( NULL );

	return( NULL );
}



/**
 * Invoke the appropriate function depending on the message received from the
 * cache client.
 * @param clientConnection [in] Client Information structure with information
 *        about the cache client.
 * @param message [in] Client message.
 * @return Nothing.
 * Test: unit test (test-filecache.c).
 */
STATIC int
CommandDispatcher(
	struct CacheClientConnection *clientConnection,
	const char                   *message
	              )
{
	const char *command;
	int        (*commandFunction)( struct CacheClientConnection*, const char* );
	int        entry;
	int        status = 0;

	/* List of commands and functions to handle the command. */
	const struct
	{
		const char *command;
		int        (*commandFunction)( struct CacheClientConnection *clientInfo,
									   const char *message );
	} dispatchTable[ ] =
	    {
			{ "FILE",       ClientRequestsLocalFilename },
			{ "CREATE",     ClientRequestsCreate },
			{ "CACHE",      ClientRequestsDownload },
			{ "DROP",       ClientRequestsFileClose },
			{ "CONNECT",    ClientConnects },
			{ "DISCONNECT", ClientDisconnects },
			{ "QUIT",       ClientRequestsShutdown },
			{ "DEBUG",      ClientRequestsDebugMessage },
			{ NULL, NULL }
		};

	/* Find the command in the list and execute it. */
	entry = 0;
	while( ( command = dispatchTable[ entry ].command ) != NULL )
	{
		if( strncasecmp( message, command, strlen( command ) ) == 0 )
		{
			printf( "executing command %s\n", command );
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
		status = SendMessageToClient( clientConnection->connectionHandle,
									  "ERROR: unknown command" );
		/* If the client has terminated, the pipe is broken.  Terminate
		   the receiving thread. */
		if( status == -EPIPE )
		{
			printf( "Pipe broken, exiting thread\n" );
			pthread_exit( NULL );
		}
	}

	return (status );
}



/**
 * Create a local filename for the S3 file, and create a database entry
 * with the two related file names and the S3 file attributes.
 * @param bucket [in] Bucket in which the S3 file is stored.
 * @param path [in] Full S3 pathname for the file.
 * @param uid [in] Ownership of the S3 file.
 * @param gid [in] Ownership of the S3 file.
 * @param permissions [in] Permissions for the S3 file.
 * @param mtime [in] Last modification time of the S3 file.
 * @param parentId [in] ID of the parent directory.
 * @param localfile [out] Filename of the local file.
 * @return ID for the database entry, or \a -1 if an error occurred.
 * Test: unit test (test-filecache.c).
 */
STATIC sqlite3_int64
CreateLocalFile(
	const char    *bucket,
    const char    *path,
    int           uid,
    int           gid,
    int           permissions,
    time_t        mtime,
	sqlite3_int64 parentId,
    char          **localfile
	            )
{
	const char    *template = "XXXXXX";
    char          *localname;
    int           fileHd;
    sqlite3_int64 id;
	bool          exists;

    assert( path != NULL );

    /* Construct a template for the filename. */
    localname = malloc( strlen( CACHE_INPROGRESS ) + strlen( template )
						+ sizeof( char ) );
	strcpy( localname, CACHE_INPROGRESS );
	strcat( localname, template );
    /* Create a uniquely named, empty file. */
	fileHd = mkstemp( localname );
    close( fileHd );

    /* Keep the non-redundant part of the local filename. */
	*localfile = malloc( strlen( localname ) - strlen( template )
						 + sizeof( char ) );
    strcpy( *localfile,
			&localname[ strlen( localname ) - strlen( template ) ] );

    /* Insert the filename combo into the database. */
	id = Query_CreateLocalFile( bucket, path, uid, gid, permissions,
								mtime, parentId, *localfile, &exists );
	/* Delete the unique file if the file was already in the database. */
	if( exists || ( id == 0 ) )
	{
		unlink( localname );
	}
    free( localname );

    return( id );
}



/**
 * Create a local directory where downloaded files or newly created files
 * are stored and keep it in the database.  The local directory serves to
 * mimic ownership and permissions of an S3 parent directory.
 * @param path [in] Directory path on the S3 storage.
 * @param uid [in] uid of the directory.
 * @param gid [in] gid of the directory.
 * @param permissions [in] Permission flags of the directory.
 * @return Database id of the directory entry in the database.
 * Test: unit test (test-filecache.c).
 */
STATIC sqlite3_int64
CreateLocalDir(
    const char *path,
    uid_t      uid,
    gid_t      gid,
    int        permissions
	            )
{
	const char    *template = "XXXXXX";
    char          *localname;
    char          *localdir;
    sqlite3_int64 id;
	bool          alreadyExists;

    assert( path != NULL );

    /* Construct a template for the filename. */
    localname = malloc( strlen( CACHE_INPROGRESS ) + strlen( template )
						+ sizeof( char ) );
	strcpy( localname, CACHE_INPROGRESS );
	strcat( localname, template );
    /* Create a uniquely named directory. */
	(void) mkdtemp( localname );
    /* Keep the non-redundant part of the local directory name. */
	localdir = malloc( strlen( localname ) - strlen( template )
					   + sizeof( char ) );
    strcpy( localdir,
			&localname[ strlen( localname ) - strlen( template ) ] );

    /* Insert the filename combo into the database. */
	id = Query_CreateLocalDir( path, (int) uid, (int) gid, permissions,
							   localdir, &alreadyExists );
	/* Delete the temporary directory if it had already been inserted. */
	if( alreadyExists )
	{
		rmdir( localname );
	}
    free( localname );
	free( localdir );

    return( id );
}



/**
 * Thread that listens to new connections and adds client connections as
 * they are detected.  A new thread is initiated for each new connection
 * so that the process is able to wait for activity on them without blocking
 * the other threads.
 * @return Thread status (always 0 ).
 * Test: none (however, if the function failed, several unit tests would
 *       report errors).
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
			/* Connected. No action required. */
		}
    }
	printf( "Exiting\n" );
    unlink( SOCKET_NAME );

	return( 0 );
}
#pragma GCC diagnostic pop



/**
 * Close the connection and terminate the connection thread.
 * @param clientConnection [in] Client Connection structure.
 * @param request [in] Request parameters (unused).
 * @return Nothing.
 * Test: implied blackbox (test-filecache.c).
 */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
STATIC int
ClientDisconnects(
    struct CacheClientConnection *clientConnection,
	const char                   *request
	              )
{
	close( clientConnection->connectionHandle );
	pthread_exit( NULL );
	return( 0 );
}
#pragma GCC diagnostic pop



/**
 * Shutdown the file cache.  This function requires the client to either be
 * root or have the same uid as the file cache.
 * @param clientConnection [in] Client Connection structure.
 * @param request [in] Request parameters (unused).
 * @return Nothing.
 * Test: implied blackbox (in test-process.c).
 */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
STATIC int
ClientRequestsShutdown(
    struct CacheClientConnection *clientConnection,
	const char                   *request
	                   )
{
	if( ( clientConnection->uid == 0 ) || clientConnection->uid == getuid( ) )
	{
		ShutdownFileCache( );
		exit( EXIT_SUCCESS );
	}

	return( 0 );
}
#pragma GCC diagnostic pop



/**
 * The client sends a connect message passing the bucket, the key ID, and the
 * secret key.  The CacheClientConnection structure for the client is updated
 * with this information.
 * The server responds with CONNECTED or ERROR.
 * @param clientConnection [in/out] CacheClientConnection structure for the
 *        client.
 * @param request [in] Connection request parameters.
 * @return 0 on success, or \a -errno on failure.
 * Test: unit test (test-filecache.c).
 */
STATIC int
ClientConnects(
    struct CacheClientConnection *clientConnection,
	const char                   *request
	          )
{
	char       *bucket;
	char       *uidStr;
	char       *keyId;
	char       *secretKey;
	GMatchInfo *matchInfo;
    uid_t      uid;
	int        status = -EINVAL;

	/* Grep the bucket, the key, and the secret key. */
	g_regex_ref( regexes.connectAuth );
	if( g_regex_match( regexes.connectAuth, request, 0, &matchInfo ) )
	{
		bucket    = g_match_info_fetch( matchInfo, 1 );
		uidStr    = g_match_info_fetch( matchInfo, 2 );
		keyId     = g_match_info_fetch( matchInfo, 3 );
		secretKey = g_match_info_fetch( matchInfo, 4 );
		g_match_info_free( matchInfo );

		/* Store the bucket name and the keys in the client connection info
		   structure, and return a success message if the keys have the
		   correct length. */
		if( ( strlen( keyId ) == 20 ) && ( strlen( secretKey ) == 40 ) )
		{
			clientConnection->bucket = bucket;
			strncpy( clientConnection->keyId, keyId,
					 sizeof( clientConnection->keyId ) );
			strncpy( clientConnection->secretKey, secretKey,
					 sizeof( clientConnection->secretKey ) );
			/* Add the user to the database. */
			uid = atoi( uidStr );
			Query_AddUser( uid, keyId, secretKey );
			SendMessageToClient( clientConnection->connectionHandle,
								 "CONNECTED" );
			status = 0;
		}
		else
		{
			g_free( bucket );
			status = -EKEYREJECTED;
		}
		g_free( keyId );
		g_free( secretKey );
	}
	g_regex_unref( regexes.connectAuth );

	if( status != 0 )
	{
		SendMessageToClient( clientConnection->connectionHandle,
							 "ERROR: unable to parse keys" );
	}

	return( status );
}



/**
 * The client requests the local path name of a file that may be cached.  The
 * function returns the pathname as the garbled directory name and the garbled
 * file name.
 * @param clientConnection [in/out] CacheClientConnection structure for the
 *        client.
 * @param request [in] Creation request parameters with the filename of the
 *        remove file.
 * @return Name of the local file, or \a NULL the file was not cached.
 * Test: unit test (test-filecache.c).
 */
STATIC int
ClientRequestsLocalFilename(
    struct CacheClientConnection *clientConnection,
	const char                   *request
	                        )
{
	const char *trimmed;
	const char *localpath;
	char message[ 5 + 7 + 6 + 1 ];

	/* Trim the request string, but do not free it, as happens in the TrimString
	   function. */
	g_regex_ref( regexes.trimString );
	trimmed = g_regex_replace( regexes.trimString, request,
							   -1, 0, "", 0, NULL );
	g_regex_unref( regexes.trimString );

	localpath = Query_GetLocalPath( trimmed );
	if( localpath == NULL )
	{
		sprintf( message, "FILE -" );
	}
	else
	{
		sprintf( message, "FILE %s", localpath );
	}
	free( (char*) trimmed );
	free( (char*) localpath );
	SendMessageToClient( clientConnection->connectionHandle, message );

	return( 0 );
}



/**
 * Enter client subscribtion to a file for download.
 * @param clientConnection [in] Client Connection structure.
 * @param request [in] Request parameters.
 * @return Always \a 0.
 */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
STATIC int
ClientRequestsDownload(
    struct CacheClientConnection *clientConnection,
	const char                   *request
	                  )
{
	sqlite3_uint64 fileId;
	char           localname[ 7 ]; /* unused */
	bool           isCached;

	/* Determine the file ID for the path. */
	fileId = FindFile( request, localname );
	isCached = Query_IsFileCached( fileId );
	if( isCached )
	{
		SendMessageToClient( clientConnection->connectionHandle, "OK" );
	}
	else
	{
		if( 0 < fileId )
		{
			ReceiveDownload( fileId, clientConnection->uid );
			SendMessageToClient( clientConnection->connectionHandle, "OK" );
		}
		else
		{
			SendMessageToClient( clientConnection->connectionHandle, "ERROR " );
		}
	}
	return( 0 );
}



/**
 * 
 * @param clientConnection [in/out] CacheClientConnection structure for the
 *        client.
 * @param request [in] Subscription request parameters.
 * @return 0 on success, or \a -errno on failure.
 * Test: implied blackbox (test-filecache.c and test-process.c).
 */
STATIC int ClientRequestsCreate(
    struct CacheClientConnection *clientConnection,
	const char                   *request
	                           )
{
	int         status;

	GMatchInfo *matchInfo;
	gchar      *parentUidStr;
	gchar      *parentGidStr;
	gchar      *parentPermissionsStr;
	gchar      *uidStr;
	gchar      *gidStr;
	gchar      *permissionsStr;
	gchar      *mtimeStr;
	gchar      *path;

	char          *parentdir;
	int           parentUid;
	int           parentGid;
	int           parentPermissions;
	sqlite3_int64 parentId;
	int           uid;
	int           gid;
	int           permissions;
	long long     mtime;
	char          *filename;
	char          *localfile;
	char          *reply;
	sqlite3_int64 fileId;

	/* Extract parent uid, parent gid, parent permissions, uid, gid,
	   permissions, mtime, and filename. */
	g_regex_ref( regexes.createFileOptions );
	if( g_regex_match( regexes.createFileOptions, request, 0, &matchInfo ) )
	{
		parentUidStr         = g_match_info_fetch( matchInfo, 1 );
		parentGidStr         = g_match_info_fetch( matchInfo, 2 );
		parentPermissionsStr = g_match_info_fetch( matchInfo, 3 );
		uidStr               = g_match_info_fetch( matchInfo, 4 );
		gidStr               = g_match_info_fetch( matchInfo, 5 );
		permissionsStr       = g_match_info_fetch( matchInfo, 6 );
		mtimeStr             = g_match_info_fetch( matchInfo, 7 );
		path                 = g_match_info_fetch( matchInfo, 8 );
		g_match_info_free( matchInfo );
		/* Create numeric values for uid, gid, permissions, and mtime. */
		parentUid         = atoi( parentUidStr );
		parentGid         = atoi( parentGidStr );
		parentPermissions = atoi( parentPermissionsStr );
		uid               = atoi( uidStr );
		gid               = atoi( gidStr );
		permissions       = atoi( permissionsStr );
		mtime             = atoll( mtimeStr );
		g_free( parentUidStr );
		g_free( parentGidStr );
		g_free( parentPermissionsStr );
		g_free( uidStr );
		g_free( gidStr );
		g_free( permissionsStr );
		g_free( mtimeStr );

		filename = TrimString( path );
		/* Identify the directory name. */
		parentdir = g_path_get_dirname( path );
		if( strcmp( parentdir, "." ) == 0 )
		{
			parentdir[ 0 ] = '/';
		}

		/* Create a local name for the directory and get the ID of
		   its database entry. */
		parentId = CreateLocalDir( parentdir, parentUid,
								   parentGid, parentPermissions );
		free( parentdir );
		if( 0 < parentId )
		{
			/* Create a local file for the filename and return the name.
			   The creation automatically increments the subscription count. */
			if( ( fileId = CreateLocalFile( clientConnection->bucket, filename,
											uid, gid, permissions,
											mtime, parentId, &localfile ) )
				> 0 )
			{
				reply = malloc( strlen( "CREATED " ) + sizeof( localfile )
								+ 15 + sizeof( char ) );
				sprintf( reply, "CREATED %s %lld", localfile, fileId );
				SendMessageToClient( clientConnection->connectionHandle,
									 reply );
				free( reply );
				status = 0;
			}
			/* Couldn't create the local file. */
			else
			{
				status = -EIO;
			}
		}
		/* Couldn't create the parent directory. */
		else
		{
			status = -EIO;
			SendMessageToClient( clientConnection->connectionHandle,
								 "ERROR: cannot create local directory" );
		}
	}
	else
	{
		status = -EINVAL;
		SendMessageToClient( clientConnection->connectionHandle,
							 "ERROR: cannot parse request parameters" );
	}

	free( filename );
	g_regex_unref( regexes.createFileOptions );

	return( status );
}



/**
 * Remove whitespace before and after a string.  The original string memory
 * is released.
 * @param original [in/out] String with possibly leading and/or trailing
 *        whitespace.
 * @return String without leading and trailing whitespace.
 * Test: unit test (test-filecache.c).
 */
STATIC char*
TrimString(
	char *original
	       )
{
	char *trimmed;

	g_regex_ref( regexes.trimString );
	trimmed = g_regex_replace( regexes.trimString, original,
							   -1, 0, "", 0, NULL );
	g_free( original );
	g_regex_unref( regexes.trimString );
	return( trimmed );
}



/**
 * Compile regular expressions before use.
 * @return Nothing.
 * Test: implied blackbox (test-filecache.c).
 */
STATIC void
CompileRegexes(
    void
	           )
{
	/* Grep "bucket:uid:keyid:secretkey" from the parameter string. */
	const char const *connectAuth =
		"^\\s*([a-zA-Z0-9-\\+_]+)\\s*:\\s*([0-9]{1,5})\\s*:\\s*"
		"([a-zA-Z0-9\\+/=]{20})\\s*:\\s*([a-zA-Z0-9\\+/=]{40})\\s*$";

	/* Grep uid:gid:perm:mtime:string */
	const char const *createFileOptions =
		"([0-9]{1,5})\\s*:\\s*([0-9]{1,5})\\s*:\\s*([0-9]{1,3})\\s*:\\s*"
		"([0-9]{1,5})\\s*:\\s*([0-9]{1,5})\\s*:\\s*([0-9]{1,3})\\s*:\\s*"
		"([0-9]{1,20})\\s*:\\s*(.+)";

    /* Replace leading and trailing spaces. */
	const char const *trimString = "^[\\s]+|[\\s]+$";

	/* Rename a file or directory. */
	const char const *rename = "(FILE|DIR)\\s*(.+)";

	/* Extract the hostname from a URL. */
	const char const *hostname = "^http(s?)://(.+\\.amazonaws\\.com).*";

	/* Extract the region from a URL. */
	const char const *regionPart =
		"^http[s]?://([^\\.]+\\.)?([^\\.]+)\\.amazonaws\\.com";

	/* Extract the file path from a URL. */
	const char const *removeHost = "^http[s]?://.+\\.amazonaws\\.com(/.*)$";

	/* Extract the upload ID from an S3 response. */
	const char const *getUploadId = "<UploadId>[\\s]*(.+)[\\s]*</UploadId>";


	/* Compile regular expressions. */
    #define COMPILE_REGEX( regex ) regexes.regex = \
			g_regex_new( regex, G_REGEX_OPTIMIZE, G_REGEX_MATCH_NOTEMPTY, NULL )

	COMPILE_REGEX( connectAuth );
	COMPILE_REGEX( createFileOptions );
	COMPILE_REGEX( trimString );
	COMPILE_REGEX( rename );
	COMPILE_REGEX( hostname );
	COMPILE_REGEX( regionPart );
	COMPILE_REGEX( removeHost );
	COMPILE_REGEX( getUploadId );
}



/**
 * Release the allocations in the regular expressions.
 * @return Nothing.
 * Test: none.
 */
static void
FreeRegexes(
	void
	        )
{
	g_regex_unref( regexes.connectAuth );
	g_regex_unref( regexes.createFileOptions );
	g_regex_unref( regexes.trimString );
	g_regex_unref( regexes.rename );
	g_regex_unref( regexes.hostname );
	g_regex_unref( regexes.regionPart );
	g_regex_unref( regexes.removeHost );
	g_regex_unref( regexes.getUploadId );
}



/**
 * This function tests that the permissions grant module is available.
 * @param clientConnection [in] Unused.
 * @param request [in] Unused.
 * @return Always 0.
 * Test: none.
 */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
static int
ClientRequestsDebugMessage(
    struct CacheClientConnection *clientConnection,
	const char                   *request
	                       )
{
	char buffer[ 50 ];

	/* Send a message that does not carry a valid request. */
	sprintf( buffer, "DEBUG test socket" );
	SendGrantMessage( testSocket, buffer, buffer, sizeof( buffer ) );
	SendMessageToClient( clientConnection->connectionHandle, buffer );
	return( 0 );
}
#pragma GCC diagnostic pop



/**
 * Close a file, marking it ready for synchronization.
 * @param clientConnection [in/out] CacheClientConnection structure for the
 *        client.
 * @param request [in] File close request parameter string with the file ID.
 * @return Always \0.
 */
static int
ClientRequestsFileClose(
	struct CacheClientConnection *clientConnection,
    const char                   *request
	                    )
{
	sqlite3_int64 fileId;
	char localname[ 14 ];

	bool result;

	fileId = FindFile( request, localname );
	if( fileId > 0 )
	{
		result = Query_DecrementSubscriptionCount( fileId );
		printf( "Decremented subscription count for %d with status %d\n", (int)fileId, result );
	}
	SendMessageToClient( clientConnection->connectionHandle, "OK" );
	return( 0 );
}



/**
 * Calculate how many parts a multipart upload should be divided into.
 * Anything above 5 GBytes must be split into multiple uploads, but much
 * smaller files may be split as well.  This is preferred on computers
 * that may not be on 24/7 or which have limited upload bandwidth or
 * unstable connectivity.  These computers may benefit from partial uploads
 * allowing the upload to be cancelled and resumed later.  The maximum
 * number of parts may not exceed 10,000 transfers, and each part must be
 * at least 5 MBytes.
 * @param filesize [in] The total file size.
 * @return Number of multipart uploads for this file.
 * Test: unit test (in test-uploadqueue.c).
 */
int
NumberOfMultiparts(
	long long int filesize
	               )
{

	const int           CHUNK_SIZE         = PREFERRED_CHUNK_SIZE * MEGABYTES;
	const int           MAXIMUM_MULTIPARTS = 10000;

	int                 parts;

	/* Attempt to divide the file into chunks of the preferred size. */
	if( filesize <= ( long long int ) CHUNK_SIZE * MAXIMUM_MULTIPARTS )
	{
		parts = ( filesize + CHUNK_SIZE - 1 ) / CHUNK_SIZE;
	}
	/* If that is not possible, we'll have to accept the fact that the
	   upload chunks are bigger than the preferred size. */
	else
	{
		parts = MAXIMUM_MULTIPARTS;
	}

	return( parts );
}



