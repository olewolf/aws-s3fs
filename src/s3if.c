/**
 * \file s3if.c
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


#include <config.h>
#include <ctype.h>
#include <stdbool.h>
#include <malloc.h>
#include <string.h>
#include <errno.h>
#include <curl/curl.h>
#include <time.h>
#include <locale.h>
#include "aws-s3fs.h"
#include "s3if.h"
#include "digest.h"
#include "statcache.h"


#include <stdlib.h>


/* Amazon host names for various regions, ordered according to the enumeration
   "bucketRegions". */
static const char *amazonHost[ ] =
{
    "s3-us-east-1",      /* US_STANDARD */
    "s3-us-west-2",      /* OREGON */
    "s3-us-west-1",      /* NORTHERN_CALIFORNIA */
    "s3-eu-west-1",      /* IRELAND */
    "s3-ap-southeast-1", /* SINGAPORE */
    "s3-ap-northeast-1", /* TOKYO */
    "s3-sa-east-1"       /* SAO_PAULO */
};



/**
 * Callback function for freeing the memory allocated for an S3FileInfo
 * structure when an entry is deleted from the cache.
 * @param toDelete [in/out] Structure that should be deallocated.
 * @return Nothing.
 */
static void DeleteS3FileInfoStructure(
    void *toDelete
				      )
{
    if( toDelete != NULL )
    {
        free( toDelete );
    }
}



/**
 * Create a generic S3 request header which is used in all requests.
 * @param httpMethod [in] HTTP verb (GET, HEAD, etc.) for the request.
 * @return Header list.
 */
#ifndef AUTOTEST
static
#endif
struct curl_slist*
BuildGenericHeader(
    const char *httpMethod
		    )
{
    struct curl_slist *headers       = NULL;
    char              headbuf[ 4096 ];

    time_t            now;
    const struct tm   *tnow;
    char              *locale;

    /* Make Host: address a virtual host. */
    sprintf( headbuf, "Host: %s.%s.amazonaws.com",
	     globalConfig.bucketName, amazonHost[ globalConfig.region ] );
    headers = curl_slist_append( headers, headbuf );

    /* Generate time string. */
    locale = setlocale( LC_TIME, "en_US.UTF-8" );
    now    = time( NULL );
    tnow   = localtime( &now );
    strftime( headbuf, sizeof( headbuf ), "Date: %a, %d %b %Y %T %z", tnow );
    setlocale( LC_TIME, locale );
    headers = curl_slist_append( headers, headbuf );

    /* Add user agent. */
    headers = curl_slist_append( headers, "User-Agent: curl" );

    return( headers );
}



/**
 * Extract the \a value part from a header string formed as \a key:value.
 * @param headerString [in] Header string with key: value string.
 * @return String with the \a value content.
 */
#ifndef AUTOTEST
static
#endif
char*
GetHeaderStringValue( const char *headerString )
{
    char *value = NULL;
    int  idx    = 0;
    char ch;

    /* Skip past the key to where the value is. */
    while( ( ch = headerString[ idx++ ] ) != '\0' )
    {
        /* ':' marks the end of the header key. */
        if( ch == ':' )
	{
	    /* Skip past whitespace. */
	    while( ( ( ch = headerString[ idx ] ) != '\0' )
		   && isspace( ch ) )
	    {
	        idx++;
	    }
	    /* Return the value, which is the remainder of the string. */
	    value = strdup( &headerString[ idx ] );
	    break;
	}
    }

    /* Return an empty string if no value was found. */
    if( value == NULL )
    {
        value = malloc( sizeof( char ) );
        value[ 0 ] = '\0';
    }
    
    return( value );
}



/**
 * Add a string + '\n' to the header text that is to be signed with the AWS
 * key, then free the string. If the string is NULL, only the '\n' is appended
 * to the header text.
 * @param messageToSign [in/out] The header text that will be signed.
 * @param headerValue [in/out] The string to append to the header text.
 * @return Number of bytes added to the header text.
 */
#ifndef AUTOTEST
static
#endif
int
AddHeaderValueToSignString(
    char *messageToSign,
    char *headerValue
			   )
{
    int addedLength = 0;

    assert( messageToSign != NULL );
    /* Add the header value and free its memory. */
    if( headerValue != NULL )
    {
	strcat( messageToSign, headerValue );
        addedLength += strlen( headerValue );
	free( headerValue );
    }
    /* Add a trailing LF regardless of whether the header value was NULL. */
    strcat( messageToSign, "\n" );
    addedLength += strlen( "\n" );

    return( addedLength );
}



/**
 * Create an Amazon AWS signature for the header and add it to the header.
 * Note: the AWS signature adds a leading '\n', marking the termination of all
 * headers.
 *
 * @param httpMethod [in] HTTP method, e.g., "GET" or "PUT".
 * @param headers [in/out] Request headers.
 * @param path [in] Path of the requested file relative to the bucket.
 * @param keyId [in] User's Amazon Key ID.
 * @param secretKey [in] User's Secret Key.
 * @return Nothing.
 */
#ifndef AUTOTEST
static
#endif
struct curl_slist*
CreateAwsSignature(
    const char        *httpMethod,	   
    struct curl_slist *headers,
    const char        *path
		   )
{
    char              messageToSign[ 4096 ];
    char              amzHeaders[ 2048 ];
    char              awsHeader[ 100 ];
    int               messageLength;
    const char        *signature;

    struct curl_slist *currentHeader;
    char              ch;
    char              *headerString;
    int               idx;
    char              *contentMd5  = NULL;
    char              *contentType = NULL;
    char              *dateString  = NULL;
    bool              convertingHeaderKey;

    /* Build HTTP method, content MD5 placeholder, content type, and time. */
    if( httpMethod == NULL )
    {
        httpMethod = "";
    }
    sprintf( messageToSign, "%s\n", httpMethod );
    messageLength = strlen( messageToSign );

    /* Go through all headers and extract the Content-MD5, Content-type,
       Date, and x-amz-* headers, and add them to the message. */
    amzHeaders[ 0 ] = '\0';
    currentHeader = headers;
    while( currentHeader != NULL )
    {
        headerString = currentHeader->data;

	/* Get x-amz-* headers. NOTE: Must be sorted!!! */
        if( strncasecmp( headerString, "x-amz-", strlen( "x-amz-" ) ) == 0 )
	{
	    /* Convert the x-amz- header to lower case. */
	    idx = 0;
	    convertingHeaderKey = true;
	    while( ( ch = headerString[ idx ] ) != '\0' )
	    {
	        if( ch == ':' )
		{
		    convertingHeaderKey = false;
		}
	        else if( convertingHeaderKey )
	        {
		    ch = tolower( ch );
		}
	        headerString[ idx++ ] = ch;
	    }
	    headerString[ idx ] = '\0';
	    strcat( amzHeaders, headerString );
	    strcat( amzHeaders, "\n" );
	    messageLength += strlen( headerString ) + strlen( "\n" );
	}
	/* Get Content-MD5 header. */
	else if( strncasecmp( headerString,
			      "Content-MD5", strlen( "Content-MD5" ) ) == 0 )
	{
	    contentMd5 = GetHeaderStringValue( headerString );
	}
	/* Get Content-Type header. */
	else if( strncasecmp( headerString,
			      "Content-Type", strlen( "Content-Type" ) ) == 0 )
	{
	    contentType = GetHeaderStringValue( headerString );
	}
	/* Get Date header. */
	else if( strncasecmp( headerString,
			      "Date", strlen( "Date" ) ) == 0 )
	{
	    dateString = GetHeaderStringValue( headerString );
	}

	assert( messageLength < sizeof( messageToSign ) - 200 );
	currentHeader = currentHeader->next;
    }

    messageLength += AddHeaderValueToSignString( messageToSign, contentMd5 );
    messageLength += AddHeaderValueToSignString( messageToSign, contentType );
    messageLength += AddHeaderValueToSignString( messageToSign, dateString );
    strcat( messageToSign, amzHeaders );

    /* Add the path. */
    /* Path is already assumed to be in UTF-8. Is this safe? */
    /*/urlPath = utf8Encode( path );*/
    sprintf( &messageToSign[ strlen( messageToSign ) ],
	     "/%s%s", globalConfig.bucketName, path );
    messageLength = strlen( path );

    /* Sign the message and add the Authorization header. */
    signature = HMAC( (const unsigned char*) messageToSign,
		      strlen( messageToSign ),
		      (const char*) globalConfig.secretKey,
		      HASH_SHA1, HASHENC_BASE64 );
    sprintf( awsHeader, "Authorization: AWS %s:%s\n",
	     globalConfig.keyId, signature );
    headers = curl_slist_append( headers, awsHeader );

    return headers;
}



/**
 * Helper function for the qsort function in \a BuildS3Request. The helper
 * functions ensures that strings are sorted in descending order, because
 * the \a BuildS3Request reinserts them in a linked list in reverse order;
 * this will cause the strings to eventually be sorted in ascending order.
 * @param a [in] First string in the qsort comparison.
 * @param b [in] Second string in the qsort comparison.
 * @return 0 if \a a = \a b, -1 if \a a > \a b, or 1 if \a a < \a b.
 */
static inline int
qsort_strcmp( const void *a, const void *b )
{
    return( -strcmp( (const char*)a, (const char*)b ) );
}



/**
 * Create an S3 request that is ready to be submitted via CURL.
 * @param httpMethod [in] The HTTP verb for the request type.
 * @param additionalHeaders[ in ] Any additional HTTP headers that should be
 *        included in the request.
 * @param filename [in] The full path of the file that is accessed.
 * @return Complete HTTP header list, including an AWS signature.
 */
#ifndef AUTOTEST
static
#endif
struct curl_slist*
BuildS3Request(
    const char        *httpMethod,
    struct curl_slist *additionalHeaders,
    const char        *filename
	       )
{
    struct curl_slist *allHeaders;
    struct curl_slist *currentHeader;
    char              *extraHeaders[ 100 ];
    int               count;
    int               i;

    /* Build basic headers. */
    allHeaders = BuildGenericHeader( httpMethod );

    /* Add any additional headers, if any, in sorted order. */
    currentHeader = additionalHeaders;
    count         = 0;
    while( currentHeader != NULL )
    {
	extraHeaders[ count++ ] = currentHeader->data;
        currentHeader = currentHeader->next;
	if( count == sizeof( extraHeaders ) / sizeof( char* ) )
	{
	    break;
	}
    }
    /* Sort the headers. */
    qsort( &extraHeaders[ 0 ], count, sizeof( char* ), &qsort_strcmp );
    /* Add the headers to the main header list and get rid of the extras
       list. */
    for( i = 0; i < count; i++ )
    {
        allHeaders = curl_slist_append( allHeaders, extraHeaders[ i ] );
    }
    curl_slist_free_all( additionalHeaders );

    /* Add a signature header. */
    allHeaders = CreateAwsSignature( httpMethod, allHeaders, filename );

    return( allHeaders );
}



/**
 * Retrieve information on a specific file in an S3 path.
 * @param filename [in] Full path of the file, relative to the bucket.
 * @param fileInfo [out] S3 FileInfo structure.
 * @return 0 on success, or \a -errno on failure.
 */
int
S3FileStatRequest(
    const char        *filename,
    struct S3FileInfo **fileInfo
		  )
{
    struct curl_slist *headers = NULL;
    struct S3FileInfo *newFileInfo;
    int               status = 0;

    /* Create specific file information request headers. */
    /* (None required.) */

    /* Setup S3 headers. */
    BuildS3Request( "HEAD", headers, filename );

    /* Make request via curl and wait for response. */


    curl_slist_free_all( headers );

    /* Translate file attributes to an S3 File Info structure. */
    newFileInfo = malloc( sizeof (struct S3FileInfo ) );
    assert( newFileInfo != NULL );
    /*
    newFileInfo->uid         = ;
    newFileInfo->gid         = ;
    newFileInfo->permissions = ;
    newFileInfo->fileType    = ;
    newFileInfo->exeUid      = ;
    newFileInfo->exeGid      = ;
    newFileInfo->size        = ;
    newFileInfo->atime       = ;
    newFileInfo->mtime       = ;
    newFileInfo->ctime       = ;
    */

    *fileInfo = newFileInfo;
    return( status );
}



/**
 * Read the specified file's attributes from S3 and insert them into
 * the stat cache.
 * @param filename [in] Full path of the file to be stat'ed.
 * @param fi [out] Where the S3 File Info pointer should be stored.
 * @return 0 if successful, or \a -errno on failure.
 */
static int
ResolveS3FileStatCacheMiss(
    const char        *filename,
    struct S3FileInfo **fi
			   )
{
    int               status;
    struct S3FileInfo *newFileInfo;

    /* Read the file attributes for the specified file. */
    status = S3FileStatRequest( filename, &newFileInfo );
    if( status == 0 )
    {
        InsertCacheElement( filename, newFileInfo, &DeleteS3FileInfoStructure );
    }
    *fi = newFileInfo;
    return( status );
}



/**
 * Return the S3 File Info for the specified file.
 * @param filename [in] Filename of the file.
 * @param fi [out] Pointer to a pointer to the S3 File Info.
 * @return 0 on success, or \a -errno on failure.
 */
int
S3GetFileStat(
    const char        *filename,
    struct S3FileInfo **fi
	   )
{
    struct S3FileInfo *fileInfo;
    int               status;

    /* Attempt to read the S3FileStat from the stat cache. */
    status = 0;
    fileInfo = SearchStatEntry( filename );
    if( fileInfo == NULL )
    {
        /* Read the file stat from S3. */
        status = ResolveS3FileStatCacheMiss( filename, &fileInfo );

        /* Add the file stat to the cache. */
        if( status != 0 )
        {
	    InsertCacheElement( filename, fileInfo,
				&DeleteS3FileInfoStructure );
	    *fi = fileInfo;
	}
    }
    if( fileInfo == NULL )
    {
        status = -ENOENT;
    }

    return( status );
}









int S3ReadDir( struct S3FileInfo *fi, const char *dir,
	       char **nameArray[ ], int *nFiles )
{
    /* Stub */
    return 0;
}

int S3ReadFile( const char *path, char *buf, size_t size, off_t offset )
{
    /* Stub */
    return 0;
}

int S3FlushBuffers( const char *path )
{
    /* Stub */
    return 0;
}

int S3FileClose( const char *path )
{
    /* Stub */
    return 0;
}


