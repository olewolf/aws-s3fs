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
#include "s3if.h"

#include <pthread.h>


struct Configuration globalConfig =
{
    .region = SINGAPORE,
    .mountPoint = "mountdir",
    .bucketName = "bucket-name",
    .path       = "mounted-dir",
    .keyId      = "1234",
    .secretKey  = "5678",
    .logfile    = NULL,
    .verbose =
                  {
                      false,
                      false
		  },
    .logLevel   = log_DEBUG,
    .daemonize  = false
};



extern struct curl_slist* BuildGenericHeader( const char *httpMethod );
extern char *GetHeaderStringValue( const char *string );
extern int AddHeaderValueToSignString( char*, char* );
extern struct curl_slist *CreateAwsSignature( const char *httpMethod,
    struct curl_slist *headers, const char *path );
extern struct curl_slist *BuildS3Request( const char *httpMethod,
    struct curl_slist *additionalHeaders, const char *filename );
extern int SubmitS3Request( const char *httpVerb, struct curl_slist *headers,
    const char *filename );
extern unsigned char *curlWriteBuffer;
extern size_t curlWriteBufferLength;

static void test_BuildGenericHeader( const char *parms );
static void test_GetHeaderStringValue( const char *parms );
static void test_AddHeaderValueToSignString( const char *parms );
static void test_CreateAwsSignature( const char *param );
static void test_BuildS3Request( const char *param );
static void test_SubmitS3Request( const char *param );


const struct dispatchTable dispatchTable[ ] =
{
    { "SubmitS3Request", test_SubmitS3Request },
    { "BuildS3Request", test_BuildS3Request },
    { "CreateAwsSignature", test_CreateAwsSignature },
    { "AddHeaderValueToSignString", test_AddHeaderValueToSignString },
    { "GetHeaderStringValue", test_GetHeaderStringValue },
    { "BuildGenericHeader", test_BuildGenericHeader },
    { NULL, NULL }
};


static void test_BuildGenericHeader( const char *parms )
{
    struct curl_slist *headers;
    struct curl_slist *header;
    int i;

    headers = BuildGenericHeader( "GET" );
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
    for( i = 0; i < strlen( signString ); i++ )
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
    const char *path = "http://the.web.address/with.html?some=parameter";

    headers = curl_slist_append( headers, "Content-MD5: kahaKUW/a80945+a553" );
    headers = curl_slist_append( headers, "Content-Type: image/jpeg" );
    headers = curl_slist_append( headers, "X-AMZ-metavariable: Something" );
    headers = curl_slist_append( headers, "Date: Sun, Jun 17 2012 17:58:24 +0200" );
    headers = curl_slist_append( headers, "x-amz-also-metavariable: something else" );

    headers = CreateAwsSignature( "HEAD", headers, path );

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

    globalConfig.keyId = keyId;
    globalConfig.secretKey = secretKey;

    headers = curl_slist_append( headers, "Content-MD5: kahaKUW/a80945+a553" );
    headers = curl_slist_append( headers, "Content-Type: image/jpeg" );
    headers = curl_slist_append( headers, "x-amz-metavariable: something" );
    headers = curl_slist_append( headers, "x-amz-also-metavariable: something else" );
    headers = BuildS3Request( method, headers, path );

    currentHeader= headers;
    while( currentHeader != NULL )
    {
        printf( "%d: %s\n", ++i, currentHeader->data );
        currentHeader = currentHeader->next;
    }
}



static void test_SubmitS3Request( const char *param )
{
    FILE               *conf;
    char               buf[ 100 ];
    int                i;
    char               *configKey   = NULL;
    char               *configValue = NULL;

    struct curl_slist *headers = NULL;
    char *curlOut;

    globalConfig.bucketName = NULL;
    globalConfig.keyId      = NULL;
    globalConfig.secretKey  = NULL;

    if( param == NULL )
    {
        printf( "Config file with authentication data required for live tests.\n" );
	exit( 77 );
    }

    /* Read the test config file. */
    conf = fopen( param, "r" );
    if( ! conf )
    {
        printf( "Cannot open config file \"%s\".\n", param );
	exit( 77 );
    }
    while( ! feof( conf ) )
    {
        if( fgets( buf, sizeof( buf ), conf ) != NULL )
	{
	    if( ( buf[ 0 ] != '#' ) && ( ! isspace( buf[ 0 ] ) ) )
	    {
		configKey = &buf[ 0 ];
		configValue = NULL;
		for( i = 0; ( buf[ i ] != '\0' ) && ( buf[ i ] != '\n' ); i++ )
		{
		    if( buf[ i ] == ':' )
		    {
		        configValue = &buf[ i + 1 ];
			break;
		    }
		}
		if( configValue != NULL )
		{
		    while( configValue[ i ] != '\0' )
		    {
		        if( configValue[ i ] == '\n' )
			{
			    configValue[ i ] = '\0';
			}
			i++;
		    }
		}
		if( strncmp( configKey, "key", 3 ) == 0 )
	        {
		    i = 0;
		    while( configValue[ i ] != ':' ) i++;
		    globalConfig.keyId = malloc( i + 1 );
		    strncpy( globalConfig.keyId, configValue, i );
		    i++;
		    globalConfig.secretKey = malloc( strlen( &configValue[ i ] ) + 1 );
		    strcpy( globalConfig.secretKey, &configValue[ i ] );
		}
		else if( strncmp( configKey, "bucket", 6 ) == 0 )
		{
		    globalConfig.bucketName = strdup( configValue );
		}
		else if( strncmp( configKey, "region", 6 ) == 0 )
	        {
		    globalConfig.region = atoi( configValue );
		}
		else
		{
		    printf( "Config key not recognized.\n" );
		}
	    }
	}
    }

    InitializeS3If( );

    headers = BuildS3Request( "HEAD", NULL, "/README" );

    i = SubmitS3Request( "HEAD", headers, "/README" );

    free( globalConfig.keyId );
    free( globalConfig.secretKey );
    free( globalConfig.bucketName );

    if( i == 0 )
    {
        printf( "Received %d bytes\n", (int)curlWriteBufferLength );
        curlOut = malloc( curlWriteBufferLength + sizeof (char ) );
	strncpy( curlOut, (const char*)curlWriteBuffer, curlWriteBufferLength );
	free( curlWriteBuffer );
        curlOut[ curlWriteBufferLength ] = '\0';
	printf( "%s\n", curlOut );
	free( curlOut );
    }
    else
    {
	printf( "CURL error\n" );
    }

}

