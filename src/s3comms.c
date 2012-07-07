/**
 * \file s3comms.c
 * \brief Shared S3 communications functions.
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
#include <stdlib.h>
#include <malloc.h>
#include <string.h>
#include <locale.h>
#include <pthread.h>
#include <curl/curl.h>
#include <errno.h>
#include <glib.h>
#include <assert.h>
#include "s3comms.h"


#ifdef AUTOTEST
#define STATIC
#else
#define STATIC static
#endif


/* Define the custom data type that is passed by the CURL call-back for
   writing data received from S3. */
struct CurlWriteBuffer
{
    unsigned char *data;
    size_t        size;
};

/* Define the custom data type that is passed by the CURL call-back for
   sending data to S3. */
struct CurlReadBuffer
{
    unsigned char *data;
    size_t        size;
    off_t         offset;
};


/* For keeping track of processes using the library. */
static pthread_mutex_t handles_mutex = PTHREAD_MUTEX_INITIALIZER;
static GSList *handles = NULL;



/**
 * Lock access to CURL for the current thread.
 * @return Nothing.
 */
static inline void
LockCurl(
    pthread_mutex_t *mutex
        )
{
    pthread_mutex_lock( mutex );
}


/**
 * Permit access to CURL for the current thread.
 * @return Nothing.
 */
static inline void
UnlockCurl(
    pthread_mutex_t *mutex
	       )
{
    pthread_mutex_unlock( mutex );
}



/**
 * Create a handle in the library in order to use the S3 functions.
 * (The hashing functions do not require this handle.)
 * @param region [in] The region in which the bucket resides.
 * @param bucket [in] The name of the bucket.
 * @param keyId [in] Your Amazon Access Key ID.
 * @param secretKey [in] Your secret Amazon key.
 * @return Handle or \a NULL if an error occurred.
 */
S3COMM*
s3_open( 
	enum  bucketRegions region,
	const char          *bucket,
	const char          *keyId,
	const char          *secretKey
        )
{
	S3COMM *newInstance;
	const pthread_mutex_t defaultMutex = PTHREAD_MUTEX_INITIALIZER;

	/* Create a new instance, including a new CURL session. */
	newInstance = malloc( sizeof( S3COMM ) );
	newInstance->curl = curl_easy_init( );
	if( newInstance->curl != NULL )
	{
		newInstance->region     = region;
		newInstance->bucket     = strdup( bucket );
		newInstance->keyId      = strdup( keyId );
		newInstance->secretKey  = strdup( secretKey );
		memcpy( &newInstance->curl_mutex, &defaultMutex,
				sizeof( pthread_mutex_t ) );

		pthread_mutex_lock( &handles_mutex );
		handles = g_slist_append( handles, newInstance );
		pthread_mutex_unlock( &handles_mutex );
	}
	else
	{
		free( newInstance );
		newInstance = NULL;
	}

	return( newInstance );
}



/**
 * Release a handle.
 * @param handle [in] The handle that should be released.
 * @return Nothing.
 */
void
s3_close(
	S3COMM *handle
         ) 
{
	pthread_mutex_lock( &handles_mutex );
	handles = g_slist_remove( handles, handle );
	pthread_mutex_unlock( &handles_mutex );

	free( handle->bucket );
	free( handle->keyId );
	free( handle->secretKey );
	curl_easy_cleanup( handle->curl );
	free( handle );
}



/**
 * Callback function for CURL write where header data is expected. The function
 * adds headers to an expanding array organized as {header-name, header-value}
 * pairs.
 * @param ptr [in] Source of the data from CURL.
 * @param size [in] The size of each data block.
 * @param nmemb [in] Number of data blocks.
 * @param userdata [in] Passed from the CURL write-back as a pointer to a
 *        struct CurlWriteBuffer.
 * @return Number of bytes copied from \a ptr.
 */
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
    struct CurlWriteBuffer *writeBuffer = userdata;

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
					( ptr[ dataEndIdx ] == '\r' ) ||
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
        if( writeBuffer->data == NULL )
		{
			writeBuffer->size = 0;
			writeBuffer->data = malloc( HEADER_ALLOC_AMOUNT *
										2 * sizeof( char* ) );
		}
		else
        {
			/* Increase pair count. */
			writeBuffer->size = writeBuffer->size + 1;
			/* Expand the buffer by HEADER_ALLOC_AMOUNT items if necessary. */
			if( ( writeBuffer->size % HEADER_ALLOC_AMOUNT ) == 0 )
			{
				newSize = ( writeBuffer->size + 1 ) * HEADER_ALLOC_AMOUNT;
				writeBuffer->data = realloc( writeBuffer->data,
											 newSize * 2 * sizeof( char* ) );
				bufIdx = writeBuffer->size * 2;
				for( i = 0; i < HEADER_ALLOC_AMOUNT; i++ )
				{
					( (char**)writeBuffer->data )[ bufIdx + i * 2 ] = NULL;
				}
			}
		}

		/* Write the key and the value into the buffer. */
		bufIdx = writeBuffer->size * 2;
		( (char**)writeBuffer->data )[ bufIdx     ] = header;
		( (char**)writeBuffer->data )[ bufIdx + 1 ] = data;
    }

    return( toCopy );
}



/**
 * Callback function for CURL read where body data is expected. The function
 * may be called several times, transferring a chunk of data each time. The
 * function is called from an already mutex-locked CURL function.
 * @param ptr [out] Destination for the data to CURL.
 * @param size [in] The allowed size of each data block.
 * @param nmemb [in] The allowed number of data blocks.
 * @param userdata [in] Pointer to a CurlReadBuffer structure.
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
    /* Save the amount of data copied to the offset for the next block of
       data. */
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
 * @param userdata [in] Passed from the CURL write-back as a pointer to a
 *        struct CurlWriteBuffer.
 * @return Number of bytes copied from \a ptr.
 */
static size_t
CurlWriteData(
    char   *ptr,
    size_t size,
    size_t nmemb,
    void   *userdata
	          )
{
    struct CurlWriteBuffer *writeBuffer = userdata;
    size_t        toCopy = size * nmemb;
    unsigned char *destBuffer;
    int           i;
    size_t        toAllocate;

    /* Allocate or expand the buffer to receive the new data. */
    if( writeBuffer->data == NULL )
    {
		writeBuffer->size = 0;
        writeBuffer->data = malloc( toCopy + sizeof( char ) );
		destBuffer = writeBuffer->data;
    }
    else
    {
        toAllocate = writeBuffer->size + toCopy + sizeof( char );
        writeBuffer->data = realloc( writeBuffer->data, toAllocate );
		destBuffer = &( (unsigned char*)writeBuffer->data )[ writeBuffer->size ];
    }

    /* Receive data and zero-terminate. */
    for( i = 0; i < (int) toCopy; i++ )
    {
		*destBuffer++ = *ptr++;
    }
    *destBuffer = '\0';
    writeBuffer->size = writeBuffer->size + toCopy + 1;

    return( toCopy );
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
 * Return a string with the S3 host name corresponding to the region where the
 * specified bucket is located.
 * @param region [in] The region of the bucket.
 * @param bucket [in] Name of the bucket, which is used to create a virtual
 *        host name.
 * @return An allocated buffer with the S3 host name.
 */
static char*
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

    if( region != US_STANDARD )
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
 * Remove any query parameters from the path that are not accepted by the S3,
 * and sort the remaining ones alphabetically. This is required for signing the
 * S3 request.
 * @param path [in] Intended URL for the S3.
 * @return Path that is valid for signing.
 */
static const char*
SignablePath(
	const char *path
             )
{
/*
	const char *const requiredQueries[ ] =
	{
		"response-cache-control",
		"response-content-disposition",
		"response-content-encoding",
		"response-content-language",
		"response-content-type",
		"response-expires"
	};
*/
	char *signablePath = malloc( strlen( path ) + sizeof( char ) );
	int  idx = 0;
	char ch;

	/* Find the first query and copy to the signable path as we go. */
	while( ( ( ch = path[ idx ] ) != 0 ) && ( ch != '?' ) )
	{
		signablePath[ idx ] = path[ idx ];
		idx++;
	}

	/* For now, simply strip the remainder of the path; the current version
	   of aws-s3fs does not use any queries that should be included in the
	   CanonicalizedResource. */
	signablePath[ idx ] = '\0';

	return( (const char*) signablePath );
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
    const char         *httpMethod,	   
    struct curl_slist  *headers,
	enum bucketRegions region,
	const char         *bucket,
    const char         *path,
	const char         *keyId,
	const char         *secretKey
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
	const char        *signablePath;

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

	/* Remove all sub-resources that are not required from the path. */
	signablePath = SignablePath( path );
	if( region != US_STANDARD )
	{
		sprintf( &messageToSign[ strlen( messageToSign ) ],
				 "/%s%s", bucket, signablePath );
	}
	else
	{
		sprintf( &messageToSign[ strlen( messageToSign ) ], "%s",
				 signablePath );
	}
	free( (char*) signablePath );

    /* Sign the message and add the Authorization header. */
    signature = HMAC( (const unsigned char*) messageToSign,
					  strlen( messageToSign ),
					  (const char*) secretKey,
					  HASH_SHA1, HASHENC_BASE64 );
    awsHeader = malloc( 100 );
    sprintf( awsHeader, "Authorization: AWS %s:%s",
			 keyId, signature );
    free( (char*) signature );
    headers = curl_slist_append( headers, strdup( awsHeader ) );
    return headers;
}



/**
 * Completely purge a curl_slist linked list of its data, calling the self-
 * destruct function in each entry to deallocate any data.
 * @param toDelete [in] Pointer to the first entry in the curl_slist.
 * @return Nothing.
 */
void
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
 * Create a generic S3 request header which is used in all requests.
 * @param httpMethod [in] HTTP verb (GET, HEAD, etc.) for the request.
 * @return Header list.
 */
STATIC struct curl_slist*
BuildGenericHeader(
	const char *hostname
	               )
{
    struct curl_slist *headers = NULL;
    char              headbuf[ 512 ];
    time_t            now;
    struct tm         tnow;
    char              *locale;

    char *host;
    char *date;
    char *userAgent;

	sprintf( headbuf, "Host: %s", hostname );
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
struct curl_slist*
BuildS3Request(
	S3COMM             *instance,
    const char         *httpMethod,
	const char         *hostname,
    struct curl_slist  *additionalHeaders,
    const char         *filename
               )
{
    struct curl_slist *allHeaders;
    struct curl_slist *currentHeader;
    char              *extraHeaders[ 100 ];
    size_t            count;
    int               i;

    /* Build basic headers. */
    allHeaders = BuildGenericHeader( hostname );

    /* Add any additional headers, if any, in sorted order. */
    currentHeader = additionalHeaders;
    count         = 0;
    while( ( currentHeader != NULL )
		   && ( count < sizeof( extraHeaders ) / sizeof( char* ) ) )
    {
		extraHeaders[ count++ ] = currentHeader->data;
        currentHeader = currentHeader->next;
    }

    /* Sort the headers. */
    qsort( extraHeaders, count, sizeof( extraHeaders[ 0 ] ), qsort_strcmp );
    /* Add the headers to the main header list and get rid of the extras
       list. */
    for( i = 0; i < (int) count; i++ )
    {
        allHeaders = curl_slist_append( allHeaders, extraHeaders[ i ] );
    }
    /* Free the list and its elements, but not the headers themselves. */
    curl_slist_free_all( additionalHeaders );

    /* Add a signature header. */
    allHeaders = CreateAwsSignature( httpMethod, allHeaders,
									 instance->region, instance->bucket,
									 filename,
									 instance->keyId, instance->secretKey );

    return( allHeaders );
}



/**
 * Convert an HTTP response code to a meaningful filesystem error number for
 * FUSE.
 * @param httpStatus [in] HTTP response code.
 * @return \a -errno for the HTTP response code.
 */
static int
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



/**
 * Submit a sequence of headers containing an S3 request and receive the
 * output in the local write buffer. The headers list is deallocated.
 * The request may include PUT requests, as long as there is no body data
 * to put.
 * @param httpVerb [in] HTTP method (GET, HEAD, etc.).
 * @param region [in] The region where the bucket is located.
 * @param bucket [in] Name of the bucket.
 * @param headers [in/out] The CURL list of headers with the S3 request.
 * @param filename [in] Full path name of the file that is accessed.
 * @param response [out] Pointer to the response data.
 * @param responseLength [out] Pointer to the response length.
 * @return 0 on success, or CURL error number on failure.
 */
int
s3_SubmitS3Request(
	S3COMM             *instance,
    const char         *httpVerb,
    struct curl_slist  *headers,
    const char         *filename,
    void               **data,
    int                *dataLength
	                )
{
    char                   *url;
    char                   *hostName;
    int                    urlLength;
    int                    status = 0;
    long                   httpStatus;
	CURL                   *curl       = instance->curl;
    struct CurlWriteBuffer writeBuffer = { NULL, 0 };

    printf( "s3if: SubmitS3Request (%s)\n", filename );

    /* Determine the virtual host name. */
    hostName = GetS3HostNameByRegion( instance->region, instance->bucket );
    headers = BuildS3Request( instance, httpVerb, hostName, headers, filename );

    /* Submit request via CURL and wait for the response. */
    LockCurl( &instance->curl_mutex );
    curl_easy_reset( curl );
    /* Set callback function according to HTTP method. */
    if( ( strcmp( httpVerb, "HEAD" ) == 0 )
		|| ( strcmp( httpVerb, "DELETE" ) == 0 )
		|| ( strcmp( httpVerb, "POST" ) == 0 ) )
    {
        curl_easy_setopt( curl, CURLOPT_NOBODY, 1 );
        curl_easy_setopt( curl, CURLOPT_HEADERFUNCTION, CurlWriteHeader );
		curl_easy_setopt( curl, CURLOPT_WRITEHEADER, &writeBuffer );
		if( strcmp( httpVerb, "DELETE" ) == 0 )
		{
			curl_easy_setopt( curl, CURLOPT_CUSTOMREQUEST, "DELETE" );
		}
		else if( strcmp( httpVerb, "POST" ) == 0 )
		{
			curl_easy_setopt( curl, CURLOPT_CUSTOMREQUEST, "POST" );
		}
    }
    else if( strcmp( httpVerb, "GET" ) == 0 )
    {
        curl_easy_setopt( curl, CURLOPT_WRITEFUNCTION, CurlWriteData );
		curl_easy_setopt( curl, CURLOPT_WRITEDATA, &writeBuffer );
    }
    else if( strcmp( httpVerb, "PUT" ) == 0 )
    {
        curl_easy_setopt( curl, CURLOPT_NOBODY, 1 );
        curl_easy_setopt( curl, CURLOPT_HEADERFUNCTION, CurlWriteHeader );
		curl_easy_setopt( curl, CURLOPT_WRITEHEADER, &writeBuffer );
		curl_easy_setopt( curl, CURLOPT_UPLOAD, true );
		curl_easy_setopt( curl, CURLOPT_INFILESIZE, 0 );
    }
    curl_easy_setopt( curl, CURLOPT_HTTPHEADER, headers );
    /* Determine the length of the URL. */
    urlLength = strlen( hostName )
                + strlen( "https://" )
                + strlen( instance->bucket ) + sizeof( char )
                + strlen( filename )
                + sizeof( char )
                + sizeof( char );
    /* Build the full URL, adding a '/' to the host if the filename does not
       include it as its leading character. */
    url = malloc( urlLength );
    if( instance->region != US_STANDARD )
    {
        sprintf( url, "https://%s%s%s", hostName,
				 filename[ 0 ] == '/' ? "" : "/",
				 filename );
    }
    else
    {
        sprintf( url, "https://%s/%s%s%s", hostName,
				 instance->bucket,
				 filename[ 0 ] == '/' ? "" : "/",
				 filename );
    }
    free( hostName );
    curl_easy_setopt( curl, CURLOPT_URL, url );
    /*
	curl_easy_setopt( curl, CURLOPT_VERBOSE, 1 );
    */
    status = curl_easy_perform( curl );
    curl_easy_getinfo( curl, CURLINFO_RESPONSE_CODE, &httpStatus );
    UnlockCurl( &instance->curl_mutex );

    /* Return the response. */
    *data       = writeBuffer.data;
    *dataLength = writeBuffer.size;

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
 * @param region [in] The region where the bucket is located.
 * @param bucket [in] Name of the bucket.
 * @param headers [in/out] The CURL list of headers with the S3 request.
 * @param filename [in] Full path name of the file that is accessed.
 * @param response [out] Pointer to the response data.
 * @param responseLength [out] Pointer to the response length.
 * @param bodyData [in] Pointer to the upload data.
 * @param bodyLength [in] Size of the upload data.
 * @return 0 on success, or CURL error number on failure.
 */
int
s3_SubmitS3PutRequest(
	S3COMM             *instance,
    struct curl_slist  *headers,
    const char         *filename,
    void               **response,
    int                *responseLength,
    unsigned char      *bodyData,
    size_t             bodyLength
		)
{
    char       *url;
    char       *hostName;
    int        urlLength;
    int        status     = 0;
    long       httpStatus;
	CURL                  *curl      = instance->curl;
    struct CurlReadBuffer readBuffer = { bodyData, bodyLength, 0 };


    /* Determine the virtual host name. */
    hostName = GetS3HostNameByRegion( instance->region, instance->bucket );
    /* Determine the length of the URL. */
    urlLength = strlen( hostName )
                + strlen( "https://" )
                + strlen( instance->bucket ) + sizeof( char )
                + strlen( filename )
                + sizeof( char )
                + sizeof( char );
    /* Build the full URL, adding a '/' to the host if the filename does not
       include it as its leading character. */
    url = malloc( urlLength );
    if( instance->region != US_STANDARD )
    {
        sprintf( url, "https://%s%s%s", hostName,
				 filename[ 0 ] == '/' ? "" : "/",
				 filename );
    }
    else
    {
        sprintf( url, "https://%s/%s%s%s", hostName, instance->bucket,
				 filename[ 0 ] == '/' ? "" : "/",
				 filename );
    }
    headers = BuildS3Request( instance, "HEAD", hostName, headers, filename );
    free( hostName );

    /* Submit request via CURL and wait for the response. */
    LockCurl( &instance->curl_mutex );
    curl_easy_reset( curl );
    curl_easy_setopt( curl, CURLOPT_READFUNCTION, CurlReadData );
    curl_easy_setopt( curl, CURLOPT_READDATA, &readBuffer );
    curl_easy_setopt( curl, CURLOPT_UPLOAD, true );
    curl_easy_setopt( curl, CURLOPT_HTTPHEADER, headers );
    curl_easy_setopt( curl, CURLOPT_URL, url );
    status = curl_easy_perform( curl );
    curl_easy_getinfo( curl, CURLINFO_RESPONSE_CODE, &httpStatus );
    UnlockCurl( &instance->curl_mutex );

    /* Indicate that there is no response data. */
    *response             = NULL;
    *responseLength       = 0;

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



