/**
 * \file fuseif.c
 * \brief Interface to the FUSE .
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


#define FUSE_USE_VERSION 26

#include <config.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdbool.h>
#include <unistd.h>
#include <malloc.h>
#include <string.h>
#include <errno.h>
#include <pthread.h>
#include <fuse/fuse.h>
/*#include <linux/major.h>*/
#include "aws-s3fs.h"


/* Stub */
struct ThreadsafeLogging *logger = NULL;



static int s3fs_getattr( const char  *path, struct stat *stat );
static int s3fs_open( const char *, struct fuse_file_info* );

/*
int s3fs_readlink(const char *, char *, size_t);
int s3fs_getdir( const char *, char *, size_t);
int s3fs_mknod(const char *, mode_t, dev_t);
int s3fs_mkdir(const char *, mode_t);
int s3fs_unlink(const char *);
int s3fs_rmdir(const char *);
int s3fs_symlink(const char *, const char *);
int s3fs_rename(const char *, const char *);
int s3fs_link(const char *, const char *);
int s3fs_chmod(const char *, mode_t);
int s3fs_chown(const char *, uid_t, gid_t);
int s3fs_truncate(const char *, off_t);
int s3fs_utime(const char *, struct utimbuf *);
int s3fs_read(const char *, char *, size_t, off_t, struct fuse_file_info *);
int s3fs_write(const char *, const char *, size_t, off_t, struct fuse_file_info *);
int s3fs_statfs(const char *, struct statvfs *);
int s3fs_flush(const char *, struct fuse_file_info *);
int s3fs_release(const char *, struct fuse_file_info *);
int s3fs_fsync(const char *, int, struct fuse_file_info *);
int s3fs_setxattr(const char *, const char *, const char *, size_t, int);
int s3fs_getxattr(const char *, const char *, char *, size_t);
int s3fs_listxattr(const char *, char *, size_t);
int s3fs_removexattr(const char *, const char *);
int s3fs_opendir(const char *, struct fuse_file_info *);
int s3fs_readdir(const char *, void *, fuse_fill_dir_t, off_t, struct fuse_file_info *);
int s3fs_releasedir(const char *, struct fuse_file_info *);
int s3fs_fsyncdir(const char *, int, struct fuse_file_info *);
void *s3fs_init(struct fuse_conn_info *conn);
void s3fs_destroy(void *);
int s3fs_access(const char *, int);
int s3fs_create(const char *, mode_t, struct fuse_file_info *);
int s3fs_ftruncate(const char *, off_t, struct fuse_file_info *);
int s3fs_fgetattr(const char *, struct stat *, struct fuse_file_info *);
int s3fs_lock(const char *, struct fuse_file_info *, int cmd, struct flock *);
int s3fs_utimens(const char *, const struct timespec tv[2]);
int s3fs_bmap(const char *, size_t blocksize, uint64_t *idx);
int s3fs_ioctl(const char *, int cmd, void *arg, struct fuse_file_info *, unsigned int flags, void *data);
int s3fs_poll(const char *, struct fuse_file_info *, struct fuse_pollhandle *ph, unsigned *reventsp);
*/

struct fuse_operations s3fsOperations =
{
    .getattr     = s3fs_getattr,
    /*
    .readlink    = s3fs_readlink,
    .mknod       = s3fs_mknod,
    .mkdir       = s3fs_mkdir,
    .unlink      = s3fs_unlink,
    .rmdir       = s3fs_rmdir,
    .symlink     = s3fs_symlink,
    .rename      = s3fs_rename,
    .link        = s3fs_link,
    .chmod       = s3fs_chmod,
    .chown       = s3fs_chown,
    .truncate    = s3fs_truncate,
    .utime       = s3fs_utime,
    */
    .open        = s3fs_open,
    /*
    .read        = s3fs_read,
    .write       = s3fs_write,
    .statfs      = s3fs_statfs,
    .flush       = s3fs_flush,
    .release     = s3fs_release,
    .fsync       = s3fs_fsync,
    .setxattr    = s3fs_setxattr,
    .getxattr    = s3fs_getxattr,
    .listxattr   = s3fs_listxattr,
    .removexattr = s3fs_removexattr,
    .opendir     = s3fs_opendir,
    .readdir     = s3fs_readdir,
    .releasedir  = s3fs_releasedir,
    .fsyncdir    = s3fs_syncdir,
    .init        = s3fs_init,
    .destroy     = s3fs_destroy,
    .access      = s3fs_access,
    .create      = s3fs_create,
    .ftruncate   = s3fs_ftruncate,
    .fgetattr    = s3fs_fgetattr,
    .lock        = s3fs_lock,
    .utimens     = s3fs_utimens,
    .bmap        = s3fs_bmap,
    .flag_nullpath_ok = 0,
    .flag_reserved    = 0,
    .ioctl       = s3fs_ioctl,
    .poll        = s3fs_poll
    */
};



struct OpenFlags
{
    bool of_RDONLY    : 1;
    bool of_WRONLY    : 1;
    bool of_RDWR      : 1;
    bool of_CREAT     : 1;
    bool of_APPEND    : 1;
    bool of_EXCL      : 1;
    bool of_DIRECT    : 1;
    bool of_DIRECTORY : 1;
    bool of_LARGEFILE : 1;
    bool of_NOATIME   : 1;
    bool of_NONBLOCK  : 1;
    bool of_NDELAY    : 1;
    bool of_SYNC      : 1;
    bool of_TRUNC     : 1;
    bool of_NOCTTY    : 1;
    bool of_ASYNC     : 1;
    bool of_NOFOLLOW  : 1;
};


struct s3FileInfo
{
    uid_t            uid;
    gid_t            gid;
    unsigned int     permissions;
    char             fileType;
    bool             exeUid : 1;
    bool             exeGid : 1;
    bool             sticky : 1;
    off_t            size;
    time_t           atime;
    time_t           mtime;
    time_t           ctime;
};


pthread_mutex_t fileDescriptorsMutex = PTHREAD_MUTEX_INITIALIZER;
struct s3FileInfo *fileDescriptors[ MAX_FILE_DESCRIPTORS ];


struct s3FileInfo *S3FileStat( const char *path, int *status )
{
    /* Stub */
    return NULL;
}




/**
 * Determine if the current user has read, write, or execute permission to the
 * file specified by the fileInfo structure.
 * @param fi [in] fileInfo structure for the file.
 * @param permissionFlag [in] Permission flags; 0 to 7 for all combinations of
 *        [r w x] ~ [4 2 1].
 * @return \a true if the user has permission; \a false otherwise.
 */
bool
IsUserAccessible(
    const struct s3FileInfo *fi,
    unsigned int            permissionFlag
		 )
{
    int   permissions   = fi->permissions;
    bool  hasPermission = true;
    uid_t uid;
    gid_t gid;

    /* Get uid and gid for the user who mounted the fs. */
    uid = getuid( );
    gid = getgid( );

    /* Try world permission first. */
    if( ( permissions & permissionFlag ) == 0 )
    {
        /* Try group permissions. */
        if( ( ( permissions & ( permissionFlag << 3 ) ) == 0 ) ||
	      ( gid != fi->gid ) )
	{
	    /* Try user permissions. */
	    if( ( ( permissions & ( permissionFlag << 6 ) ) == 0 ) ||
		  ( uid != fi->uid ) )
	    {
	        /* Root always has access. */
	        if( ( uid != 0 ) && ( gid != 0 ) )
		{
		    hasPermission = false;
		}
	    }
        }
    }
    return( hasPermission );
}



/**
 * Determine whether the current user has read access to the file with the
 * specified FileInfo.
 * @param fi [in] FileInfo for the file.
 * @return \a true if the user has read access, \a false otherwise.
 */
bool
IsReadable( const struct s3FileInfo *fi )
{
    return( IsUserAccessible( fi, 04 ) );
}



/**
 * Determine whether the current user has write access to the file with the
 * specified FileInfo.
 * @param fi [in] FileInfo for the file.
 * @return \a true if the user has write access, \a false otherwise.
 */
bool
IsWriteable( const struct s3FileInfo *fi )
{
    return( IsUserAccessible( fi, 02 ) );
}



void
SetOpenFlags( struct OpenFlags *openFlags, int flags )
{
    openFlags->of_RDONLY    = ( flags & O_RDONLY ) ? true : false;
    openFlags->of_WRONLY    = ( flags & O_WRONLY ) ? true : false;
    openFlags->of_RDWR      = ( flags & O_RDWR ) ? true : false;
    openFlags->of_CREAT     = ( flags & O_CREAT ) ? true : false;
    openFlags->of_APPEND    = ( flags & O_APPEND ) ? true : false;
    openFlags->of_EXCL      = ( flags & O_EXCL ) ? true : false;
    openFlags->of_DIRECT    = ( flags & O_DIRECT ) ? true : false;
    openFlags->of_DIRECTORY = ( flags & O_DIRECTORY ) ? true : false;
    openFlags->of_LARGEFILE = ( flags & O_LARGEFILE ) ? true : false;
    openFlags->of_NOATIME   = ( flags & O_NOATIME ) ? true : false;
    openFlags->of_NONBLOCK  = ( flags & O_NONBLOCK ) ? true : false;
    openFlags->of_NDELAY    = ( flags & O_NDELAY ) ? true : false;
    openFlags->of_SYNC      = ( flags & O_SYNC ) ? true : false;
    openFlags->of_TRUNC     = ( flags & O_TRUNC ) ? true : false;
    /* Not supported in S3FS, because they don't make sense on S3. */
    openFlags->of_NOCTTY    = ( flags & O_NOCTTY ) ? true : false;
    openFlags->of_ASYNC     = ( flags & O_ASYNC ) ? true : false;
    openFlags->of_NOFOLLOW  = ( flags & O_NOFOLLOW ) ? true : false;
}



/**
 * Extract the first directory component of a path; for example, if the
 * specified path is "component1/component2/filename", the function extracts
 * the string "component1". The function eventually returns the filename
 * and NULL as the path is depleted of components.
 * @param origPath [in] Pointer to the first character in the path.
 * @param component [out] String with the first component.
 * @param length [out] Length of the component that was extracted, or 0 if
 *        there are no more components. Ignored if NULL.
 * @return Offset to the beginning of the next component, relative
 *         to the beginning of origPath.
 */
static int
GetNextPathComponent(
    const char *origPath,
    char       **component,
    int        *length
		     )
{
    int  endPos;
    bool escaped;
    char ch;
    int  componentLength;

    /* The ugly while construct means: scan from the current position until
       either an unescaped '/' is found or until the end of the string is
       encountered. */
    endPos = 0;
    escaped = false;
    while( ( ( ch = origPath[ endPos++ ] ) != '\0' )
	   && ( ! ( ( ch == '/' ) && ( bool_equal( escaped, true ) ) ) ) )
    {
        if( ch == '\\' )
	{
	    escaped = ! escaped;
	}
	else
	{
	    escaped = false;
	}
    }
    componentLength = endPos;
    /* Skip past any additional '/' characters in case the path looks like:
       path/component1//component2..., which is valid. */
    while( ( ch = origPath[ endPos ] ) == '/' )
    {
        endPos++;
    }

    /* Copy the component into a new string. */
    if( 1 < componentLength )
    {
        *component = malloc( componentLength + sizeof( char ) );
	strncpy( *component, origPath, componentLength );
	(*component)[ componentLength ] = '\0';
	if( length != NULL )
	{
	    *length = componentLength;
	}
	return( endPos );
    }
    /* No additional components found. */
    else
    {
        *component = NULL;
        return( 0 );
    }
}



/**
 * Return the directory component of a path that contains both directories
 * and filename, that is, anything but the "basename" of a file.
 * @param path [in] Full path of the file.
 * @return Directory component or NULL if only the filename was specified.
 */
static char*
GetPathPrefix(
    const char *path
	      )
{
    int  lastPos;
    char *pathPrefix;

    lastPos = strlen( path ) - 1;
    while( 0 <= --lastPos )
    {
        /* A '/' marks the last directory unless it the character is escaped. */
        if( path[ lastPos ] == '/' )
	{
	    if( ( lastPos != 0 ) && ( path[ lastPos ] != '\\' ) )
	    {
	        pathPrefix = malloc( lastPos + sizeof( char ) );
		strncpy( pathPrefix, path, lastPos );
		pathPrefix[ lastPos ] = '\0';
		return( pathPrefix );
	    }
	}
    }

    return( NULL );
}



/**
 * Get file attributes; similar to stat( ). The 'std_dev' and 'st_blksize'
 * fields are ignored. The 'st_ino' field is ignored unless the 'use_ino'
 * mount option is given.
 * @param path [in] The file whose attributes should be read.
 * @param stat [out] Stat structure to be filled with the file attributes.
 * @return 0 on success, or -errno otherwise.
 */
static int
s3fs_getattr(
    const char  *path,
    struct stat *stat
		 )
{
    char *pathPrefix;
    char *pathComponent;
    int  position;
    char *accumulatedPath;
    int  status = 0;
    struct s3FileInfo *fileInfo;

    if( ( path == NULL ) || ( strcmp( path, "" ) == 0 ) )
    {
        status = -ENOENT;
	return( status );
    }

    /* All of the file's parent directories must be searchable (+x) in order
       to stat the file. Scan through the directories one at a time. */
    pathPrefix = GetPathPrefix( path );
    if( pathPrefix != NULL )
    {
        pathComponent        = NULL;
        position             = 0;
	accumulatedPath      = malloc( sizeof( char ) );
	accumulatedPath[ 0 ] = '\0';
	do
	{
	    if( pathComponent != NULL )
	    {
		free( pathComponent );
	    }
	    position += GetNextPathComponent( &path[ position ],
					      &pathComponent, NULL );
	    if( pathComponent != NULL )
	    {
	        /* Grow a path from the path components. */
	        accumulatedPath = realloc( accumulatedPath,
					   strlen( accumulatedPath ) +
					   sizeof( char ) /* for: '/' */ +
					   strlen( pathComponent ) +
					   sizeof( char ) /* for: '\0' */ );
		strcat( accumulatedPath, "/" );
		strcat( accumulatedPath, pathComponent );
		/* Examine the path at its current depth. */
		fileInfo = S3FileStat( accumulatedPath, &status );
		if( status == 0 )
		{
		    /* If any component of the path prefix is not a
		       directory, the error is ENOTDIR. */
		    if( fileInfo->fileType != 'd' )
		    {
		        status = -ENOTDIR;
			break;
		    }
		    /* Check permissions. The directory must be searchable
		       by the user's gid and uid.  */
		    if( ! IsUserAccessible( fileInfo, 01 ) )
		    {
		        status = -ENOTDIR;
			break;
		    }
		}
		else
		{
		    /* If any component of the path does not exist, the error
		       is ENOENT. */
		    status = -ENOTDIR;
		    break;
		}
	    }
	} while( pathComponent != NULL );

	/* Free all temporarily allocated memory. */
	free( pathPrefix );
	if( pathComponent != NULL )
	{
	    free( pathComponent );
	}
	if( strlen( accumulatedPath ) != 0 )
	{
	    free( accumulatedPath );
	}
    }

    /* Examine the full path where the file itself may have any permission. */
    fileInfo = S3FileStat( path, &status );
    if( status == 0 )
    {
        /* Update the stat structure with file information. */
        memset( stat, 0, sizeof( struct stat ) );
	stat->st_mode |= ( fileInfo->fileType == 'd' ? S_IFDIR : 0 );
	stat->st_mode |= ( fileInfo->fileType == 'f' ? S_IFREG : 0 );
	stat->st_mode |= ( fileInfo->fileType == 'l' ? S_IFLNK : 0 );
	stat->st_mode |= fileInfo->permissions;
	stat->st_uid  =  fileInfo->uid;
	stat->st_gid  =  fileInfo->gid;
	stat->st_size =  fileInfo->size;
	stat->st_mode |= ( fileInfo->exeGid ? S_ISGID : 0 );
	stat->st_mode |= ( fileInfo->exeUid ? S_ISUID : 0 );
	stat->st_mode |= ( fileInfo->sticky ? S_ISVTX : 0 );
	memcpy( &stat->st_atime, &fileInfo->atime, sizeof( time_t ) );
	memcpy( &stat->st_mtime, &fileInfo->mtime, sizeof( time_t ) );
	memcpy( &stat->st_ctime, &fileInfo->ctime, sizeof( time_t ) );
    }

    return( status );
}



/**
 * The "open" function doesn't create or read data; it merely checks whether
 * the specified operation is permitted. No creation or truncation flags
 * (O_CREAT, O_EXCL, O_TRUNC) will be passed to open( ), except.....
 * @param path [in] Relative file path.
 * @param fi [in/out] FUSE file info structure.
 * @return 0 on success, or \a -errno on failure.
*/
static int
s3fs_open(
    const char            *path,
    struct fuse_file_info *fi
	  )
{
    int               status    = 0;
    int               fh        = 0;
    struct s3FileInfo *fileInfo;
    struct OpenFlags  openFlags;

    /* If no filename is provided, return immediately. */
    if( ( path == NULL ) || ( strcmp( path, "" ) == 0 ) )
    {
        status = -ENOENT;
    }

    /* Allocate a file handle. */
    do
    {
        /* A NULL entry indicates an empty slot, which can be allocated. */
        pthread_mutex_lock( &fileDescriptorsMutex );
        if( fileDescriptors[ fh ] == NULL )
	{
	    status = -EACCES;

	    /* The file stat cache provides information about the file. */
	    fileInfo = S3FileStat( path, &status );
	    if( status != 0 )
	    {
	        status = -EACCES;
	    }
	    else
	    {
	        fileDescriptors[ fh ] = fileInfo;
		Syslog( logger, log_DEBUG, "File handle %d allocated\n", fh );
	    /* http://sourceforge.net/apps/mediawiki/fuse/index.php?title=Fuse_file_info */
		SetOpenFlags( &openFlags, fi->flags );

		/* O_WRONLY is allowed if the file exists and has write
		   permissions. (If the file doesn't exist, the O_CREAT flag
		   must also be set. However, this flag isn't passed to this
		   function.)
		   See if the file exists and has write permissions. */
		if( openFlags.of_WRONLY )
		{
		    if( IsWriteable( fileInfo ) )
		    {
			status = 0;
		    }
		}
		/* For O_RDONLY, the file must have read permissions. For
		   O_RDWR and O_APPEND, the file must have both read and write
		   permissions. */
		else if( openFlags.of_WRONLY || openFlags.of_RDWR
			 || openFlags.of_APPEND )
		{
		    /* Todo: if O_RDWR and O_TRUNC are set, the file will be
		       created if necessary. But if O_TRUNC is not passed to
		       this function, how do we determine whether the file may
		       be opened? (This question is answered in FUSE v.26.) */
		    if( IsReadable( fileInfo ) )
		    {
		        status = 0;
		    }
		    if( ( openFlags.of_RDWR || openFlags.of_APPEND )
			&& ( ! IsWriteable( fileInfo ) ) )
		    {
		        status = -EACCES;
		    }
		}

	    }

	    if( status != 0 )
	    {
	         /* The file info structure is released when it expires from
		 the stat cache. */
		fileDescriptors[ fh ] = NULL;
		fh = -1;
	    }

	    pthread_mutex_unlock( &fileDescriptorsMutex );
	    break;
	}
    } while ( ++fh < MAX_FILE_DESCRIPTORS );

    /* All file descriptors in use. */
    if( fh == MAX_FILE_DESCRIPTORS )
    {
	fh = -1;
        status = -ENFILE;
	Syslog( logger, log_INFO, "All file handles in use\n" );
    }

    fi->fh = fh;
    return status;
}












#if 0
int OpenFileForWriting( int fh, const char *path, struct OpenFlags *openFlags )
{
    /* File may be opened for writing if it exists on S3 and has write
       permissions (ownership and access permissions apply). */

}



int OpenFileForReading( int fh, const char *path, struct OpenFlags *openFlags )
{
    /* File may be opened for reading if exists on S3 and has read permissions
       (ownership and access permissions apply). */

}
#endif
