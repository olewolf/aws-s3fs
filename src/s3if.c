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
/*
static unsigned char *curlReadBuffer;
static size_t        curlReadBufferLength;
*/

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
PrepareCurlBuffers(
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
    }

    /* Indicate the type of the next fill. */
    curlWriteBufferIsHeaders = prepareForHeaders;
    curlWriteBuffer          = NULL;
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
    if( toDelete != NULL )
    {
        free( toDelete );
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
    struct curl_slist *headers       = NULL;
    char              headbuf[ 512 ];
    char              *hostName;

    time_t            now;
    struct tm         tnow;
    char              *locale;

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
    headers = curl_slist_append( headers, headbuf );

    /* Generate time string. */
    locale = setlocale( LC_TIME, "en_US.UTF-8" );
    now    = time( NULL );
    /* localtime is not thread-safe; use localtime_r. */
    localtime_r( &now, &tnow );
    strftime( headbuf, sizeof( headbuf ), "Date: %a, %d %b %Y %T %z", &tnow );
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
 * Convert an HTTP response code to a meaningful filesystem error number for
 * FUSE.
 * @param httpStatus [in] HTTP response code.
 * @return \a -errno for the HTTP response code.
 */
STATIC int
ConverHttpStatusToErrno(
    int httpStatus
			)
{
    int status;

    if( httpStatus == 200 )
    {
	status = 0;
    }
    /* Redirection: consider it a "not found" error. */
    else if( ( 300 <= httpStatus ) || ( httpStatus <= 399 ) )
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
    int        j;

    char       statusBuf[ 10 ];
    int        toCopy;

    /* Assume that the first entry in the buffer contains the HTTP status
       reply. */
    httpReply = headers[ 0 ];
    if( httpReply != NULL )
    {
        /* Find the HTTP status code. */
        i = strlen( "HTTP/1.1 " );
	if( strncmp( httpReply, "HTTP/1.1 ", i ) == 0 )
	{
	    j = i;
	    while( ( ( ch = httpReply[ j ] ) != '\0' ) && ( ! isdigit( ch ) ) )
	    {
		j++;
	    }
	    i = j;
	    while( isdigit( httpReply[ j ] ) )
	    {
		j++;
	    }
	    toCopy = j - i;
	    if( (int) sizeof( statusBuf ) - 1 < toCopy )
	    {
		toCopy = sizeof( statusBuf );
	    }
	    strncpy( statusBuf, &httpReply[ i ], toCopy );
	    statusBuf[ toCopy ] = '\n';
	    sscanf( statusBuf, "%d", &httpStatus );
	}
    }

    return( httpStatus );
}



/**
 * Submit a sequence of headers containing an S3 request and receive the
 * output in the local write buffer. The headers list is deallocated.
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
    void              **response,
    int               *responseLength
		)
{
    char       *url;
    char       *hostName;
    int        urlLength;
    int        status = 0;
    int        httpStatus;

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
    /* Set different callback functions for HEAD and GET requests. */
    if( strcmp( httpVerb, "HEAD" ) == 0 )
    {
        PrepareCurlBuffers( true );
        curl_easy_setopt( curl, CURLOPT_NOBODY, 1 );
        curl_easy_setopt( curl, CURLOPT_HEADERFUNCTION, CurlWriteHeader );
    }
    else
    {
        PrepareCurlBuffers( false );
        curl_easy_setopt( curl, CURLOPT_WRITEFUNCTION, CurlWriteData );
    }
    curl_easy_setopt( curl, CURLOPT_HTTPHEADER, headers );
    curl_easy_setopt( curl, CURLOPT_URL, url );
    /*
      curl_easy_setopt( curl, CURLOPT_VERBOSE, 1 );
    */
    status = curl_easy_perform( curl );
    if( status == 0 )
    {
        /* Move the response to a safe place and prepare the CURL write buffer
	for new data. */
        *response       = curlWriteBuffer;
	*responseLength = curlWriteBufferLength;
	curlWriteBuffer       = NULL;
	curlWriteBufferLength = 0;
    }
    pthread_mutex_unlock( &curl_mutex );
    curl_slist_free_all( headers );

    /* Report errors back if necessary. */
    if( status != 0 )
    {
        status = -EIO;
	return( status );
    }

    /* Translate the HTTP request status, if any, to an errno value. */
    if( strcmp( httpVerb, "HEAD" ) == 0 )
    {
        httpStatus = GetHttpStatus( *response );
	if( httpStatus == 0 )
	{
	    status = 0;
	}
	else
        {
	    status = ConverHttpStatusToErrno( httpStatus );
	}
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
	newFileInfo->fileType    = 'f';
	newFileInfo->permissions = 0644;
	/* A trailing slash in the filename indicates that it is a directory. */
	if( filename[ strlen( filename ) - 1 ] == '/' )
	{
	    newFileInfo->fileType    = 'd';
	    newFileInfo->permissions = 0755;
	}
	newFileInfo->uid         = getuid( );
	newFileInfo->gid         = getgid( );
	/* Translate header values to S3 File Info values. */
	for( headerIdx = 0; headerIdx < length; headerIdx++ )
	{
	    headerKey   = response[ headerIdx * 2     ];
	    headerValue = response[ headerIdx * 2 + 1 ];
	    /*
	    printf( "DEBUG: %s:%s\n", headerKey, headerValue );
	    */
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
		if( strcasecmp( headerValue, "x-directory" ) == 0 )
		{
		    newFileInfo->fileType = 'd';
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
 * @return 0 if successful, or \a -errno on failure.
 */
static int
ResolveS3FileStatCacheMiss(
    const char        *filename,
    struct S3FileInfo **fi
			   )
{
    int               status;
    struct S3FileInfo *newFileInfo = NULL;

    /* Read the file attributes for the specified file. */
    status = S3GetFileStat( filename, &newFileInfo );
    if( status == 0 )
    {
        InsertCacheElement( filename, newFileInfo, &DeleteS3FileInfoStructure );
	/*
	printf( "Cache miss: %s (perms=%o)\n", filename,
		newFileInfo->permissions );
	*/
    }
    *fi = newFileInfo;
    return( status );
}



static char*
StripTrailingSlash(
    const char *filename
		 )
{
    char *dirname;
    int  filenameLength;

    if( filename == NULL )
    {
        filenameLength = 0;
    }
    else
    {
	filenameLength = strlen( filename );
    }
    dirname = malloc( filenameLength + sizeof( char ) );
    strncpy( dirname, filename, filenameLength );
    dirname[ filenameLength ] = '\0';
    /* Strip all trailing slashes. */
    while( ( --filenameLength != 0 ) && ( dirname[ filenameLength ] == '/' ) )
    {
        dirname[ filenameLength ] = '\0';
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
    }
    else
    {
	filenameLength = strlen( filename );
    }
    dirname = malloc( filenameLength + 3 * sizeof( char ) );
    strncpy( dirname, filename, filenameLength );
    /* Add trailing slash if necessary. */
    if( dirname[ filenameLength - 1 ] != '/' )
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

    /* Prepare a directory name version of the file. */
    dirname = AddTrailingSlash( filename );

    /* Attempt to read the S3FileStat from the stat cache. */
    status = 0;
    fileInfo = SearchStatEntry( filename );
    if( fileInfo == NULL )
    {
        /* If the file was not found, attempt the directory name version. */
        if( filename[ strlen( filename ) - 1 ] != '/' )
	{
	    fileInfo = SearchStatEntry( dirname );
	}
    }

    if( fileInfo == NULL )
    {
        /* Read the file stat from S3. */
        status = ResolveS3FileStatCacheMiss( filename, &fileInfo );
	/* If unsuccessful, attempt the directory name version. */
	if( status != 0 )
	{
	    status = ResolveS3FileStatCacheMiss( dirname, &fileInfo );
	}
    }
    else
    {
      /*
        printf( "Cache hit: %s\n", filename );
      */
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
    free( dirname );
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
 * @param directory [in/out] Linked list of directory entries.
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
    struct curl_slist *directory,
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
		    directory = curl_slist_append( directory, direntry );
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
int S3ReadDir( struct S3FileInfo *fi, const char *dirname,
	       char ***nameArray, int *nFiles )
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

    char              **dirArray;
    int               dirIdx;
    struct curl_slist *nextEntry;

    /* Construct a base query with a prefix and a delimiter. */

    /* Skip any leading slashes in the dirname. */
    while( dirname[ toSkip ] == '/' )
    {
        toSkip++;
    }
    /* Create prefix and a delimiter for the S3 list. The prefix is the entire
       path including dirname plus trailing slash, so add a slash to the
       dirname if it is not already specified. The delimiter is a '/'. */
    prefix    = StripTrailingSlash( &dirname[ toSkip ] );
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
    free( (char*) urlSafePrefix );

    /* Fake the ".." and "." paths. */
    directory = curl_slist_append( directory, "." );
    directory = curl_slist_append( directory, ".." );
    fileCounter = 2;

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
				      directory, &fromFile, &fileCounter );
		}
		xmlFreeDoc( xmlResponse );
		xmlCleanupParser( );
	    }
	}
    } while( fromFile != NULL );

    /* Move the linked-list file names into an array. */
    dirArray = malloc( sizeof( char* ) * fileCounter );
    dirIdx   = 0;
    while( directory )
    {
        dirArray[ dirIdx++ ] = StripTrailingSlash( directory->data );
	free( directory->data );
	nextEntry = directory->next;
	if( directory != NULL )
	{
	    free( directory );
	}
	directory = nextEntry;
    }

    *nameArray = dirArray;
    *nFiles    = fileCounter;

    return( status );
}
#pragma GCC diagnostic pop



/* Disable warning that userdata is unused. */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
int S3ReadFile( const char *path, char *buf, size_t size, off_t offset )
{
    /* Stub */
    return 0;
}
#pragma GCC diagnostic pop


/* Disable warning that userdata is unused. */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
int S3FlushBuffers( const char *path )
{
    /* Stub */
    return 0;
}
#pragma GCC diagnostic pop


/* Disable warning that userdata is unused. */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
int S3FileClose( const char *path )
{
    /* Stub */
    return 0;
}
#pragma GCC diagnostic pop


