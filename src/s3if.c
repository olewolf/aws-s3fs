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
/*
static unsigned char *curlReadBuffer;
static size_t        curlReadBufferLength;
*/


struct HttpHeaders
{
    const char *headerName;
    char       *content;
};



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

    /* Allocate room for 10 headers at a time. */
    static const int HEADER_ALLOC_AMOUNT = 10;

    /* Extract header key, which is everything up to ':'. That is, unless
       the header key takes up the entire line, in which case it isn't a useful
       header. ("HTTP/1.1 200 OK" is returned, but it isn't a key:value
       header.) */
    for( i = 0; ( i < toCopy ) && ( ptr[ i ] != ':' ); i++ );
    header = malloc( ( i + 1 ) * sizeof( char ) );
    memcpy( header, ptr, i );
    header[ i ] = '\0';

    /* Extract the value for the header key. */
    if( i != toCopy )
    {
        /* Extract data. Skip ':[[:space:]]*' */
        while( i < toCopy )
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
	if( i != toCopy )
	{
	    /* Extract header value. If the header value ends with a newline,
	       terminate it prematurely. */
	    dataEndIdx = i;
	    while( dataEndIdx < toCopy )
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

    /* Allocate or expand the buffer to receive the new data. */
    if( curlWriteBuffer == NULL )
    {
	curlWriteBufferLength = 0;
        curlWriteBuffer = malloc( toCopy );
	destBuffer = curlWriteBuffer;
    }
    else
    {
        curlWriteBuffer = realloc( curlWriteBuffer,
				   curlWriteBufferLength + toCopy );
	destBuffer = &( (unsigned char*)curlWriteBuffer )[ curlWriteBufferLength ];
    }

    /* Receive data. */
    for( i = 0; i < toCopy; i++ )
    {
	*destBuffer++ = *ptr++;
    }
    curlWriteBufferLength = curlWriteBufferLength + toCopy;

    return( toCopy );
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
 * Create a generic S3 request header which is used in all requests.
 * @param httpMethod [in] HTTP verb (GET, HEAD, etc.) for the request.
 * @return Header list.
 */
STATIC struct curl_slist*
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
    char *url;
    int  urlLength;
    int  status = 0;

    /* Determine the length of the URL. */
    urlLength = strlen( "https://..amazonaws.com" )
                + strlen( globalConfig.bucketName )
                + strlen( amazonHost[ globalConfig.region ] )
                + strlen( filename )
                + sizeof( char )
                + sizeof( char );
    /* Build the full URL, adding a '/' to the host if the filename does not
       include it as its leading character. */
    url = malloc( urlLength );
    sprintf( url, "https://%s.%s.amazonaws.com%s%s",
	     globalConfig.bucketName, amazonHost[ globalConfig.region ],
	     filename[ 0 ] == '/' ? "" : "/", filename );

    /* Submit request via CURL and wait for the response. */
    pthread_mutex_lock( &curl_mutex );
    if( strcmp( httpVerb, "HEAD" ) == 0 )
    {
        curl_easy_setopt( curl, CURLOPT_NOBODY, 1 );
        curl_easy_setopt( curl, CURLOPT_HEADERFUNCTION, CurlWriteHeader );
    }
    else
    {
        curl_easy_setopt( curl, CURLOPT_WRITEFUNCTION, CurlWriteData );
    }
    curl_easy_setopt( curl, CURLOPT_HTTPHEADER, headers );
    curl_easy_setopt( curl, CURLOPT_URL, url );
    status = curl_easy_perform( curl );
    /* Move the response to a safe place and prepare the CURL write buffer
       for new data. */
    *response       = curlWriteBuffer;
    *responseLength = curlWriteBufferLength;
    curlWriteBuffer       = NULL;
    curlWriteBufferLength = 0;
    pthread_mutex_unlock( &curl_mutex );

    curl_slist_free_all( headers );

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
    char              *response;
    int               length;

    /*
    struct HttpHeaders statHeaders[ ] =
    {
        {
            .headerName = "Last-Modified: ",
	    .content    = NULL
	},
	{
	    .headerName = "x-amz-meta-uid: ",
	    .content    = NULL
	},
	{
	    .headerName = "x-amz-meta-gid: ",
	    .content    = NULL
	},
	{
	    .headerName = "x-amz-meta-mode: ",
	    .content    = NULL
	},
	{
	    .headerName = "Content-Length: ",
	    .content    = NULL
	},
	{
	  .headerName = "Content-Type: ",
	    .content    = NULL
	},
	{
	    .headerName = "x-amz-meta-mtime: ",
	    .content    = NULL
	},
	{
	    .headerName = "x-amz-meta-atime: ",
	    .content    = NULL
	},
	{
	    .headerName = "x-amz-meta-ctime: ",
	    .content    = NULL
	}
    };
    */

    /* Create specific file information request headers. */
    /* (None required.) */

    /* Setup S3 headers. */
    BuildS3Request( "HEAD", headers, filename );

    /* Make request via curl and wait for response. */
    status = SubmitS3Request( "HEAD", headers, filename, (void**)&response, &length );

    /* Separate headers into headers array. */
    /*
    SeparateHeaders( response, length, &statHeaders,
		     sizeof( statHeaders ) / sizeof( struct HttpHeaders ) );
    */

    /* Translate file attributes to an S3 File Info structure. */


    newFileInfo = malloc( sizeof (struct S3FileInfo ) );
    assert( newFileInfo != NULL );
    /*
    newFileInfo->uid         = GetHeaderInt( "x-amz-meta-uid" );
    newFileInfo->gid         = GetHeaderInt( "x-amz-meta-gid" );
    newFileInfo->permissions = GetHeaderInt( "x-amz-meta-mode" );
    newFileInfo->fileType    = Content-Type: "x-directory" or regular;
    newFileInfo->exeUid      = ;
    newFileInfo->exeGid      = ;
    newFileInfo->size        = GetHeaderInt( "Content-Length" );
    newFileInfo->atime       = ;
    newFileInfo->mtime       = GetHeaderTime( "x-amz-meta-mtime" ); bzzt! Last-Modified instead!
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


    /*
    xmlDocPtr  xmlResponse;
    xmlNodePtr currentNode;

    xmlResponse = xmlReadMemory( response, length, "s3.xml", NULL, 0 );
    if( xmlResponse == NULL )
    {
        status = -EIO;
    }
    else
    {
	 currentNode = xmlDocGetRootElement( xmlResponse );
	 if( currentNode == NULL )
	 {
	      status = -EIO;
	      xmlFreeDoc( xmlResponse );
	 }
	 else
	 {
	     if( xmlStrcmp( currentNode->name, (const xmlChar*) "story") )
	     {
	         status = -EIO;
		 xmlFreeDoc( xmlResponse );
	     }
	     else
	     {
	       
	     }
	 }
    }
    */
