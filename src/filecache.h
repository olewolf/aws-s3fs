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
#include <glib-2.0/glib.h>
#include "aws-s3fs.h"


#define MAX_SIMULTANEOUS_TRANSFERS 3

/* The preferred upload part size for multipart uploads, in megabytes. */
#define PREFERRED_CHUNK_SIZE 25


struct RegularExpressions
{
	GRegex *connectAuth;
	GRegex *createFileOptions;
	GRegex *trimString;
	GRegex *rename;
	GRegex *hostname;
	GRegex *regionPart;
	GRegex *removeHost;
	GRegex *getUploadId;
};

extern struct RegularExpressions regexes;


void InitializeFileCache( void );
void ShutdownFileCache( void );
void InitializeDownloadCache( void );
void ShutdownDownloadQueue( void );
bool ConnectToFileCache( const char *bucket, const char *keyId,
						 const char *secretKey );
void DisconnectFromFileCache( void );
int CreateCachedFile( const char *path, uid_t parentUid, gid_t parentGid,
					  int parentPermissions, uid_t uid, gid_t gid,
					  int permissions, time_t mtime );
int DownloadCacheFile( const char *path );
int CloseCacheFile( const char *path );
const char *SendCacheRequest( const char *message );
const char *ReceiveCacheReply( void );
void InitializePermissionsGrant( pid_t childPid, int socketHandle );
void *ProcessTransferQueues( void *socket );
void ReceiveDownload( sqlite3_int64 fileId, uid_t owner );
int NumberOfMultiparts( long long int filesize );
int CreateFilePart( int socket, const char *filename, int part,
					long long int filesize, const char **localPath );


/* Database */
void ShutdownFileCacheDatabase( void );
void InitializeFileCacheDatabase( void );
sqlite3_int64 Query_CreateLocalFile( const char *bucket,
									 const char *path, int uid, int gid,
									 int permissions, time_t mtime,
									 sqlite3_int64 parentId,
									 char *localfile, bool *alreadyExists );
sqlite3_int64 Query_CreateLocalDir( const char *path, int uid, int gid,
									int permissions, char *localdir,
									bool *alreadyExists );
bool Query_GetDownload( sqlite3_int64 fileId, char **bucket, char **remotepath,
						char **localfile, char **keyId, char **secretKey );
bool Query_GetUpload( sqlite3_int64 fileId, int *part, char **bucket,
					  char **remotePath, char **uploadId,
					  uid_t *uid, gid_t *gid, int *permissions,
					  long long int *filesize, char **localPath,
					  char **keyId, char **secretKey );
bool Query_GetOwners( sqlite3_int64 fileId, char **parentdir,
					  uid_t *parentUid, gid_t *parentGid, char **filename,
					  uid_t *uid, gid_t *gid, int *permissions );
bool Query_DeleteTransfer( sqlite3_int64 fileId );
bool Query_AddDownload( sqlite3_int64 fileId, uid_t uid );
bool Query_AddUser( uid_t uid, char keyId[ 21 ], char secretKey[ 41 ] );
const char *Query_GetLocalPath( const char *remotename );
bool Query_DecrementSubscriptionCount( sqlite3_int64 fileId );
bool Query_IncrementSubscriptionCount( sqlite3_int64 fileId );
sqlite3_int64 FindFile( const char *filename, char *localname );
void Query_MarkFileAsCached( sqlite3_int64 fileId );
bool Query_IsFileCached( sqlite3_int64 fileId );
const char *GetLocalFilename( const char *remotepath );
void Query_CreateMultiparts( sqlite3_int64 fileId, int parts );
bool Query_AddUpload( sqlite3_int64 fileId, uid_t owner,
					  long long int filesize );
void Query_SetUploadId( sqlite3_int64, char *uploadId );
sqlite3_int64 Query_FindPendingUpload( void );
void Query_SetPartETag( sqlite3_int64 fileId, int part, const char *md5sum );
bool Query_AllPartsUploaded( sqlite3_int64 fileId );
const char *Query_GetPartETag( sqlite3_int64 fileId, int part );
bool Query_DeleteUploadTransfer( sqlite3_int64 fileId );


#endif /* __FILECACHE_H */
