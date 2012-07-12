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
#include "aws-s3fs.h"
#include "filecache.h"
#include "testfunctions.h"


#pragma GCC diagnostic ignored "-Wunused-parameter"

struct Configuration globalConfig;

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

static void test_ReceiveDownload( const char *param );


#define DISPATCHENTRY( x ) { #x, test_##x }
const struct dispatchTable dispatchTable[ ] =
{

    DISPATCHENTRY( ReceiveDownload ),

    { NULL, NULL }
};



static bool      downloadSlotFree = true;
static pthread_t monitor;
static int       counter = 0;

static void *test_ReceiveDownload_SimulateDownload( void *data )
{
	struct DownloadSubscription *subscription = data;

	/* Occupy the download slot. */
//	printf( "Downloader: Starting download of file %d (%d subscriber(s))\n", (int)subscription->fileId, subscription->subscribers );
	pthread_mutex_lock( &mainLoop_mutex );
	downloadSlotFree = false;
	pthread_mutex_unlock( &mainLoop_mutex );
	/* Simulated download time passes very quickly for this simulation. The
	   download finishes and the download thread signals to the monitor
	   that it is complete. */
//	printf( "Downloader: Ending download of file %d\n", (int)subscription->fileId );
	pthread_mutex_lock( &mainLoop_mutex );
	/* Remove the entry from the download queue. */
	g_queue_remove( &downloadQueue, subscription );
	downloadSlotFree = true;
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

static int processed;

static void *test_ReceiveDownload_Monitor( void *data )
{
	processed = 0;
	struct DownloadSubscription *subscription;
	pthread_t downloadThread;

//	printf( "Monitor: Starting queue monitor\n" );

	while( processed != 3 )
	{
//		printf( "Monitor: Waiting for lock to process queue...\n" );

		/* Prevent others from modifying the queue while we're
		   processing it. */
		pthread_mutex_lock( &mainLoop_mutex );
//		printf( "Monitor: Locking. Processing queue\n" );

		/* Fill the download slots with downloads until either all download
		   slots are occupied or the queue is empty. */
		while( downloadSlotFree )
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
		if( ( ! downloadSlotFree ) || ( subscription == NULL ) )
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

	printf( "Subscribe (%d): Subscribing to download of file %d\n", ++counter, (int) fileId );
	ReceiveDownload( fileId );
	printf( "Subscribe (%d): Download of file %d complete\n", ++counter, (int) fileId );
	pthread_mutex_lock( &mainLoop_mutex );
	processed++;
	pthread_mutex_unlock( &mainLoop_mutex );
	if( processed == 3 )
	{
		pthread_cancel( monitor );
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

	pthread_create( &download1, NULL, test_ReceiveDownload_Subscribe, &file1 );
	pthread_create( &download2, NULL, test_ReceiveDownload_Subscribe, &file1 );
	sleep( 1 );
	pthread_create( &monitor, NULL, test_ReceiveDownload_Monitor, NULL );
	pthread_create( &download3, NULL, test_ReceiveDownload_Subscribe, &file3 );
//	sleep( 1 );
/*
	pthread_join( download1, NULL );
	pthread_join( download2, NULL );
	pthread_join( download3, NULL );
*/
	pthread_join( monitor, NULL );
}



int ReadEntireMessage( int connectionHandle, char **clientMessage )
{
	return( 0 );
}
