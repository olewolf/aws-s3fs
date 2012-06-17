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
 * @param bucket [in] Bucket name.
 * @return Header list.
 */
static struct curl_slist*
BuildGenericHeader(
    const char *bucket
		    )
{
    struct curl_slist *headers       = NULL;
    char              headbuf[ 256 ];
    time_t            now            = time( NULL );
    const struct tm   *tnow          = gmtime( &now );
    char              *locale;

    /* Add user agent. */
    headers = curl_slist_append( headers, "User-Agent: curl" );

    /* Make Host: address a virtual host. */
    sprintf( headbuf, "Host: %s.s3.amazonaws.com", bucket );
    headers = curl_slist_append( headers, headbuf );

    /* Generate time string. */
    locale = setlocale( LC_TIME, "C" );
    strftime( headbuf, sizeof( headbuf ), "Date: %a, %d %b %Y %T %z", tnow );
    setlocale( LC_TIME, locale );
    headers = curl_slist_append( headers, headbuf );

    return( headers );
}



/**
 * Extract the \a value part from a header string formed as \a key:value.
 * @param headerString [in] Header string with key: value string.
 * @return String with the \a value content.
 */
static char*
GetHeaderStringValue( const char *headerString )
{
    char *value = NULL;
    int  idx    = 0;
    char ch;

    /* Skip past the key to where the value is. */
    while( ( ch = headerString[ idx ] ) != '\0' )
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
	idx++;
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
static int
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
    /* Add a trailing LF. */
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
void
CreateAwsSignature(
    const char        *httpMethod,	   
    struct curl_slist *headers,
    const char        *path,
    const char        *keyId,
    const char        *secretKey
		   )
{
    char              messageToSign[ 1024 ];
    char              awsHeader[ 100 ];
    int               messageLength;
    const char        *signature;

    struct curl_slist *currentHeader;
    char              ch;
    char              *headerString;
    int               idx;
    char              *urlPath;
    char              *contentMd5;
    char              *contentType;
    char              *dateString;

    /* Build HTTP method, content MD5 placeholder, content type, and time. */
    if( httpMethod == NULL )
    {
        httpMethod = "";
    }
    sprintf( messageToSign, "%s\n", httpMethod );
    messageLength = strlen( messageToSign );

    /* Go through all headers and extract the Content-MD5, Content-type,
       Date, and x-amz-* headers, and add them to the message. */
    currentHeader = headers;
    while( currentHeader != NULL )
    {
        headerString = currentHeader->data;

	/* Get x-amz-* headers. */
        if( strncasecmp( headerString, "x-amz-", strlen( "x-amz-" ) ) == 0 )
	{
	    /* Convert the x-amz- header to lower case. */
	    idx = 0;
	    while( ( ch = tolower( headerString[ idx ] ) != ':' )
		   && ( ch != '\0' ) )
	    {
	        headerString[ idx++ ] = ch;
	    }
	    strcat( messageToSign, headerString );
	    strcat( messageToSign, "\n" );
	    messageLength += strlen( headerString ) + strlen( "\n" );
	}
	/* Get Content-MD5 header. */
	else if( strncasecmp( headerString,
			      "Content-MD5", strlen( "Content-MD5" ) ) == 0 )
	{
	    contentMd5 = GetHeaderStringValue( headerString );
	    messageLength += AddHeaderValueToSignString( messageToSign,
							 contentMd5 );
	}
	/* Get Content-Type header. */
	else if( strncasecmp( headerString,
			      "Content-Type", strlen( "Content-Type" ) ) == 0 )
	{
	    contentType = GetHeaderStringValue( headerString );
	    messageLength += AddHeaderValueToSignString( messageToSign,
							 contentType );
	}
	/* Get Date header. */
	else if( strncasecmp( headerString,
			      "Date", strlen( "Date" ) ) == 0 )
	{
	    dateString = GetHeaderStringValue( headerString );
	    messageLength += AddHeaderValueToSignString( messageToSign,
							 dateString );
	}

	assert( messageLength < sizeof( messageToSign ) - 200 );
	currentHeader = currentHeader->next;
    }

    /* Add the path. */
    /* Path is already assumed to be in UTF-8. Is this safe? */
    /*/urlPath = utf8Encode( path );*/
    urlPath = strdup( path );
    strcat( messageToSign, urlPath );
    strcat( messageToSign, "\n" );
    messageLength += strlen( urlPath ) + sizeof( "\n" );
    /* Sign the message and add the Authorization header. */
    signature = HMAC( (const unsigned char*) messageToSign,
		      strlen( messageToSign ) /* + sizeof( char ) for '\0'? */,
		      (const char*) secretKey,
		      HASH_SHA1, HASHENC_BASE64 );
    sprintf( awsHeader, "\nAuthorization: AWS %s:%s\n", keyId, signature );
    headers = curl_slist_append( headers, awsHeader );
}



void
MakeS3Request(
    const char        *bucket,
    struct curl_slist *additionalHeaders
	      )
{
    struct curl_slist *allHeaders;
    struct curl_slist *currentHeader;

    /* Build basic headers. */
    allHeaders = BuildGenericHeader( bucket );
    /* Add any additional headers, if any. */
    currentHeader = additionalHeaders;
    while( currentHeader != NULL )
    {
	allHeaders = curl_slist_append( allHeaders, "User-Agent: curl" );
        currentHeader = currentHeader->next;
    }


}



/**
 * Retrieve information on a specific file in an S3 path.
 * @param filename [in] Full path of the file, relative to the bucket.
 * @return S3 FileInfo structure.
 */
int
S3FileStatRequest(
    const char        *filename,
    struct S3FileInfo **fileInfo
		  )
{
    struct curl_slist *headers = NULL;
    const char        *bucket;
    struct S3FileInfo *newFileInfo;
    int               status = 0;

    /* Add specific file information request headers. */
    /* (None required.) */

    /* Setup generic S3 REST headers. */
    bucket = globalConfig.bucketName;
    MakeS3Request( bucket, headers );

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


