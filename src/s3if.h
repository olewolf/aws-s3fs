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
    off_t            size;
    time_t           atime;
    time_t           mtime;
    time_t           ctime;
};


int S3GetFileStat( const char *path, struct S3FileInfo ** );

int S3ReadDir( struct S3FileInfo *fi, const char *dir,
	       char **nameArray[ ], int *nFiles );

int S3ReadFile( const char *path, char *buf, size_t size, off_t offset );

int S3FlushBuffers( const char *path );

int S3FileClose( const char *path );



#endif /* __S3IF_H */
