/**
 * \file filecacheclient.c
 * \brief Client for the file cache.
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
#include <string.h>
#include <malloc.h>
#include <assert.h>
#include <sys/time.h>
#include "aws-s3fs.h"
#include "socket.h"
#include "filecache.h"



/* Socket handle and address for communicating with the cache server. */
static int                cacheSocketFd;
static struct sockaddr_un cacheSocketAddress;


/**
 * Connect to the file cache socket. The connection requires that the
 * cache receive the user's AWS keys. This is necessary for the upload/
 * download handler to sign postponed requests, because AWS signatures
 * appear to be valid for 15 minutes only.
 * @param keyId [in] Amazon Access Key ID.
 * @param secretKey [in] Secret key ID.
 * @return \a true if successfully connected; \a false otherwise.
 * Test: none.
 */
bool
ConnectToFileCache(
	const char *bucket,
    const char *keyId,
    const char *secretKey
	               )
{
    char *request;
    char *reply;
    bool success = false;

	/* Open socket connection to the cache server. */
	CreateClientStreamSocket( SOCKET_NAME, &cacheSocketFd,
							  &cacheSocketAddress );
	if( 0 < cacheSocketFd )
	{
		/* Send connection data. */
		request = malloc( strlen( "CONNECT :::" ) + sizeof( char )
						  + strlen( bucket )
						  + 5 * sizeof( char )
						  + strlen( keyId ) + strlen( secretKey ) );
		sprintf( request, "CONNECT %s:%d:%s:%s",
				 bucket, (int) getuid( ), keyId, secretKey );
		reply = (char*) SendCacheRequest( request );
		free( request );

		if( strcasecmp( reply, "CONNECTED" ) == 0 )
		{
			success = true;
		}
		free( reply );
	}

    return( success );
}



void
DisconnectFromFileCache(
	void
	                    )
{
    char *reply;

	reply = (char*) SendCacheRequest( "DISCONNECT" );
	free( reply );
}



/**
 * Retrieve the garbled name of the cached file from the cache.
 * @param cachedFilename [in] Remote filename of the cached file.
 * @return The name of the file in the cache directory, or NULL if
 *         the file is not cached.
 */
const char*
GetCachedFilename(
    const char *cachedFilename
                  )
{
    char       *request;
	char       *reply;
	const char *cachedFile = NULL;

    request = malloc( strlen( "FILE " ) + sizeof( char )
					  + strlen( cachedFilename ) );
	reply = (char*) SendCacheRequest( request );
	free( request );

	if( strcasecmp( request, "-" ) != 0 )
	{
		cachedFile = reply;
	}
	else
	{
		free( reply );
	}
	return( cachedFile );
}



/**
 * Instruct the cache to open a file that is either cached already or will be
 * cached once the file read or file write functions are used. If a non-cached
 * file is opened, the file cache creates a temporary filename for the file
 * and returns it, but the file is otherwise left empty until the file access
 * functions are used.
 * @param path [in] Path name of the file to open.
 * @param parentUid [in] uid of the file's parent directory.
 * @param parentGid [in] gid of the file's parent directory.
 * @param parentPermissions [in] Permissions of the file's parent directory.
 * @param uid [in] The file's uid.
 * @param gid [in] The file's gid.
 * @param permissions [in] The file's permissions.
 * @param mtime [in] The file's last modification time.
 * @param localname [out] Pointer to the local file, relative to the cache dir.
 * @param fileId [out] File ID of the file in the file cache database.
 * return 0 on success, or \a -errno on failure.
 */
int
CreateCachedFile(
	const char *path,
	uid_t      parentUid,
	gid_t      parentGid,
	int        parentPermissions,
	uid_t      uid,
	gid_t      gid,
	int        permissions,
	time_t     mtime
	             )
{
	char *request;
	char *reply;
	int  status;

	/* Build a cache filename request. */
	request = malloc( 7 + 6 + 6 + 6 + 6 + 6 + 6 + 21 + strlen( path ) + 1 );
	sprintf( request, "CREATE %5d:%5d:%5d:%5d:%5d:%5d:%20lld:%s",
			 (int) parentUid, (int) parentGid, parentPermissions,
			 (int) uid, (int) gid, permissions, (long long) mtime, path );
	reply = (char*) SendCacheRequest( request );
	if( strncmp( reply, "CREATED ", 8 ) == 0 )
	{
		status = 0;
	}
	else
	{
		/* Reply = "ERROR errno" */
		status = atoi( &reply[ 7 ] );
	}
	free( reply );
	free( request );
	return( status );
}



/**
 * Ask the file cache to retrieve a file from external storage. If the
 * file is already in the cache, the function returns immediately without
 * downloading the file.
 * @param fileId [in] ID of the file in the file cache database.
 * @return 0 on success, or \a -errno on failure.
 */
int
DownloadCacheFile(
    const char *path
	              )
{
	char *request;
	char *reply;
	int  status;

	/* Build a cache filename request. */
	request = malloc( strlen( "CACHE " ) + strlen( path ) + sizeof( char ) );
	strcpy( request, "CACHE " );
	strcat( request, path );
#if 0
	request = malloc( strlen( "CACHE " ) + 22 * sizeof( char ) );
	sprintf( request, "CACHE %lld", fileId );
#endif
	/* Tell the cache to start caching this file. */
	reply = (char*) SendCacheRequest( request );
	if( strncmp( reply, "OK", 2 ) == 0 )
	{
		status = 0;
	}
	else
	{
		/* Otherwise, ERROR n */
		status = atoi( &reply[ 6 ] );
	}
	free( reply );

	return( status );
}



/**
 * Retrieve the local name, relative to the cache dir, of a cached file.
 * @param remotepath [in] The name of the cached file in the S3 storage.
 * @return Local name, or \a NULL if the file is not known by the cache.
 */
const char*
GetLocalFilename(
	const char *remotepath
	             )
{
	char *request;
	char *reply;
	char *localfile;

	request = malloc( strlen( remotepath ) + strlen( "FILE  " ) );
	strcpy( request, "FILE " );
	strcat( request, remotepath );
	reply = (char*) SendCacheRequest( request );
	if( strncmp( reply, "FILE -", 6 ) != 0 )
	{
		localfile = strdup( &reply[ 5 ] );
	}
	else
	{
		localfile = NULL;
	}
	printf( "reply: %s\n", reply );
	printf( "%s is local file: %s\n", remotepath, localfile );
	sleep( 1 );
	free( reply );
	free( request );
	return( localfile );
}



/**
 * Close a cached file, meaning it is no longer accessed by a particular
 * user. Prepare for synchronization once all access to the file is released.
 * @param path [in] Path name of the file.
 * @return 0 on success, or \a -errno on failure.
 */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
int
CloseCacheFile(
	const char *path
	           )
{
	/* Stub */
	return( 0 );
}
#pragma GCC diagnostic pop



/**
 * Synchronize stat info for the cached files with the stat info in the stat
 * cache. The function sends all the S3FileStat files whose stat info have
 * changed to the file cache, which returns the files whose stat do not match.
 * @param filenames [in] Array of files that should be synced with the
 *        file cache.
 * @param nFiles [in] Number of files in the array.
 * @param dirtyFiles [out] Array of files that must be synchronized.
 * @return Number of files that must be synchronized.
 */
int
GetDirtyStatsList(
    const char **filenames,
	int        nFiles,
	char       ***dirtyFiles
	              )
{
	char request[ 20 ];
	char **dirtyList = NULL;
	int  nDirty;
	int  i;
	char *reply;

	/* Send the number of files followed by a list of filenames. */
	strcpy( request, "DIRTYSTAT %d" );
	reply = (char*) SendCacheRequest( request );
	for( i = 0; i < nFiles; i++ )
	{
		SendCacheRequest( filenames[ i ] );
	}
	/* Receive similar reply. */
	reply = (char*) ReceiveCacheReply( );
	if( strncasecmp( reply, "DIRTYSTAT ", strlen( "DIRTYSTAT " ) == 0 ) )
	{
		nDirty = atoi( &reply[ strlen( "DIRTYSTAT " ) ] );
		free( reply );
		/* Build list of dirty files. */
		dirtyList = malloc( nDirty * sizeof( char* ) );
		for( i = 0; i < nDirty; i++ )
		{
			reply = (char*) ReceiveCacheReply( );
			dirtyList[ i ] = reply;
		}
	}
	*dirtyFiles = dirtyList;
	return( nDirty );
}



/*
 * Set a new atime for a possibly cached file. This will cause the file's atime
 * setting in the cache to become dirty, but will not trigger any action until
 * any of the following situations occur: (1) the file's stat information is
 * flushed from the file stat cache, or (2) the file is flushed from the file
 * cache. (The atime may thus be updated twice for the same file if the file
 * is scheduled for an upload that does not complete until after the FUSE
 * filesystem has been shut down.)
 *
 *
 */


/**
 * Receive a command from the cache, if any.
 * @return Message from the cache, or NULL if no message was available.
 */
#if 0
char*
ReceiveCacheCommand(
    void
	               )
{
	struct timeval tv = { 0, 0 };
	fd_set         rdfs;

	FD_ZERO( &rdfs );
	FD_SET( cacheSocketFd, &rdfs );
	status = select( cacheSocketFd + 1, &rdfs, NULL, NULL, &tv );
}
#endif




const char*
SendCacheRequest(
    const char *request
                 )
{
	/* Send the request and tell the cache server that a message is pending. */
	(void) write( cacheSocketFd, request, strlen( request ) + sizeof( char ) );
	/* Wait for the reply. */
    return( ReceiveCacheReply( ) );
}



const char*
ReceiveCacheReply(
	void
	              )
{
    char buffer[ 4096 ];
	int  nBytes;

	nBytes = read( cacheSocketFd, buffer, sizeof( buffer ) );
	if( nBytes < 0 )
    {
		sprintf( buffer, "ERROR" );
    }

	return( strdup( buffer ) );
}


