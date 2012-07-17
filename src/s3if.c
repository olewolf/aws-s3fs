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
#include <pthread.h>
#include <curl/curl.h>
#include <time.h>
#include <libxml/parser.h>
#include "aws-s3fs.h"
#include "s3if.h"
#include "digest.h"
#include "statcache.h"
#include "dircache.h"
#include "s3comms.h"
#include "filecache.h"


/* The REST interface does not allow the creation of directories. Instead,
   specify a bogus file within the directory that is never listed by the
   readdir function. When stat on the directory fails, check whether this
   file exists in the directory. */
#define IS_S3_DIRECTORY_FILE "/.----s3--dir--do-not-delete"


#ifdef AUTOTEST
#define STATIC
#else
#define STATIC static
#endif


struct HttpHeaders
{
    const char *headerName;
    char       *content;
};

/* For cache locking. */
static pthread_mutex_t cache_mutex = PTHREAD_MUTEX_INITIALIZER;

/* Initialized at start-up, then remains constant. */
static long int localTimezone;

/* Handle for the digest and S3 communications lib. */
STATIC S3COMM *s3comm;


/**
 * Lock the file stat and directory caches by locking the mutex.
 * @return Nothing.
 */
static inline void
LockCaches(
void
	   )
{
    pthread_mutex_lock( &cache_mutex );
}


/**
 * Unlock the file stat and directory caches by locking the mutex.
 * @return Nothing.
 */
static inline void
UnlockCaches(
void
	   )
{
    pthread_mutex_unlock( &cache_mutex );
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

	/* Connect to the file cache daemon. */
	if( ! ConnectToFileCache( globalConfig.bucketName, 
							  globalConfig.keyId, globalConfig.secretKey ) )
	{
		fprintf( stderr, "Cannot connect to the file cache daemon\n" );
	}

	/* Initialize CURL. */
	curl_global_init( CURL_GLOBAL_ALL );

	/* Load functions for digests, signing, and sending and receiving
	 * messages to S3. */
#ifndef AUTOTEST
	s3comm = s3_open( globalConfig.region, globalConfig.bucketName,
					  globalConfig.keyId, globalConfig.secretKey );
#endif

    /* Initialize libxml. */
    LIBXML_TEST_VERSION

    InitializeDirectoryCache( );

    /* Determine local timezone. */
    tnow = time( NULL );
    localtime_r( &tnow, &tm );
    localTimezone = tm.tm_gmtoff;
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
    ch = ((string)[ idx ]);										\
    chNext = ((string)[ (idx) + 1 ]);									\
    if( ( ! isdigit( ch ) ) || ( ! isdigit( chNext ) ) ) return( -EILSEQ ); \
    (value) = ch - '0';													\
    (value) = (value) * 10 + chNext - '0';								\
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
 * Retrieve information on a specific file in an S3 path. This function
 * must be called from a mutex'ed function.
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

    /* Make request via curl and wait for response. */
    status = s3_SubmitS3Request( s3comm, "HEAD", headers, filename,
								 (void**)&response, &length );
    if( status == 0 )
    {
        /* Prepare an S3 File Info structure. */
        newFileInfo = malloc( sizeof (struct S3FileInfo ) );
		assert( newFileInfo != NULL );
		/* Set default values. */
		memset( newFileInfo, 0, sizeof( struct S3FileInfo ) );
		/* If the file is known to not exist, cache that information here. */
		newFileInfo->filenotfound = false;
		newFileInfo->fileType    = 'f';
		newFileInfo->permissions = 0644;
		/* A trailing slash in the filename indicates that it is a directory. */
		if( filename[ strlen( filename ) - 1 ] == '/' )
		{
			newFileInfo->fileType    = 'd';
			newFileInfo->permissions = 0755;
		}
		/* By default, the current user's uid and gid. */
		newFileInfo->uid = getuid( );
		newFileInfo->gid = getgid( );
		/* If the file is a symbolic link, cache the target here. */
		newFileInfo->symlinkTarget = NULL;
		/* No local file handle yet. */
		newFileInfo->localFd = -1;
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



/**
 * Get the name of the specified file's parent directory. This is primarily
 * used to determine the permissions of the parent directory.
 * @param path [in] A file path.
 * @return The parent directory of the file path.
 */
static char*
GetParentDir( const char *path )
{
    int  endIdx;
    char *parent;

    /* Find the first slash from the end. */
    endIdx = strlen( path ) - sizeof( char );
    while( ( endIdx != 0 ) && ( path[ endIdx ] != '/' ) )
    {
		endIdx--;
    }
    /* If there is no parent, specify "/" as parent. */
    if( endIdx == 0 )
    {
        parent = strdup( "/" );
    }
    else
    {
        /* Copy from the beginning of the string until the endIdx. */
        parent = malloc( endIdx + sizeof( char ) );
		strncpy( parent, path, endIdx );
		parent[ endIdx ] = '\0';
    }

    return( parent );
}



/**
 * Remove any occurrences of '/' after a filename.
 * @param filename [in/out] File path that may end with one or more slashes.
 * @param makeCopy [in] If \a true, return a copy of the file path (without
 *        the trailing slashes). If \a false, remove the slash in the
 *        original string.
 * @return Filename without trailing slashes.
 */
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



/**
 * Add a trailing slash after a filename unless there already is one.
 * @param filename [in] Filename path.
 * @return Filename path with an appended '/'.
 */
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
    LockCaches( );

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
		/* Indicate that we do not have to bother the file cache with
		   inquiries until the file itself is cached. */
		if( fileInfo != NULL )
		{
			fileInfo->statonly = true;
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
    UnlockCaches( );

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



/**
 * Create a URL safe version of a raw URL.
 * @param url [in] Raw URL that is to be URL-encoded.
 * @return URL-safe version of the URL.
 */
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
 * Recursive function that parses an XML file and adds the contents of
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



/**
 * Read the contents of a directory and place it in the directory cache, then
 * return the directory contents. If the directory is already in the cache,
 * mark it as least recently used.
 * @param fi [in] File info for the directory.
 * @param dirname [in] Path name of the directory.
 * @param nameArray [out] Pointer to where the directory contents (an array
 *        of strings) is stored.
 * @param nFiles [out] The number of files in the directory, including '.'
 *        and "..".
 * @param maxRead [in] The maximum number of directory entries to read. If -1,
 *        read the entire directory.
 * @return 0 on success, or \a -errno on failure.
 */
int
S3ReadDir(
    const char        *dirname,
    char              ***nameArray,
    int               *nFiles,
    int               maxRead
	     )
{
    int        status = 0;

    const char *delimiter;
    const char *prefix;
    char       *parentDir;
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
    parentDir = malloc( strlen( prefix ) + 2 * sizeof( char ) );
    parentDir[ 0 ] = '/';
    strcpy( &parentDir[ 1 ], prefix );

    /* Lookup in the directory cache. */
    dirArray = (char**) LookupInDirectoryCache( parentDir, &fileCounter );
    if( dirArray == NULL )
    {
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
		   Omit the prefix if the root folder was specified.
		   The reason for the multiple tests for max-keys is that Amazon
		   will probably soon require all the parameters in the query
		   to be ordered alphabetically. */
		if( strlen( prefix ) == 0 )
		{
			sprintf( queryBase, "%s?delimiter=%s", relativeRoot, delimiter );
			if( maxRead != -1 )
			{
				sprintf( &queryBase[ strlen( queryBase ) ],
						 "&max-keys=%d", maxRead );
			}
		}
		else
		{
			sprintf( queryBase, "%s?delimiter=%s", relativeRoot, delimiter );
			if( maxRead != -1 )
			{
				sprintf( &queryBase[ strlen( queryBase ) ],
						 "&max-keys=%d", maxRead );
			}
			sprintf( &queryBase[ strlen( queryBase ) ], "&prefix=%s/",
					 urlSafePrefix );
		}
		free( (char*) urlSafePrefix );

		fileCounter = 0;
		fileLimit   = ( maxRead == -1 ) ? 999999l : maxRead;
		/* Retrieve truncated directory lists by specifying the base query plus
		   a marker. */
		LockCaches( );
		do
		{
			/* Get an XML list of directories and decode the directory
			   contents. */
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
			status = s3_SubmitS3Request( s3comm, "GET", headers, query,
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
		InsertInDirectoryCache( strdup( parentDir ), fileCounter,
								(const char**) dirArray );
		UnlockCaches( );
    }

    *nFiles    = fileCounter;
    *nameArray = dirArray;

    free( parentDir );
    free( (char*) prefix );

    return( status );
}



/**
 * Convert an OpenFlags structure to the value that is accepted by the open( )
 * function.
 * @param openFlags [in] Pointer to an OpenFlags structure.
 * @return Value for the open( ) function.
 */
int
ConvertOpenFlagsToValue(
	const struct OpenFlags *openFlags
	                   )
{
    #define SET_OFLAGS( flag ) if( openFlags->of_##flag ) oFlags |= O_##flag

	int oFlags = O_RDONLY;
	SET_OFLAGS( WRONLY );
	SET_OFLAGS( RDWR );
	SET_OFLAGS( CREAT );
	SET_OFLAGS( APPEND );
	SET_OFLAGS( EXCL );
	SET_OFLAGS( DIRECT );
	SET_OFLAGS( DIRECTORY );
	SET_OFLAGS( LARGEFILE );
	SET_OFLAGS( NOATIME );
	SET_OFLAGS( NONBLOCK );
	SET_OFLAGS( NDELAY );
	SET_OFLAGS( SYNC );
	SET_OFLAGS( TRUNC );
	SET_OFLAGS( NOCTTY );
	SET_OFLAGS( ASYNC );
	SET_OFLAGS( NOFOLLOW );

	return( oFlags );
}



/**
 * Create a file. The function assumes that the FUSE interface has already
 * determined that file access is allowed according to the open flags.
 * @param path [in] Path name of the file.
 * @param permissions [in] File permissions, if the file is to be created.
 * @return 0 on success, or \a -errno on failure.
 */
#if 0
int
S3Create(
	const char *path,
	mode_t     permissions
	     )
{
	struct S3FileInfo *fi;
	int               status;
	char              *localname;

	status = S3FileStat( path, &fi );
	if( status == 0 )
	{
		status = OpenCacheFile( path, fi->uid, fi->gid,
								fi->permissions, time( NULL ), &localname );
		if( status == 0 )
		{
			/* Delete the local file first. */
			LockCaches( );
			if( fi->openFlags.of_TRUNC || fi->openFlags.of_CREAT )
			{
				unlink( localname );
			}
			/* Create the file with the specified open and permissions flags. */
			status = creat( localname,
							permissions );
			UnlockCaches( );

			if( 0 <= status )
			{
				fi->localFd = status;
				/* Set access time to now. */
				fi->atime = time( NULL );
				status = 0;
			}
			free( localname );
		}
	}
	return( status );
}
#endif



char*
PrependHttpsToPath(
	const char *path
	             )
{
	const char *hostname;
	char       *url;
	size_t     toAlloc;

	hostname = GetS3HostNameByRegion( globalConfig.region,
									  globalConfig.bucketName );
	toAlloc = strlen( hostname ) + strlen( "https://" )
		      + strlen( path ) + sizeof( char );
	url = malloc( toAlloc );
	sprintf( url, "https://%s%s", hostname, path );
	free( (char*) hostname );

	return( url );
}



/**
 * Open a file. The function assumes that the FUSE interface has already
 * determined that file access is allowed according to the open flags.
 * @param path [in] Path name of the file.
 * @return 0 on success, or \a -errno on failure.
 */
int
S3Open(
	const char *path
	  )
{
	struct S3FileInfo *fi;
	int               status;
	struct S3FileInfo *parentFi;
	char              *parentDir;
	char              *url;

	printf( "s3Open %s\n", path );

	status = S3FileStat( path, &fi );
	if( status == 0 )
	{
		/* Prepare to cache the file. */
		if( fi->openFlags.of_RDONLY || fi->openFlags.of_RDWR
			|| fi->openFlags.of_APPEND )
		{
			parentDir = g_path_get_dirname( path );
			status = S3FileStat( parentDir, &parentFi );
			g_free( parentDir );
			if( status == 0 )
			{
				url = PrependHttpsToPath( path );
				status = CreateCachedFile( url, parentFi->uid, parentFi->gid,
										   parentFi->permissions,
										   fi->uid, fi->gid, fi->permissions,
										   fi->mtime );
				free( url );
				if( status == 0 )
				{

				}
			}
		}

#if 0
		if( status == 0 )
		{
			LockCaches( );
			if( fi->openFlags.of_TRUNC || fi->openFlags.of_CREAT )
			{
				unlink( localname );
			}
//			status = open( localname,
//						   ConvertOpenFlagsToValue( &fi->openFlags ) );
			UnlockCaches( );
			if( 0 <= status )
			{
				fi->localFd = status;
				/* Set access time to now. */
				fi->atime = time( NULL );
				status = 0;
			}
//			free( localname );
		}
#endif
	}

	return( status );
}



/**
 * Close an open file and signal to the caches that the file is ready for
 * synchronization.
 * @param path [in] File path.
 * @return 0 on success, or \a -errno on failure.
 */
int
S3Close(
	const char *path
	   )
{
	struct S3FileInfo *fi;
	int               success = -EIO;

	success = S3FileStat( path, &fi );
	if( success == 0 )
	{
		success = close( fi->localFd );
		fi->localFd = -1;
	}

	CloseCacheFile( path );

	return( success );
}



/**
 * Read data from an open file. If the file is not cached, the read stalls
 * until the entire file has been downloaded.
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
	char              *url;
	const char        *localname;
	char              *localpath;
	int               nBytes;

	printf( "s3ReadFile %s\n", path );

    status = S3FileStat( path, &fi );
    if( status == 0 )
    {
		if( fi->fileType == 'f' )
		{
			/* Queue file for download and wait until it is received. */
			url = PrependHttpsToPath( path );
			status = DownloadCacheFile( url );
			/* Then turn over the file read to the local read function now
			   that the file is stored locally. */
			if( status == 0 )
			{
				if( fi->localFd < 0 )
				{
					/* First access: open the file. */
					localname = GetLocalFilename( url );
					localpath = malloc( strlen( CACHE_FILES ) +
										strlen( localname ) + sizeof( char ) );
					strcpy( localpath, CACHE_FILES );
					strcat( localpath, localname );
					printf( "Attempting to open %s\n", localpath );
					/* TODO: change the open flags to those requested by the
					   open( ) function. */
					fi->localFd = open( localpath, O_RDONLY );
					free( localpath );
					free( (char*) localname );
				}
				nBytes = pread( fi->localFd, buf, maxSize, offset );
				if( 0 <= nBytes )
				{
					*actuallyRead = nBytes;
				}
			}
			free( url );
		}
		else
		{
			status = -ENOENT;
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
					status  = s3_SubmitS3Request( s3comm, "GET", headers, link,
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
    status  = s3_SubmitS3Request( s3comm, "PUT", headers, newName,
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
    const char        *parentDir;
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
    fi->permissions   = 0777;
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

    parentDir = GetParentDir( path );

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
    LockCaches( );
    status = s3_SubmitS3PutRequest( s3comm, headers, linkname,
									(void**) &response, &responseLength,
									(unsigned char*) path, pathLength );
    /* We already have the FileInfo structure, so because the file will be
       stat'ed as soon as we return, let's add it to the stat cache. Delete
       whatever might already be in the cache. */
    DeleteStatEntry( linkname );
    InsertCacheElement( linkname, fi, &DeleteS3FileInfoStructure );
    InvalidateDirectoryCacheElement( parentDir );
    UnlockCaches( );

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
    ShutdownDirectoryCache( );
    TruncateCache( 0 );
    /* Cleanup libxml. */
    xmlCleanupParser( );
#ifndef AUTOTEST
	/* Close the S3 communications library. */
	s3_close( s3comm );
#endif
	curl_global_cleanup( );

	DisconnectFromFileCache( );
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
 * may be faked. Everything is a key, so directories do not really exist,
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
    const char        *parentDir;
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
    parentDir = GetParentDir( cleanName );

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
    LockCaches( );
    status  = s3_SubmitS3Request( s3comm, "PUT", headers, secretFile,
								  (void**) &response, &responseLength );
    InvalidateDirectoryCacheElement( parentDir );
    /* Update the stat cache entry for the directory. */
    free( (char*) parentDir );
    free( secretFile );
    oldFi = SearchStatEntry( cleanName );
    if( oldFi != NULL )
    {
        memcpy( oldFi, &newFi, sizeof( struct S3FileInfo ) );
    }
    UnlockCaches( );
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
    const char        *parentDir;
    struct curl_slist *headers = NULL;
    char              *response;
    int               responseLength;
    int               status;

    cleanName = CleanPath( filename );
    parentDir = GetParentDir( cleanName );

    LockCaches( );
    status  = s3_SubmitS3Request( s3comm, "DELETE", headers, cleanName,
								  (void**) &response, &responseLength );
    InvalidateDirectoryCacheElement( parentDir );
    DeleteStatEntry( cleanName );
    UnlockCaches( );
    free( (char*) parentDir );
    free( (char*) cleanName );

    if( response != NULL )
    {
        free( response );
    }

    return( status );
}



/**
 * Determine whether a directory is empty, that is, that the directory contains
 * nothing but the "secret" file.
 * @param dirname [in] Name of the directory.
 * @return \a true if the directory is empty, or \a false otherwise.
 */
static bool
IsDirectoryEmpty(
    const char *dirname
		 )
{
    bool success;
    char **directory;
    int  nFiles;
    int  status;

    /* Read four files so that we can account for the secret file, the ".",
       the "..", and finally any file that makes the directory non-empty. */
    success = false;
    status = S3ReadDir( dirname, &directory, &nFiles, 4 );
    if( status == 0 )
    {
        /* We compare against two files instead of three (where above we
		   requested four), because the secret file is not returned. */
        if( nFiles <= 2 )
		{
			success = true;
		}
    }

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
    const char        *parentDir;
    char              *secretFile;
    struct S3FileInfo *fi;
    struct curl_slist *headers = NULL;
    char              *response;
    int               responseLength;
    int               status;

    cleanName = CleanPath( dirname );
    parentDir = GetParentDir( dirname );

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
					LockCaches( );
					status = s3_SubmitS3Request( s3comm, "DELETE", headers,
												 cleanName, (void**) &response,
												 &responseLength );
					InvalidateDirectoryCacheElement( parentDir );
					DeleteStatEntry( cleanName );
					UnlockCaches( );
					free( (char*) parentDir );
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
        LockCaches( );
		fi->mtime       = now;
		fi->permissions = mode;
		
		status = UpdateAmzHeaders( file, fi, NULL );
		UnlockCaches( );
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
        LockCaches( );
		fi->mtime                     = now;
		if( (int) uid != -1 ) fi->uid = uid;
		if( (int) gid != -1 ) fi->gid = gid;

		status = UpdateAmzHeaders( file, fi, NULL );
		UnlockCaches( );
    }
    return( status );
}


