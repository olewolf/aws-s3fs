/**
 * \file statcache.c
 * \brief Keep file stat information in an LRU memory cache.
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
#include <pthread.h>
#include <uthash.h>
#include "statcache.h"
#include "aws-s3fs.h"


#ifdef AUTOTEST
#undef MAX_STAT_CACHE_SIZE
#define MAX_STAT_CACHE_SIZE 4
#endif


struct StatCacheEntry
{
    const char *filename;
    void       *data;
    void       (*dataDeleteFunction)( void* );

    UT_hash_handle hh;
};

static void DeleteS3FileInfoStructure( void *toDelete );






static struct S3FileInfo*
ResolveS3FileStatCacheMiss(
    const struct ThreadsafeLogging *logger,
    const char *filename
			   )
{
  return( NULL );
}



struct S3FileInfo*
S3FileStat(
    const struct ThreadsafeLogging *logger,
    const char                     *filename
	   )
{
    struct S3FileInfo *toReturn = SearchStatEntry( logger, filename );
    if( toReturn == NULL )
    {
        /* Read the file stat from S3. */
        toReturn = ResolveS3FileStatCacheMiss( logger, filename );

        /* Add the file stat to the cache. */
        if( toReturn != NULL )
        {
	    InsertCacheElement( logger, filename, toReturn,
				&DeleteS3FileInfoStructure );
	}
    }

    return toReturn;
}






/**
 * Callback function for freeing the memory allocated for an S3FileInfo
 * structure when an entry is deleted from the cache.
 * @param toDelete [in/out] Structure that should be deallocated.
 * @return Nothing.
 */
static void DeleteS3FileInfoStructure(
    void *toDelete
				      )
{
    if( toDelete != NULL )
    {
        free( toDelete );
    }
}



static pthread_mutex_t mutex_statCache = PTHREAD_MUTEX_INITIALIZER;
static struct StatCacheEntry *statCache = NULL;



/**
 * Search for an entry in the stat cache log based on the filename.
 * @param logger [in] Logging facility.
 * @param filename [in] Name of the file whose stats are to be queried.
 * @return Pointer to the file stat, or NULL if the file stat wasn't found.
 */
void*
SearchStatEntry(
    const struct ThreadsafeLogging *logger,
    const        char              *filename
		)
{
    struct StatCacheEntry *entry;
    void                  *toReturn = NULL;

    pthread_mutex_lock( &mutex_statCache );

    HASH_FIND_STR( statCache, filename, entry );
    if( entry != NULL )
    {
        /* Remove the entry and re-add it, which makes it the last item in
	   the list. */
        HASH_DELETE( hh, statCache, entry );
        HASH_ADD_KEYPTR( hh, statCache, 
			 entry->filename, strlen( entry->filename ), entry );
	toReturn = entry->data;
    }

    pthread_mutex_unlock( &mutex_statCache );

    if( toReturn != NULL )
    {
	Syslog( logger, LOG_DEBUG, "Stat cache hit, marking entry as LRU\n" );
    }
    else
    {
	Syslog( logger, LOG_DEBUG, "Stat cache miss\n" );
    }

    return( toReturn );
}



/**
 * Delete an entry in the file stat cache, specified by its filename.
 * @param logger [in] Logging facility.
 * @param filename [in] Name of the file whose cached information is to be
 *        deleted. If the entry contains a delete function, the entry data
 *        is deleted.
 * @return Nothing.
 */
void
DeleteStatEntry(
    const struct ThreadsafeLogging *logger,
    const char                     *filename
		)
{
    struct StatCacheEntry *entry;
    bool                  deleted = false;

    pthread_mutex_lock( &mutex_statCache );

    HASH_FIND_STR( statCache, filename, entry );
    if( entry != NULL )
    {
        HASH_DEL( statCache, entry );
	free( (char*) entry->filename );
	if( entry->dataDeleteFunction != NULL )
	{
	    (entry->dataDeleteFunction)( entry->data );
	}
	free( entry );
	deleted = true;
    }

    pthread_mutex_unlock( &mutex_statCache );

    if( deleted )
    {
	Syslog( logger, LOG_DEBUG, "Stat cache entry deleted\n" );
    }
    else
    {
	Syslog( logger, LOG_DEBUG, "Stat cache entry deletion failed\n" );
    }
}



/**
 * Expire the least recently used cache entries until the cache reaches its
 * maximum size.
 * @param logger [in] Logging facility.
 * @return Nothing.
 */
void
TruncateCache(
    const struct ThreadsafeLogging *logger
	      )
{
    struct StatCacheEntry *entry;
    struct StatCacheEntry *tmpEntry;
    int                   cacheSize     = HASH_COUNT( statCache );
    int                   toDelete      = cacheSize - MAX_STAT_CACHE_SIZE;
    int                   numberDeleted = 0;

    if( 0 < toDelete )
    {
        /* Delete from the beginning of the list where the oldest entries are
	   stored. */
        pthread_mutex_lock( &mutex_statCache );

	HASH_ITER( hh, statCache, entry, tmpEntry )
	{
	    HASH_DELETE( hh, statCache, entry );
	    free( (char*) entry->filename );
	    if( entry->dataDeleteFunction != NULL )
	    {
		(entry->dataDeleteFunction)( entry->data );
	    }
	    free( entry );
	    numberDeleted++;
	    if( numberDeleted == toDelete )
	    {
	        break;
	    }
	}

        pthread_mutex_unlock( &mutex_statCache );

	Syslog( logger, LOG_DEBUG, "%d entr%s expired from cache\n",
		numberDeleted, numberDeleted == 1 ? "y" : "ies" );
    }
}



/**
 * Add an element to the cache.
 * @param logger [in] Logging facility.
 * @param filename [in] Name of the file.
 * @param data [in] File stat info for the file. Data is not copied; only
 *        the pointer to the data is recorded.
 * @param deleteFun [in] Pointer to a function that is responsible for deleting
 *        the data structure, or NULL if no such function is necessary (e.g.,
 *        if the data is not dynamically allocated).
 * @return Nothing.
 */
void
InsertCacheElement(
    const struct ThreadsafeLogging *logger,
    const char                     *filename,
    void                           *data,
    void                           (*deleteFun)(void *)
		   )
{
    struct StatCacheEntry *entry;

    entry = malloc( sizeof( struct StatCacheEntry ) );
    assert( entry != NULL );
    entry->filename = strdup( filename );
    assert( entry->filename != NULL );
    entry->data = data;
    entry->dataDeleteFunction = deleteFun;

    pthread_mutex_lock( &mutex_statCache );
    HASH_ADD_KEYPTR( hh, statCache, 
		     entry->filename, strlen( entry->filename ), entry );
    pthread_mutex_unlock( &mutex_statCache );
    Syslog( logger, LOG_DEBUG, "Entry added to stat cache\n" );

    TruncateCache( logger );
}

