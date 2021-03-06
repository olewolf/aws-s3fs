/**
 * \file s3if.h
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

#ifndef __S3IF_H
#define __S3IF_H


#include <config.h>
#include <stdbool.h>
#include <fcntl.h>
#include <time.h>
#include <curl/curl.h>


struct OpenFlags
{
    bool of_RDONLY    : 1;
    bool of_WRONLY    : 1;
    bool of_RDWR      : 1;
    bool of_CREAT     : 1;
    bool of_APPEND    : 1;
    bool of_EXCL      : 1;
    bool of_DIRECT    : 1;
    bool of_DIRECTORY : 1;
    bool of_LARGEFILE : 1;
    bool of_NOATIME   : 1;
    bool of_NONBLOCK  : 1;
    bool of_NDELAY    : 1;
    bool of_SYNC      : 1;
    bool of_TRUNC     : 1;
    bool of_NOCTTY    : 1;
    bool of_ASYNC     : 1;
    bool of_NOFOLLOW  : 1;
};


struct S3FileInfo
{
    uid_t            uid;
    gid_t            gid;
    unsigned int     permissions;
    char             fileType;
    bool             exeUid : 1;
    bool             exeGid : 1;
    bool             sticky : 1;
    /* filenotfound is used to cache 404 errors. It obviously means that
       the entire structure contents are otherwise invalid. */
    bool             filenotfound : 1;
	/* statonly indicates that the file has not been read from or written to;
	   that is, only the stat information is available. This is used to
	   avoid bothering the file cache with stat inquiries. */
	bool             statonly : 1;
    char             *symlinkTarget;
    off_t            size;
    time_t           atime;
    time_t           mtime;
    time_t           ctime;
	/* File handle for the locally cached file. */
	int              localFd;
	struct OpenFlags openFlags;
};


void InitializeS3If( void );
void S3Destroy( void );

int S3FileStat( const char *path, struct S3FileInfo ** );
int S3Open( const char *path );
int S3Create( const char *path, mode_t permissions );
int S3FileClose( const char *path );
int S3ReadLink( const char *link, char **target );
int S3ReadFile( const char *path, char *buf,
		size_t size, off_t offset, size_t *actuallyRead );
int S3ReadDir( const char *dir, char **nameArray[ ], int *nFiles, int maxKeys );
int S3FlushBuffers( const char *path );
int S3ModifyTimeStamps( const char *file, time_t atime, time_t mtime );
int S3CreateLink( const char *linkname, const char *target );
int S3Mkdir( const char* dirname, mode_t mode );
int S3Unlink( const char *path );
int S3Rmdir( const char *path );
int S3Chmod( const char *path, mode_t mode );
int S3Chown( const char *path, uid_t uid, gid_t gid );


#endif /* __S3IF_H */
