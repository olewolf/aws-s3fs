/**
 * \file s3if.c
 * \brief Interface to the Amazon S3.
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
#include <malloc.h>
#include <string.h>
#include <errno.h>
#include "aws-s3fs.h"
#include "statcache.h"



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



static void DeleteS3FileInfoStructure( void *toDelete );


static struct S3FileInfo*
ResolveS3FileStatCacheMiss(
    const char *filename
			   )
{
  return( NULL );
}



/**
 * Return the S3 File Info for the specified file.
 * @param filename [in] Filename of the file.
 * @param fi [out] Pointer to a pointer to the S3 File Info.
 * @return 0 on success, or \a -errno on failure.
 */
int
S3FileStat(
    const char  *filename,
    struct S3FileInfo **fi
	   )
{
    struct S3FileInfo *fileInfo;
    int               status;

    /* Attempt to read the S3FileStat from the stat cache. */
    status = 0;
    fileInfo = SearchStatEntry( filename );
    if( fileInfo == NULL )
    {
        /* Read the file stat from S3. */
        fileInfo = ResolveS3FileStatCacheMiss( filename );

        /* Add the file stat to the cache. */
        if( fileInfo != NULL )
        {
	    InsertCacheElement( filename, fileInfo,
				&DeleteS3FileInfoStructure );
	    *fi = fileInfo;
	}
    }
    if( fileInfo == NULL )
    {
        status = -ENOENT;
    }

    return( status );
}









int S3ReadDir( struct S3FileInfo *fi, const char *dir,
	       char **nameArray[ ], int *nFiles )
{
    /* Stub */
    return 0;
}

int S3ReadFile( const char *path, char *buf, size_t size, off_t offset )
{
    /* Stub */
    return 0;
}

int S3FlushBuffers( const char *path )
{
    /* Stub */
    return 0;
}

int S3FileClose( const char *path )
{
    /* Stub */
    return 0;
}



