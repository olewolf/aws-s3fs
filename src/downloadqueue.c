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
#include <pthread.h>
#include <glib-2.0/glib.h>
#include <errno.h>
#include "aws-s3fs.h"
#include "filecache.h"



#define CACHE_INPROGRESS CACHE_FILES "unfinished/"


static pthread_mutex_t downloadQueue_mutex;
static GQueue          downloadQueue = G_QUEUE_INIT;


struct DownloadSubscription
{
	sqlite3_int64   fileId;
	int             subscribers;
	pthread_cond_t  waitCond;
	pthread_mutex_t waitMutex;
};



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



static void
LockDownloadQueue(
    void
	              )
{
	pthread_mutex_lock( &downloadQueue_mutex );
}



static void
UnlockDownloadQueue(
    void
	              )
{
	pthread_mutex_unlock( &downloadQueue_mutex );
}



static FindInDownloadQueue(
	gconstpointer queueData,
	gconstpointer cmpVal
	                       )
{
	struct DownloadSubscription *subscription = queueData;
	sqlite3_int64               value         = * (sqlite3_int64*) cmpVal;

	return( cmpVal == queueData->fileId ? 0 : 1 );
}



static struct DownloadSubscription*
ScheduleDownload(
	sqlite3_int64 fileId
	             )
{
	GList                       *subscriptionEntry;
	struct DownloadSubscription *subscription;
	pthread_mutexattr_t         mutexAttr;
	pthread_condattr_t          condAttr;


	/* Find out if a subscription to the same file is already queued. */
	LockDownloadQueue( );
	subscriptionEntry = g_find_queue_custom( &downloadQueue, &fileId,
											 FindInDownloadQueue );
	/* No other subscriptions, so create one. */
	if( subscriptionEntry == NULL )
	{
		/* Create a new subscription entry. */
		subscription = malloc( sizeof( struct DownloadSubscription ) );
		subscription->fileId      = fileId;
		subscription->subscribers = 1;
		pthread_mutexattr_init( &mutexAttr );
		pthread_mutex_init( &subscription->waitMutex, &mutexAttr );
		pthread_mutexattr_destroy( &mutexAttr );
		pthread_condattr_init( &condAttr );
		pthread_cond_init( &subscription->waitCond, &condAttr );
		pthread_condattr_destroy( &condAttr );
		/* Enqueue the entry. */
		g_queue_push_tail( subscription );
	}
	/* Another thread is subscribing to the same download, so use its wait
	   condition. */
	else
	{
		/* Subscribe to the download. */
		subscription = subscriptionEntry->data;
		subscription->subscribers++;
	}
	/* TODO: investigate whether the following order of locks could cause
	   deadlocks. I think it's safe because no other thread should be able
	   to access the lock while the queue itself is locked: the lock will
	   always be granted immediately, because no other threads can lock the
	   mutex while we have the thread lock. However, waiting until the queue
	   lock is released will create a small window where the queue entry
	   could change before we get the chance to wait for the condition. */
	pthread_mutex_lock( &subscription->waitMutex );
	UnlockDownloadQueue( );
	pthread_cond_wait( &subscription->waitCond, &subscription->waitMutex );
}


