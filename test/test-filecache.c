/**
 * \file test-filecache.c
 * \brief .
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
#include <stdio.h>
#include <ctype.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sqlite3.h>
#include "aws-s3fs.h"
#include "filecache.h"
#include "testfunctions.h"


#pragma GCC diagnostic ignored "-Wunused-parameter"


struct Configuration globalConfig;

struct CacheClientConnection
{
	int       connectionHandle;
	pthread_t thread;
	pid_t     pid;
	uid_t     uid;
	gid_t     gid;
	char      *bucket;
	char      keyId[ 21 ];
	char      secretKey[ 41 ];
};



extern sqlite3_int64 FindFile( const char *path, char *localname );
extern sqlite3_int64 FindParent( const char *path, char *localname );
extern sqlite3 *GetCacheDatabase( void );
extern void CompileRegexes( void );
extern char *TrimString( char *original );
extern sqlite3_int64 CreateLocalDir( const char *path, uid_t uid, gid_t gid,
									 int permissions );
extern sqlite3_int64 CreateLocalFile( const char *bucket, const char *path,
									  int uid, int gid, int permissions,
									  time_t mtime, sqlite3_int64 parentId,
									  char **localfile );
extern int ClientConnects( struct CacheClientConnection *clientConnection,
						   const char *request );
extern int ClientRequestsCreate( struct CacheClientConnection *clientConnection,
								 const char *request );
extern int ClientRequestsLocalFilename(
	struct CacheClientConnection *clientConnection, const char *request );
extern void *ReceiveRequests( void *data );
extern int CommandDispatcher( struct CacheClientConnection *clientConnection,
							  const char *message );
void FillDatabase( void );

static void test_InitializeFileCacheDatabase( const char *param );
static void test_FindFile( const char *param );
static void test_FindParent( const char *param );
static void test_GetLocalPathQuery( const char *param );
static void test_CreateLocalFileQuery( const char *param );
static void test_CreateLocalDirQuery( const char *param );
static void test_GetDownload( const char *param );
static void test_GetOwners( const char *param );
static void test_DeleteTransfer( const char *param );
static void test_TrimString( const char *param );
static void test_CreateLocalDir( const char *param );
static void test_CreateLocalFile( const char *param );
static void test_ClientConnects( const char *param );
static void test_ClientRequestsCreate( const char *param );
static void test_ReceiveRequests( const char *param );
static void test_CommandDispatcher( const char *param );
static void test_ClientRequestsLocalFilename( const char *param );
static void test_AddUploadQuery( const char *param );
static void test_CreateMultiparts( const char *param );
static void test_GetUpload( const char *param );
static void test_SetUploadId( const char *param );
static void test_AllPartsUploaded( const char *param );
static void test_PartETag( const char *param );
static void test_FindPendingUpload( const char *param );



#define DISPATCHENTRY( x ) { #x, test_##x }
const struct dispatchTable dispatchTable[ ] =
{

    DISPATCHENTRY( InitializeFileCacheDatabase ),
	DISPATCHENTRY( FindFile ),
	DISPATCHENTRY( FindParent ),
	DISPATCHENTRY( GetLocalPathQuery ),
	DISPATCHENTRY( CreateLocalFileQuery ),
	DISPATCHENTRY( CreateLocalDirQuery ),
	DISPATCHENTRY( GetDownload ),
	DISPATCHENTRY( GetOwners),
	DISPATCHENTRY( DeleteTransfer ),
	DISPATCHENTRY( AddUploadQuery ),
	DISPATCHENTRY( CreateMultiparts ),
	DISPATCHENTRY( GetUpload ),
	DISPATCHENTRY( SetUploadId ),
	DISPATCHENTRY( AllPartsUploaded ),
	DISPATCHENTRY( PartETag ),
	DISPATCHENTRY( FindPendingUpload ),

	DISPATCHENTRY( TrimString ),
	DISPATCHENTRY( CreateLocalDir ),
	DISPATCHENTRY( CreateLocalFile ),
	DISPATCHENTRY( ClientConnects ),
	DISPATCHENTRY( ClientRequestsCreate ),
	DISPATCHENTRY( ClientRequestsLocalFilename ),
	DISPATCHENTRY( ReceiveRequests ),
	DISPATCHENTRY( CommandDispatcher ),

    { NULL, NULL }
};



#if 0
static void PrepareLiveTestData( const char *configFile )
{
	const sqlite3 *cacheDb = GetCacheDatabase( );

	ReadLiveConfig( configFile );
	
}
#endif



/* This function tests only the creation of the database, because the
   compiled queries are implicitly tested during the query tests. */
static void test_InitializeFileCacheDatabase( const char *param )
{
	char sysCmd[ 200 ];

	CheckSQLiteUtil( );
	unlink( CACHE_DATABASE );
	rmdir( CACHE_DIR );
	mkdir( CACHE_DIR, 0750 );
	InitializeFileCacheDatabase( );
	sprintf( sysCmd, "echo \".dump\" | sqlite3 %s", CACHE_DATABASE );
	system( sysCmd );
}



static void test_FindFile( const char *param )
{
	sqlite3_int64 id;
	char          localfile[ 10 ];

	FillDatabase( );
	id = FindFile( "http://remote1", localfile );
	printf( "1: id=%d, file=%s\n", (int) id, localfile );
	id = FindFile( "http://remote3", localfile );
	printf( "2: id=%d, file=%s\n", (int) id, localfile );
	id = FindFile( "http://remote5", localfile );
	printf( "3: id=%d, file=%s\n", (int) id, localfile );
}



static void test_FindParent( const char *param )
{
	sqlite3_int64 id;
	char          localfile[ 10 ];

	FillDatabase( );
	id = FindParent( "http://remotedir1", localfile );
	printf( "1: id=%d, file=%s\n", (int) id, localfile );
	id = FindParent( "http://remotedir13", localfile );
	printf( "2: id=%d, file=%s\n", (int) id, localfile );
}



static void test_CreateLocalFileQuery( const char *param )
{
	sqlite3_int64 id;
	bool          exists;
	char          localfile[ 10 ];

	FillDatabase( );
	strcpy( localfile, "FILE05" );
	id = Query_CreateLocalFile( "bucket", "http://remote5", 1010, 1005, 0640,
								100, 1, localfile, &exists );
	printf( "1: id=%d, file=%s, existed=%d\n", (int) id, localfile, (int) exists );
	strcpy( localfile, "---------" );
	id = Query_CreateLocalFile( "bucket", "http://remote5", 1011, 1015, 0755,
								101, 1, localfile, &exists );
	printf( "2: id=%d, file=%s, existed=%d\n", (int) id, localfile, (int) exists );
	/* Attempting to create a file with no parent (value = 10) must fail. */
	strcpy( localfile, "FILE06" );
	id = Query_CreateLocalFile( "bucket", "http://remote6", 1011, 1015, 0755,
								101, 10, localfile, &exists );
	printf( "3: id=%d, file=%s\n", (int) id, localfile );
}



static void test_CreateLocalDirQuery( const char *param )
{
	sqlite3_int64 id;
	bool          exists;
	char          localfile[ 10 ];

	FillDatabase( );
	strcpy( localfile, "DIR003" );
	id = Query_CreateLocalDir( "http://remote3", 1010, 1005, 0700,
								localfile, &exists );
	printf( "1: id=%d, dir=%s, existed=%d\n", (int) id, localfile, (int) exists );
	strcpy( localfile, "---------" );
	id = Query_CreateLocalDir( "http://remote3", 1011, 1015, 0755,
								localfile, &exists );
	printf( "2: id=%d, dir=%s, existed=%d\n", (int) id, localfile, (int) exists );
}



static void test_GetDownload( const char *param )
{
	bool          status;
	char          *bucket = NULL;
	char          *remotePath = NULL;
	char          *localPath = NULL;
	char          *keyId = NULL;
	char          *secretKey = NULL;

	FillDatabase( );

	status = Query_GetDownload( 2, &bucket, &remotePath, &localPath,
								&keyId, &secretKey );
	printf( "1: %d - %s, %s, %s, %s, %s\n", status, bucket, remotePath,
			localPath, keyId, secretKey );
	status = Query_GetDownload( 1, &bucket, &remotePath, &localPath,
								&keyId, &secretKey );
	printf( "2: %d - %s, %s, %s, %s, %s\n", status, bucket, remotePath,
			localPath, keyId, secretKey );
}



static void test_GetOwners( const char *param )
{
	bool  status;
	char  *parentname;
	uid_t parentUid;
	gid_t parentGid;
	char  *filename;
	uid_t uid;
	gid_t gid;
	int   permissions;

	FillDatabase( );
	status = Query_GetOwners( 1, &parentname, &parentUid, &parentGid,
							  &filename, &uid, &gid, &permissions );
	printf( "1: (%d) %s %d %d / %s %d %d %3o\n", status,
			parentname, (int) parentUid, (int) parentGid,
			filename, (int) uid, (int) gid, permissions );
	status = Query_GetOwners( 10, &parentname, &parentUid, &parentGid,
							  &filename, &uid, &gid, &permissions );
	printf( "2: (%d) %s %d %d / %s %d %d %3o\n", status,
			parentname, (int) parentUid, (int) parentGid,
			filename, (int) uid, (int) gid, permissions );
}



static void test_DeleteTransfer( const char *param )
{
	bool  status;

	FillDatabase( );
	status = Query_DeleteTransfer( 2 ); /* deletes transfer.id = 1 */
	printf( "1: %d\n", status );
	status = Query_DeleteTransfer( 3 ); /* deletes transfer.id = 2 */
	printf( "2: %d\n", status );
	status = Query_DeleteTransfer( 200 );
	printf( "3: %d\n", status );
}



static void test_TrimString( const char *param )
{
	char *string1 = "Nowhitespace";
	char *string2 = "  Whitespace front";
	char *string3 = "Whitespace back  ";
	char *string4 = "\n Advanced  whitespace\f ";
	char *string5 = "   ";

	CompileRegexes( );
	string1 = TrimString( strdup( string1 ) );
	string2 = TrimString( strdup( string2 ) );
	string3 = TrimString( strdup( string3 ) );
	string4 = TrimString( strdup( string4 ) );
	string5 = TrimString( strdup( string5 ) );
	printf( "1: \"%s\"\n", string1 );
	printf( "2: \"%s\"\n", string2 );
	printf( "3: \"%s\"\n", string3 );
	printf( "4: \"%s\"\n", string4 );
	printf( "5: \"%s\"\n", string5 );
}



static void test_CreateLocalDir( const char *param )
{
	sqlite3_int64 dirId;
	sqlite3_int64 parentId;
	char          localname[ 10 ];
	char          path[ 40 ];
	struct stat   stat;
	const char    *type;

	FillDatabase( );

	dirId = CreateLocalDir( "http://testdir1", 1001, 1002, 0705 );
	parentId = FindParent( "http://testdir1", localname );
	sprintf( path, "%s%s", CACHE_INPROGRESS, localname );
	lstat( path, &stat );
	if( S_ISDIR( stat.st_mode ) ) type = "dir";
	else                          type = "other";
	printf( "1: %d=%d: \"%s\", type=%s\n", (int)dirId, (int)parentId, localname, type );

	dirId = CreateLocalDir( "http://testdir1", 1001, 1002, 0705 );
	parentId = FindParent( "http://testdir1", localname );
	sprintf( path, "%s%s", CACHE_INPROGRESS, localname );
	lstat( path, &stat );
	if( S_ISDIR( stat.st_mode ) ) type = "dir";
	else                          type = "other";
	printf( "2: %d=%d: \"%s\", type=%s\n", (int)dirId, (int)parentId, localname, type );
}



static void test_CreateLocalFile( const char *param )
{
	sqlite3_int64 fileId;
	sqlite3_int64 id;
	char          *localname;
	char          path[ 40 ];
	struct stat   stat;
	const char    *type;

	FillDatabase( );

	fileId = CreateLocalFile( "bucketname", "http://testfile1", 1002, 1003,
							  0664, 100, 1, &localname );
	sprintf( path, "%s%s", CACHE_INPROGRESS, localname );
	lstat( path, &stat );
	if( S_ISREG( stat.st_mode ) ) type = "file";
	else                          type = "other";
	printf( "1: %d: \"%s\", type=%s\n", (int)fileId, localname, type );

	id = CreateLocalFile( "bucketname", "http://testfile1", 1002, 1003,
						  0664, 100, 1, &localname );
	sprintf( path, "%s%s", CACHE_INPROGRESS, localname );
	lstat( path, &stat );
	if( S_ISREG( stat.st_mode ) ) type = "file";
	else                          type = "other";
	printf( "2: %d=%d: \"%s\", type=%s\n", (int)fileId, (int)id, localname, type );

	/* Invalid parent (id 20) must fail. */
	fileId = CreateLocalFile( "bucketname", "http://testfile2", 1002, 1003,
							  0664, 100, 20, &localname );
	printf( "3: %d\n", (int)fileId );
}



static void test_ClientConnects( const char *param )
{
	struct CacheClientConnection clientConnection;

	FillDatabase( );
	CompileRegexes( );

	printf( "1: " );
	ClientConnects( &clientConnection, " bucketname : 1000 : aAzZ56789+1/34567890 : 123/5+7aAzZ23456789012345678901234567890 " );
	printf( "1: %s, %s:%s\n", clientConnection.bucket, clientConnection.keyId, clientConnection.secretKey );
	printf( "2: " );
	ClientConnects( &clientConnection, " bucketname : 1001: 2345678901234567890 : 1234567890123456789012345678901234567890 " );
	printf( "3: " );
	ClientConnects( &clientConnection, " bucketname : 1002: 12345678901234567890 : 12345678901234567890123456789012345678901 " );
	printf( "4: " );
	ClientConnects( &clientConnection, " bucketname : 1003 : 12345678901234567890 ; 1234567890123456789012345678901234567890 " );
}



static int PrintCallback( void *dummy, int columns, char **data, char **names )
{
	char *uid, *gid, *permissions, *parentUid, *parentGid, *parentPermissions,
		*parentRemote, *parentLocal, *localname, *mtime;

	localname         = data[ 0 ];
	uid               = data[ 1 ];
	gid               = data[ 2 ];
	permissions       = data[ 3 ];
	parentUid         = data[ 6 ];
	parentGid         = data[ 7 ];
	parentRemote      = data[ 4 ];
	parentLocal       = data[ 5 ];
	parentPermissions = data[ 8 ];
	mtime             = data[ 9 ];

	printf( "%s (%d %d %3o %d) %s=%s (%d %d %o)\n", localname, atoi( uid ),
			atoi( gid ), atoi( permissions ), atoi( mtime ), parentRemote,
			parentLocal, atoi( parentUid ), atoi( parentGid ),
			atoi( parentPermissions ) );

	return( 0 );
}



static void PrintFileFromDatabase( const char *remotename )
{
	sqlite3 *cacheDb;
	char    *errMsg;
	int     rc;
	char    query[ 1000 ];

	cacheDb = GetCacheDatabase( );

	sprintf( query, "SELECT files.localname, files.uid, files.gid, "
			 "files.permissions, parents.remotename, parents.localname, "
			 "parents.uid, parents.gid, parents.permissions, "
			 "files.mtime "
			 "FROM files LEFT JOIN parents "
			 "ON parents.id = files.parent WHERE files.remotename = '%s'",
			 remotename );
	rc = sqlite3_exec( cacheDb, query, PrintCallback, NULL, &errMsg );
	if( rc != SQLITE_OK )
	{
		printf( "SQLite error (%i): %s\n", rc, errMsg );
		exit( EXIT_FAILURE );
	}


}


static void test_ClientRequestsCreate( const char *param )
{
	struct CacheClientConnection clientConnection;

	FillDatabase( );
	CompileRegexes( );

	clientConnection.bucket = "bucketname";

	printf( "1: " );
	ClientRequestsCreate( &clientConnection, " 1001:1002:493:1000:1003:420:100:http://remotetest1" );
	printf( "1: " );
	PrintFileFromDatabase( "http://remotetest1" );
}



/* Simulation function. */
int ReadEntireMessage( int connectionHandle, char **clientMessage )
{
	static int messageNumber = 0;
	char       *message;
	char       *messages[ ] =
	{
		"CONNECT bucketname:1000:12345678901234567890:1234567890123456789012345678901234567890",
		"CREATE 1001:1002:493:1000:1003:420:100:http://remotetest1",
		"DISCONNECT",
		NULL
	};

	message = messages[ messageNumber++ ];

	if( *clientMessage != NULL )
	{
		*clientMessage = strdup( message );
		return( strlen( message ) );
	}
	else
	{
		*clientMessage = NULL;
		return( -1 );
	}
}


static void test_ReceiveRequests( const char *param )
{
	struct CacheClientConnection clientConnection;
	pthread_t                    thread;

	FillDatabase( );
	CompileRegexes( );

	/* The function uses the simulated ReceiveAllMessages function, above. */
	pthread_create( &thread, NULL, ReceiveRequests, &clientConnection );
	pthread_join( thread, NULL );
}


static void test_CommandDispatcher( const char *param )
{
	struct CacheClientConnection clientConnection;
	int                          status;

	FillDatabase( );
	CompileRegexes( );

	status = CommandDispatcher( &clientConnection, "CONNECT bucketname:1000:12345678901234567890:1234567890123456789012345678901234567890" );
	printf( "1: Status: %d\n", status );
	status = CommandDispatcher( &clientConnection, "And now for something completely different" );
	printf( "2: Status: %d\n", status );
}



static void test_GetLocalPathQuery( const char *param )
{
	const char *localpath;

	FillDatabase( );

	localpath = Query_GetLocalPath( "http://remote1" );
	printf( "1: %s\n", localpath );

	localpath = Query_GetLocalPath( "http://nonexistent" );
	printf( "2: %s\n", localpath );
}



static void test_ClientRequestsLocalFilename( const char *param )
{
	struct CacheClientConnection clientConnection;

	FillDatabase( );
	CompileRegexes( );

	printf( "1: " );
	ClientRequestsLocalFilename( &clientConnection, "http://remote1" );
	printf( "2: " );
	ClientRequestsLocalFilename( &clientConnection, "http://nonexistent" );
}



static void test_AddUploadQuery( const char *param )
{
	bool status;

	CheckSQLiteUtil( );
	FillDatabase( );

	status = Query_AddUpload( 4, 1005, 30*1024*1024 );
	printf( "1: %d\n", status );
	system( "echo \"SELECT filesize FROM transfers WHERE file = 4;\" | sqlite3 cachedir/cache.sl3" );
	status = Query_AddUpload( 4, 1005, 130*1024*1024 );
	printf( "2: %d\n", status );
}



static void test_CreateMultiparts( const char *param )
{
	bool status;

	CheckSQLiteUtil( );
	FillDatabase( );

	status = Query_AddUpload( 4, 1005, 70*1024*1024 );
	printf( "1: %d\n", status );
	Query_CreateMultiparts( 4, 3 );
	system( "echo \"SELECT COUNT(part) FROM transferparts WHERE transfer = 4;\" | sqlite3 cachedir/cache.sl3" );
}



static void test_GetUpload( const char *param )
{
	int           part;
	char          *bucket;
	char          *remotepath;
	char          *uploadid;
	long long int filesize;
	uid_t         uid;
	gid_t         gid;
	int           permissions;
	char          *localpath;
	char          *keyid;
	char          *secretkey;
	bool          status;

	CheckSQLiteUtil( );
	FillDatabase( );

	Query_AddUpload( 4, 1005, 70*1024*1024 );
	Query_CreateMultiparts( 4, 3 );

	status = Query_GetUpload( 4, &part, &bucket, &remotepath, &uploadid,
							  &uid, &gid, &permissions, &filesize,
							  &localpath, &keyid, &secretkey );
	printf( "%d - %s : %s : %s : %d:%d - %d : %lld %s %s %s\n", status,
			bucket, remotepath, uploadid, (int)uid, (int)gid, permissions,
			filesize, localpath, keyid, secretkey );
}



static void test_SetUploadId( const char *param )
{
	int           part;
	char          *bucket;
	char          *remotepath;
	char          *uploadid;
	long long int filesize;
	uid_t         uid;
	gid_t         gid;
	int           permissions;
	char          *localpath;
	char          *keyid;
	char          *secretkey;
	bool          status;

	CheckSQLiteUtil( );
	FillDatabase( );

	Query_AddUpload( 4, 1005, 70*1024*1024 );
	Query_SetUploadId( 4, "2537akdgalk56t45lJGHKGHJ" );
	Query_CreateMultiparts( 4, 3 );

	status = Query_GetUpload( 4, &part, &bucket, &remotepath, &uploadid,
							  &uid, &gid, &permissions, &filesize,
							  &localpath, &keyid, &secretkey );
	printf( "%d - %s : %s : %s : %d:%d - %d : %lld %s %s %s\n", status,
			bucket, remotepath, uploadid, (int)uid, (int)gid, permissions,
			filesize, localpath, keyid, secretkey );
}



static void test_AllPartsUploaded( const char *param )
{
	CheckSQLiteUtil( );
	FillDatabase( );

	Query_AddUpload( 4, 1005, 70*1024*1024 );
	Query_CreateMultiparts( 4, 3 );
	system( "echo \"UPDATE transferparts SET inprogress = '1' WHERE part = '4';\" | sqlite3 cachedir/cache.sl3" );
	printf( "1: %d\n", Query_AllPartsUploaded( 4 ) );

	system( "echo \"UPDATE transferparts SET inprogress = '0' WHERE part = '4';\" | sqlite3 cachedir/cache.sl3" );
	printf( "2: %d\n", Query_AllPartsUploaded( 4 ) );

	system( "echo \"UPDATE transferparts SET completed = '1' WHERE part = '4';\" | sqlite3 cachedir/cache.sl3" );
	printf( "3: %d\n", Query_AllPartsUploaded( 4 ) );

	system( "echo \"UPDATE transferparts SET completed = '1' WHERE part = '5';\" | sqlite3 cachedir/cache.sl3" );
	system( "echo \"UPDATE transferparts SET inprogress = '1' WHERE part = '3';\" | sqlite3 cachedir/cache.sl3" );
	printf( "4: %d\n", Query_AllPartsUploaded( 4 ) );

	system( "echo \"UPDATE transferparts SET completed = '1', inprogress = '0' WHERE 1;\" | sqlite3 cachedir/cache.sl3" );
	printf( "5: %d\n", Query_AllPartsUploaded( 4 ) );
}



static void test_PartETag( const char *param )
{
	const char *etag;

	FillDatabase( );

	Query_AddUpload( 4, 1005, 70*1024*1024 );
	Query_CreateMultiparts( 4, 3 );
	Query_SetPartETag( 4, 2, "Etag 1" );

	etag = Query_GetPartETag( 4, 2 );
	printf( "1: Etag = %s\n", etag );
	etag = Query_GetPartETag( 4, 1 );
	printf( "2: Etag = %s\n", etag );
}



static void test_FindPendingUpload( const char *param )
{
	sqlite3_int64 fileId;

	FillDatabase( );

	fileId = Query_FindPendingUpload( );
	printf( "1: %d\n", (int)fileId );

	Query_AddUpload( 4, 1005, 70*1024*1024 );
	Query_CreateMultiparts( 4, 3 );
	fileId = Query_FindPendingUpload( );
	printf( "2: %d\n", (int)fileId );

	Query_DeleteUploadTransfer( 3 );
	fileId = Query_FindPendingUpload( );
	printf( "3: %d\n", (int)fileId );

	Query_DeleteUploadTransfer( 4 );
	fileId = Query_FindPendingUpload( );
	printf( "4: %d\n", (int)fileId );
}

