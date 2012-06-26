/**
 * \file dircache.c
 * \brief Maintain a directory cache.
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
#include <stdbool.h>
#include <string.h>
#include <malloc.h>
#include <pthread.h>
#include "dircache.h"


static struct DirCache
{
    const char *dirname;
    int        size;
    const char **contents;
} directoryCache[ DIR_CACHE_SIZE ];

static pthread_mutex_t dirCache_mutex = PTHREAD_MUTEX_INITIALIZER;

static void DeleteDirectoryEntry( struct DirCache *entry );
static void DeleteLastInDirectoryCache( void );
static const char **LookupInDirectoryCacheWithoutMutex( const char *dirname,
							int *size );



/**
 * Initialize the directory cache system.
 * @return Nothing.
 */
void
InitializeDirectoryCache( void )
{
    int i;

    for( i = 0; i < DIR_CACHE_SIZE; i++ )
    {  
	directoryCache[ i ].dirname  = NULL;
	directoryCache[ i ].size     = 0;
	directoryCache[ i ].contents = NULL;
    }
}



/**
 * Shutdown the directory cache system.
 * @return Nothing.
 */
void
ShutdownDirectoryCache( void )
{
    int             i;
    struct DirCache *entry;

    for( i = 0; i < DIR_CACHE_SIZE; i++ )
    {
        entry = &directoryCache[ i ];
	DeleteDirectoryEntry( entry );
    }
}



/**
 * Delete the oldest entry in the directory cache. This function is not
 * mutex-locked.
 * @return Nothing.
 */
static void
DeleteLastInDirectoryCache(
    void
			   )
{
    struct DirCache *lastEntry;
    int             i;

    /* Delete the last entry, freeing its memory. */
    lastEntry = &directoryCache[ DIR_CACHE_SIZE - 1 ];
    DeleteDirectoryEntry( lastEntry );

    /* Push all entries one place back. */
    for( i = DIR_CACHE_SIZE - 1; i > 0; i-- )
    {
	directoryCache[ i ].dirname  = directoryCache[ i - 1 ].dirname;
	directoryCache[ i ].size     = directoryCache[ i - 1 ].size;
	directoryCache[ i ].contents = directoryCache[ i - 1 ].contents;
    }

    /* Free the first entry. */
    directoryCache[ 0 ].dirname  = NULL;
    directoryCache[ 0 ].size     = 0;
    directoryCache[ 0 ].contents = NULL;
}



/**
 * Insert a directory name and its contents into the cache, which takes
 * ownership of the allocated memory.
 * @param dirname [in] Directory name.
 * @param size [in] Number of filenames in the directory.
 * @param contents [in] String array with filenames.
 * @return Nothing.
 */
void
InsertInDirectoryCache(
    const char *dirname,
    int        size,
    const char **contents
	       )
{
    int dummy;

    pthread_mutex_lock( &dirCache_mutex );

    /* If the directory has already been inserted, mark it as least recently
       used instead. */
    if( LookupInDirectoryCacheWithoutMutex( dirname, &dummy ) == NULL )
    {
        /* Otherwise, delete the last entry... */
        DeleteLastInDirectoryCache( );
	/* ... and insert the cache entry at the vacated position. */
	directoryCache[ 0 ].dirname  = dirname;
	directoryCache[ 0 ].size     = size;
	directoryCache[ 0 ].contents = contents;
    }

    pthread_mutex_unlock( &dirCache_mutex );
}



/**
 * Find a directory in the cache, and return its contents. This operation
 * moves the directory to the front of the cache, marking it as most recently
 * used.
 * @param dirname [in] Name of the directory to locate in the cache.
 * @param size [out] Number of elements in the directory contents.
 * @return Contents of the directory, or \a NULL if not found.
 */
const char**
LookupInDirectoryCache(
    const char *dirname,
    int        *size
		       )
{
    const char **contents;

    pthread_mutex_lock( &dirCache_mutex );
    contents = LookupInDirectoryCacheWithoutMutex( dirname, size );
    pthread_mutex_unlock( &dirCache_mutex );

    return( contents );
}



/**
 * Find a directory in the cache, and return its contents. This operation
 * moves the directory to the front of the cache, marking it as most recently
 * used. This function does not mutex-lock the cache.
 * @param dirname [in] Name of the directory to locate in the cache.
 * @param size [out] Number of elements in the directory contents.
 * @return Contents of the directory, or \a NULL if not found.
 */
static const char**
LookupInDirectoryCacheWithoutMutex(
    const char *dirname,
    int        *size
				   )
{
    const char *foundName      = NULL;
    const char **foundContents = NULL;
    int        i;

    /* Find the directory name in the directory cache using a brute-force
       search. It is effective enough for very small caches. */
    for( i = 0; i < DIR_CACHE_SIZE; i++ )
    {
        if( directoryCache[ i ].dirname != NULL )
        {
	    if( strcmp( directoryCache[ i ].dirname, dirname ) == 0 )
	    {
	        foundName     = directoryCache[ i ].dirname;
	        foundContents = directoryCache[ i ].contents;
		*size         = directoryCache[ i ].size;
		break;
	    }
	}
    }

    /* Move the directory to the front of the cache if found, marking it as
       most recently used. */
    if( foundContents != NULL )
    {
        for( ; i >= 1; i-- )
	{
	    directoryCache[ i ].dirname  = directoryCache[ i - 1 ].dirname;
	    directoryCache[ i ].size     = directoryCache[ i - 1 ].size;
	    directoryCache[ i ].contents = directoryCache[ i - 1 ].contents;
	}
	directoryCache[ 0 ].dirname  = foundName;
	directoryCache[ 0 ].size     = *size;
	directoryCache[ 0 ].contents = foundContents;
    }

    return( foundContents );
}



/**
 * Mark a directory in the cache as invalid.
 * @param dirname [in] The directory name that is now invalid.
 * @return Nothing.
 */
void
InvalidateDirectoryCacheElement(
    const char *dirname
				)
{
    int dummy;

    pthread_mutex_unlock( &dirCache_mutex );

    /* If the directory is cached, finding it will move it to the front. */
    if( LookupInDirectoryCache( dirname, &dummy ) != NULL )
    {
        /* Then delete the first entry. */
        DeleteDirectoryEntry( &directoryCache[ 0 ] );
    }

    pthread_mutex_unlock( &dirCache_mutex );
}



/**
 * Free the strings allocated for a directory entry. The directory cache
 * structure otherwise remains intact, leaving the entry pointer dangling.
 * The function is not mutex-locked.
 * @param entry [in] Directory entry.
 * @return Nothing.
 */
static void
DeleteDirectoryEntry(
    struct DirCache *entry
		     )
{
    int i;

    if( entry != NULL )
    {
        /* Free all memory allocated for the directory entry strings. */
        if( entry->dirname != NULL )
	{
	    free( (char*) entry->dirname );
	    entry->dirname = NULL;
	}
	if( entry->contents != NULL )
	{
	    for( i = 0; i < entry->size; i++ )
	    {
		if( entry->contents[ i ] != NULL )
		{
		    free( (char*) entry->contents[ i ] );
		}
	    }
	    entry->size = 0;
	    free( (char*) entry->contents );
	    entry->contents = NULL;
	}
    }
}

