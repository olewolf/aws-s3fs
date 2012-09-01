/**
 * \file test-uploadqueue.c
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
#include <unistd.h>
#include <pthread.h>
#include <sys/stat.h>
#include "aws-s3fs.h"
#include "filecache.h"
#include "testfunctions.h"


#pragma GCC diagnostic ignored "-Wunused-parameter"

struct Configuration globalConfig;

extern struct
{
	bool   isReady;
	CURL   *curl;
	S3COMM *s3Comm;
} downloaders[ MAX_SIMULTANEOUS_TRANSFERS ];


struct UploadStarter
{
	int                       socket;
	int                       uploader;
	struct UploadSubscription *subscription;
};


extern pthread_mutex_t mainLoop_mutex;
extern pthread_cond_t  mainLoop_cond;


extern sqlite3_int64 GetSubscriptionFromUploadQueue( void );
extern void CompileRegexes( void );
extern int PutUpload( const char *path, int part );
extern bool ExtractHostAndFilepath( const char *remotePath,
									const char **hostname, char **filepath );
extern void CreateFileChunk( const char *parameters );



/* Dummy function for testing. */
void DeleteCurlSlistAndContents( struct curl_slist *headers )
{
}
/* Dummy function for testing. */
struct curl_slist* BuildS3Request( S3COMM *instance, const char *httpMethod,
	const char *hostname, struct curl_slist *additionalHeaders,
	const char *filename )
{
	return( NULL );
}
/* Dummy function for testing. */
int s3_SubmitS3Request( S3COMM *handle, const char *httpVerb,
						struct curl_slist *headers, const char *filename,
						void **data, int *dataLength )
{
	return( 0 );
}
/* Dummy function for testing. */
int ReadEntireMessage( int connectionHandle, char **clientMessage )
{
	return( 0 );
}


static void test_GetSubscriptionFromUploadQueue( const char *param );
static void test_NumberOfMultiparts( const char *param );
static void test_PutUpload( const char *param );
static void test_ExtractHostAndFilepath( const char *param );
static void test_CreateFilePart( const char *param );
static void test_CreateFileChunk( const char *param );


#define DISPATCHENTRY( x ) { #x, test_##x }
const struct dispatchTable dispatchTable[ ] =
{
    DISPATCHENTRY( GetSubscriptionFromUploadQueue ),
	DISPATCHENTRY( NumberOfMultiparts ),
    DISPATCHENTRY( PutUpload ),
	DISPATCHENTRY( ExtractHostAndFilepath ),
	DISPATCHENTRY( CreateFilePart ),
	DISPATCHENTRY( CreateFileChunk ),
//	DISPATCHENTRY( InitiateMultipartUpload ),
//	DISPATCHENTRY( CompleteMultipartUpload ),
//	DISPATCHENTRY( BeginUpload ),

    { NULL, NULL }
};




static void test_GetSubscriptionFromUploadQueue( const char *param )
{
	sqlite3_int64 fileId;

	FillDatabase( );
	CompileRegexes( );

	fileId = GetSubscriptionFromUploadQueue( );
	printf( "1: " );
	if( fileId == 0ll )
	{
		printf( "No subscription found\n" );
	}
	else
	{
		printf( "File ID found: %d\n", (int) fileId );
	}
	Query_DeleteUploadTransfer( fileId );

	fileId = GetSubscriptionFromUploadQueue( );
	printf( "2: " );
	if( fileId == 0ll )
	{
		printf( "No subscription found\n" );
	}
	else
	{
		printf( "File ID found: %d\n", (int) fileId );
	}
}



static void test_NumberOfMultiparts( const char *param )
{
	int parts;

	long long int MBYTES = 1024l * 1024l;
	long long int GBYTES = 1024l * MBYTES;
	long long int TBYTES = 1024l * GBYTES;

	parts = NumberOfMultiparts( 100 );
	printf( "1: %d\n", parts );
	parts = NumberOfMultiparts( 25 * MBYTES );
	printf( "2: %d\n", parts );
	parts = NumberOfMultiparts( 25 * MBYTES + 1 );
	printf( "3: %d\n", parts );
	parts = NumberOfMultiparts( 150 * GBYTES );
	printf( "4: %d\n", parts );
	parts = NumberOfMultiparts( 5ll * TBYTES );
	printf( "5: %d\n", parts );
	parts = NumberOfMultiparts( 6ll * TBYTES );
	printf( "6: %d\n", parts );
}



static void test_PutUpload( const char *param )
{
	CheckSQLiteUtil( );
	FillDatabase( );
	CompileRegexes( );

	system( "cp ../../../README cachedir/files/FILE01" );
	PutUpload( "http://remote1", 1005 );
	system( "echo \"SELECT transferparts.part, transfers.id FROM "
			"transferparts INNER JOIN transfers "
			"ON transferparts.transfer = transfers.id "
			"WHERE transfers.file = '1';\" | sqlite3 cachedir/cache.sl3" );
}



static void test_ExtractHostAndFilepath( const char *param )
{
	const char *hostname;
	char       *filepath;

	CompileRegexes( );

	ExtractHostAndFilepath( "http://s3.amazonaws.com/bucketname/DIR01/FILE05",
							&hostname, &filepath );
	printf( "1: %s, %s\n", hostname, filepath );

	ExtractHostAndFilepath( "http://bucketname.s3-ap-northeast-1.amazonaws.com/DIR01/FILE05", &hostname, &filepath );
	printf( "2: %s, %s\n", hostname, filepath );

	ExtractHostAndFilepath( "http://bucketname.s3-ap-northeast-1.amazonaws.com/", &hostname, &filepath );
	printf( "3: %s, %s\n", hostname, filepath );

	ExtractHostAndFilepath( "http://bucketname.s3.amazonaws.com/DIR01/", &hostname, &filepath );
	printf( "4: %s, %s\n", hostname, filepath );

	ExtractHostAndFilepath( "http://bucketname.amazonaws.com/DIR01/", &hostname, &filepath );
	printf( "5: %s, %s\n", hostname, filepath );

	ExtractHostAndFilepath( "http://bucketname.amazonaws.com", &hostname, &filepath );
	printf( "6: %s, %s\n", hostname, filepath );
}


static void test_CreateFilePart( const char *param )
{
	int        partLength;
	const char *localFilePartPath;

	system( "mkdir -p " CACHE_INPROGRESS );

	partLength = CreateFilePart( 0, "filename", 1, 26l * 1024l * 1024l,
								 &localFilePartPath );
	printf( "1: %s, %d bytes\n", localFilePartPath, partLength );

	partLength = CreateFilePart( 0, "filename", 2, 26l * 1024l * 1024l,
								 &localFilePartPath );
	printf( "2: %s, %d bytes\n", localFilePartPath, partLength );

	system( "echo -n \"Files created: \"; ls -1 " CACHE_INPROGRESS " | wc -l" );
}



static void test_CreateFileChunk( const char *param )
{
	unsigned char buf[ 129 ];
	int           i;
	FILE          *fd;

	system( "mkdir -p " CACHE_FILES "hJire8" );
	system( "mkdir -p " CACHE_INPROGRESS );

	/* Create a file whose size is larger than 25 MBytes and not divisible
	   by 65,536. */
	for( i = 0; i < 129; i++ )
	{
		buf[ i ] = (char) i;
	}
	fd = fopen( CACHE_FILES "hJire8/kj6Upq", "w+" );
	for( i = 0; i < ( 26 * 1024 * 1024 + 128 ) / 129; i++ )
	{
		fwrite( buf, sizeof( buf ), 1, fd );
	}
	fclose( fd );
	/* Prepare file chunks. */
	system( "touch " CACHE_INPROGRESS "/dChnk1 " CACHE_INPROGRESS "/dChnk2" );

	CreateFileChunk( "1:hJire8/kj6Upq:dChnk1" );
	CreateFileChunk( "2:hJire8/kj6Upq:dChnk2" );
	system( "ls -1 -g -o " CACHE_INPROGRESS );
}
