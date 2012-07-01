/**
 * \file filecache.h
 * \brief Maintain a file cache.
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

#ifndef __FILECACHE_H
#define __FILECACHE_H

#include <sqlite3.h>
#include <stdbool.h>


#undef CACHE_DIR
#define CACHE_DIR "./temp"
#undef SOCKET_NAME
#define SOCKET_NAME "temp/aws-s3fs.sock"


#define CACHE_DATABASE CACHE_DIR "/cache.sl3"
#define CACHE_FILES    CACHE_DIR "/files/"


void InitializeFileCache( void );
void ShutdownFileCache( void );
bool ConnectToFileCache( const char *keyId, const char *secretKey );
int OpenCacheFile( const char *path, uid_t uid, gid_t gid,
				   int permissions, time_t mtime, char **localname );
int DownloadCacheFile( const char *path );
int CloseCacheFile( const char *path );
const char *SendCacheRequest( const char *message );
const char *ReceiveCacheReply( void );


/* Database */
void ShutdownFileCacheDatabase( void );
void InitializeFileCacheDatabase( void );
sqlite3_int64 Query_CreateLocalFile( const char *path, int uid, int gid,
									 int permissions, time_t mtime,
									 char *localfile, bool *alreadyExists );
sqlite3_int64 Query_CreateLocalDir( const char *path, int uid, int gid,
									int permissions, char *localdir,
									bool *alreadyExists );


#endif /* __FILECACHE_H */
