/**
 * \file test-filedownloadqueue.c
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
#include <unistd.h>
#include <pthread.h>
#include <sys/stat.h>
#include "aws-s3fs.h"
#include "filecache.h"
#include "testfunctions.h"


#pragma GCC diagnostic ignored "-Wunused-parameter"

struct Configuration globalConfig;

extern struct
{
	bool   isReady;
	CURL   *curl;
	S3COMM *s3Comm;
} downloaders[ MAX_SIMULTANEOUS_DOWNLOADS ];


struct DownloadStarter
{
	int                         socket;
	int                         downloader;
	struct DownloadSubscription *subscription;
};


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

extern GQueue downloadQueue;
extern pthread_mutex_t mainLoop_mutex;
extern pthread_cond_t  mainLoop_cond;


extern sqlite3_int64 FindFile( const char *path, char *localname );
extern void LockDownloadQueue( void );
extern void UnlockDownloadQueue( void );
extern struct DownloadSubscription *GetSubscriptionFromQueue( void );
extern int FindAvailableDownloader( void );
extern enum bucketRegions HostnameToRegion( const char *hostname );
extern void CompileRegexes( void );
extern void UnsubscribeFromDownload( struct DownloadSubscription *sub );
extern void *BeginDownload( void *ctx );
extern struct DownloadSubscription *GetSubscriptionFromQueue( void );
extern bool MoveToSharedCache( int socketHandle, const char *parentname,
							   uid_t parentUid, gid_t parentGid,
							   const char *filename, uid_t uid, gid_t gid );
extern void *ProcessDownloadQueue( void *socket );
extern sqlite3 *GetCacheDatabase( void );

static void test_ReceiveDownload( const char *param );
static void test_FindAvailableDownloader( const char *param );
static void test_HostnameToRegion( const char *param );
static void test_UnsubscribeFromDownload( const char *param );
static void test_MoveToSharedCache( const char *param );
static void test_BeginDownload( const char *param );
static void test_GetSubscriptionFromQueue( const char *param );
static void test_ProcessDownloadQueue( const char *param );


/* Dummy function for testing. */
void DeleteCurlSlistAndContents( struct curl_slist *headers )
{
}
/* Dummy function for testing. */
struct curl_slist* BuildS3Request( S3COMM *instance, const char *httpMethod,
	const char *hostname, struct curl_slist *additionalHeaders,
	const char *filename )
{
	return( NULL );
}



/* Dummy function to allow the test program to compile. */
int ReadEntireMessage( int connectionHandle, char **clientMessage )
{
	return( 0 );
}


#define DISPATCHENTRY( x ) { #x, test_##x }
const struct dispatchTable dispatchTable[ ] =
{
    DISPATCHENTRY( ProcessDownloadQueue ),
    DISPATCHENTRY( ReceiveDownload ),
    DISPATCHENTRY( FindAvailableDownloader ),
    DISPATCHENTRY( HostnameToRegion ),
    DISPATCHENTRY( UnsubscribeFromDownload ),
    DISPATCHENTRY( BeginDownload ),
    DISPATCHENTRY( GetSubscriptionFromQueue ),
    DISPATCHENTRY( MoveToSharedCache ),
    { NULL, NULL }
};



/*-------------------------------------------------------------------------*/
static bool      test_ReceiveDownload_downloadSlotFree = true;
static pthread_t test_ReceiveDownload_monitor;
static int       test_ReceiveDownload_counter = 0;
static int       test_ReceiveDownload_processed;

static void *test_ReceiveDownload_SimulateDownload( void *data )
{
	struct DownloadSubscription *subscription = data;

	/* Occupy the download slot. */
//	printf( "Downloader: Starting download of file %d (%d subscriber(s))\n", (int)subscription->fileId, subscription->subscribers );
	pthread_mutex_lock( &mainLoop_mutex );
	test_ReceiveDownload_downloadSlotFree = false;
	pthread_mutex_unlock( &mainLoop_mutex );
	/* Simulated download time passes very quickly for this simulation. The
	   download finishes and the download thread signals to the monitor
	   that it is complete. */
//	printf( "Downloader: Ending download of file %d\n", (int)subscription->fileId );
	pthread_mutex_lock( &mainLoop_mutex );
	/* Remove the entry from the download queue. */
	g_queue_remove( &downloadQueue, subscription );
	test_ReceiveDownload_downloadSlotFree = true;
	/* Inform the subscribers that their download is available. */
	pthread_mutex_lock( &subscription->waitMutex );
	subscription->downloadActive = false;
	printf( "Downloader: Signaling %d subscriber(s)\n", subscription->subscribers );
	pthread_cond_broadcast( &subscription->waitCond );
	pthread_mutex_unlock( &subscription->waitMutex );
	/* Inform the monitor that a download slot is available. */
	pthread_cond_signal( &mainLoop_cond );
	pthread_mutex_unlock( &mainLoop_mutex );

	/* Wait until the last subscriber has acknowledged its unsubscription. */
	pthread_mutex_lock( &subscription->acknowledgeMutex );
	if( subscription->subscribers != 0 )
	{
		pthread_cond_wait( &subscription->acknowledgeCond, &subscription->acknowledgeMutex );
	}
	pthread_mutex_unlock( &subscription->acknowledgeMutex );
	free( subscription );

	return( NULL );
}

static void *test_ReceiveDownload_Monitor( void *data )
{
	test_ReceiveDownload_processed = 0;
	struct DownloadSubscription *subscription;
	pthread_t downloadThread;

//	printf( "Monitor: Starting queue monitor\n" );

	while( test_ReceiveDownload_processed != 3 )
	{
//		printf( "Monitor: Waiting for lock to process queue...\n" );

		/* Prevent others from modifying the queue while we're
		   processing it. */
		pthread_mutex_lock( &mainLoop_mutex );
//		printf( "Monitor: Locking. Processing queue\n" );

		/* Fill the download slots with downloads until either all download
		   slots are occupied or the queue is empty. */
		while( test_ReceiveDownload_downloadSlotFree )
		{
//			printf( "Monitor: Download slot available\n" );
			/* Find an entry in the queue that isn't processed yet. */
			subscription = GetSubscriptionFromQueue( );
			if( subscription )
			{
//				printf( "Monitor: Download subscription found\n" );
				subscription->downloadActive = true;
				pthread_create( &downloadThread, NULL,
								test_ReceiveDownload_SimulateDownload,
								subscription );
			}
			else
			{
//				printf( "Monitor: No download subscription found\n" );
				break;
			}
		}

		/* If the queue is emptied or all download slots are occupied,
		   then wait until there are new entries in the queue or the
		   download finishes, whichever comes first.
		   We have the mutex lock, so we won't receive any signals yet.
		   Any new entries or finished downloads will be waiting for us to
		   release the lock. */
		if( ( ! test_ReceiveDownload_downloadSlotFree ) ||
			( subscription == NULL ) )
		{
//			printf( "Monitor: Waiting for event...\n" );
			pthread_cond_wait( &mainLoop_cond, &mainLoop_mutex );
//			printf( "Monitor: Event received\n" );
		}
		/* Otherwise, process more entries until resources are starved. */
		pthread_mutex_unlock( &mainLoop_mutex );
	}

	return( NULL );
}

static void *test_ReceiveDownload_Subscribe( void *data )
{
	sqlite3_int64 fileId = * (sqlite3_int64*) data;

	printf( "Subscribe (%d): Subscribing to download of file %d\n", ++test_ReceiveDownload_counter, (int) fileId );
	ReceiveDownload( fileId );

	printf( "Subscribe (%d): Download of file %d complete\n", ++test_ReceiveDownload_counter, (int) fileId );
	pthread_mutex_lock( &mainLoop_mutex );
	test_ReceiveDownload_processed++;
	pthread_mutex_unlock( &mainLoop_mutex );
	if( test_ReceiveDownload_processed == 3 )
	{
		pthread_cancel( test_ReceiveDownload_monitor );
	}
	pthread_exit( NULL );
	return( NULL );
}

static void test_ReceiveDownload( const char *param )
{
	pthread_t     download1;
	pthread_t     download2;
	pthread_t     download3;
	sqlite3_int64 file1 = 1;
	sqlite3_int64 file3 = 3;
	sqlite3       *db;
	char          query[ 200 ];

	FillDatabase( );
/*	CompileRegexes( );*/
	pthread_mutex_lock( &mainLoop_mutex );
	db = GetCacheDatabase( );
	/* Insert the current user's uid so that file owner refers to an existing
	   value. */
	sprintf( query, "DELETE FROM users WHERE uid='%d';", (int) getuid( ) );
	sqlite3_exec( db, query, NULL, NULL, NULL );
	sprintf( query, "INSERT INTO users( uid, keyid, secretkey ) "
			 "VALUES( '%d', '12345678901234567890', "
			 "'1234567890123456789012345678901234567890' );", (int) getuid( ) );
	sqlite3_exec( db, query, NULL, NULL, NULL );
	pthread_mutex_unlock( &mainLoop_mutex );

	pthread_create( &download1, NULL, test_ReceiveDownload_Subscribe, &file1 );
	pthread_create( &download2, NULL, test_ReceiveDownload_Subscribe, &file1 );
	sleep( 1 );
	pthread_create( &test_ReceiveDownload_monitor, NULL, test_ReceiveDownload_Monitor, NULL );
	pthread_create( &download3, NULL, test_ReceiveDownload_Subscribe, &file3 );
	pthread_join( test_ReceiveDownload_monitor, NULL );
}
/*-------------------------------------------------------------------------*/



static void test_FindAvailableDownloader( const char *param )
{
	int i;

	for( i = 0; i < MAX_SIMULTANEOUS_DOWNLOADS; i++ )
	{
		downloaders[ i ].isReady = true;
	}
	printf( "1: %d\n", FindAvailableDownloader( ) );
	downloaders[ 0 ].isReady = false;
	printf( "2: %d\n", FindAvailableDownloader( ) );
	downloaders[ MAX_SIMULTANEOUS_DOWNLOADS - 1 ].isReady = false;
	printf( "3: %d\n", FindAvailableDownloader( ) );
	for( i = 0; i < MAX_SIMULTANEOUS_DOWNLOADS; i++ )
	{
		downloaders[ i ].isReady = false;
	}
	printf( "4: %d\n", FindAvailableDownloader( ) );
}



static void test_HostnameToRegion( const char *param )
{
	CompileRegexes( );

	printf( "1: %d\n", HostnameToRegion( "https://s3.amazonaws.com/bucket/somepath" ) );
	printf( "2: %d\n", HostnameToRegion( "https://bucket.s3-us-west-1.amazonaws.com/somepath" ) );
	printf( "3: %d\n", HostnameToRegion( "https://bucket.s3-us-west-2.amazonaws.com/somepath" ) );
	printf( "4: %d\n", HostnameToRegion( "https://bucket.s3-eu-west-1.amazonaws.com/somepath" ) );
	printf( "5: %d\n", HostnameToRegion( "https://bucket.s3-ap-southeast-1.amazonaws.com/somepath" ) );
	printf( "6: %d\n", HostnameToRegion( "https://bucket.s3-ap-northeast-1.amazonaws.com/somepath" ) );
	printf( "7: %d\n", HostnameToRegion( "https://bucket.s3-sa-east-1.amazonaws.com/somepath" ) );
	printf( "8: %d\n", HostnameToRegion( "https://bucket.nowhere.amazonaws.com/somepath" ) );
}



/*-------------------------------------------------------------------------*/
static void *test_UnsubscribeFromDownload_Wait( void *data )
{
	struct DownloadSubscription *subscription;
	subscription = data;
	pthread_mutex_lock( &subscription->acknowledgeMutex );
	pthread_cond_wait( &subscription->acknowledgeCond, &subscription->acknowledgeMutex );
	printf( "Received signal; %d subscribers\n", subscription->subscribers );
	pthread_mutex_unlock( &subscription->acknowledgeMutex );
	return( NULL );
}


static void *test_UnsubscribeFromDownload_Unsubscribe( void *data )
{
	struct DownloadSubscription *subscription;
	subscription = data;
	UnsubscribeFromDownload( subscription );
	return( NULL );
}


static void test_UnsubscribeFromDownload( const char *param )
{
	pthread_t                   waiter;
	pthread_t                   thread;
	struct DownloadSubscription subscription;
	pthread_mutexattr_t         mutexAttr;
	pthread_condattr_t          condAttr;

	pthread_mutexattr_init( &mutexAttr );
	pthread_mutex_init( &subscription.acknowledgeMutex, &mutexAttr );
	pthread_mutexattr_destroy( &mutexAttr );
	pthread_condattr_init( &condAttr );
	pthread_cond_init( &subscription.acknowledgeCond, &condAttr );
	pthread_condattr_destroy( &condAttr );

	subscription.subscribers = 2;
	pthread_create( &waiter, NULL, test_UnsubscribeFromDownload_Wait,
					&subscription );
	sleep( 1 );
	pthread_create( &thread, NULL, test_UnsubscribeFromDownload_Unsubscribe,
					&subscription );
	pthread_create( &thread, NULL, test_UnsubscribeFromDownload_Unsubscribe,
					&subscription );
	pthread_join( waiter, NULL );
}
/*-------------------------------------------------------------------------*/



/*-------------------------------------------------------------------------*/
static void *test_BeginDownload_Download( void *data )
{
	sleep( 1 );
	BeginDownload( data );
	return( NULL );
}

static void test_BeginDownload( const char *param )
{
	struct DownloadStarter      *downloadStarter;
	struct DownloadSubscription *subscription;
	pthread_mutexattr_t         mutexAttr;
	pthread_condattr_t          condAttr;
	S3COMM                      s3Comm;
	pthread_t                   thread;
	struct stat                 filestat;

	FillDatabase( );
	CompileRegexes( );

	downloaders[ 0 ].isReady = true;
	downloaders[ 0 ].curl    = curl_easy_init( );
	downloaders[ 0 ].s3Comm  = &s3Comm;

	subscription = malloc( sizeof( struct DownloadSubscription ) );

	downloadStarter = malloc( sizeof( struct DownloadStarter ) );
	downloadStarter->socket       = 0;
	downloadStarter->downloader   = 0;
	downloadStarter->subscription = subscription;

	subscription->fileId = 5;
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

	mkdir( CACHE_DIR, 0750 );
	mkdir( CACHE_FILES, 0750 );
	mkdir( CACHE_INPROGRESS, 0750 );

	g_queue_push_tail( &downloadQueue, subscription );

	pthread_mutex_lock( &subscription->waitMutex );

	pthread_create( &thread, NULL, test_BeginDownload_Download, downloadStarter );
	pthread_cond_wait( &subscription->waitCond, &subscription->waitMutex );
	pthread_mutex_unlock( &subscription->waitMutex );
	pthread_mutex_lock( &subscription->acknowledgeMutex );
	pthread_cond_signal( &subscription->acknowledgeCond );
	pthread_mutex_unlock( &subscription->acknowledgeMutex );
    pthread_join( thread, NULL );

	stat( CACHE_INPROGRESS "FILE07", &filestat );
	if( S_ISREG( filestat.st_mode ) )
	{
		printf( "FILE07 exists\n" );
	}
	else
	{
		printf( "No FILE07 found\n" );
	}
	printf( "%d entries in queue\n", g_queue_get_length( &downloadQueue ) );
}
/*-------------------------------------------------------------------------*/



static void test_GetSubscriptionFromQueue( const char *param )
{
	struct DownloadSubscription subscription1;
	struct DownloadSubscription subscription2;
	struct DownloadSubscription subscription3;
	struct DownloadSubscription subscription4;
	struct DownloadSubscription *subscription;

	subscription1.fileId = 1;
	subscription2.fileId = 2;
	subscription3.fileId = 3;
	subscription4.fileId = 4;
	subscription1.downloadActive = true;
	subscription2.downloadActive = false;
	subscription3.downloadActive = false;
	subscription4.downloadActive = false;

	g_queue_push_tail( &downloadQueue, &subscription1 );
	g_queue_push_tail( &downloadQueue, &subscription2 );
	g_queue_push_tail( &downloadQueue, &subscription3 );
	g_queue_push_tail( &downloadQueue, &subscription4 );

	subscription = GetSubscriptionFromQueue( );
	printf( "1: %d\n", (int) subscription->fileId );
	subscription2.downloadActive = true;
	subscription = GetSubscriptionFromQueue( );
	printf( "2: %d\n", (int) subscription->fileId );
	g_queue_remove( &downloadQueue, &subscription3 );
	subscription = GetSubscriptionFromQueue( );
	printf( "3: %d\n", (int) subscription->fileId );
}



static void test_MoveToSharedCache( const char *param )
{
	MoveToSharedCache( 0, "DIR001", 1010, 1011, "FILE01", 1001, 1002 );
}



/*-------------------------------------------------------------------------*/
static void *test_ProcessDownloadQueue_Downloads( void *data )
{
	sqlite3_int64 fileId;

	fileId = * ( sqlite3_int64*) data;
	ReceiveDownload( fileId );
	pthread_mutex_lock( &mainLoop_mutex );
	test_ReceiveDownload_processed++;
	printf( "Processed download %d\n", test_ReceiveDownload_processed );
	pthread_mutex_unlock( &mainLoop_mutex );
	if( test_ReceiveDownload_processed == 40 )
	{
		pthread_cancel( test_ReceiveDownload_monitor );
	}
	return( NULL );
}

static void test_ProcessDownloadQueue( const char *param )
{
	sqlite3       *db;
	char          query[ 200 ];

	pthread_t     request;
	int           socket = 0;
	int           i;
	sqlite3_int64 fileIds[ ] = { 1000, 1001, 1002, 1003, 1004,
								 1000, 1001, 1002, 1003, 1004,
								 1000, 1001, 1002, 1003, 1004,
								 1000, 1001, 1002, 1003, 1004,
								 1000, 1001, 1002, 1003, 1004,
								 1000, 1001, 1002, 1003, 1004,
								 1000, 1001, 1002, 1003, 1004,
								 1000, 1001, 1002, 1003, 1004 };


	pthread_mutex_lock( &mainLoop_mutex );
	FillDatabase( );
	CompileRegexes( );
	/* Delete all current transfers from the database. */
	db = GetCacheDatabase( );
	sqlite3_exec( db, "DELETE FROM transferparts WHERE 1;", NULL, NULL, NULL );
	sqlite3_exec( db, "DELETE FROM transfers WHERE 1;", NULL, NULL, NULL );
	/* Insert the current user's uid so that file owner refers to an existing
	   value. */
	sprintf( query, "DELETE FROM users WHERE uid='%d';", (int) getuid( ) );
	sqlite3_exec( db, query, NULL, NULL, NULL );
	sprintf( query, "INSERT INTO users( uid, keyid, secretkey ) "
			 "VALUES( '%d', '12345678901234567890', "
			 "'1234567890123456789012345678901234567890' );", (int) getuid( ) );
	sqlite3_exec( db, query, NULL, NULL, NULL );

	test_ReceiveDownload_processed = 0;
	pthread_mutex_unlock( &mainLoop_mutex );

	for( i = 0; i < 40; i++ )
	{
		if( i == 8 )
		{
			pthread_create( &test_ReceiveDownload_monitor, NULL,
							ProcessDownloadQueue, &socket );
		}
		if( ( i % 10 ) == 0 ) sleep( 1 );
		pthread_create( &request, NULL, test_ProcessDownloadQueue_Downloads,
						&fileIds[ i ] );
	}
	pthread_join( test_ReceiveDownload_monitor, NULL );
}
/*-------------------------------------------------------------------------*/
