/**
 * \file grant.c
 * \brief Privileged operations for performing chown on files.
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
#include <stdlib.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include <malloc.h>
#include <assert.h>
#include <ctype.h>
#include "socket.h"
#include <sys/stat.h>
#include <fcntl.h>
#include "aws-s3fs.h"
#include "filecache.h"


#define COMPARESTRINGS( x, y ) \
    ( ( ( strlen( y ) < length ) && \
		( strncasecmp( x, y, strlen( y ) ) == 0 ) ) ? 0 : 1 )


/**
 * Extract the next integer parameter from a string whose parameters are
 * separated by ':'.
 * @param parameterlist [in] Parameter string.
 * @param result [out] Value of the integer parameter.
 * @return Number of bytes processed in the parameter string.
 */
STATIC int
GetIntParameter(
	const char *parameterlist,
	int        *result
	            )
{
	int  toReturn = 0;
	int  pos;
	char ch;
	int  valueLength;

	/* Skip non-alphanumeric characters. */
	pos = 0;
	while( ( ( ch = parameterlist[ pos ] ) != '\0' ) && ( ! isalnum( ch ) ) )
	{
		pos++;
	}
	/* Extract at most five digits. */
	valueLength = 0;
	while( isdigit( ch = parameterlist[ pos ] ) && ( valueLength < 5 ) )
	{
		toReturn = toReturn * 10 + ( ch - '0' );
		pos++;
	}
	/* Now expect either ':' or '\0'. */
	if( ( ch != ':' ) && ( ch != '\0' ) )
	{
		toReturn = -1;
	}

	*result = toReturn;
	return( pos );
}



/**
 * Extract the next six-character file name from a string whose parameters are
 * separated by ':'.
 * @param parameterlist [in] Parameter string.
 * @param filename [out] Null-terminated file name.
 * @return Number of bytes processed in the parameter string.
 */
STATIC int
GetFileParameter(
	const char *parameterlist,
	char       filename[ 7 ]
	             )
{
	int  pos = 0;
	char ch;
	int  filenamePos;

	/* Skip whitespace and non-alphanumeric characters. */
	while( ( ( ch = parameterlist[ pos ] ) != '\0' )
		   && ( isspace( ch ) || ispunct(ch ) ) )
	{
		pos++;
	}
	filenamePos = 0;
	/* Get at most six characters of a file which must contain only alphanumeric
	   characters. */
	while( ( ( ch = parameterlist[ pos++ ] ) != '\0' ) && isalnum( ch )
		   && ( filenamePos < 6 ) )
	{
		filename[ filenamePos++ ] = ch;
	}
	filename[ filenamePos ] = '\0';

	return( pos );
}



/**
 * Verify that a file name consists of exactly six characters in the set
 * [0-9a-zA-Z].
 * @param filename [in] Filename to verify.
 * @return \a true if the file name is valid, or \a false otherwise.
 */
STATIC bool
VerifyFilename( char filename[ 7 ] )
{
	int  pos;
	bool valid = true;

	for( pos = 0; pos < 6; pos++ )
	{
		if( ! isalnum( filename[ pos ] ) )
		{
			valid = false;
		}
	}
	if( filename[ 6 ] != '\0' )
	{
		valid = false;
	}

	return( valid );
}



/**
 * Move a file from the in-progress cache directory into the shared cache
 * folder.
 * @param parameters [in] String with six-character parent directory name
 * and six-character filename, separated by '/'.
 * @return Nothing.
 */
static void
GrantPublish( const char *parameters )
{
	char directory[ 7 ];
	char filename[ 7 ];
	int  pos;
	bool valid;
	char *filepath;
	char *dirpath;
	char *destpath;

	/* Get the directory. */
	pos = GetFileParameter( parameters, directory );
	/* Get the file. */
	GetFileParameter( &parameters[ pos ], filename );

	/* Sanity check that the directory and the filename are both 6-letter
	   names consisting of [0-9a-zA-Z]. */
	valid = VerifyFilename( directory );
	if( valid )
	{
		valid = VerifyFilename( filename );
		if( valid )
		{
			/* The directory may already exist in the shared cache directory,
			   so it cannot simply be moved out of the in-progress directory.
			   Instead, attempt to move it ignoring any errors that are caused
			   because it already exists.  Then remove it from the in-progress
			   directory ignoring any errors that are caused because it is
			   not empty.  (It ought to be empty because no files are supposed
			   to be stored in it.) */

			/* Move the directory in the CACHE_DIR/directory. */
			destpath = malloc( strlen( CACHE_FILES ) + 7 * sizeof( char ) );
			strcpy( destpath, CACHE_FILES );
			strcat( destpath, directory );
			dirpath = malloc( strlen( CACHE_INPROGRESS ) + 7 * sizeof( char ) );
			strcpy( dirpath, CACHE_INPROGRESS );
			strcat( dirpath, directory );
			rename( dirpath, destpath );
			/* Remove the directory from the in-progress directory. */
			rmdir( dirpath );
			free( dirpath );

			/* Move the file to CACHE_DIR/directory/filename. */
			filepath = malloc( strlen( CACHE_INPROGRESS )
							   + 7 * sizeof( char ) );
			destpath = malloc( strlen( CACHE_FILES ) + 6 * sizeof( char )
							   + 8 * sizeof( char ) );
			strcpy( filepath, CACHE_INPROGRESS );
			strcat( filepath, filename );
			strcpy( destpath, CACHE_FILES );
			strcat( destpath, directory );
			strcat( destpath, "/" );
			strcat( destpath, filename );
			rename( filepath, destpath );
			free( filepath );
			free( destpath );
		}
	}
}



/**
 * Change ownership of a file in the shared cache directory.
 * @param parameters [in] uid, gid, and filename separated by ':'.
 * @return Nothing.
 */
static void
GrantChown( const char *parameters )
{
	char filename[ 14 ];
	int  uid;
	int  gid;
	char *filepath;
	bool valid;
	bool hasDirectory = false;
	int  pos;

	/* Get the uid. */
	pos = GetIntParameter( parameters, &uid );
	if( uid < 0 )
	{
		return;
	}
	/* Get the gid. */
	pos += GetIntParameter( &parameters[ pos ], &gid );
	if( gid < 0 )
	{
		return;
	}

	/* Get the filename. */
	pos += GetFileParameter( &parameters[ pos ], filename );
	/* If the filename includes a parent directory, add the filename. */
	if( parameters[ pos - 1 ] == '/' )
	{
		hasDirectory = true;
		filename[ 6 ] = '\0';
		pos += GetFileParameter( &parameters[ pos ], &filename[ 7 ] );
		filename[ 13 ] = '\0';
	}

	/* Sanity check that both directory and filename are 6-letter names
	   consisting of [0-9a-zA-Z]. */
	valid = VerifyFilename( filename );
	if( valid )
	{
		if( hasDirectory )
		{
			filename[ 6 ] = '/';
			valid = VerifyFilename( &filename[ 7 ] );
		}
		if( valid )
		{
			filepath = malloc( strlen( CACHE_FILES ) + strlen( filename )
							   + sizeof( char ) );
			strcpy( filepath, CACHE_FILES );
			strcat( filepath, filename );
			chown( filepath, uid, gid );
		}
	}
}



/**
 * Copy a chunk from a file into the in-progress directory, preparing it for
 * an S3 multipart upload.  The chunk offset and size is determined by the
 * part number and the file size.
 * @param parameters [in] Part number and file path relative to the shared
 *        cache directory, separated by ':'.
 * @return Nothing.
 */
STATIC void
CreateFileChunk( const char *parameters )
{
	int       part;
	char      filename[ 14 ];
	char      *srcfilepath;
	char      *destfilepath;
	bool      valid;
	bool      hasDirectory = false;
	int       pos;
	const int copyChunkSize = 262144;

	struct stat   fileStat;
	int           status;
	long long int filesize;
	int           parts;
	int           partSize;
	long long int offset;
	int           src;
	int           dest;
	int           relativeOffset;
	int           index;
	int           bytesCopied;
	int           chunkSize;
	unsigned char *chunk;
	int           nBytes;

	/* Get the part number. */
	pos = GetIntParameter( parameters, &part );
	/* Sanity check the part number. */
	if( ( part < 0 ) || ( 10000 < part ) )
	{
		return;
	}

	/* Get the first filename. */
	pos += GetFileParameter( &parameters[ pos ], filename );
	/* If the filename includes a parent directory, add the filename. */
	if( parameters[ pos - 1 ] == '/' )
	{
		hasDirectory = true;
		filename[ 6 ] = '\0';
		pos += GetFileParameter( &parameters[ pos ], &filename[ 7 ] );
		filename[ 13 ] = '\0';
	}
	/* Sanity check that the directory and the filename are both 6-letter
	   names consisting of [0-9a-zA-Z]. */
	valid = VerifyFilename( filename );
	if( valid )
	{
		if( hasDirectory )
		{
			filename[ 6 ] = '/';
			valid = VerifyFilename( &filename[ 7 ] );
		}
		if( valid )
		{
			/* Build the full source file path. */
			srcfilepath = malloc( strlen( CACHE_FILES ) + strlen( filename )
							   + sizeof( char ) );
			strcpy( srcfilepath, CACHE_FILES );
			strcat( srcfilepath, filename );

			/* Get the second filename and sanity check it. */
			pos += GetFileParameter( &parameters[ pos ], filename );
			valid = VerifyFilename( filename );
			if( valid )
			{
				/* Build the full destination file path. */
				destfilepath = malloc( strlen( CACHE_INPROGRESS )
									   + strlen( filename ) + sizeof( char ) );
				strcpy( destfilepath, CACHE_INPROGRESS );
				strcat( destfilepath, filename );

				/* Copy the file chunk in, well, chunks. */
				chunk = malloc( copyChunkSize );
				if( chunk != NULL )
				{
					/* Calculate the number of bytes to copy and the offset
					   into the source file. */
					status = stat( srcfilepath, &fileStat );
					if( 0 <= status )
					{
						partSize = PREFERRED_CHUNK_SIZE * 1024 * 1024;
						/* The last part size is the remainder of the
						   division. */
						filesize = fileStat.st_size;
						parts = NumberOfMultiparts( filesize );
						if( part == parts )
						{
							partSize = filesize % partSize;
						}
						offset = (long long int) (part - 1 )
							* PREFERRED_CHUNK_SIZE * 1024 * 1024;
						/* Copy small chunks. */
						src  = open( srcfilepath, O_RDONLY | O_LARGEFILE );
						dest = open( destfilepath, O_TRUNC | O_WRONLY );
						if( ( 0 <= src ) && ( 0 <= dest ) )
						{
							index       = 0;
							bytesCopied = 0;
							do
							{
								/* Determine size of the chunk to read. */
								chunkSize = copyChunkSize;
								/* Determine offset into the file. */
								relativeOffset = index * chunkSize;
								if( chunkSize + relativeOffset > partSize )
								{
									chunkSize = partSize - relativeOffset;
								}
								/* Copy the chunk. */
								nBytes = pread( src, chunk, chunkSize,
												offset + relativeOffset );
								if( 0 < nBytes )
								{
									write( dest, chunk, nBytes );
									bytesCopied = bytesCopied + nBytes;
								}
								index++;
							} while( ( 0 < nBytes ) &&
									 ( bytesCopied < partSize ) );
						}
					}
				}
			}
		}
	}
}



/**
 * Initialize the Permission Grant module.
 * @param childPid [in] pid of the download queue; the permission grant module
 *        will accept socket communication only from a process with this pid.
 * @param socketHandle [in] Socket handle for communicating with the download
 *        queue.
 * @return Nothing.
 */
void
InitializePermissionsGrant(
	pid_t childPid,
	int   socketHandle
	                       )
{
	struct ucred credentials;
	size_t       length;
	char         request[ 100 ];
	int          fd; /* unused */


	while( 1 )
	{
		/* Wait for a request from the download queue. */
		length = SocketReceiveDatagramFromClient( socketHandle, request,
												  sizeof( request ),
												  &credentials, &fd );

		/* Validate the sender: it must have the pid of the download queue. */
		if( credentials.pid == childPid )
		{
			/* Process the request. */
			if( COMPARESTRINGS( request, "CHOWN " ) == 0 )
			{
				GrantChown( &request[ 6 ] );
			}
			else if( COMPARESTRINGS( request, "PUBLISH " ) == 0 )
			{
				GrantPublish( &request[ 8 ] );
			}
			else if( COMPARESTRINGS( request, "CHUNK " ) == 0 )
			{
				CreateFileChunk( &request[ 6 ] );
			}
			else if( COMPARESTRINGS( request, "DELETE " ) == 0 )
			{
			}
			else
			{
			}

			/* Acknowledge the receipt. */
			SocketSendDatagramToClient( socketHandle, "ACK", 4, -1 );
		}
		/* Ignore the message if it was not sent from the download
		   queue. */
		else
		{
			fprintf( stderr, "Warning: received socket message from an "
					 "unauthorized source (pid = %d).\n", credentials.pid );
			SocketSendDatagramToClient( socketHandle,
										"Not authorized", 15, -1 );
		}
	}
}


