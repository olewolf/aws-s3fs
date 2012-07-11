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

extern sqlite3_int64 FindFile( const char *path, char *localname );
extern sqlite3_int64 FindParent( const char *path, char *localname );
extern const sqlite3 *GetCacheDatabase( void );

static void test_InitializeFileCacheDatabase( const char *param );
static void test_FindFile( const char *param );
static void test_FindParent( const char *param );
static void test_CreateLocalFile( const char *param );
static void test_CreateLocalDir( const char *param );
static void test_GetDownload( const char *param );
static void test_GetOwners( const char *param );
static void test_DeleteTransfer( const char *param );
static void FillDatabase( void );


const struct dispatchTable dispatchTable[ ] =
{
    { "InitializeFileCacheDatabase", test_InitializeFileCacheDatabase },
	{ "FindFile", test_FindFile },
	{ "FindParent", test_FindParent },
	{ "CreateLocalFile", test_CreateLocalFile },
	{ "CreateLocalDir", test_CreateLocalDir },
	{ "GetDownload", test_GetDownload },
	{ "GetOwners", test_GetOwners },
	{ "DeleteTransfer", test_DeleteTransfer },
    { NULL, NULL }
};



static void CheckSQLiteUtil( void )
{
#ifndef HAVE_SQLITE_UTIL
	printf( "sqlite3 not found; skipping test.\n" );
	exit( 77 );
#endif
}


#if 0
static void PrepareLiveTestData( const char *configFile )
{
	const sqlite3 *cacheDb = GetCacheDatabase( );

	ReadLiveConfig( configFile );
	
}
#endif


static void CreateDatabase( void )
{
	unlink( CACHE_DATABASE );
	rmdir( CACHE_DIR );
	mkdir( CACHE_DIR, 0750 );
	InitializeFileCacheDatabase( );
}


static void FillDatabase( void )
{
	CheckSQLiteUtil( );
	CreateDatabase( );
	if( system( "echo \"PRAGMA foreign_keys = ON;\n.read ../../testdata/cache.sql\" | sqlite3 " CACHE_DATABASE ) != 0 )
	{
		exit( EXIT_FAILURE );
	}
}




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



static void test_CreateLocalFile( const char *param )
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



static void test_CreateLocalDir( const char *param )
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
/*
	CreateDatabase( );
	const sqlite3 *cacheDb;
	char          *errMsg;
	int           rc;
	cacheDb = GetCacheDatabase( );
	rc = sqlite_exec( cacheDb, "INSERT INTO USERS ...", NULL, NULL, &errMsg );
	if( rc != SQLITE_OK )
	{
		fprintf( "SQLite error (%i): %s\n", rc, errMsg );
		exit( EXIT_FAILURE );
	}
*/
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
	status = Query_DeleteTransfer( 2 );
	printf( "1: %d\n", status );
	status = Query_DeleteTransfer( 3 );
	printf( "2: %d\n", status );
	status = Query_DeleteTransfer( 200 );
	printf( "3: %d\n", status );
}
