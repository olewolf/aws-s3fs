/**
 * \file test-s3if.c
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
#include <curl/curl.h>
#include "testfunctions.h"
#include "aws-s3fs.h"
#include "statcache.h"
#include "s3if.h"
#include "filecache.h"

#pragma GCC diagnostic ignored "-Wunused-parameter"


struct Configuration globalConfig =
{
    .region     = SINGAPORE,
    .mountPoint = "mountdir",
    .bucketName = "bucket-name",
    .path       = "mounted-dir",
    .keyId      = "12344567891234567890",
    .secretKey  = "1234567890123456789012345678901234567890",
    .logfile    = NULL,
    .verbose =
                  {
                      false,
                      false
		  },
    .logLevel   = log_DEBUG
};


S3COMM handle =
{
	0,
	NULL,
	NULL,
	NULL,
	NULL,
	PTHREAD_MUTEX_INITIALIZER
};


extern S3COMM *s3comm;
extern struct curl_slist* BuildGenericHeader( const char *hostname );
extern char *GetHeaderStringValue( const char *string );
extern int AddHeaderValueToSignString( char*, char* );
extern struct curl_slist *CreateAwsSignature( const char *httpMethod,
    struct curl_slist *headers,
    enum bucketRegions region, const char *bucket, const char *path,
    const char *keyId, const char *secretKey );
extern struct curl_slist *BuildS3Request(
	S3COMM             *handle,
    const char         *httpMethod,
	const char         *hostname,
    struct curl_slist  *additionalHeaders,
    const char         *filename );
extern int s3_SubmitS3Request(
	S3COMM             *handle,
    const char         *httpVerb,
    struct curl_slist  *headers,
    const char         *filename,
    void               **data,
    int                *dataLength );
extern int S3GetFileStat( const char *filename, struct S3FileInfo **fileInfo );

static void test_BuildGenericHeader( const char *parms );
static void test_GetHeaderStringValue( const char *parms );
static void test_AddHeaderValueToSignString( const char *parms );
static void test_CreateAwsSignature( const char *param );
static void test_BuildS3Request( const char *param );
static void test_SubmitS3RequestHead( const char *param );
static void test_SubmitS3RequestData( const char *param );
static void test_S3GetFileStat( const char *param );
static void test_S3FileStat_File( const char *param );
static void test_S3FileStat_Dir( const char *param );
static void test_S3ReadDir( const char *param );


const struct dispatchTable dispatchTable[ ] =
{
    { "S3ReadDir", test_S3ReadDir },
    { "S3FileStatDir", test_S3FileStat_Dir },
    { "S3FileStatFile", test_S3FileStat_File },
    { "S3GetFileStat", test_S3GetFileStat },
    { "SubmitS3RequestHead", test_SubmitS3RequestHead },
    { "SubmitS3RequestData", test_SubmitS3RequestData },
    { "BuildS3Request", test_BuildS3Request },
    { "CreateAwsSignature", test_CreateAwsSignature },
    { "AddHeaderValueToSignString", test_AddHeaderValueToSignString },
    { "GetHeaderStringValue", test_GetHeaderStringValue },
    { "BuildGenericHeader", test_BuildGenericHeader },
    { NULL, NULL }
};


static void SetupHandle( void )
{
	handle.region = globalConfig.region;
	handle.bucket = globalConfig.bucketName;
	handle.keyId = globalConfig.keyId;
	handle.secretKey = globalConfig.secretKey;
	handle.curl = curl_easy_init( );
}



static void test_BuildGenericHeader( const char *parms )
{
    struct curl_slist *headers;
    struct curl_slist *header;
    int i;

    headers = BuildGenericHeader( "bucket-name.s3-ap-southeast-1.amazonaws.com" );
    header = headers;
    i = 1;
    while( header != NULL )
    {
        printf( "%d: %s\n", i++, header->data );
	header = header->next;
    }
}


static void test_GetHeaderStringValue( const char *parms )
{
    char *value1, *value2;
    value1 = GetHeaderStringValue( " Test header 1:  value1" );
    value2 = GetHeaderStringValue( " Test header 2 :value2" );
    printf( "%s, %s\n", value1, value2 );
    free( value1 ); free( value2 );
}


static void test_AddHeaderValueToSignString( const char *parms )
{
    char signString[ 1024 ];
    char *toAdd;
    int addedlen;
    int i;

    strcpy( signString, "Test line 1\n" );

    addedlen = AddHeaderValueToSignString( signString, NULL );
    printf( "1: %d\n", addedlen );

    toAdd = malloc( 100 );
    strcpy( toAdd, "Test line 3" );
    addedlen = AddHeaderValueToSignString( signString, toAdd );
    for( i = 0; i < (int) strlen( signString ); i++ )
    {
        if( signString[ i ] == '\n' )
		{
			signString[ i ] = '_';
		}
    }
    printf( "2: %d %s\n", addedlen, signString );
}



static void test_CreateAwsSignature( const char *param )
{
    struct curl_slist *headers = NULL;
    struct curl_slist *header;
    int i;
    const char *path = "/the-path/with.html?some=parameter";

    headers = curl_slist_append( headers, "Content-MD5: kahaKUW/a80945+a553" );
    headers = curl_slist_append( headers, "Content-Type: image/jpeg" );
    headers = curl_slist_append( headers, "x-amz-also-metavariable: something else" );
    headers = curl_slist_append( headers, "X-AMZ-metavariable: Something" );
    headers = curl_slist_append( headers, "Date: Sun, Jun 17 2012 17:58:24 +0200" );

    headers = CreateAwsSignature( "HEAD", headers, globalConfig.region,
								  globalConfig.bucketName, path,
								  globalConfig.keyId, globalConfig.secretKey );

    header = headers;
    i = 1;
    while( header != NULL )
    {
        printf( "%d: %s\n", i++, header->data );
	header = header->next;
    }
}



static void test_BuildS3Request( const char *param )
{
    struct curl_slist *headers = NULL;
    struct curl_slist *currentHeader;
    int               i = 0;

    char *method = "HEAD";
    char *path = "/with.html?some=parameter";
    char *keyId     = "aboguskeyid";
    char *secretKey = "somebogussecret";

	SetupHandle( );
	handle.keyId  = keyId;
    handle.secretKey = secretKey;

    headers = curl_slist_append( headers, "Content-MD5: kahaKUW/a80945+a553" );
    headers = curl_slist_append( headers, "Content-Type: image/jpeg" );
    headers = curl_slist_append( headers, "x-amz-metavariable: something" );
    headers = curl_slist_append( headers, "x-amz-also-metavariable: something else" );
    headers = BuildS3Request( &handle, method,
							  "bucket-name.s3-ap-southeast-1.amazonaws.com",
							  headers, path );

    currentHeader= headers;
    while( currentHeader != NULL )
    {
        printf( "%d: %s\n", ++i, currentHeader->data );
        currentHeader = currentHeader->next;
    }
}



static void test_SubmitS3RequestHead( const char *param )
{
    int               i;
    struct curl_slist *headers = NULL;
    char              **curlOut = NULL;
    int               outLength = 0;

    ReadLiveConfig( param );
	SetupHandle( );
    InitializeS3If( );

    i = s3_SubmitS3Request( &handle, "HEAD", headers,
							"/README", (void**)&curlOut, &outLength );

    free( globalConfig.keyId );
    free( globalConfig.secretKey );
    free( globalConfig.bucketName );

    if( i == 0 )
    {
        for( i = 0; i < outLength; i++ )
	{
	  printf( "%s: %s\n", curlOut[ i * 2], curlOut[ i * 2 + 1] );
	}
    }

    else
    {
        printf( "CURL error %d\n", i );
    }

}



static void test_SubmitS3RequestData( const char *param )
{
    struct curl_slist *headers = NULL;
    char **curlOut  = NULL;
    int  outLength = 0;
    int  i;
    char *outPrint;

    ReadLiveConfig( param );
	SetupHandle( );
    InitializeS3If( );

    i = s3_SubmitS3Request( &handle, "GET",
						 headers, "/?delimiter=/&prefix=directory/",
						 (void**)&curlOut, &outLength );

    free( globalConfig.keyId );
    free( globalConfig.secretKey );
    free( globalConfig.bucketName );

    if( i == 0 )
    {
        outPrint = malloc( outLength + sizeof (char ) );
	strncpy( outPrint, (char*)curlOut, outLength );
	free( curlOut );
        outPrint[ outLength ] = '\0';
	printf( "%s\n", outPrint );
	free( outPrint );
    }
    else
    {
	printf( "CURL error\n" );
    }

}



static void test_S3GetFileStat( const char *param )
{
    struct S3FileInfo *fi;
    int               status;

    ReadLiveConfig( param );
	SetupHandle( );
    InitializeS3If( );
	s3comm = &handle;

    status = S3GetFileStat( "/README", &fi );
    if( status != 0) exit( 1 );
    if( fi == NULL ) exit( 1 );
    printf( "t=%c s=%d p=%3o uid=%d gid=%d\na=%s",
	    fi->fileType, (int)fi->size, fi->permissions, fi->uid, fi->gid,
	    ctime(&fi->atime) );
    printf( "m=%s", ctime(&fi->mtime) );
    printf( "c=%ssp=%d\n", ctime(&fi->ctime),
	    ( fi->exeUid ? S_ISUID : 0 ) | ( fi->exeGid ? S_ISGID : 0 ) |
	    ( fi->exeUid ? S_ISVTX : 0 ) );

    free( fi );
}



static void test_S3FileStat_File( const char *param )
{
    struct S3FileInfo *fi;
    struct S3FileInfo *cachedFi;
    int               status;

    ReadLiveConfig( param );
	SetupHandle( );
    InitializeS3If( );
	s3comm = &handle;

    status = S3FileStat( "/README", &fi );
    if( status != 0)
    {
        fprintf( stderr, "Error: %d\n", status );
	exit( 1 );
    }
    if( fi == NULL ) exit( 1 );

    /* Verify that the file was found in the cache. */
    cachedFi = SearchStatEntry( "/README" );
    if( cachedFi == NULL )
    {
        printf( "File not found in stat cache.\n" );
        exit( 1 );
    }
    else
    {
        printf( "t=%c s=%d p=%3o uid=%d gid=%d\na=%s",
		fi->fileType, (int)fi->size, fi->permissions, fi->uid, fi->gid,
		ctime(&fi->atime) );
	printf( "m=%s", ctime(&fi->mtime) );
	printf( "c=%ssp=%d\n", ctime(&fi->ctime),
		( fi->exeUid ? S_ISUID : 0 ) | ( fi->exeGid ? S_ISGID : 0 ) |
		( fi->exeUid ? S_ISVTX : 0 ) );
    }
}



static void test_S3FileStat_Dir( const char *param )
{
    struct S3FileInfo *fi;
    struct S3FileInfo *cachedFi;
    int               status;

    ReadLiveConfig( param );
	SetupHandle( );
    InitializeS3If( );
	s3comm = &handle;
    status = S3FileStat( "/directory", &fi );
    if( status != 0) exit( 1 );
    if( fi == NULL ) exit( 1 );

    /* Verify that the directory was found in the cache. */
    cachedFi = SearchStatEntry( "/directory" );
    if( cachedFi == NULL )
    {
        printf( "Directory not found in stat cache.\n" );
        exit( 1 );
    }
    else
    {
        printf( "t=%c s=%d p=%3o uid=%d gid=%d\na=%s",
		fi->fileType, (int)fi->size, fi->permissions, fi->uid, fi->gid,
		ctime(&fi->atime) );
		printf( "m=%s", ctime(&fi->mtime) );
		printf( "c=%ssp=%d\n", ctime(&fi->ctime),
		( fi->exeUid ? S_ISUID : 0 ) | ( fi->exeGid ? S_ISGID : 0 ) |
		( fi->exeUid ? S_ISVTX : 0 ) );
    }
}



static void test_S3ReadDir( const char *param )
{
    char **directory;
    int  dirEntries;
    int  status;
    int  i;

    ReadLiveConfig( param );
	SetupHandle( );
    InitializeS3If( );
	s3comm = &handle;

    status = S3ReadDir( NULL, "/directory", &directory, &dirEntries, -1 );
    if( status != 0) exit( 1 );

    for( i = 0; i < dirEntries; i++ )
    {
	printf( "%d: %s\n", i, directory[ i ] );
	free( directory[ i ] );
    }
    free( directory );
}

