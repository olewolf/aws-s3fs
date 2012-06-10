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
#include <uthash.h>
#include "statcache.h"
#include "aws-s3fs.h"


#ifdef AUTOTEST
#undef MAX_STAT_CACHE_SIZE
#define MAX_STAT_CACHE_SIZE 4
#endif


 struct StatCacheEntry *statCache = NULL;



void*
SearchStatEntry(
    const struct ThreadsafeLogging *logger,
    const        char              *filename
		)
{
    struct StatCacheEntry *entry;

    HASH_FIND_STR( statCache, filename, entry );
    if( entry != NULL )
    {
        /* Remove the entry and re-add it, which makes it the last item in
	   the list. */
        HASH_DELETE( hh, statCache, entry );
        HASH_ADD_KEYPTR( hh, statCache, 
			 entry->filename, strlen( entry->filename ), entry );
	Syslog( logger, LOG_DEBUG, "Stat cache hit, marking entry as LRU\n" );
        return( entry->data );
    }
    Syslog( logger, LOG_DEBUG, "Stat cache miss\n" );
    return( NULL );
}



void
DeleteStatEntry(
    const struct ThreadsafeLogging *logger,
    const char                     *filename
		)
{
    struct StatCacheEntry *entry;

    HASH_FIND_STR( statCache, filename, entry );
    if( entry != NULL )
    {
        HASH_DEL( statCache, entry );
	free( (char*) entry->filename );
	free( entry );
	Syslog( logger, LOG_DEBUG, "Stat cache entry deleted\n" );
    }
    else
    {
	Syslog( logger, LOG_DEBUG, "Stat cache entry deletion failed\n" );
    }
}



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
	HASH_ITER( hh, statCache, entry, tmpEntry )
	{
	    HASH_DELETE( hh, statCache, entry );
	    free( (char*) entry->filename );
	    free( entry );
	    numberDeleted++;
	    if( numberDeleted == toDelete )
	    {
	        break;
	    }
	}
	Syslog( logger, LOG_DEBUG, "%d entr%s expired from cache\n",
		numberDeleted, numberDeleted == 1 ? "y" : "ies" );
    }
}



void
InsertCacheElement(
    const struct ThreadsafeLogging *logger,
    const char                     *filename,
    void                           *data
		   )
{
    struct StatCacheEntry *entry;

    entry = malloc( sizeof( struct StatCacheEntry ) );
    assert( entry != NULL );
    entry->filename = strdup( filename );
    assert( entry->filename != NULL );
    entry->data = data;

    HASH_ADD_KEYPTR( hh, statCache, 
		     entry->filename, strlen( entry->filename ), entry );
    Syslog( logger, LOG_DEBUG, "Entry added to stat cache\n" );

    TruncateCache( logger );
}

