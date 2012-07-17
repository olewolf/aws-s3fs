/**
 * \file downloadqueue.c
 * \brief Download queue for the file cache server.
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
#include <malloc.h>
#include <assert.h>
#include <sys/stat.h>
#include <string.h>
#include <pthread.h>
#include <glib-2.0/glib.h>
#include <errno.h>
#include "aws-s3fs.h"
#include "filecache.h"
#include "s3comms.h"
#include "socket.h"


STATIC GQueue downloadQueue = G_QUEUE_INIT;

/* Event trigger and queue lock for the download manager. */
STATIC pthread_mutex_t mainLoop_mutex = PTHREAD_MUTEX_INITIALIZER;
STATIC pthread_cond_t  mainLoop_cond  = PTHREAD_COND_INITIALIZER;


STATIC struct
{
	bool   isReady;
	CURL   *curl;
	S3COMM *s3Comm;
} downloaders[ MAX_SIMULTANEOUS_DOWNLOADS ];



struct DownloadSubscription
{
	sqlite3_int64   fileId;
	bool            downloadActive;
	bool            downloadComplete;
	int             subscribers;
	/* Used by clients to wait for the download to complete. */
	pthread_cond_t  waitCond;
	pthread_mutex_t waitMutex;
	/* Used by the downloader to wait for the last client to unsubscribe. */
	pthread_cond_t  acknowledgeCond;
	pthread_mutex_t acknowledgeMutex;
};


struct DownloadStarter
{
	int                         socket;
	int                         downloader;
	struct DownloadSubscription *subscription;
};



STATIC int FindAvailableDownloader( void );
STATIC void *BeginDownload( void *threadCtx );
STATIC void UnsubscribeFromDownload(
	struct DownloadSubscription *subscription );
STATIC struct DownloadSubscription *GetSubscriptionFromQueue( void );
STATIC bool MoveToSharedCache( int socketHandle,
							   const char *parentname, uid_t parentUid,
							   gid_t parentGid, const char *filename,
							   uid_t uid, gid_t gid );
int SendGrantMessage( int socketHandle, const char *privopRequest,
					  char *reply, int replyMaxLength );



void
InitializeDownloadQueue(
	void
                        )
{
}



void
ShutdownDownloadQueue(
	void
                      )
{
}



/**
 * Helper function for the \a ScheduleDownload function which serves to
 * identify the data that is searched for.
 * @param queueData [in] Pointer to the data field in a GQueue queue.
 * @param cmpVal [in] Pointer to the data that the data should be compared with.
 * @return \a 1 if the data matches, or \0 otherwise.
 * Test: implied blackbox (test-downloadqueue.c).
 */
static int
FindInDownloadQueue(
	gconstpointer queueData,
	gconstpointer cmpVal
	                )
{
	const struct DownloadSubscription *subscription = queueData;
	sqlite3_int64                     value         = * (sqlite3_int64*) cmpVal;

	return( value == subscription->fileId ? 0 : 1 );
}



/**
 * Add a file to the download queue, or subscribe to a file that has already
 * been added to the download queue. The thread waits until the file has been
 * downloaded.
 * @param fileId [in] The ID of the file in the database.
 * @return Nothing.
 * Test: unit test (test-downloadqueue.c).
 */
void
ReceiveDownload(
	sqlite3_int64 fileId
	            )
{
	GList                       *subscriptionEntry;
	struct DownloadSubscription *subscription;
	pthread_mutexattr_t         mutexAttr;
	pthread_condattr_t          condAttr;

	/* Find out if a subscription to the same file is already queued. */
	pthread_mutex_lock( &mainLoop_mutex );
	subscriptionEntry = g_queue_find_custom( &downloadQueue, &fileId,
											 FindInDownloadQueue );
	/* No other subscriptions, so create one. */
	if( subscriptionEntry == NULL )
	{
		/* Create a new subscription entry. */
		subscription = malloc( sizeof( struct DownloadSubscription ) );
		subscription->fileId         = fileId;
		subscription->downloadActive = false;
		subscription->subscribers    = 1;
		pthread_mutexattr_init( &mutexAttr );
		pthread_mutex_init( &subscription->waitMutex, &mutexAttr );
		pthread_mutex_init( &subscription->acknowledgeMutex, &mutexAttr );
		pthread_mutexattr_destroy( &mutexAttr );
		pthread_condattr_init( &condAttr );
		pthread_cond_init( &subscription->waitCond, &condAttr );
		pthread_cond_init( &subscription->acknowledgeCond, &condAttr );
		pthread_condattr_destroy( &condAttr );
		/* Add the entry to the transfers list. */
		Query_AddDownload( fileId, getuid( ) );
		/* Enqueue the entry. */
		g_queue_push_tail( &downloadQueue, subscription );
	}
	/* Another thread is subscribing to the same download, so use its wait
	   condition. */
	else
	{
		/* Subscribe to the download. */
		subscription = subscriptionEntry->data;
		subscription->subscribers++;
	}

	/* Lock the download completion mutex before the download is clear to
	   begin. */
	pthread_mutex_lock( &subscription->waitMutex );
	/* Tell the download manager that a new subscription has been entered. */
	pthread_cond_signal( &mainLoop_cond );
	pthread_mutex_unlock( &mainLoop_mutex );

	/* Wait until the download is complete. */
	pthread_cond_wait( &subscription->waitCond, &subscription->waitMutex );
	pthread_mutex_unlock( &subscription->waitMutex );

	/* Unsubscribe from the download. */
	UnsubscribeFromDownload( subscription );
}



/**
 * The opposite of scheduling a download: unsubscribing from the list of
 * users that request the download. This should be done after receiving a
 * signal that the download has completed.
 * @param subscription [in/out] The subscription to the download.
 * @return Nothing.
 * Test: unit test (test-downloadqueue.c).
 */
STATIC void
UnsubscribeFromDownload(
	struct DownloadSubscription *subscription
	                    )
{
	/* Decrement the subscription count. */
	pthread_mutex_lock( &mainLoop_mutex );
	/* Don't do anything if another thread has set the subscription count
	   to zero (although since we're still here, how should that happen?). */
	if( subscription->subscribers != 0 )
	{
		subscription->subscribers = subscription->subscribers - 1;
		/* If we're the last subscriber, tell the download manager that it is
		   safe to delete the subscription entry. */
		if( subscription->subscribers == 0 )
		{
			pthread_mutex_lock( &subscription->acknowledgeMutex );
			pthread_cond_signal( &subscription->acknowledgeCond );
			pthread_mutex_unlock( &subscription->acknowledgeMutex );
		}
	}
	pthread_mutex_unlock( &mainLoop_mutex );
}












/**
 * Pull requests from the download queue and start downloads. This function
 * is started as a thread.
 * @param socket [in] The socket handle for communicating with the permissions
 *        grant module.
 * @return Nothing.
 */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
void*
ProcessDownloadQueue(
	void *socket
	                 )
{
	int                         i;
	struct DownloadSubscription *subscription;
	int                         downloader;
	struct DownloadStarter      *downloadStarter;
	pthread_t                   threadId;
	int                         socketHandle;

	socketHandle = * (int*) socket;

	/* Initialize downloaders. The S3COMM handle is not acquired via
	   s3_open( ), because for downloads, its contents are file specific
	   and would require us to open and close the s3comms module for each
	   file.  We can "fake" an S3COMM handle, however, because we just use it
	   for the s3BuildRequest function, which uses it only for reading the
	   region, the bucket, the keyId, and the secretKey entries.  All we need
	   to do is specify these values in the handle for each download. */
	curl_global_init( CURL_GLOBAL_ALL );
	for( i = 0; i < MAX_SIMULTANEOUS_DOWNLOADS; i++ )
	{
		downloaders[ i ].curl    = curl_easy_init( );
		downloaders[ i ].s3Comm  = malloc( sizeof( S3COMM ) );
		downloaders[ i ].isReady = true;
	}

	/* Main loop. */
	for( ; ; )
	{
		/* Wait for a subscription entry or a download completion. */

		/* Prevent other threads from modifying the queue while we're
		   processing it. */
		pthread_mutex_lock( &mainLoop_mutex );

		/* Fill the download slots with downloads until either all download
		   slots are occupied or the queue is empty. */
		while( ( downloader = FindAvailableDownloader( ) ) != -1 )
		{
			/* Find an entry in the queue that isn't processed yet. */
			subscription = GetSubscriptionFromQueue( );
			if( subscription != NULL )
			{
				subscription->downloadActive      = true;
                downloaders[ downloader ].isReady = false;
				downloadStarter = malloc( sizeof( struct DownloadStarter ) );
				downloadStarter->downloader   = downloader;
				downloadStarter->subscription = subscription;
				downloadStarter->socket       = socketHandle;
				pthread_create( &threadId, NULL, BeginDownload,
								downloadStarter );
			}
			else
			{
				break;
			}
		}

		/* If the queue is emptied or all download slots are occupied,
		   then wait until there are new entries in the queue or the download
		   finishes, whichever comes first.
		   We still hav the mutex lock, so we won't receive any signals yet.
		   Any new entries or finished downloads will be waiting for us to
		   release the lock. */
		if( ( downloader == -1 ) || ( subscription == NULL ) )
		{
			pthread_cond_wait( &mainLoop_cond, &mainLoop_mutex );
		}
		/* Otherwise, process more entries until the queue is empty or all
		   download slots are taken. */
		pthread_mutex_unlock( &mainLoop_mutex );
	}


	/* Take down downloaders. */
	for( i = 0; i < MAX_SIMULTANEOUS_DOWNLOADS; i++ )
	{
		curl_easy_cleanup( downloaders[ i ].curl );
	}
	curl_global_cleanup( );
}
#pragma GCC diagnostic pop



/**
 * Find any downloader that is not currently busy.
 * @return Handle of an available downloader, or -1 if all downloaders are
 *         occupied.
 * Test: unit test (test-downloadqueue.c).
 */
STATIC int
FindAvailableDownloader(
	void
	                    )
{
	int i;

	/* Find an available downloader. */
	for( i = 0; i < MAX_SIMULTANEOUS_DOWNLOADS; i++ )
	{
		if( downloaders[ i ].isReady )
		{
			return( i );
		}
	}
	/* Return -1 if no downloader was available. */
	return( -1 );
}



/**
 * Determine the region based on the hostname.
 * @param url [in] Complete S3 URL.
 * @return Bucket region for the specified hostname.
 * Test: unit test (test-downloadqueue.c).
 */
STATIC enum bucketRegions
HostnameToRegion(
	const char *url
	             )
{
    static const char *amazonHost[ ] =
    {
        "s3",                /* US_STANDARD */
		"s3-us-west-2",      /* OREGON */
		"s3-us-west-1",      /* NORTHERN_CALIFORNIA */
		"s3-eu-west-1",      /* IRELAND */
		"s3-ap-southeast-1", /* SINGAPORE */
		"s3-ap-northeast-1", /* TOKYO */
		"s3-sa-east-1"       /* SAO_PAULO */
    };
	GMatchInfo         *matchInfo;
	const char         *regionStr;
	enum bucketRegions region;

	g_regex_match( regexes.regionPart, url, 0, &matchInfo );
	regionStr = g_match_info_fetch( matchInfo, 2 );
	g_match_info_free( matchInfo );

	printf( "url %s yields region string: %s\n", url, regionStr );
	if( regionStr != NULL )
	{
		for( region = 0;
			 region < sizeof( amazonHost ) / sizeof( char* ); region++ )
		{
			if( strcmp( amazonHost[ region ], regionStr ) == 0 )
			{
				break;
			}
		}
		free( (char*) regionStr );
		if( region == sizeof( amazonHost ) / sizeof( char* ) )
		{
			/* Handle unknown region error */
			region = -1;
		}
	}

	return( region );
}



/**
 * Start the next download in the download queue. This function must be
 * started as a thread so that it does not block other downloads.
 * @param ctx [in] Pointer to a DownloadStarter structure. The structure is
 *        freed from memory.
 * @return Nothing.
 * Test: unit test (test-downloadqueue.c).
 */
STATIC void*
BeginDownload(
	void *ctx
	          )
{
	int                         socketHandle;
	struct DownloadStarter      *downloadStarter;
	int                         downloader;
	struct DownloadSubscription *subscription;

	CURL              *curl;
	S3COMM            *s3Comm;
	struct curl_slist *headers;
	char              *downloadPath;
	char              *remotePath;
	char              *bucket;
	char              *keyId;
	char              *secretKey;
	char              *downloadFile;
	FILE              *downFile;
	const char        *hostname;
	GMatchInfo        *matchInfo;
	int               status;
	const char        *filepath;

	char              *parentname;
	uid_t             parentUid;
	gid_t             parentGid;
	char              *filename;
	uid_t             uid;
	gid_t             gid;
	int               permissions;

	struct timeval    now;
	struct timespec   oneMinute;

	downloadStarter = (struct DownloadStarter*) ctx;
	downloader      = downloadStarter->downloader;
	subscription    = downloadStarter->subscription;
	socketHandle    = downloadStarter->socket;
	free( ctx );

	curl   = downloaders[ downloader ].curl;
	s3Comm = downloaders[ downloader ].s3Comm;

	/* Fetch local filename, remote filename, bucket, keyId, and
	   secretKey. */
	pthread_mutex_lock( &mainLoop_mutex );
	Query_GetDownload( subscription->fileId, &bucket, &remotePath,
					   &downloadPath, &keyId, &secretKey );
	/* Set the path for the local file and open it for writing. */
	downloadFile = malloc( strlen( CACHE_INPROGRESS )
						   + strlen( downloadPath ) + sizeof( char ) );
	strcpy( downloadFile, CACHE_INPROGRESS );
	strcat( downloadFile, downloadPath );
	free( downloadPath );
	downFile = fopen( downloadFile, "w" );
	/* Set user write-only and mandatory locking (the latter is indicated by
	   group-execute off and set-group-ID on). */
/* OW: removed while debugging.
	chmod( downloadFile, S_IWUSR | S_ISGID );
*/

	/* Extract the hostname from the remote filename. */
	g_regex_match( regexes.hostname, remotePath, 0, &matchInfo );
	hostname = g_match_info_fetch( matchInfo, 2 );
	g_match_info_free( matchInfo );
	s3Comm->bucket    = (char*) bucket;
	s3Comm->keyId     = (char*) keyId;
	s3Comm->secretKey = (char*) secretKey;
	s3Comm->region    = HostnameToRegion( remotePath );

	g_regex_match( regexes.removeHost, remotePath, 0, &matchInfo );
	filepath = g_match_info_fetch( matchInfo, 1 );
	headers = BuildS3Request( s3Comm, "GET", hostname, NULL, filepath );
	free( bucket );
	free( keyId );
	free( secretKey );
	free( (char*) hostname );
	free( (char*) filepath );

	/* Download the file and wait until it has been received. */
	curl_easy_reset( curl );
	curl_easy_setopt( curl, CURLOPT_WRITEDATA, downFile );
	curl_easy_setopt( curl, CURLOPT_HTTPHEADER, headers );

	curl_easy_setopt( curl, CURLOPT_URL, remotePath );

	pthread_mutex_unlock( &mainLoop_mutex );
#ifdef AUTOTEST_SKIP_COMMUNICATIONS
	status = 0;
#else
	printf( "Executing HTTP request\n" );
	status = curl_easy_perform( curl );
#endif
	DeleteCurlSlistAndContents( headers );
	free( remotePath );
	fclose( downFile );

	if( status == 0 )
	{
		/* Determine the owners and the permissions of the file. */
		pthread_mutex_lock( &mainLoop_mutex );
	    Query_GetOwners( subscription->fileId,
						 &parentname, &parentUid, &parentGid,
						 &filename, &uid, &gid, &permissions );
		/* Set the permissions while we still own the file. */
		pthread_mutex_unlock( &mainLoop_mutex );
		chmod( downloadFile, permissions );
		free( downloadFile );
		/* Grant appropriate rights to the file and move it into the shared
		   cache folder. */
		MoveToSharedCache( socketHandle, parentname, parentUid, parentGid,
						   filename, uid, gid );
		free( parentname );
		free( filename );

		/* Lock the queue to prevent threads from subscribing to this
		   download after we broadcast a signal that the file is available. */
		pthread_mutex_lock( &mainLoop_mutex );
		/* Remove the file from the download queue and the downloads table. */
		g_queue_remove( &downloadQueue, subscription );
		status = Query_DeleteTransfer( subscription->fileId );
		/* Mark the downloader as ready for another download. */
		downloaders[ downloader ].isReady = true;
		/* Before signaling to the subscribers that the file is available,
		   make sure none of them can acknowledge before we're ready to
		   receive the acknowledgment. */
		pthread_mutex_lock( &subscription->acknowledgeMutex );
		pthread_mutex_lock( &subscription->waitMutex );
		subscription->downloadActive = false;
		/* Inform the subscribers that the file is available. */
		pthread_cond_broadcast( &subscription->waitCond );
		/* Inform the queue processor that a download slot is available. */
		pthread_cond_signal( &mainLoop_cond );
		pthread_mutex_unlock( &mainLoop_mutex );
		pthread_mutex_unlock( &subscription->waitMutex );
		/* Wait for the last subscriber to acknowledge that the subscription
		   count is zero. If no response is received within a minute, probably
		   some subscribers are dead. */
		if( subscription->subscribers != 0 )
		{
			gettimeofday( &now, NULL );
			oneMinute.tv_sec  = now.tv_sec + 60;
			oneMinute.tv_nsec = 0;
			pthread_cond_timedwait( &subscription->acknowledgeCond,
									&subscription->acknowledgeMutex,
									&oneMinute );
		}
		pthread_mutex_unlock( &subscription->acknowledgeMutex );
		/* Delete the subscription entry whether all threads have acknowledged
		   or not. */
		free( subscription );
	}

	pthread_exit( NULL );
}



/**
 * Find the next subscription entry that is not yet being downloaded, if any,
 * in the download queue. The entry is not removed from the queue, because
 * subscriptions should be allowed until the download is complete.
 * @return Next subscription entry, or NULL if no subscription entries are
 *         available.
 * Test: unit test (test-downloadqueue.c).
 */
STATIC struct DownloadSubscription*
GetSubscriptionFromQueue(
	void
	                     )
{
	struct DownloadSubscription *subscription = NULL;
	int                         index;

	index = 0;
	do
	{
		subscription = g_queue_peek_nth( &downloadQueue, index++ );
	} while( ( subscription != NULL ) && ( subscription->downloadActive ) );

	return( subscription );
}



/**
 * Function that interfaces with the priviliged process, which changes
 * ownership of parent directory and file and places them in the shared
 * cache.
 * @param socketHandle [in] Socket handle for the permissions grant module.
 * @param parentname [in] Local name of the parent directory.
 * @param parentUid [in] new uid for the local parent directory.
 * @param parentGid [in] new gid for the local parent directory.
 * @param filename [in] Local name of the downloaded file.
 * @param uid [in] new uid for the downloaded file.
 * @param uid [in] new gid for the downloaded file.
 * @return \a true if successful, or \a false otherwise.
 * Test: unit test (test-downloadqueue.c).
 */
#ifdef AUTOTEST
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#endif
STATIC bool
MoveToSharedCache(
	int        socketHandle,
	const char *parentname,
	uid_t      parentUid,
	gid_t      parentGid,
	const char *filename,
	uid_t      uid,
	gid_t      gid
	              )
{
	char *chownRequest;
	char *publishRequest;
	char reply[ 40 ];

	/* Give the directory its original owners. It was created with proper
	   permissions to begin with. */
	chownRequest = malloc( strlen( parentname ) + strlen( "chown xxxxx xxxxx " )
						   + sizeof( char ) );
	sprintf( chownRequest, "CHOWN %d:%d:", (int) parentUid, (int) parentGid );
	strcat( chownRequest, parentname );
	SendGrantMessage( socketHandle, chownRequest, reply, sizeof( reply ) );
#ifdef AUTOTEST
	printf( "1: %s\n", chownRequest );
#endif
	free( chownRequest );

	/* Similarly, give the downloaded file appropriate ownership. */
	chownRequest = malloc( strlen( parentname ) + sizeof( char )
						   + strlen( filename ) + strlen( "chown xxxxx xxxxx " )
						   + sizeof( char ) );
	sprintf( chownRequest, "CHOWN %d:%d:", (int) uid, (int) gid );
	strcat( chownRequest, parentname );
	strcat( chownRequest, "/" );
	strcat( chownRequest, filename );
	SendGrantMessage( socketHandle, chownRequest, reply, sizeof( reply ) );
#ifdef AUTOTEST
	printf( "2: %s\n", chownRequest );
#endif
	free( chownRequest );

	/* Move the file to the shared directory. */
	publishRequest = malloc( strlen( parentname ) + strlen( filename ) +
							 strlen( "publish :" ) + sizeof( char ) );
	sprintf( publishRequest, "PUBLISH %s:%s", parentname, filename );
	SendGrantMessage( socketHandle, publishRequest, reply, sizeof( reply ) );

	return( true );
}
#ifdef AUTOTEST
#pragma GCC diagnostic pop
#endif



/**
 * Send a message to the privileged process. The function does not return
 * until a reply is received from the privileged process.
 * @param privopRequest [in] Request message.
 * @return Nothing.
 * Test: unit test (test-process.c).
 */
int
SendGrantMessage(
	int        socketHandle,
    const char *privopRequest,
	char       *reply,
	int        replyMaxLength
	             )
{
	bool status;
	int  nBytes;
	int  fileHandle;

	/* Send the message to the privileged process. */
	status = SocketSendDatagramToServer( socketHandle, privopRequest,
										 strlen( privopRequest ) + 1 );
	if( status == true )
	{
		/* Receive the reply; the reply message is not used but serves only
		   to block the thread until the server has completed its task. */
		nBytes = SocketReceiveDatagramFromServer( socketHandle, reply,
												  replyMaxLength,
												  &fileHandle );
	}
	if( ( status == false ) || ( nBytes < 0 ) )
	{
		fprintf( stderr, "Error communicating with permissions grant\n" );
	}

	return( nBytes );
}
