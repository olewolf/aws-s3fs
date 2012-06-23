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
#include <unistd.h>
#include <ctype.h>
#include <stdbool.h>
#include <malloc.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <curl/curl.h>
#include <time.h>
#include <locale.h>
#include <pthread.h>
#include <libxml/parser.h>
#include "aws-s3fs.h"
#include "s3if.h"
#include "digest.h"
#include "statcache.h"


/* The REST interface does not allow the creation of directories. Instead,
   specify a bogus file within the directory that is never listed by the
   readdir function. When stat on the directory fails, check whether this
   file exists in the directory. */
#define IS_S3_DIRECTORY_FILE "/.----s3--dir--do-not-delete"

/* Number of concurrent CURL threads. */
/*
#define CURL_THREADS 5
*/

#ifdef AUTOTEST
#define STATIC
#else
#define STATIC static
#endif


/*
static struct
{
    CURL *curl;
    union
    {
        unsigned char *read;
        unsigned char *write;
    } buffer;
} curlThreads[ CURL_THREADS ];
*/

pthread_mutex_t curl_mutex = PTHREAD_MUTEX_INITIALIZER;
static CURL *curl = NULL;
/* Buffers for the read/write CURL callback functions. The buffer lengths
   indicate either the number of body data bytes, or the number of headers. */
static void   *curlWriteBuffer;
static size_t curlWriteBufferLength;
static bool   curlWriteBufferIsHeaders;
static struct CurlReadBuffer
{
    unsigned char *data;
    size_t        size;
    off_t         offset;
} curlReadBuffer;

static long int localTimezone;


struct HttpHeaders
{
    const char *headerName;
    char       *content;
};



/*
static void DumpStat( struct S3FileInfo *fi )
{
    if( fi == NULL ) return;
    printf( "  uid = %d, gid = %d\n", fi->uid, fi->gid );
    printf( "  perms = %o, type = %c\n", fi->permissions, fi->fileType );
    printf( "  mtime = %ld\n", (long int)fi->mtime );
}
*/



static void
DeleteCurlSlistAndContents(
    struct curl_slist *toDelete
			   )
{
    if( toDelete != NULL )
    {
	DeleteCurlSlistAndContents( toDelete->next );
	free( toDelete->data );
	free( toDelete );
    }
}



/**
 * Callback function for CURL write where header data is expected. The function
 * adds headers to an expanding array organized as {header-name, header-value}
 * pairs.
 * @param ptr [in] Source of the data from CURL.
 * @param size [in] The size of each data block.
 * @param nmemb [in] Number of data blocks.
 * @param userdata [in] Unused.
 * @return Number of bytes copied from \a ptr.
 */
/* Disable warning that userdata is unused. */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
static size_t
CurlWriteHeader(
    char   *ptr,
    size_t size,
    size_t nmemb,
    void   *userdata
		)
{
    int    i;
    int    bufIdx;
    int    dataEndIdx;
    size_t toCopy = size * nmemb;
    size_t newSize;
    char   *header;
    char   *data = NULL;
    bool   hasData = true;

    /* Allocate room for 10 headers at a time. */
    static const int HEADER_ALLOC_AMOUNT = 10;

    /* Skip all non-alphanumeric characters. */
    while( ! isalnum( *ptr ) )
    {
        ptr++;
    }

    /* Extract header key, which is everything up to ':', unless the header
       key takes up the entire line. For example, "HTTP/1.1 200 OK" is
       returned without value. */
    for( i = 0;
	 ( i < (int) toCopy ) && ( ptr[ i ] != ':' ) &&
	     ( ptr[ i ] != '\n' ) && ( ptr[ i ] != '\r' );
	 i++ );
    header = malloc( ( i + 1 ) * sizeof( char ) );
    memcpy( header, ptr, i );
    header[ i ] = '\0';
    /* A newline indicates the end of the header, without data. If a
       newline is encountered, rewind the pointer and indicate that only
       the header key is available in this header. */
    if( ( ptr[ i ] == '\n' ) || ( ptr[ i ] == '\r' ) )
    {
        hasData = false;
    }

    /* Extract the value for this header key provided there is one. */
    if( ( i != (int) toCopy ) && hasData )
    {
        /* Extract data. Skip ':[[:space:]]*' */
      while( i < (int) toCopy )
	{
	    if( ( ptr[ i ] == ':' ) || isspace( ptr[ i ] ) )
	    {
	        i++;
	    }
	    else
	    {
		break;
	    }
	}
      if( i != (int) toCopy )
	{
	    /* Extract header value. If the header value ends with a newline,
	       terminate it prematurely. */
	    dataEndIdx = i;
	    while( dataEndIdx < (int) toCopy )
	    {
	        if( ( ptr[ dataEndIdx ] == '\0' ) ||
		    ( ptr[ dataEndIdx ] == '\n' ) )
	        {
		    break;
		}
		dataEndIdx++;
	    }
	    data = malloc( ( dataEndIdx - i + 1 ) * sizeof( char ) );
	    memcpy( data, &ptr[ i ], dataEndIdx - i );
	    data[ dataEndIdx - i ] = '\0';
	}
    }

    /* Ignore the header and data if it's an empty line. */
    if( ( strlen( header ) != 0 ) || ( data != NULL ) )
    {
        /* Allocate or expand the buffer to receive the new data. */
        if( curlWriteBuffer == NULL )
	{
	    curlWriteBufferLength = 0;
	    curlWriteBuffer = malloc( HEADER_ALLOC_AMOUNT *
				      2 * sizeof( char* ) );
	}
	else
        {
	    /* Increase pair count. */
	    curlWriteBufferLength = curlWriteBufferLength + 1;
	    /* Expand the buffer by HEADER_ALLOC_AMOUNT items if necessary. */
	    if( ( curlWriteBufferLength % HEADER_ALLOC_AMOUNT ) == 0 )
	    {
		newSize = ( curlWriteBufferLength + 1 ) * HEADER_ALLOC_AMOUNT;
		curlWriteBuffer = realloc( curlWriteBuffer,
					   newSize * 2 * sizeof( char* ) );
		bufIdx = curlWriteBufferLength * 2;
		for( i = 0; i < HEADER_ALLOC_AMOUNT; i++ )
		{
		    ( (char**)curlWriteBuffer )[ bufIdx + i * 2 ] = NULL;
		}
	    }
	}

	/* Write the key and the value into the buffer. */
	bufIdx = curlWriteBufferLength * 2;
	( (char**)curlWriteBuffer )[ bufIdx     ] = header;
	( (char**)curlWriteBuffer )[ bufIdx + 1 ] = data;
    }

    return( toCopy );
}
#pragma GCC diagnostic pop



/**
 * Callback function for CURL read where body data is expected. The function
 * may be called several times, transferring a chunk of data each time. The
 * function is called from an already mutex-locked CURL function.
 * @param ptr [out] Destination for the data to CURL.
 * @param size [in] The allowed size of each data block.
 * @param nmemb [in] The allowed number of data blocks.
 * @param userdata [in] Pointer to the curlReadBuffer structure.
 * @return Number of bytes copied to \a ptr.
 */
static size_t
CurlReadData(
    char   *ptr,
    size_t size,
    size_t nmemb,
    void   *userdata
	     )
{
    struct CurlReadBuffer *curlReadBuffer = userdata;
    int maxWrite    = (int) ( size * nmemb );
    int bytesCopied = 0;
    int accumulatedTotal;

    /* Copy a block of data to the CURL buffer. */
    accumulatedTotal = (int) curlReadBuffer->offset;
    while( ( bytesCopied <= maxWrite )
	   && ( accumulatedTotal < (int) curlReadBuffer->size ) )
    {
        ptr[ bytesCopied++ ] = curlReadBuffer->data[ accumulatedTotal++ ];
    }
    curlReadBuffer->offset = accumulatedTotal;

    return( bytesCopied );
}



/**
 * Callback function for CURL write where body data is expected. The function
 * keeps track of a dynamically expanding buffer that may be filled by multiple
 * callbacks. The function is called from an already mutex-locked CURL function.
 * @param ptr [in] Source of the data from CURL.
 * @param size [in] The size of each data block.
 * @param nmemb [in] Number of data blocks.
 * @param userdata [in] Unused.
 * @return Number of bytes copied from \a ptr.
 */
/* Disable warning that userdata is unused. */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
static size_t
CurlWriteData(
    char   *ptr,
    size_t size,
    size_t nmemb,
    void   *userdata
	  )
{
    size_t        toCopy = size * nmemb;
    unsigned char *destBuffer;
    int           i;
    size_t        toAllocate;

    /* Allocate or expand the buffer to receive the new data. */
    if( curlWriteBuffer == NULL )
    {
	curlWriteBufferLength = 0;
        curlWriteBuffer = malloc( toCopy + sizeof( char ) );
	destBuffer = curlWriteBuffer;
    }
    else
    {
        toAllocate = curlWriteBufferLength + toCopy + sizeof( char );
        curlWriteBuffer = realloc( curlWriteBuffer, toAllocate );
	destBuffer = &( (unsigned char*)curlWriteBuffer )[ curlWriteBufferLength ];
    }

    /* Receive data. */
    for( i = 0; i < (int) toCopy; i++ )
    {
	*destBuffer++ = *ptr++;
    }
    *destBuffer = '\0';
    curlWriteBufferLength = curlWriteBufferLength + toCopy + 1;

    return( toCopy );
}
#pragma GCC diagnostic pop



/**
 * Delete all entries in a headers array, as well as the array itself, which
 * were filled by the CURL callback function.
 * @param table [in/out] Pointer to the headers array.
 * @param nEntries [in] Number of headers in the array.
 * @return Nothing.
 */
STATIC void
DeleteHeaderPairsTable(
    char** table,
    int    nEntries
    		       )
{
    int i;

    if( table != NULL )
    {
        for( i = 0; i < nEntries * 2; i++ )
	{
	    if( table[ i ] != NULL )
	    {
		free( table[ i ] );
	    }
	}
	free( table );
    }
}



/**
 * Deallocate the write-back buffers if they are non-empty and prepare the
 * buffers to be filled with either headers or body data.
 * @param prepareForHeaders [in] If \a true, indicate that the buffer will
 *        be filled with headers; if \a false, the buffer will be filled with
 *        body data.
 * @return Nothing.
 */
STATIC void
PrepareCurlWriteBuffers(
    bool prepareForHeaders
		)
{
    if( curlWriteBufferLength != 0 )
    {
        /* If the write buffer contains pointers to headers, then the headers
	   must each be deleted before the write buffer is deleted. */
        if( curlWriteBufferIsHeaders )
	{
	    DeleteHeaderPairsTable( curlWriteBuffer, curlWriteBufferLength );
	}
	else
	{
	    free( curlWriteBuffer );
	}
    }

    /* Indicate the type of the next fill. */
    curlWriteBufferIsHeaders = prepareForHeaders;
    curlWriteBuffer          = NULL;
}



/**
 * Prepare the read buffer with information about the pending upload.
 * @param data [in] Data to be uploaded.
 * @param size [in] Size of the data.
 * @return Nothing.
 */
STATIC void
PrepareCurlReadBuffer(
    unsigned char *data,
    size_t        size
		       )
{
    curlReadBuffer.data   = data;
    curlReadBuffer.size   = size;
    curlReadBuffer.offset = 0l;
}



/**
 * Initialize the S3 Interface module.
 * @return Nothing.
 */
void
InitializeS3If(
    void
	       )
{
    time_t    tnow;
    struct tm tm;

    /* Initialize libxml. */
    LIBXML_TEST_VERSION

    pthread_mutex_lock( &curl_mutex );
    if( curl == NULL )
    {
	curl = curl_easy_init( );
	curlWriteBuffer = NULL;
	/*	curlReadBuffer  = NULL;*/
    }
    pthread_mutex_unlock( &curl_mutex );

    /* Determine local timezone. */
    tnow = time( NULL );
    localtime_r( &tnow, &tm );
    localTimezone = tm.tm_gmtoff;

    curlWriteBufferLength = 0;
}



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
    struct S3FileInfo *fi = toDelete;

    if( fi != NULL )
    {
        if( fi->symlinkTarget != NULL )
	{
	    free( fi->symlinkTarget );
	}
        free( fi );
    }
}



/**
 * Return a string with the S3 host name corresponding to the region where the
 * specified bucket is located.
 * @param region [in] The region of the bucket.
 * @param bucket [in] Name of the bucket, which is used to create a virtual
 *        host name.
 * @return An allocated buffer with the S3 host name.
 */
STATIC char*
GetS3HostNameByRegion(
    enum bucketRegions region,
    const char         *bucket
		      )
{
    /* Amazon host names for various regions, ordered according to the
       enumeration "bucketRegions". */
    static const char *amazonHost[ ] =
    {
        "s3",                /* US_STANDARD */
	"s3-us-west-2",      /* OREGON */
	"s3-us-west-1",      /* NORTHERN_CALIFORNIA */
	"s3-eu-west-1",      /* IRELAND */
	"s3-ap-southeast-1", /* SINGAPORE */
	"s3-ap-northeast-1", /* TOKYO */
	"s3-sa-east-1"       /* SAO_PAULO */
    };
    char *hostname;
    int  toAlloc;

    assert( region <= SAO_PAULO );

    toAlloc = strlen( "s3-ap-southeast-1" ) +
              strlen( "..amazonaws.com" ) +
              strlen( bucket ) +
              sizeof( char );
    hostname = malloc( toAlloc );
    assert( hostname != NULL );

    if( globalConfig.region != US_STANDARD )
    {
        sprintf( hostname, "%s.%s.amazonaws.com",
		 bucket, amazonHost[ region ] );
    }
    else
    {
        /* US Standard does not have a virtual host name. */
        sprintf( hostname, "s3.amazonaws.com" );
    }
    return( hostname );
}



/**
 * Create a generic S3 request header which is used in all requests.
 * @param httpMethod [in] HTTP verb (GET, HEAD, etc.) for the request.
 * @return Header list.
 */
STATIC struct curl_slist*
BuildGenericHeader(
    void
		    )
{
    struct curl_slist *headers = NULL;
    char              headbuf[ 512 ];
    char              *hostName;
    time_t            now;
    struct tm         tnow;
    char              *locale;

    char *host;
    char *date;
    char *userAgent;

    /* Make Host: address a virtual host. */
    if( globalConfig.region != US_STANDARD )
    {
        hostName = GetS3HostNameByRegion( globalConfig.region,
					  globalConfig.bucketName );
        sprintf( headbuf, "Host: %s", hostName );
	free( hostName );
    }
    else
    {
        /* US Standard does not have a virtual host name. */
        sprintf( headbuf, "Host: s3.amazonaws.com" );
    }
    host = strdup( headbuf );
    headers = curl_slist_append( headers, host );

    /* Generate time string. */
    locale = setlocale( LC_TIME, "en_US.UTF-8" );
    now    = time( NULL );
    localtime_r( &now, &tnow );
    strftime( headbuf, sizeof( headbuf ), "Date: %a, %d %b %Y %T %z", &tnow );
    setlocale( LC_TIME, locale );
    date = strdup( headbuf );
    headers = curl_slist_append( headers, date );

    /* Add user agent. */
    userAgent = strdup( "User-Agent: curl" );
    headers = curl_slist_append( headers, userAgent );

    return( headers );
}



/**
 * Extract the \a value part from a header string formed as \a key:value.
 * @param headerString [in] Header string with key: value string.
 * @return String with the \a value content.
 */
STATIC char*
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
STATIC int
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
STATIC struct curl_slist*
CreateAwsSignature(
    const char        *httpMethod,	   
    struct curl_slist *headers,
    const char        *path
		   )
{
    char              messageToSign[ 4096 ];
    char              amzHeaders[ 2048 ];
    char              *awsHeader;
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
	    /* Convert the x-amz-* header to lower case. */
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

	assert( messageLength < (int) sizeof( messageToSign ) - 200 );
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
    awsHeader = malloc( 100 );
    sprintf( awsHeader, "Authorization: AWS %s:%s",
	     globalConfig.keyId, signature );
    free( (char*) signature );
    headers = curl_slist_append( headers, strdup( awsHeader ) );
    return headers;
}



/**
 * Helper function for the qsort function in \a BuildS3Request.
 * @param a [in] First string in the qsort comparison.
 * @param b [in] Second string in the qsort comparison.
 * @return 0 if \a a = \a b, 1 if \a a > \a b, or -1 if \a a < \a b.
 */
static int
qsort_strcmp( const void *a, const void *b )
{
    return( strcasecmp( *((const char**) a ), *((const char**) b ) ) );
}



/**
 * Create an S3 request that is ready to be submitted via CURL.
 * @param httpMethod [in] The HTTP verb for the request type.
 * @param additionalHeaders[ in ] Any additional HTTP headers that should be
 *        included in the request.
 * @param filename [in] The full path of the file that is accessed.
 * @return Complete HTTP header list, including an AWS signature.
 */
STATIC struct curl_slist*
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
    allHeaders = BuildGenericHeader( );

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
    qsort( extraHeaders, count, sizeof( extraHeaders[ 0 ] ), qsort_strcmp );
    /* Add the headers to the main header list and get rid of the extras
       list. */
    for( i = 0; i < count; i++ )
    {
        allHeaders = curl_slist_append( allHeaders, extraHeaders[ i ] );
    }
    /* Free the list and its elements, but not the headers themselves. */
    curl_slist_free_all( additionalHeaders );

    /* Add a signature header. */
    allHeaders = CreateAwsSignature( httpMethod, allHeaders, filename );

    return( allHeaders );
}



/**
 * Convert an HTTP response code to a meaningful filesystem error number for
 * FUSE.
 * @param httpStatus [in] HTTP response code.
 * @return \a -errno for the HTTP response code.
 */
STATIC int
ConvertHttpStatusToErrno(
    int httpStatus
			)
{
    int status;

    if( httpStatus <= 299 )
    {
	status = 0;
    }
    /* Redirection: consider it a "not found" error. */
    else if( ( 300 <= httpStatus ) && ( httpStatus <= 399 ) )
    {
	status = -ENOENT;
    }
    /* File not found. */
    else if( ( 400 < httpStatus ) && ( httpStatus <= 499 ) )
    {
        if( ( httpStatus == 404 ) || ( httpStatus == 410 ) )
	{
	    status = -ENOENT;
	}
	/* Bad request: bad message. */
	else if( ( httpStatus == 401 ) || ( httpStatus == 403 ) ||
		 ( httpStatus == 402 ) ||
		 ( httpStatus == 407 ) || ( httpStatus == 408 ) )
	{
	    status = -EACCES;
	}
	else if( ( httpStatus == 400 ) || ( httpStatus == 405 ) ||
		 ( httpStatus == 406 ) )
	{
	    status = -EBADMSG;
	}
	else if( httpStatus == 409 )
	{
	    status = -EINPROGRESS;
	}
	else
	{
	    status = -EIO;
	}
    }
    else if( ( 500 <= httpStatus ) && ( httpStatus <= 599 ) )
    {
        if( httpStatus == 500 )
	{
	    status = -ENETRESET;
	}
        else if( ( httpStatus == 501 ) || ( httpStatus == 505 ) )
	{
	    status = -ENOTSUP;
	}
	else if( ( httpStatus == 502 ) || ( httpStatus == 503 ) )
	{
	    status = -ENETUNREACH;
	}
	else if ( httpStatus == 504 )
	{
	    status = -ETIMEDOUT;
	}
	else
	{
	    status = -EIO;
	}
    }
    else
    {
	status = -EIO;
    }

    return( status );
}



#if 0
/**
 * Extract the HTTP status code from the first string in the response header,
 * and convert it to a meaningful errno.
 * @param headers [in] Array of {key,value} headers.
 * @return 0 if no error was encountered, or \a -errno if there was a HTTP
 *         error code.
 */
STATIC int
GetHttpStatus(
    const char **headers
	      )
{
    int        httpStatus = 404;
    const char *httpReply;
    char       ch;
    int        i;

    /* Assume that the first entry in the buffer contains the HTTP status
       reply. */
    httpReply = headers[ 0 ];
    if( httpReply != NULL )
    {
        /* Find the HTTP status code. */
        i = strlen( "HTTP/1.1 " );
	if( strncmp( httpReply, "HTTP/1.1 ", i ) == 0 )
	{
	    while( ( ( ch = httpReply[ i ] ) != '\0' ) && ( ! isdigit( ch ) ) )
	    {
		i++;
	    }
	    httpStatus = atoi( &httpReply[ i ] );
	}
    }

    return( httpStatus );
}
#endif



/**
 * Submit a sequence of headers containing an S3 request and receive the
 * output in the local write buffer. The headers list is deallocated.
 * The request may include PUT requests, as long as there is no body data
 * to put.
 * @param httpVerb [in] HTTP method (GET, HEAD, etc.).
 * @param headers [in/out] The CURL list of headers with the S3 request.
 * @param filename [in] Full path name of the file that is accessed.
 * @param response [out] Pointer to the response data.
 * @param responseLength [out] Pointer to the response length.
 * @return 0 on success, or CURL error number on failure.
 */
STATIC int
SubmitS3Request(
    const char        *httpVerb,
    struct curl_slist *headers,
    const char        *filename,
    void              **data,
    int               *dataLength
		)
{
    char       *url;
    char       *hostName;
    int        urlLength;
    int        status = 0;
    long       httpStatus;

    printf( "s3if: SubmitS3Request (%s)\n", filename );

    /* Determine the virtual host name. */
    hostName = GetS3HostNameByRegion( globalConfig.region,
				      globalConfig.bucketName );
    /* Determine the length of the URL. */
    urlLength = strlen( hostName )
                + strlen( "https://" )
                + strlen( globalConfig.bucketName ) + sizeof( char )
                + strlen( filename )
                + sizeof( char )
                + sizeof( char );
    /* Build the full URL, adding a '/' to the host if the filename does not
       include it as its leading character. */
    url = malloc( urlLength );
    if( globalConfig.region != US_STANDARD )
    {
        sprintf( url, "https://%s%s%s", hostName,
		 filename[ 0 ] == '/' ? "" : "/",
		 filename );
    }
    else
    {
        sprintf( url, "https://%s/%s%s%s", hostName,
		 globalConfig.bucketName,
		 filename[ 0 ] == '/' ? "" : "/",
		 filename );
    }
    free( hostName );

    /* Submit request via CURL and wait for the response. */
    pthread_mutex_lock( &curl_mutex );
    curl_easy_reset( curl );
    /* Set callback function according to HTTP method. */
    if( ( strcmp( httpVerb, "HEAD" ) == 0 )
	|| ( strcmp( httpVerb, "DELETE" ) == 0 ) )
    {
        PrepareCurlWriteBuffers( true );
        curl_easy_setopt( curl, CURLOPT_NOBODY, 1 );
        curl_easy_setopt( curl, CURLOPT_HEADERFUNCTION, CurlWriteHeader );
	if( strcmp( httpVerb, "DELETE" ) == 0 )
	{
	    curl_easy_setopt( curl, CURLOPT_CUSTOMREQUEST, "DELETE" );
	}
    }
    else if( strcmp( httpVerb, "GET" ) == 0 )
    {
        PrepareCurlWriteBuffers( false );
        curl_easy_setopt( curl, CURLOPT_WRITEFUNCTION, CurlWriteData );
    }
    else if( strcmp( httpVerb, "PUT" ) == 0 )
    {
        PrepareCurlWriteBuffers( true );
        curl_easy_setopt( curl, CURLOPT_NOBODY, 1 );
        curl_easy_setopt( curl, CURLOPT_HEADERFUNCTION, CurlWriteHeader );
	curl_easy_setopt( curl, CURLOPT_UPLOAD, true );
	curl_easy_setopt( curl, CURLOPT_INFILESIZE, 0 );
    }

    curl_easy_setopt( curl, CURLOPT_HTTPHEADER, headers );
    curl_easy_setopt( curl, CURLOPT_URL, url );
    /*
      curl_easy_setopt( curl, CURLOPT_VERBOSE, 1 );
    */
    status = curl_easy_perform( curl );
    /* Move the response to a safe place and prepare the CURL write
       buffer for new data. */
    *data                 = curlWriteBuffer;
    *dataLength           = curlWriteBufferLength;
    curlWriteBuffer       = NULL;
    curlWriteBufferLength = 0;
    curl_easy_getinfo( curl, CURLINFO_RESPONSE_CODE, &httpStatus );
    pthread_mutex_unlock( &curl_mutex );

    free( url );
    DeleteCurlSlistAndContents( headers );

    /* Report errors back if necessary. */
    if( status == 0 )
    {
        status = ConvertHttpStatusToErrno( httpStatus );
    }
    else
    {
        status = -EIO;
	return( status );
    }

    return( status );
}



/**
 * Submit a sequence of headers containing an S3 request and receive the
 * output in the local write buffer. The headers list is deallocated.
 * The request includes a body buffer for upload data; if none should be
 * uploaded, \a SubmitS3Request may be used instead. For large data,
 * use the multi-uploader.
 * @param httpVerb [in] HTTP method (GET, HEAD, etc.).
 * @param headers [in/out] The CURL list of headers with the S3 request.
 * @param filename [in] Full path name of the file that is accessed.
 * @param response [out] Pointer to the response data.
 * @param responseLength [out] Pointer to the response length.
 * @param bodyData [in] Pointer to the upload data.
 * @param bodyLength [in] Size of the upload data.
 * @return 0 on success, or CURL error number on failure.
 */
STATIC int
SubmitS3PutRequest(
    struct curl_slist *headers,
    const char        *filename,
    void              **response,
    int               *responseLength,
    unsigned char     *bodyData,
    size_t            bodyLength
		)
{
    char       *url;
    char       *hostName;
    int        urlLength;
    int        status     = 0;
    long       httpStatus;

    /* Determine the virtual host name. */
    hostName = GetS3HostNameByRegion( globalConfig.region,
				      globalConfig.bucketName );
    /* Determine the length of the URL. */
    urlLength = strlen( hostName )
                + strlen( "https://" )
                + strlen( globalConfig.bucketName ) + sizeof( char )
                + strlen( filename )
                + sizeof( char )
                + sizeof( char );
    /* Build the full URL, adding a '/' to the host if the filename does not
       include it as its leading character. */
    url = malloc( urlLength );
    if( globalConfig.region != US_STANDARD )
    {
        sprintf( url, "https://%s%s%s", hostName,
		 filename[ 0 ] == '/' ? "" : "/",
		 filename );
    }
    else
    {
        sprintf( url, "https://%s/%s%s%s", hostName,
		 globalConfig.bucketName,
		 filename[ 0 ] == '/' ? "" : "/",
		 filename );
    }
    free( hostName );

    /* Submit request via CURL and wait for the response. */
    pthread_mutex_lock( &curl_mutex );
    curl_easy_reset( curl );
    /* Prepare read buffer and no write buffer. */
    PrepareCurlReadBuffer( (unsigned char*) bodyData, bodyLength );
    curl_easy_setopt( curl, CURLOPT_READFUNCTION, CurlReadData );
    curl_easy_setopt( curl, CURLOPT_READDATA, &curlReadBuffer );
    curl_easy_setopt( curl, CURLOPT_UPLOAD, true );
    curl_easy_setopt( curl, CURLOPT_HTTPHEADER, headers );
    curl_easy_setopt( curl, CURLOPT_URL, url );
    status = curl_easy_perform( curl );
    /* Move the response to a safe place and prepare the CURL write
       buffer for new data. */
    *response             = NULL;
    *responseLength       = 0;
    curlWriteBuffer       = NULL;
    curlWriteBufferLength = 0;
    curl_easy_getinfo( curl, CURLINFO_RESPONSE_CODE, &httpStatus );
    pthread_mutex_unlock( &curl_mutex );

    free( url );
    DeleteCurlSlistAndContents( headers );

    /* Report errors back if necessary. */
    if( status == 0 )
    {
        status = ConvertHttpStatusToErrno( httpStatus );
    }
    else
    {
        status = -EIO;
	return( status );
    }

    return( status );
}






#if 0
static int
AllocateCurlBuffer( 
    unsigned char **buffer,
    int           maxBuffers
		    )
{
    int allocatedBuffer;

    do
    {
        allocatedBuffer = 0;
        while( ( allocatedBuffer < maxBuffers )
	       && ( writeBuffer[ allocated ] != NULL ) )
	{
	    allocated++;
	}

	/* Wait for a thread to end before attempting another allocation. */
	if( allocated == maxBuffers )
	{
	}

    } while( allocated == maxBuffers );

}
#endif



/**
 * Convert a string from a header to an integer value.
 * @param string [in] Header value (string).
 * @param value [out] Integer value.
 * @return 0 on success, or \a -errno on failure.
 */
STATIC long long
GetHeaderInt(
    const char *string,
    long long  *value
	     )
{
    int success = 0;

    /* Convert the value. Possible errors are EILSEQ or ERANGE. */
    if( sscanf( string, "%Ld", value ) == EOF )
    {
	success = -errno;
    }
    return( success );
}



/**
 * Convert a string from a header to a time_t value.
 * @param string [in] Header value (string).
 * @param value [out] time_t value.
 * @return 0 on success, or \a -errno on failure.
 */
STATIC time_t
GetHeaderTime(
    const char *string,
    time_t     *value
	     )
{
    struct tm tm;
    int       success = 0;

    int       idx = 0;
    int       strBegin;
    int       i;
    char      ch;
    char      chNext;
    int       val;
    const char *months[ ] = { "jan", "feb", "mar", "apr", "may", "jun",
			      "jul", "aug", "sep", "oct", "nov", "dec" };

    memset( &tm, 0, sizeof( struct tm ) );
    *value  = 0l;

    /* Convert the value. Possible errors are EILSEQ or ERANGE. */
    /* Date sample: Tue, 19 Jun 2012 10:04:06 GMT */

#define DECODE_TIME_FIND_ALPHA( src, index, character ) \
    while( ( ( (character) = (src)[index] ) != '\0' )   \
	   && ( ! isalpha( character ) ) )              \
        (index)++;                                      \
    if( character == '\0' ) return -EILSEQ

#define DECODE_TIME_FIND_DIGIT( src, index, character ) \
    while( ( ( (character) = (src)[index] ) != '\0' )   \
	   && ( ! isdigit( character ) ) )              \
        (index)++;                                      \
    if( character == '\0' ) return -EILSEQ

#define DECODE_TIME_GET_DOUBLEDIGIT( string, idx, value, max )	\
    ch = ((string)[ idx ]);                             \
    chNext = ((string)[ (idx) + 1 ]);                   \
    if( ( ! isdigit( ch ) ) || ( ! isdigit( chNext ) ) ) return( -EILSEQ ); \
    (value) = ch - '0';					\
    (value) = (value) * 10 + chNext - '0';		\
    if( (value) >= (max) ) return( -ERANGE )

    /* Decode day of month. */
    DECODE_TIME_FIND_DIGIT( string, idx, ch );
    /* ch is 1 through 9 */
    val = ch - '0';
    chNext = string[ idx + 1 ];
    if( isdigit( chNext ) )
    {
        val = val * 10 + chNext - '0';
    }
    tm.tm_mday = val;

    /* Decode month. */
    DECODE_TIME_FIND_ALPHA( string, idx, ch );
    strBegin = idx;
    while( ( ( ch = tolower( string[ idx ] ) ) != '\0' ) && isalpha( ch ) )
    {
        idx++;
    }
    if( idx - strBegin < 3 )
    {
        return( -EILSEQ );
    }
    for( val = 0; val < 12; val++ )
    {
        if( strncasecmp( months[ val ], &string[ strBegin ], 3 ) == 0 ) break;
    }
    if( val >= 12 )
    {
        return( -ERANGE );
    }
    tm.tm_mon = val;

    /* Decode year. */
    DECODE_TIME_FIND_DIGIT( string, idx, ch );
    val = ch - '0';
    for( i = 0; i < 3; i++ )
    {
        if( ! isdigit( ch = string[ ++idx ] ) )
	{
	    return( -EILSEQ );
	}
	val = val * 10 + ch - '0';
    }
    if( val < 1900 )
    {
        return( -ERANGE );
    }
    tm.tm_year = val - 1900;
    idx++;

    /* Decode time. */
    DECODE_TIME_FIND_DIGIT( string, idx, ch );
    DECODE_TIME_GET_DOUBLEDIGIT( string, idx, val, 24 );
    tm.tm_hour = val;
    idx += 3;
    DECODE_TIME_GET_DOUBLEDIGIT( string, idx, val, 60 );
    tm.tm_min  = val;
    idx += 3;
    DECODE_TIME_GET_DOUBLEDIGIT( string, idx, val, 60 );
    tm.tm_sec  = val;
    idx += 2;

    /* Decode timezone. */
    /* Don't bother: Amazon AWS apparently always returns GMT (i.e., UTC)
       regardless of the geographic location of the bucket. */
    tm.tm_gmtoff = 0;
    tm.tm_isdst = -1;

    /* Adjust the reported time for the local time zone. */
    *value = mktime( &tm ) + localTimezone;

    return success;
}



/**
 * Retrieve information on a specific file in an S3 path.
 * @param filename [in] Full path of the file, relative to the bucket.
 * @param fileInfo [out] S3 FileInfo structure.
 * @return 0 on success, or \a -errno on failure.
 */
STATIC int
S3GetFileStat(
    const char        *filename,
    struct S3FileInfo **fileInfo
	      )
{
    struct curl_slist *headers = NULL;
    struct S3FileInfo *newFileInfo = NULL;
    int               status = 0;
    char              **response = NULL;
    int               length     = 0;
    int               headerIdx;
    const char        *headerKey;
    const char        *headerValue;
    int               mode;
    long long int     tempValue;

    /* Create specific file information request headers. */
    /* (None required.) */

    /* Setup S3 headers. */
    headers = BuildS3Request( "HEAD", headers, filename );
    /* Make request via curl and wait for response. */
    status = SubmitS3Request( "HEAD", headers, filename,
			      (void**)&response, &length );
    if( status == 0 )
    {
        /* Prepare an S3 File Info structure. */
        newFileInfo = malloc( sizeof (struct S3FileInfo ) );
	assert( newFileInfo != NULL );
	/* Set default values. */
	memset( newFileInfo, 0, sizeof( struct S3FileInfo ) );
	newFileInfo->filenotfound = false;
	newFileInfo->fileType    = 'f';
	newFileInfo->permissions = 0644;
	/* A trailing slash in the filename indicates that it is a directory. */
	if( filename[ strlen( filename ) - 1 ] == '/' )
	{
	    newFileInfo->fileType    = 'd';
	    newFileInfo->permissions = 0755;
	}
	newFileInfo->uid = getuid( );
	newFileInfo->gid = getgid( );
	newFileInfo->symlinkTarget = NULL;
	/* Translate header values to S3 File Info values. */
	for( headerIdx = 0; headerIdx < length; headerIdx++ )
	{
	    headerKey   = response[ headerIdx * 2     ];
	    headerValue = response[ headerIdx * 2 + 1 ];

	    if( strcmp( headerKey, "x-amz-meta-uid" ) == 0 )
	    {
	        if( ( status = GetHeaderInt( headerValue, &tempValue ) ) != 0 )
		{
		    break;
		}
		newFileInfo->uid = tempValue;
	    }
	    else if( strcmp( headerKey, "x-amz-meta-gid" ) == 0 )
	    {
	        if( ( status = GetHeaderInt( headerValue, &tempValue ) ) != 0 )
		{
		    break;
		}
		newFileInfo->gid = tempValue;
	    }
	    else if( strcmp( headerKey, "x-amz-meta-mode" ) == 0 )
	    {
	        if( ( status = GetHeaderInt( headerValue, &tempValue ) ) != 0 )
		{
		    break;
		}
		mode = tempValue;
		newFileInfo->permissions = mode & 0777;
		newFileInfo->exeUid      = ( mode & S_ISUID ) ? true : false;
		newFileInfo->exeGid      = ( mode & S_ISGID ) ? true : false;
		newFileInfo->sticky      = ( mode & S_ISVTX ) ? true : false;
	    }
	    else if( strcmp( headerKey, "Content-Type" ) == 0 )
	    {
		if( strncmp( headerValue,
			     "application/x-directory", 23 ) == 0 )
		{
		    newFileInfo->fileType = 'd';
		}
		else if( strncmp( headerValue,
				  "application/x-symlink", 21 ) == 0 )
		{
		    newFileInfo->fileType = 'l';
		}
	    }
	    else if( strcmp( headerKey, "Content-Length" ) == 0 )
	    {
	        if( ( status = GetHeaderInt( headerValue, &tempValue ) ) != 0 )
		{
		    break;
		}
		newFileInfo->size = tempValue;
	    }
	    else if( strcmp( headerKey, "x-amz-meta-atime" ) == 0 )
	    {
  	        if( ( status =
		      GetHeaderTime( headerValue, &newFileInfo->atime ) ) != 0 )
		{
		    break;
		}
	    }
	    else if( strcmp( headerKey, "x-amz-meta-ctime" ) == 0 )
	    {
  	        if( ( status =
		      GetHeaderTime( headerValue, &newFileInfo->ctime ) ) != 0 )
		{
		    break;
		}
	    }
	    /* For s3fs compatibility. However, there is already a
	       "Last-Modified" header, which contains this data. Use that
	       instead if possible. */
	    else if( strcmp( headerKey, "x-amz-meta-mtime" ) == 0 )
	    {
	        /* Do not override the Last-Modified header. */
	        if( newFileInfo->mtime != 0l )
		{
		      if( ( status =
			    GetHeaderTime( headerValue, &newFileInfo->mtime ) )
			  != 0 )
		      {
			  break;
		      }
		}
	    }
	    else if( strcmp( headerKey, "Last-Modified" ) == 0 )
	    {
	        /* Last-Modified overrides the x-amz-meta-mtime header. */
	        if( ( status =
		      GetHeaderTime( headerValue, &newFileInfo->mtime ) ) != 0 )
		{
		    break;
		}
	    }
	}
	free( response );
    }
    if( status == 0 )
    {
        *fileInfo = newFileInfo;
    }
    else
    {
        if( newFileInfo != NULL )
	{
 	    free( newFileInfo );
	}
    }
    return( status );
}



/**
 * Read the specified file's attributes from S3 and insert them into
 * the stat cache.
 * @param filename [in] Full path of the file to be stat'ed.
 * @param fi [out] Where the S3 File Info pointer should be stored.
 * @param hashAs [in] Filename to use for hashing by the cache.
 * @return 0 if successful, or \a -errno on failure.
 */
static int
ResolveS3FileStatCacheMiss(
    const char        *filename,
    struct S3FileInfo **fi,
    const char        *hashAs
			   )
{
    int               status;
    struct S3FileInfo *newFileInfo = NULL;

    /* Read the file attributes for the specified file. */
    status = S3GetFileStat( filename, &newFileInfo );
    if( status == 0 )
    {
        InsertCacheElement( hashAs, newFileInfo, &DeleteS3FileInfoStructure );
	*fi = newFileInfo;
    }
    return( status );
}



static char*
StripTrailingSlash(
    char *filename,
    bool makeCopy
		   )
{
    char *dirname;
    int  dirnameLength;

    if( makeCopy )
    {
        if( filename == NULL )
	{
	    dirname = strdup( "" );
	}
	else
	{
	    dirname = strdup( filename );
	}
    }
    else
    {
        dirname = filename;
    }

    /* Strip all trailing slashes. */
    if( dirname != NULL )
    {
	dirnameLength = strlen( dirname );
	while( --dirnameLength != 0 )
	{
	    if( dirname[ dirnameLength ] != '/' )
	    {
		break;
	    }
	    dirname[ dirnameLength ] = '\0';
	}
    }

    return dirname;
}



static char*
AddTrailingSlash(
    const char *filename
		 )
{
    char *dirname;
    int  filenameLength;

    if( filename == NULL )
    {
        filenameLength = 0;
	dirname = malloc( 2 * sizeof( char ) );
	dirname[ 0 ] = '/';
	dirname[ 1 ] = '\0';
    }
    else
    {
	filenameLength = strlen( filename );
	dirname = malloc( filenameLength + 2 * sizeof( char ) );
	strncpy( dirname, filename, filenameLength );
    }
    /* Add trailing slash if necessary. */
    if( ( filenameLength != 0 ) && ( dirname[ filenameLength - 1 ] != '/' ) )
    {
        dirname[ filenameLength     ] = '/';
        dirname[ filenameLength + 1 ] = '\0';
    }
    return dirname;
}



/**
 * Return the S3 File Info for the specified file.
 * @param file [in] Filename of the file.
 * @param fi [out] Pointer to a pointer to the S3 File Info.
 * @return 0 on success, or \a -errno on failure.
 */
int
S3FileStat(
    const char        *file,
    struct S3FileInfo **fi
	   )
{
    int               stripIdx = 0;
    char              *filename;
    int               secretIdx = 0;
    struct S3FileInfo *fileInfo;
    int               status;
    char              *dirname;

    /* Make sure there is exactly one leading slash in the filename. */
    while( file[ stripIdx ] == '/' )
    {
	stripIdx++;
    }
    filename = malloc( strlen( file ) + 2 * sizeof( char ) );
    filename[ 0 ] = '/';
    filename[ 1 ] = '\0';
    strcat( filename, &file[ stripIdx ] );
    /* Remove all trailing slashes. */
    filename = StripTrailingSlash( filename, false );

    /* Prevent access to the "secret" file. */
    secretIdx = strlen( filename ) - strlen( IS_S3_DIRECTORY_FILE );
    if( ( 0 <= secretIdx )
	&& ( strcmp( &filename[ secretIdx ], IS_S3_DIRECTORY_FILE ) == 0 ) )
    {
	free( filename );
        return( -ENOENT );
    }

    /* Attempt to read the S3FileStat from the stat cache. */
    status = 0;
    fileInfo = SearchStatEntry( filename );

    /* If the file info is not available, resolve the cache miss. */
    if( fileInfo == NULL )
    {
        /* Read the file stat from S3. */
        status = ResolveS3FileStatCacheMiss( filename, &fileInfo, filename );
	/* If unsuccessful, attempt the directory name version with a
	   trailing slash. */
	if( status != 0 )
	{
	    /* Prepare a directory name version of the file. */
	    dirname = AddTrailingSlash( filename );
	    status = ResolveS3FileStatCacheMiss( dirname, &fileInfo, filename );
	    free( dirname );
	    /* If that is also unsuccessful, attempt to stat the "secret file"
	       in the directory. */
	    if( status != 0 )
	    {
	        dirname = malloc( strlen( filename ) + sizeof( char )
				  + strlen( IS_S3_DIRECTORY_FILE ) );
		strcpy( dirname, filename );
		strcat( dirname, IS_S3_DIRECTORY_FILE );
		status = ResolveS3FileStatCacheMiss( dirname, &fileInfo,
						     filename );
		free( dirname );
		/* If still yet unsuccessful, create a "file not found" entry
		   in the stat cache. */
		if( status != 0 )
		{
		  fileInfo = malloc( sizeof( struct S3FileInfo ) );
		  fileInfo->symlinkTarget = NULL; /* For later free() */
		  fileInfo->filenotfound  = true;
		  InsertCacheElement( filename, fileInfo,
				      &DeleteS3FileInfoStructure );
		}
	    }
	}
    }
    else
    {
        /* If the file is known to not exist, return an error. */
        if( bool_equal( fileInfo->filenotfound, true ) )
	{
	    status = -ENOENT;
	}
    }

    if( status == 0 )
    {
        *fi = fileInfo;
    }
    if( fileInfo == NULL )
    {
        status = -ENOENT;
    }

    free( filename );
    return( status );
}



STATIC char*
EncodeUrl(
    const char *url
	  )
{
    const char const *hexChar = "0123456789ABCDEF";
    char       *safeUrl;
    const char *urlPtr;
    char       *safeUrlPtr;
    char ch;

    /* Allocate the maximum number of characters that could possibly
       be used for this URL. */
    safeUrl = malloc( 3 * strlen( url ) + sizeof( char ) );
    urlPtr     = url;
    safeUrlPtr = safeUrl;
    while( *urlPtr != '\0' )
    {
        ch = *urlPtr++;

	/* Safe characters. */
        if( isalnum( ch ) || ( ch == '-' ) ||
	    ( ch == '_' ) || ( ch == '.' ) || ( ch == '~' ) )
        {
	    *safeUrlPtr++ = ch;
	}
	/* Space becomes +. */
	else if( ch == ' ' )
	{
	    *safeUrlPtr++ = '+';
	}
	/* Other characters are hex-encoded. */
	else
	{
	    *safeUrlPtr++ = '%';
	    *safeUrlPtr++ = hexChar[ ( ch >> 4 ) & 0x0f ];
	    *safeUrlPtr++ = hexChar[   ch        & 0x0f ];
	}
    }
    *safeUrlPtr = '\0';

    return( safeUrl );
}



/**
 * Recursive function that goes through an XML file and adds the contents of
 * all occurrences of <key> to a linked list. In addition, if the function
 * encounters a <marker> node, it records its contents.
 * @param node [in] Current XML node.
 * @param prefixLength [in] Number of bytes in the prefix which are skipped.
 * @param directory [in/out] Pointer to linked list of directory entries.
 * @param marker [out] Name of the marker, if found.
 * @param nFiles [out] Number of files in the directory.
 * @return Nothing.
 */
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wunused-variable"
static void
ReadXmlDirectory(
    xmlNode           *node,
    int               prefixLength,
    struct curl_slist **directory,
    char              **marker,
    int               *nFiles
		 )
{
    xmlNode    *currentNode;
    char       *direntry;
    const char *nodeName;
    const char *nodeValue;
    char       *newMarker;

    /* Search the XML tree depth first (not that it really matters, because
       the tree is very shallow). */
    for( currentNode = node; currentNode; currentNode = currentNode->next )
    {
        if( currentNode->type == XML_ELEMENT_NODE )
	{
	    /* "Key" and "Prefix" mark file or directory names,
	       respectively. */
	    nodeName =  (char*) currentNode->name;
	    nodeValue = (char*) xmlNodeGetContent( currentNode );
	    if( ( strcmp( nodeName, "Key" ) == 0 ) ||
		( strcmp( nodeName, "Prefix" ) == 0 ) )
	    {
	        /* Strip the prefix from the path and add the directory. */
	        if( strcmp( &nodeValue[ prefixLength ], "" ) != 0 )
		{
		    direntry = malloc( strlen( nodeValue )
				       - prefixLength
				       + 2 * sizeof( char ) );
		    strcpy( direntry, &nodeValue[ prefixLength ] );
		    *directory = curl_slist_append( *directory, direntry );
		    (*nFiles)++;
		}
	    }
	    /* Record marker names that might be encountered. */
	    else if( strcmp( nodeName, "NextMarker" ) == 0 )
	    {
	        if( strlen( nodeValue ) != 0 )
		{	
		    if( *marker != NULL )
		    {
		        free( *marker );
		    }
		    *marker = malloc( strlen( nodeValue ) + sizeof( char ) );
		    strcpy( *marker, nodeValue );
		}
	    }
	    /* If this is the last entry, clear the "marker" data to
	       indicate that the directory chunk loop may end. (IsTruncated
	       is actually redundant, because the presence of "NextMarker"
	       implies that the directory listing was truncated.) */
	    else if( strcmp( nodeName, "IsTruncated" ) == 0 )
	    {
	        if( strcmp( nodeValue, "false" ) == 0 )
		{
		    if( *marker != NULL )
		    {
		        free( *marker );
			*marker = NULL;
		    }
		}
	    }
        }

	ReadXmlDirectory( currentNode->children, prefixLength,
			  directory, marker, nFiles );
    }
}
#pragma GCC diagnostic pop



/* Disable warning that fi is unused. */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
int
S3ReadDir(
    struct S3FileInfo *fi,
    const char        *dirname,
    char              ***nameArray,
    int               *nFiles,
    int               maxRead
	  )
{
    int        status = 0;

    const char *delimiter;
    const char *prefix;
    char       *queryBase;
    const char *urlSafePrefix;
    int        toSkip = 0;
    char       *relativeRoot;

    char              *query;
    char              *fromFile = NULL;
    char              *urlSafeFromFile;
    struct curl_slist *headers = NULL;
    struct curl_slist *directory = NULL;
    int               prefixToSkip;
    char              *xmlData;
    int               xmlDataLength;
    xmlDocPtr         xmlResponse;
    xmlNode           *rootNode;
    int               fileCounter;
    int               fileLimit;

    char              **dirArray;
    int               dirIdx;
    struct curl_slist *nextEntry;
    char              *path;
    int               s3dirfilePos;

    /* Construct a base query with a prefix and a delimiter. */

    /* Skip any leading slashes in the dirname. */
    while( dirname[ toSkip ] == '/' )
    {
        toSkip++;
    }
    /* Create prefix and a delimiter for the S3 list. The prefix is the entire
       path including dirname plus trailing slash, so add a slash to the
       dirname if it is not already specified. The delimiter is a '/'. */
    prefix    = StripTrailingSlash( (char*) &dirname[ toSkip ], true );
    delimiter = "/";
    /* Create the base query. */
    urlSafePrefix = EncodeUrl( prefix );
    relativeRoot = malloc( strlen( globalConfig.bucketName )
			   + strlen( prefix ) + 5 * sizeof( char ) );
    relativeRoot[ 0 ] = '/';
    relativeRoot[ 1 ] = '\0';
    strcpy( relativeRoot, globalConfig.bucketName );
    strcpy( relativeRoot, "/" );
    if( strlen( prefix ) > 0 )
    {
        strcpy( relativeRoot, prefix );
	strcpy( relativeRoot, "/" );
    }
    queryBase = malloc( strlen( prefix )
			+ sizeof( char )
		        + strlen( urlSafePrefix )
			+ strlen( "/?prefix=/&delimiter=" )
			+ strlen( delimiter )
			+ strlen( "&max-keys=xxxxx" )
			+ sizeof( char ) );

    /* Add a non-encoded trailing slash to the prefix in the query.
       Omit the prefix if the root folder was specified. */
    if( strlen( prefix ) == 0 )
    {
        sprintf( queryBase, "%s?delimiter=%s", relativeRoot, delimiter );
    }
    else
    {
        sprintf( queryBase, "%s?prefix=%s/&delimiter=%s",
		 relativeRoot, urlSafePrefix, delimiter );
    }
    if( maxRead != -1 )
    {
        sprintf( &queryBase[ strlen( queryBase ) ], "&max-keys=%d", maxRead );
    }
    free( (char*) urlSafePrefix );

    fileCounter = 0;
    fileLimit   = ( maxRead == -1 ) ? 999999l : maxRead;
    /* Retrieve truncated directory lists by specifying the base query plus a
       marker. */
    do
    {
        /* Get an XML list of directories and decode the directory contents. */
        query = queryBase;
	if( fromFile != NULL )
        {
	    urlSafeFromFile = EncodeUrl( fromFile );
	    query = malloc( strlen( queryBase )
			    + strlen( "&marker=" )
			    + strlen( urlSafeFromFile )
			    + sizeof( char ) );
	    strcpy( query, queryBase );
	    strcat( query, "&marker=" );
	    strcat( query, urlSafeFromFile );
	    free( urlSafeFromFile );
	}
	/* query now contains the path for the S3 request. */
	headers = BuildS3Request( "GET", NULL, relativeRoot );
	status  = SubmitS3Request( "GET", headers, query,
				   (void**) &xmlData, &xmlDataLength );
	if( query != queryBase )
	{
	    free( query );
	}
	if( status == 0 )
	{
 	    /* Decode the XML response. */
	    xmlResponse = xmlReadMemory( xmlData, xmlDataLength,
					 "readdir.xml", NULL, 0 );
	    if( xmlResponse == NULL )
	    {
		status = -EIO;
	    }
	    else
	    {
	        /* Begin a depth-first traversal from the root node. */
	        rootNode = xmlDocGetRootElement( xmlResponse );
		if( rootNode == NULL )
		{
		    status = -EIO;
		}
		else
	        {
		    /* Skip the prefix and slash... */
		    prefixToSkip = strlen( prefix ) + 1;
		    /* ... except at the root folder which has neither. */
		    if( prefixToSkip == 1 )
		    {
		        prefixToSkip = 0;
		    }
		    ReadXmlDirectory( rootNode, prefixToSkip,
				      &directory, &fromFile, &fileCounter );
		}
		/*
		xmlCleanupParser( );
		*/
		xmlFreeDoc( xmlResponse );
	    }
	}
    } while( ( fromFile != NULL ) && ( fileCounter <= fileLimit ) );

    free( relativeRoot );
    free( (char*) prefix );

    /* Move the linked-list file names into an array. Add two entries for
       the directories "." and "..". */
    fileCounter += 2;
    dirArray = malloc( sizeof( char* ) * fileCounter );
    assert( dirArray != NULL );
    dirIdx   = 0;
    /* Fake the "." and ".." paths. */
    dirArray[ dirIdx++ ] = strdup( "." );
    dirArray[ dirIdx++ ] = strdup( ".." );
    while( directory )
    {
        path = StripTrailingSlash( directory->data, false );
	/* Don't report the IS_S3_DIRECTORY_FILE. */
	s3dirfilePos = strlen( path )
	               - strlen( &IS_S3_DIRECTORY_FILE[ 1 ] );
	if( ( 0 <= s3dirfilePos )
	    && ( strcmp( path, &IS_S3_DIRECTORY_FILE[ 1 ] ) == 0 ) )
	{
	    free( path );
	    fileCounter--;
	}
	else
	{
	    dirArray[ dirIdx++ ] = path;
	}
	nextEntry = directory->next;
	free( directory );
	directory = nextEntry;
    }
    *nameArray = dirArray;
    *nFiles    = fileCounter;

    return( status );
}
#pragma GCC diagnostic pop



/** Read data from an open file
 * @param path [in] Full file path.
 * @param buf [out] Destination buffer for the file contents.
 * @param maxSize [in] Maximum number of octets to read.
 * @param offset [in] Offset from the beginning of the file.
 * @param actuallyRead [out] Number of octets read from the file.
 * @return 0 on success, or \a -errno on failure.
 */
int
S3ReadFile(
    const char *path,
    char       *buf,
    size_t     maxSize,
    off_t      offset,
    size_t     *actuallyRead
	   )
{
    struct S3FileInfo *fi;
    int               status;
    int               receivedSize;
    char              *contents;
    struct curl_slist *headers = NULL;
    char              range[ 50 ];

    status = S3FileStat( path, &fi );
    if( status == 0 )
    {
	if( fi->fileType == 'f' )
	{
	    /* Get the file contents between "offset" and the following
	       "maxSize" bytes, both included. */
	    sprintf( range, "Range:bytes=%lld-%lld",
		     (long long) offset, (long long) offset + maxSize - 1 );
	    headers = curl_slist_append( headers, strdup( range ) );
	    headers = BuildS3Request( "GET", headers, path );
	    status  = SubmitS3Request( "GET", headers, path,
				       (void**) &contents, &receivedSize );
	    if( status == 0 )
	    {
	        /* Copy the contents to the destination and free the local
		   buffer. */
	        memcpy( buf, contents, receivedSize );
		*actuallyRead = receivedSize;
		free( contents );
	    }
	}
	else
	{
	    status = -EIO;
        }
    }

    return( status );
}



/* Disable warning that userdata is unused. */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
int S3FlushBuffers( const char *path )
{
    /* There currently aren't any buffers to flush, so just return. */
    return 0;
}
#pragma GCC diagnostic pop



/* Disable warning that userdata is unused. */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
int S3FileClose( const char *path )
{
    /* Update the last access time, and stat the file. */
    return 0;
}
#pragma GCC diagnostic pop



/**
 * Resolve a symbolic link.
 * @param link [in] File path of the link.
 * @param target [out] A pointer to where the link target string is stored.
 * @return 0 on success, or \a -errno on failure.
 */
int
S3ReadLink(
    const char *link,
    char       **target
	   )
{
    struct S3FileInfo *fi;
    int               status;
    size_t            size;
    char              *linkContents;
    int               length;
    struct curl_slist *headers = NULL;
    char              range[ 25 ];

    status = S3FileStat( link, &fi );
    if( status == 0 )
    {
	if( fi->fileType == 'l' )
	{
	    /* Read the target if it is not already in the cache. */
	    if( fi->symlinkTarget == NULL )
	    {
	        size = fi->size;
		if( size <= 4096 )
	        {
		    /* Get the file contents; it is a rather small transfer. */
		    sprintf( range, "Range:bytes=0-%d", (int)size - 1 );
		    headers = curl_slist_append( headers, strdup( range ) );
		    headers = BuildS3Request( "GET", headers, link );
		    status  = SubmitS3Request( "GET", headers, link,
					       (void**) &linkContents,
					       &length );
		    if( status == 0 )
		    {
			/* Zero-terminate and return. */
			linkContents = realloc( linkContents, length + 1 );
			linkContents[ length ] = '\0';
			fi->symlinkTarget = strdup( linkContents );
			*target = linkContents;
		    }
		}
		else
		{
		    status = -ENAMETOOLONG;
		}
	    }
	    else
	    {
	        *target = strdup( fi->symlinkTarget );
	    }
	}
	else
	{
	    status = -EISNAM;
	}
    }

    return( status );
}



/**
 * Create HTTP headers based on the information in a S3FileInfo structure.
 * @param fi [in] File info structure.
 * @param headers [in] Headers that the new HTTP headers will be appended to.
 * @return Linked list of HTTP headers.
 */
static struct curl_slist*
CreateHeadersFromFileInfo(
    const struct S3FileInfo *fi,
    struct curl_slist       *headers
			)
{
    char       timeHeader[ 50 ];
    struct tm  tm;
    const char *tFormat = "%s:%s, %d %s %d %02d:%02d:%02d GMT";
    const char *wdays[ ] = { "Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun" };
    const char *months[ ] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun",
			      "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };
    char       tmpHdr[ 30 ];

    /* Update time stamps. */
    localtime_r( &fi->atime, &tm );
    sprintf( timeHeader, tFormat, "x-amz-meta-atime",
	     wdays[ tm.tm_wday ],
	     tm.tm_mday, months[ tm.tm_mon ], tm.tm_year + 1900,
	     tm.tm_hour, tm.tm_min, tm.tm_sec );
    headers = curl_slist_append( headers, strdup( timeHeader ) );
    localtime_r( &fi->mtime, &tm );
    sprintf( timeHeader, tFormat, "Last-Modified",
	     wdays[ tm.tm_wday ],
	     tm.tm_mday, months[ tm.tm_mon ], tm.tm_year + 1900,
	     tm.tm_hour, tm.tm_min, tm.tm_sec );
    headers = curl_slist_append( headers, strdup( timeHeader ) );
    /*
    localtime_r( &fi->ctime, &tm );
    sprintf( timeHeader, tFormat, "Created",
	     wdays[ tm.tm_wday ],
	     tm.tm_mday, months[ tm.tm_mon ], tm.tm_year + 1900,
	     tm.tm_hour, tm.tm_min, tm.tm_sec );
    headers = curl_slist_append( headers, strdup( timeHeader ) );
    */

    /* Rewrite file type header. */
    if( fi->fileType == 'd' )
    {
        headers = curl_slist_append( headers,
		      strdup( "Content-Type:application/x-directory" ) );
    }
    else if( fi->fileType == 'l')
    {
        headers = curl_slist_append( headers,
		      strdup( "Content-Type:application/x-symlink" ) );
    }

    /* Update uid, gid, and permissions. */
    sprintf( tmpHdr, "x-amz-meta-uid:%d", fi->uid );
    headers = curl_slist_append( headers, strdup( tmpHdr ) );
    sprintf( tmpHdr, "x-amz-meta-gid:%d", fi->gid );
    headers = curl_slist_append( headers, strdup( tmpHdr ) );
    sprintf( tmpHdr, "x-amz-meta-mode:%d", fi->permissions );
    headers = curl_slist_append( headers, strdup( tmpHdr ) );
    /* Update s-uid bit, s-gid bit, and sticky bit. */
    sprintf( tmpHdr, "x-amz-meta-modex:%d",
	     ( fi->exeUid ? S_ISUID : 0 )
	     | ( fi->exeGid ? S_ISGID : 0 )
	     | ( fi->sticky ? S_ISVTX : 0 ) );
    headers = curl_slist_append( headers, strdup( tmpHdr ) );

    return( headers );
}



/**
 * Submit an x-amz-copy-source to file-from-samefile, but with new metadata.
 * @param file [in] File whose settings are to be updated.
 * @param fi [in] File Info structure with new information.
 * @return 0 on success, or \a -errno on failure.
 */
static int
UpdateAmzHeaders(
    const char              *file,
    const struct S3FileInfo *fi,
    const char              *newName
		 )
{
    struct curl_slist *headers = NULL;
    char              *copySourceHdr;
    char              *response;
    int               responseLength;
    int               status = 0;

    /* Replace the file by copy-source. Unfortunately, this implies a complete
       rewrite of the amz headers. */
    copySourceHdr = malloc( strlen( file )
			    + strlen( globalConfig.bucketName )
			    + strlen( "x-amz-copy-source:" )
			    + sizeof (char ) );
    strcpy( copySourceHdr, "x-amz-copy-source:" );
    strcat( copySourceHdr, globalConfig.bucketName );
    strcat( copySourceHdr, file );
    headers = curl_slist_append( headers, copySourceHdr );
    headers = curl_slist_append( headers,
		  strdup( "x-amz-metadata-directive:REPLACE" ) );
    headers = CreateHeadersFromFileInfo( fi, headers );

    /* Update the headers. */
    if( newName == NULL )
    {
        newName = file;
    }
    headers = BuildS3Request( "PUT", headers, newName );
    status  = SubmitS3Request( "PUT", headers, newName,
			       (void**) &response, &responseLength );
    free( response );

    return( status );
}



/**
 * Modify the atime and mtime timestamps for a file.
 * @param file [in] File whose timestamps are to be updated.
 * @param atime [in] New atime value.
 * @param atime [in] New mtime value.
 * @return 0 on success, or \a -errno on failure.
 */
int
S3ModifyTimeStamps(
    const char *file,
    time_t     atime,
    time_t     mtime
		   )
{
    struct S3FileInfo *fi;
    int               status;

    status = S3FileStat( file, &fi );
    if( status == 0 )
    {
	fi->atime = atime;
	fi->mtime = mtime;

	status = UpdateAmzHeaders( file, fi, NULL );
    }
    return( status );
}



/**
 * Create a symbolic link.
 * @param linkname [in] Name of the symbolic link.
 * @param path [in] Target of the symbolic link.
 * @return 0 on success, or \a -errno otherwise.
 */
int
S3CreateLink(
    const char *linkname,
    const char *path
	     )
{
    struct S3FileInfo *fi;
    struct curl_slist *headers = NULL;
    int               pathLength;
    int               status;
    time_t            now = time( NULL );
    char              *response;
    int               responseLength;
    char              md5sum[ 25 ];
    char              md5header[ 40 ];
    char              sizeHeader[ 25 ];

    /* Create a new FileInfo structure to the symbolic link. */
    fi = malloc( sizeof( struct S3FileInfo ) );
    fi->uid           = getuid( );
    fi->gid           = getgid( );
    fi->permissions   = 0644;
    fi->fileType      = 'l';
    fi->exeUid        = false;
    fi->exeGid        = false;
    fi->sticky        = false;
    fi->filenotfound  = false;
    fi->symlinkTarget = strdup( path );
    fi->size          = strlen( path );
    fi->atime         = now;
    fi->mtime         = now;
    fi->ctime         = now;

    /* Write file metadata headers. */
    pathLength = strlen( path );
    headers = CreateHeadersFromFileInfo( fi, headers );
    /* Calculate the MD5 checksum of the data and write Content-MD5 header. */
    DigestBuffer( (const unsigned char*) path, pathLength,
		  md5sum, HASH_MD5, HASHENC_BASE64 );
    md5sum[ 24 ] = '\0';
    sprintf( md5header, "Content-MD5: %s", md5sum );
    headers = curl_slist_append( headers, strdup( md5header ) );
    /* Write file size header. */
    sprintf( sizeHeader, "Content-Length: %d", pathLength );
    headers = curl_slist_append( headers, strdup( sizeHeader ) );
    /* Disable the "Expect" and "Transfer-Encoding" headers, because this
       is a very short message. */
    headers = curl_slist_append( headers, strdup( "Expect:" ) );
    headers = curl_slist_append( headers, strdup( "Transfer-Encoding:" ) );
    /* Create standard headers. */
    headers = BuildS3Request( "PUT", headers, linkname );
    status = SubmitS3PutRequest( headers, linkname,
				 (void**) &response, &responseLength,
				 (unsigned char*) path, pathLength );

    /* We already have the FileInfo structure, so because the file will be
       stat'ed as soon as we return, let's add it to the stat cache. Delete
       whatever might already be in the cache. */
    DeleteStatEntry( linkname );
    InsertCacheElement( linkname, fi, &DeleteS3FileInfoStructure );

    return( status );
}



/**
 * Shut down the S3 interface.
 * @return Nothing.
 */
void
S3Destroy(
    void
	  )
{
    curl_easy_cleanup( curl );
    TruncateCache( 0 );
    /* Preparing the write buffers means that the current contents are
       freed and that no buffer is reallocated. */
    PrepareCurlWriteBuffers( false );
    /* Cleanup libxml. */
    xmlCleanupParser( );
}



/**
 * Clean a path of unnecessary slashes.
 * @param inpath [in] The "dirty" path with superfluous slashes.
 * @return A path with only those slashes that are required.
 */
static const char*
CleanPath(
    const char *inpath
	  )
{
    char *filename;
    bool escaped    = false;
    bool slashFound = false;
    int  srcIdx;
    int  destIdx;
    char ch;

    filename = malloc( strlen( inpath ) + sizeof( char ) );

    srcIdx  = 0;
    destIdx = 0;
    /* Make sure there is at least one leading slash. */
    /*
    if( inpath[ srcIdx ] != '/' )
    {
	filename[ destIdx++ ] = '/';
    }
    */
    /* Copy while eliminating all double-slashes. */
    while( ( ch = inpath[ srcIdx ] ) != '\0' )
    {
        if( ! escaped )
	{
	    escaped = false;
	    if( ( ch == '/' ) && ( ! slashFound ) )
	    {
	        filename[ destIdx++ ] = '/';
	        slashFound = true;
	    }
	    else if( ch != '/' )
	    {
	        filename[ destIdx++ ] = ch;
		slashFound = false;
	    }
	    if( ch == '\\' )
	    {
	        escaped = true;
	    }
	}
	srcIdx++;
    }
    filename[ destIdx ] = '\0';
    /* Remove all trailing slashes. */
    filename = StripTrailingSlash( filename, false );

    return( filename );
}



/**
 * The S3 REST interface does not allow the creation of directories, but they
 * may be faked. Eerything is a key, so directories do not really exist,
 * but keys are navigated as if they were directories. This means that any
 * file may be specified as directory/.../file, autocreating the "directories".
 * By storing a secret file that is never listed in the "directory," it is
 * possible to recognize the directory as a directory. I prefer that option
 * to the use of zero-content files posing as directories. The file stat of
 * the newly created directory will fail, but by examining the secret file
 * the directory can be recognized with the permissions of the secret file
 * nonetheless.
 * @param dirname [in] Name of the directory that should be created.
 * @param mode [in] Mode bits for the directory.
 * @return 0 on success, or \a -errno otherwise.
 */
int
S3Mkdir(
    const char *dirname,
    mode_t     mode
	)
{
    const char        *cleanName;
    char              *secretFile;
    struct S3FileInfo newFi;
    struct S3FileInfo *oldFi;
    time_t            now = time( NULL );
    struct curl_slist *headers = NULL;
    char              *response;
    int               responseLength;
    int               status;

    cleanName  = CleanPath( dirname );
    secretFile = malloc( strlen( cleanName ) + strlen( IS_S3_DIRECTORY_FILE )
			 + sizeof( char ) );
    strcpy( secretFile, cleanName );
    strcat( secretFile, IS_S3_DIRECTORY_FILE );

    /* Write file metadata headers. */
    memset( &newFi, 0, sizeof( struct S3FileInfo ) );
    newFi.uid           = getuid( );
    newFi.gid           = getgid( );
    newFi.permissions   = mode;
    newFi.fileType      = 'd';
    newFi.atime         = now;
    newFi.mtime         = now;
    newFi.ctime         = now;

    headers = CreateHeadersFromFileInfo( &newFi, headers );
    headers = curl_slist_append( headers, strdup( "Expect:" ) );
    headers = curl_slist_append( headers, strdup( "Transfer-Encoding:" ) );
    headers = BuildS3Request( "PUT", headers, secretFile );
    status  = SubmitS3Request( "PUT", headers, secretFile,
			       (void**) &response, &responseLength );
    /* Update the stat cache entry for the directory. */
    free( secretFile );
    oldFi = SearchStatEntry( cleanName );
    if( oldFi != NULL )
    {
        memcpy( oldFi, &newFi, sizeof( struct S3FileInfo ) );
    }
    free( (char* )cleanName );

    if( response != NULL )
    {
        free( response );
    }

    return( status );
}



/**
 * Delete a file.
 * @param filename [in] File to delete.
 * @return 0 on success, or \a -errno on failure.
 */
int
S3Unlink( const char *filename )
{
    const char        *cleanName;
    struct curl_slist *headers = NULL;
    char              *response;
    int               responseLength;
    int               status;

    cleanName = CleanPath( filename );

    /* TODO: verify that the file is not a directory (except if it's the
       secret file). */


    headers = BuildS3Request( "DELETE", headers, cleanName );
    status  = SubmitS3Request( "DELETE", headers, cleanName,
			       (void**) &response, &responseLength );
    DeleteStatEntry( cleanName );
    free( (char*) cleanName );

    if( response != NULL )
    {
        free( response );
    }

    return( status );
}



static bool
IsDirectoryEmpty(
    const char *dirname
		 )
{
    bool success;
    char **directory;
    int  nFiles;
    int  status;
    int  i;

    /* Read four files so that we can account for the secret file, the ".",
       the "..", and finally any file that makes the directory non-empty. */
    success = false;
    status = S3ReadDir( NULL, dirname, &directory, &nFiles, 4 );
    if( status == 0 )
    {
        /* We compare against two files instead of three (where above we
	   requested four), because the secret file is not returned. */
        if( nFiles <= 2 )
	{
	    success = true;
	}
    }
    /* Delete the directory array. */
    for( i = 0; i < nFiles; i++ )
    {
        free( directory[ i ] );
    }
    free( directory );

    return( success );
}



/**
 * Remove a directory.
 * @param dirname [in] Directory to delete.
 * @return 0 on success, or \a -errno on failure.
 */
int
S3Rmdir( const char *dirname )
{
    const char        *cleanName;
    char              *secretFile;
    struct S3FileInfo *fi;
    struct curl_slist *headers = NULL;
    char              *response;
    int               responseLength;
    int               status;

    cleanName = CleanPath( dirname );

    status = S3FileStat( cleanName, &fi );
    if( status == 0 )
    {
        /* rmdir works only on directories. */
        if( fi->fileType == 'd' )
	{
 	    /* Determine if the directory is empty. */
	    if( IsDirectoryEmpty( cleanName ) )
	    {
	         /* Delete the secret file. */
	        secretFile = malloc( strlen( cleanName ) + 2 * sizeof( char )
				     + strlen( IS_S3_DIRECTORY_FILE ) );
		strcpy( secretFile, cleanName );
		strcat( secretFile, "/" );
		strcat( secretFile, IS_S3_DIRECTORY_FILE );
		status = S3Unlink( secretFile );
		if( status == 0 )
		{
		    headers = BuildS3Request( "DELETE", headers, cleanName );
		    status  = SubmitS3Request( "DELETE", headers, cleanName,
					       (void**) &response,
					       &responseLength );
		    DeleteStatEntry( cleanName );
		    free( (char*) cleanName );
		}
		/* If the secret file cannot be removed, EACCES seems like
		   the best errno. */
		else
		{
		    status = -EACCES;
		}
	    }
	    else
	    {
	        status = -ENOTEMPTY;
	    }
	}
        else
	{
	    status = -ENOTDIR;
	}
    }

    if( response != NULL )
    {
        free( response );
    }

    return( status );
}



/**
 * Change permissions of a file.
 * @param file [in/out] File whose permissions are changed.
 * @param mode [in] New permissions (rwxrwxrwx ~ 0-7,0-7,0-7).
 * @return 0 on success, or \a -errno on failure.
 */
int
S3Chmod(
    const char *file,
    mode_t     mode
	)
{
    struct S3FileInfo *fi;
    int               status;
    time_t            now = time( NULL );

    status = S3FileStat( file, &fi );
    if( status == 0 )
    {
	fi->mtime       = now;
	fi->permissions = mode;

	status = UpdateAmzHeaders( file, fi, NULL );
    }
    return( status );
}



/**
 * Change ownership of the file.
 * @param file [in/out] FileInfo for the file whose ownership is changed.
 * @param uid [in] New uid, or -1 to keep the current one.
 * @param gid [in] New gid, or -1 to keep the current one.
 * @return 0 on success, or \a -errno on failure.
 */
int
S3Chown(
    const char *file,
    uid_t      uid,
    gid_t      gid
	)
{
    struct S3FileInfo *fi;
    int               status;
    time_t            now = time( NULL );

    status = S3FileStat( file, &fi );
    if( status == 0 )
    {
	fi->mtime                     = now;
	if( (int) uid != -1 ) fi->uid = uid;
	if( (int) gid != -1 ) fi->gid = gid;

	status = UpdateAmzHeaders( file, fi, NULL );
    }
    return( status );
}


