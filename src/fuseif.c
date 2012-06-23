/**
 * \file fuseif.c
 * \brief Interface to FUSE.
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
#include <sys/types.h>
#include <sys/stat.h>
#include <ctype.h>
#include <fcntl.h>
#include <stdbool.h>
#include <unistd.h>
#include <malloc.h>
#include <string.h>
#include <errno.h>
#include <pthread.h>
#include <fuse/fuse.h>
#include "aws-s3fs.h"
#include "fuseif.h"
#include "s3if.h"


#define MAX_S3_FILE_DESCRIPTORS ( ( MAX_FILE_DESCRIPTORS ) + 100 )



static int s3fs_getattr( const char *path, struct stat *stat );
static int s3fs_open( const char*, struct fuse_file_info* );
static int s3fs_opendir( const char*, struct fuse_file_info* );
static int s3fs_readdir( const char*, void*, fuse_fill_dir_t,
			 off_t, struct fuse_file_info* );
static int s3fs_releasedir( const char*, struct fuse_file_info* );
static int s3fs_access( const char*, int );
static int s3fs_read( const char*, char*, size_t, off_t,
		      struct fuse_file_info* );
static int s3fs_fgetattr( const char*, struct stat*, struct fuse_file_info* );
static int s3fs_flush( const char*, struct fuse_file_info* );
static int s3fs_release( const char*, struct fuse_file_info* );

static int s3fs_symlink( const char*, const char* );
static int s3fs_readlink( const char*, char*, size_t );
static int s3fs_utimens( const char *file, const struct timespec tv[ 2 ] );
static int s3fs_mkdir( const char*, mode_t );
static int s3fs_unlink( const char *file );
static int s3fs_rmdir( const char* );
static void s3fs_destroy( void* );
static int s3fs_chmod( const char*, mode_t );
static int s3fs_chown( const char*, uid_t , gid_t );
//static void *s3fs_init( struct fuse_conn_info *conn );



/*
int s3fs_rename(const char *, const char *);
int s3fs_link(const char *, const char *);
int s3fs_truncate(const char *, off_t);
int s3fs_write(const char *, const char *, size_t, off_t, struct fuse_file_info *);
int s3fs_statfs(const char *, struct statvfs *);
int s3fs_fsync(const char *, int, struct fuse_file_info *);
int s3fs_setxattr(const char *, const char *, const char *, size_t, int);
int s3fs_getxattr(const char *, const char *, char *, size_t);
int s3fs_listxattr(const char *, char *, size_t);
int s3fs_removexattr(const char *, const char *);
int s3fs_fsyncdir(const char *, int, struct fuse_file_info *);
int s3fs_create(const char *, mode_t, struct fuse_file_info *);
int s3fs_ftruncate(const char *, off_t, struct fuse_file_info *);
int s3fs_lock(const char *, struct fuse_file_info *, int cmd, struct flock *);
int s3fs_bmap(const char *, size_t blocksize, uint64_t *idx);
int s3fs_ioctl(const char *, int cmd, void *arg, struct fuse_file_info *, unsigned int flags, void *data);
int s3fs_poll(const char *, struct fuse_file_info *, struct fuse_pollhandle *ph, unsigned *reventsp);
*/

struct fuse_operations s3fsOperations =
{
    .getattr     = s3fs_getattr,
    .readlink    = s3fs_readlink,
    .mkdir       = s3fs_mkdir,
    .unlink      = s3fs_unlink,
    .rmdir       = s3fs_rmdir,
    .symlink     = s3fs_symlink,
    /*
    .rename      = s3fs_rename,
    .link        = s3fs_link,
    */
    .chmod       = s3fs_chmod,
    .chown       = s3fs_chown,
    /*
    .truncate    = s3fs_truncate,
    */
    .open        = s3fs_open,
    .read        = s3fs_read,
    /*
    .write       = s3fs_write,
    .statfs      = s3fs_statfs,
    */
    .flush       = s3fs_flush,
    .release     = s3fs_release,
    /*
    .fsync       = s3fs_fsync,
    .setxattr    = s3fs_setxattr,
    .getxattr    = s3fs_getxattr,
    .listxattr   = s3fs_listxattr,
    .removexattr = s3fs_removexattr,
    */
    .opendir     = s3fs_opendir,
    .readdir     = s3fs_readdir,
    .releasedir  = s3fs_releasedir,
    /*
    .fsyncdir    = s3fs_syncdir,
    .init        = s3fs_init,
    */
    .destroy     = s3fs_destroy,
    .access      = s3fs_access,
    /*
    .create      = s3fs_create,
    .ftruncate   = s3fs_ftruncate,
    */
    .fgetattr    = s3fs_fgetattr,
    /*
    .lock        = s3fs_lock,
    */
    .utimens     = s3fs_utimens,
    /*
    .bmap        = s3fs_bmap,
    */
    .flag_nullpath_ok = 0,
    /*
    .flag_reserved    = 0,
    .ioctl       = s3fs_ioctl,
    .poll        = s3fs_poll
    */
};



pthread_mutex_t fileDescriptorsMutex = PTHREAD_MUTEX_INITIALIZER;
struct S3FileInfo *fileDescriptors[ MAX_S3_FILE_DESCRIPTORS ];

static int VerifyPathSearchPermissions( const char *path );



/**
 * Determine if a user is a member of the group with the specified gid in
 * the /etc/group list.
 * @param gid [in] Group ID that the user's membership is verified against.
 * @param myGid [in] The user's gid. 
 * @return \a true of the user is a member of the group; \a false otherwise.
 */
static bool
IsUserMemberOfGroup(
    gid_t gid,
    gid_t myGid
		    )
{
    /* Parse the /etc/group file (sigh) to determine if the user belongs to
       the file's gid. */
    FILE *etcGroup;
    char etcLine[ 4096 ];
    int  idx = 0;
    char ch;

    int        groupNameLength;
    int        groupIdBegin;
    int        groupIdLength;
    char       groupIdText[ 6 ];
    int        groupId;
    const char *groupMembers = NULL;
    char       myGroup[ 64 ] = { '\0' };
    bool       foundMyGroup = false;
    bool       foundMembers = false;

#ifdef AUTOTEST
    etcGroup = fopen( "../../testdata/etc-group", "r" );
#else
    etcGroup = fopen( "/etc/group", "r" );
#endif
    if( etcGroup == NULL )
    {
        return( false );
    }

    /* If user's gid equals the file's gid, then group membership is
       implied. */
    if( myGid == gid )
    {
	return( true );
    }

    /* Find the user's group name and the allowed member names in one swoop. */
    while( ! feof( etcGroup ) && ( ( ! foundMyGroup ) || ( ! foundMembers ) ) )
    {
        /* Scan a line from /etc/group. */
        fgets( etcLine, sizeof( etcLine ), etcGroup );

	/* Group name begin: 0; group name length: groupNameLength. */
	idx = 0;
	while( ( ch = etcLine[ idx ] ) != ':' )
	{
	    idx++;
	}
	groupNameLength = idx;
	idx += 3;

	/* Group ID begin: groupIdBegin; group ID length: groupIdLength. */
	groupIdBegin = idx++;
	while( isdigit( ch = etcLine[ idx ] ) )
	{
	    idx++;
	}
	groupIdLength = idx++ - groupIdBegin;

	/* Determine group id. */
	strncpy( groupIdText, &etcLine[ groupIdBegin ], groupIdLength );
	groupIdText[ groupIdLength ] = '\0';
	sscanf( groupIdText, "%d", &groupId );

	/* If it matches the user's gid, then we know the user's gid name. */
	if( groupId == (int) myGid )
	{
	    strncpy( myGroup, etcLine, groupNameLength );
	    myGroup[ groupNameLength ] = '\0';
	    foundMyGroup = true;
	}
	/* And if it matches the file's gid, then we know the members. */
	if( groupId == (int) gid )
	{
	    /* Group members begin. */
	    groupMembers = strdup( &etcLine[ idx ] );
	    foundMembers = true;
	}
    }

    if( foundMyGroup && foundMembers )
    {
        /* Scan the group members for a match on the user's group name. */
        idx = 0;
	while( ( ch = groupMembers[ idx ] ) != '\0' )
	{
	    /* Fast-forward past ',' or whitespace. */
	    if( ( ch == ',' ) || isspace( ch ) )
	    {
		idx++;
	    }
	    else
	    {
		/* Return if the names match. */
		if( strncmp( &groupMembers[ idx ], myGroup,
			     strlen( myGroup ) ) == 0 )
	        {
		    return( true );
		}
		/* Fast-forward until ',' or whitespace. */
		while( ( ( ch = groupMembers[ idx ] ) != '\0' )
		       && ( ( ! isspace( ch ) ) && ( ch != ',' ) ) )
		{
		    idx++;
		}
	    }
	}
    }

    if( groupMembers != NULL )
    {
        free( (char*) groupMembers );
    }
    return( false );
}



/**
 * Determine whether the user has rights to access a file according to a
 * specified permissions mask. The function takes into account the uid and
 * gid of the file combined with the user's uid and group membership.
 * @param permissions [in] File permissions.
 * @param fileUid [in] uid ownership of the file.
 * @param fileGid [in] gid ownership of the file.
 * @param mask [in] Permissions that must be honored.
 * @return \a true if the user has permission, or \a false otherwise.
 */
static bool
VerifyAccessPermission(
    int   permissions,
    uid_t fileUid,
    gid_t fileGid,
    int   mask
		       )
{
    uid_t             myUid;
    gid_t             myGid;

    myUid = getuid( );
    myGid = getgid( );

    /* If the user owns the file, then check the user's own permissions. */
    if( getuid( ) == fileUid )
    {
        if( ( ( permissions >> 6 ) & mask ) == mask )
	{
	    return( true );
	}
    }

    /* If the group permissions match the mask, verify that the user is a
       member of the file's group. */
    if( ( ( permissions >> 3 ) & mask ) == mask )
    {
        return( IsUserMemberOfGroup( fileGid, getgid( ) ) );
    }

    /* If the others permissions match the mask, verify that the user is NOT
       the owner of the file. (That's what "others" means; it does not mean
       access for yourself.) */
    if( ( ( permissions & mask ) == mask ) && ( myUid != fileUid ) )
    {
        return( true );
    }

    /* If the user is root or member of root gid, full access is granted,
       regardless of permissions. */
    if( ( myUid == 0 ) || ( myGid == 0 ) )
    {
        return( true );
    }

    /* All else fails. */
    return( false );
}



/**
 * Allocate an unused file descriptor.
 * @return Allocated file descriptor, or -1 if all file descriptors are in use.
 */
static int AllocateFileDescriptor( )
{
    int fd;
    int foundFd = -1;

    pthread_mutex_lock( &fileDescriptorsMutex );
    for( fd = 0; fd < MAX_S3_FILE_DESCRIPTORS; fd++ )
    {
        if( fileDescriptors[ fd ] == NULL )
	{
	    foundFd = fd;
	    break;
	}
    }
    pthread_mutex_unlock( &fileDescriptorsMutex );

    return( foundFd );
}



/**
 * Determine whether the current user has read access to the file with the
 * specified FileInfo.
 * @param fi [in] FileInfo for the file.
 * @return \a true if the user has read access, \a false otherwise.
 */
static bool
IsReadable( const struct S3FileInfo *fi )
{
    int   permissions = fi->permissions;
    uid_t uid         = fi->uid;
    gid_t gid         = fi->gid;

    return( VerifyAccessPermission( permissions, uid, gid, 0b0100 ) );
}



/**
 * Determine whether the current user has execute/search access to the file
 * or directory with the specified FileInfo.
 * @param fi [in] FileInfo for the file/directory.
 * @return \a true if the user has execute/search access, \a false otherwise.
 */
static bool
IsExecutable( const struct S3FileInfo *fi )
{
    int   permissions = fi->permissions;
    uid_t uid         = fi->uid;
    gid_t gid         = fi->gid;

    return( VerifyAccessPermission( permissions, uid, gid, 0b0001 ) );
}



/**
 * Determine whether the current user has write access to the file with the
 * specified FileInfo.
 * @param fi [in] FileInfo for the file.
 * @return \a true if the user has write access, \a false otherwise.
 */
static bool
IsWriteable( const struct S3FileInfo *fi )
{
    int   permissions = fi->permissions;
    uid_t uid         = fi->uid;
    gid_t gid         = fi->gid;

    return( VerifyAccessPermission( permissions, uid, gid, 0b0010 ) );
}



/**
 * Set a structure of flags for easy decoding of open flags.
 * @param openFlags [out] Structure to receive the flags.
 * @param flags [in] FUSE open flags.
 * @return Nothing.
 */
static void
SetOpenFlags( struct OpenFlags *openFlags, int flags )
{
    openFlags->of_RDONLY    = ( ( flags & O_WRONLY ) || ( flags & O_RDWR ) ) ?
                                  false : true;
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
 * @param path [in] Path from which to extract the first directory component.
 * @param component [out] String with the first component.
 * @param length [out] Length of the component that was extracted, or 0 if
 *        there are no more components. Ignored if NULL.
 * @return Offset to the beginning of the next component, relative
 *         to the beginning of origPath.
 */
static int
GetNextPathComponent(
    const char *path,
    char       **component,
    int        *length
		     )
{
    int  endPos;
    bool escaped;
    char ch;
    int  componentLength;

    /* The ugly while construct means: scan from the current position until
       either an unescaped '/' is found or until the of the string. */
    endPos = 0;
    escaped = false;

    while( ( ( ch = path[ endPos ] ) != '\0' )
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
	endPos++;
    }

    /* endPos is the position right after the first '/' encountered or right
       after the end of the string. */
    componentLength = ( ch == '\0' ) ? endPos - 1 : endPos;

    /* Skip past any additional '/' characters in case the path looks like:
       path/component1//component2..., which is valid. */
    if( endPos < (int) strlen( path ) )
    {
        while( ( ch = path[ endPos ] ) == '/' )
	{
	    endPos++;
	}
    }

    /* Return the component length. */
    if( length != NULL )
    {
	*length = componentLength;
    }

    /* Copy the component into a new string. */
    if( 0 < componentLength )
    {
        *component = malloc( componentLength + sizeof( char ) );
	strncpy( *component, path, componentLength );
	(*component)[ componentLength ] = '\0';
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
 * and filename, that is, everything but the "basename" of a file.
 * @param path [in] Full path of the file.
 * @return Directory component or NULL if only the filename was specified.
 */
static char*
GetPathPrefix(
    const char *path
	      )
{
    int  idx = 0;
    int  lastSlashPos = -1;
    char *pathPrefix;
    char ch;
    bool escaped = false;

    /* Find the last unescaped '/'. */
    while( ( ch = path[ idx ] ) != '\0' )
    {
        if( ! escaped )
	{
	    if( ch == '/' )
	    {
	        lastSlashPos = idx;
	    }
	    else if( ch == '\\' )
	    {
	        escaped = true;
	    }
	}
	else
	{
	    escaped = false;
	}
	idx++;
    }
    if( lastSlashPos >= 0 )
    {
        pathPrefix = malloc( lastSlashPos + 2 * sizeof( char ) );
	strncpy( pathPrefix, path, lastSlashPos + 1 );
	pathPrefix[ lastSlashPos + 1 ] = '\0';
	return( pathPrefix );
    }

    return( NULL );
}



static int
ValidateDirectoryComponents(
    const char *path,
    bool       verifyExecutionBit
			    )
{
    char              *pathPrefix;
    char              *pathComponent;
    int               componentLength;
    int               position;
    char              *accumulatedPath;
    struct S3FileInfo *fileInfo;
    int               status;


    status = 0;

    pathPrefix = GetPathPrefix( path );
    if( pathPrefix != NULL )
    {
        accumulatedPath      = malloc( strlen( pathPrefix )
				       + 2 * sizeof( char ) );
	accumulatedPath[ 0 ] = '\0';
	position             = 0;
	position = GetNextPathComponent( &pathPrefix[ position ],
					 &pathComponent, &componentLength );
	while( componentLength > 0 )
	{
	    /* Grow a path from the path components. */
	    strcat( accumulatedPath, pathComponent );

	    /* Examine the path at its current depth. */
	    status = S3FileStat( accumulatedPath, &fileInfo );
	    if( status == 0 )
	    {
	        /* If any component of the path prefix is not a
		   directory, the error is ENOTDIR. */
	        if( fileInfo->fileType != 'd' )
		{
		    status = -ENOTDIR;
		    break;
		}
		if( verifyExecutionBit )
		{
		    /* Check permissions. The directory must be searchable
		       by the user's gid and uid.  */
		    if( ! IsExecutable( fileInfo ) )
		    {
			status = -EACCES;
			break;
		    }
		}
	    }
	    else
	    {
	        /* If any component of the path does not exist, the error
		   is ENOENT. */
	        status = -ENOENT;
		break;
	    }

	    free( pathComponent );
	    strcat( accumulatedPath, "/" );
	    position += GetNextPathComponent( &pathPrefix[ position ],
					      &pathComponent,
					      &componentLength );
	}
	free( accumulatedPath );
	free( pathPrefix );
    }

    return( status );
}



/**
 * Verify path components' search access. All of a file's parent directories
 * must be searchable (+x) in order to stat the file. Scan through the
 * directories one at a time.
 * @param path [in] Path of the file.
 * @return 0 on success, or \a -errno on failure.
 */
static int
VerifyPathSearchPermissions(
    const char *path
			    )
{
    return( ValidateDirectoryComponents( path, true ) );
}



/**
 * Copy an S3FileInfo structure to a file stat structure.
 * @param fileInfo [in] S3 file information structure.
 * @param stat [out] File stat structure.
 * @return Nothing.
 */
void
CopyFileInfoToFileStat(
    const struct S3FileInfo *fileInfo,
    struct stat             *stat
		       )
{
    memset( stat, 0, sizeof( struct stat ) );
    stat->st_mode |= ( fileInfo->fileType == 'd' ? S_IFDIR : 0 );
    stat->st_mode |= ( fileInfo->fileType == 'f' ? S_IFREG : 0 );
    stat->st_mode |= ( fileInfo->fileType == 'l' ? S_IFLNK : 0 );
    stat->st_mode |= fileInfo->permissions;
    if( fileInfo->fileType == 'l' )
    {
        stat->st_mode |= 0777; /* rwxrwxrwx for symlinks. */
    }
    stat->st_uid  =  fileInfo->uid;
    stat->st_gid  =  fileInfo->gid;
    stat->st_size =  fileInfo->size;
    stat->st_blocks = ( fileInfo->size + 511 ) / 512;
    stat->st_blksize = 65536l;
    stat->st_mode |= ( fileInfo->exeGid ? S_ISGID : 0 );
    stat->st_mode |= ( fileInfo->exeUid ? S_ISUID : 0 );
    stat->st_mode |= ( fileInfo->sticky ? S_ISVTX : 0 );
    memcpy( &stat->st_atime, &fileInfo->atime, sizeof( time_t ) );
    memcpy( &stat->st_mtime, &fileInfo->mtime, sizeof( time_t ) );
    memcpy( &stat->st_ctime, &fileInfo->ctime, sizeof( time_t ) );
    /* Set st_nlink = 1 for directories to make "find" work (see
       the FUSE FAQ). */
    stat->st_nlink = 1;
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
    int               status = 0;
    struct S3FileInfo *fileInfo;

    if( ( path == NULL ) || ( strcmp( path, "" ) == 0 ) )
    {
        printf( "s3fs_getattr: null path\n" );
        status = -ENOENT;
	return( status );
    }

    /* Verify that the full path has search (+x) permissions. */
    status = VerifyPathSearchPermissions( path );
    if( status == 0 )
    {
        /* Examine the full path where the file itself may have any
	   permission. */
        status = S3FileStat( path, &fileInfo );
	if( status == 0 )
	{
	    /* Update the stat structure with file information. */
	    CopyFileInfoToFileStat( fileInfo, stat );
	}
	if( status == 0 )
	    printf( "s3fs_getattr: %s perms = %07o\n", path, stat->st_mode );
	else
  	    printf( "s3fs_getattr: %s not found\n", path );
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
    struct S3FileInfo *fileInfo;
    struct OpenFlags  openFlags;
    struct S3FileInfo *parentFi;

    //    int  position;
    char *parent;

    printf( "s3fs_open: %s\n", path );

    /* If no filename is provided, return immediately. */
    if( ( path == NULL ) || ( strcmp( path, "" ) == 0 ) )
    {
        status = -ENOENT;
	return( status );
    }
    /* Verify that all path components but the last one are directories. */
    status = ValidateDirectoryComponents( path, false );
    if( status != 0 )
    {
	return( status );
    }
#if 0
    /* Get the parent directory of the file that is to be opened. */
    position = strlen( path );
    while( ( 0 <= --position ) && ( path[ position ] == '/' ) );
    while( ( 0 <= --position ) && ( path[ position ] != '/' ) );
    if( position != 0 )
    {
        parent = malloc( position + sizeof( char ) );
	strncpy( parent, path, position );
	parent[ position ] = '\0';
    }
    else
    {
        parent = NULL;
    }
#endif
    parent = GetPathPrefix( path );
    status = S3FileStat( parent, &parentFi );
    if( status != 0 )
    {
        return( -EACCES );
    }
    /* The parent must have search permissions. */
    if( ! IsExecutable( parentFi ) )
    {
        return( -EACCES );
    }

    /* Allocate a file handle. */
    fh = AllocateFileDescriptor( );
    if( fh != -1 )
    {
        status = -EACCES;

	/* The file stat cache provides information about the file. */
	status = S3FileStat( path, &fileInfo );
	if( status != 0 )
	{
	    status = -ENOENT;
	}
	else
	{
	    status = -EACCES;

	    fileDescriptors[ fh ] = fileInfo;
	    Syslog( log_DEBUG, "File handle %d allocated\n", fh );
	    /* http://sourceforge.net/apps/mediawiki/fuse/index.php?title=Fuse_file_info */
	    SetOpenFlags( &openFlags, fi->flags );
	    printf( "Checking flags... fi->flags = %08x\n", fi->flags );

	    /* Do not follow symbolic links. */
	    if( ( openFlags.of_NOFOLLOW ) && ( fileInfo->fileType == 'l' ) )
	    {
	        status = -EACCES;
		goto open_end;
	    }
	    if( openFlags.of_WRONLY || openFlags.of_RDWR )
	    {
	        /* O_WRONLY or O_RDWR applied to a directory. */
	        if( fileInfo->fileType == 'd' )
	        {
		    status = -EISDIR;
		    goto open_end;
		}
		/* If a write is specified, the parent must have write
		   permissions. */
		if( ! IsExecutable( parentFi ) )
		{
		    return( -EACCES );
		}
	    }
	    /* O_WRONLY is allowed if the file exists and has write
	       permissions. (If the file doesn't exist, the O_CREAT flag
	       must also be set. However, this flag isn't passed to this
	       function. Or? There's something about kernel 2.6 and FUSE.)
	       See if the file exists and has write permissions. */
	    if( ( openFlags.of_WRONLY ) && IsWriteable( fileInfo ) )
	    {
	        status = 0;
	    }
	    /* For O_RDONLY, the file must have read permissions. For
	       O_RDWR and O_APPEND, the file must have both read and write
	       permissions. */
	    else if( openFlags.of_RDONLY || openFlags.of_RDWR
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
	    else
	    {
	        status = -EACCES;
	    }
	}
    }
    /* All file descriptors in use. */
    else
    {
        status = -EMFILE;
	Syslog( log_INFO, "All file handles in use\n" );
    }

 open_end:
    if( status != 0 )
    {
        /* The file info structure is released when it expires from
	   the stat cache. */
        fileDescriptors[ fh ] = NULL;
	fh = -1;
    }
    if( parent != NULL )
    {
	free( parent );
    }

    fi->fh = fh;
    return status;
}



/**
 * Determine whether the user may open the specified directory.
 * @param path [in] Relative file path.
 * @param fi [out] FUSE file info structure.
 * @return 0 on success, or \a -errno on failure.
 */
static int
s3fs_opendir(
    const char            *dir,
    struct fuse_file_info *fi
	     )
{
    struct S3FileInfo *fileInfo;
    int               status = 0;
    int               dh = 0;

    /* Get information on the directory. */
    status = S3FileStat( dir, &fileInfo );
    if( status == 0 )
    {
        /* Determine if the user may open the directory. */
        if( IsExecutable( fileInfo ) )
	{
	    /* If allowed, allocate a file descriptor and return. */
	    dh = AllocateFileDescriptor( );
	    if( dh != -1 )
	    {
	        fileDescriptors[ dh ] = fileInfo;
		fi->fh = dh;
	    }
	    else
	    {
		status = -ENFILE;
	    }
	}
	else
	{
	    status = -EACCES;
	}
    }
    else
    {
        status = -ENFILE;
    }
    printf( "s3fs_opendir %s, status %d, file handle %d\n", dir, status, dh );
    return( status );
}



/**
 * Read directory based on a directory handle.
 *
 * The filesystem may choose between two modes of operation:
 *
 * 1) The readdir implementation ignores the offset parameter, and
 * passes zero to the filler function's offset.  The filler
 * function will not return '1' (unless an error happens), so the
 * whole directory is read in a single readdir operation.  This
 * works just like the old getdir() method.
 *
 * 2) The readdir implementation keeps track of the offsets of the
 * directory entries.  It uses the offset parameter and always
 * passes non-zero offset to the filler function.  When the buffer
 * is full (or an error happens) the filler function will return '1'.
 *
 * @param dir [in] Name of the directory to read.
 * @param buffer [out] Used by the FUSE filesystem for directory contents.
 * @param filler [in/out] Call-back function that is used to fill the buffer.
 * @param offset [in] Offset of the directory entries.
 * @param fi [in] FUSE file info structure.
 * @return 0 if the directory was read, \a -errno otherwise.
 */
/* Disable warning that offset is not used. */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
static int
s3fs_readdir(
    const char            *dir,
    void                  *buffer,
    fuse_fill_dir_t       filler,
    off_t                 offset,
    struct fuse_file_info *fi
	     )
{
    int  dh;
    int  status;
    char **s3Directory = NULL;
    int  nFiles        = 0;
    char *dirEntry;
    int  i;

    printf( "s3fs_readdir: %s\n", dir );

    /* Get directory handle. */
    dh = fi->fh;
    if( fileDescriptors[ dh ] == NULL )
    {
        status = -ESTALE;
	/* Or EBADF? */
    }
    else
    {
        /* Read the directory from the S3 storage. */
        status = S3ReadDir( fileDescriptors[ dh ], dir, &s3Directory, &nFiles,
			    -1 );
	if( status == 0 )
	{
	    /* Copy the entire directory to into the buffer. */
	    for( i = 0; i < nFiles; i++ )
	    {
	        dirEntry = s3Directory[ i ];
	        /* Call the filler with names unless an error has occurred. */
		if( status == 0 )
		{
		    status = filler( buffer, dirEntry, NULL, 0 );
		}
		free( dirEntry );
	    }
	    free( s3Directory );
	}
    }

    return( status );
}
#pragma GCC diagnostic pop



/**
 * Release the directory handle.
 * @param dir [in] Name of the directory (unused).
 * @param fi [in] FUSE file info structure.
 * @return 0 if the directory was released, \a -errno otherwise.
 */
/* Disable warning that dir is not used. */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
static int
s3fs_releasedir(
    const char            *dir,
    struct fuse_file_info *fi
		)
{
    int status;
    int dh = fi->fh;

    if( fileDescriptors[ dh ] == NULL )
    {
        status = -ESTALE;
	/* Or EBADF? */
    }
    else
    {
        /* Clear the file descriptor. */
        fileDescriptors[ dh ] = NULL;
	/* The file info structure will be deleted from memory when it
	   expires from the stat cache. */
	status = 0;
    }
    printf( "s3fs_releasedir %s, fh = %d\n", dir, (int)fi->fh );

    return( status );
}
#pragma GCC diagnostic pop



/**
 * Check file access permissions
 *
 * This will be called for the access() system call.  If the
 * 'default_permissions' mount option is given, this method is not
 * called.
 * @param path [in] File path for the file to check permissions on.
 * @param mask [in] Access mask bits.
 * @param 
 */ 
static int
s3fs_access(
    const char *path,
    int        mask
	    )
{
    int               status;
    struct S3FileInfo *fileInfo;
    int               permissions = 0;

    printf( "s3fs_access %s, mask %04o\n", path, mask );

    /* Verify that the path has search permissions throughout. */
    status = VerifyPathSearchPermissions( path );
    if( status == 0 )
    {
        /* Examine the file. */
        status = S3FileStat( path, &fileInfo );
	if( status == 0 )
	{
	    /* F_OK means just check that the file exists. */
	    if( mask != F_OK )
	    {
	        if( IsReadable( fileInfo ) )
		{
		    permissions |= R_OK;
		}
		if( IsWriteable( fileInfo ) )
		{
		    permissions |= W_OK;
		}
		if( IsExecutable( fileInfo ) )
		{
		    permissions |= X_OK;
		}
		if( ( permissions & mask ) != mask )
		{
		    status = -EACCES;
		}
	    }

	}
    }

    return( status );
}



/** Read data from an open file
 *
 * According to the FUSE documentation:
 *
 * "Read should return exactly the number of bytes requested except
 * on EOF or error, otherwise the rest of the data will be
 * substituted with zeroes.  An exception to this is when the
 * 'direct_io' mount option is specified, in which case the return
 * value of the read system call will reflect the return value of
 * this operation."
 *
 * However, a usual read operation returns the number of bytes actually
 * read, which may not be the number requested. This function aims to return
 * the number of bytes read rather than the number requested.
 *
 * @param path [in] Full file path.
 * @param buf [in] Destination buffer for the file contents.
 * @param size [in] Number of octets to read.
 * @param offset [in] Offset from the beginning of the file.
 * @param fi [in] FUSE file information.
 * @return Number of bytes read, or \a -errno on failure.
 */
/* Disable warning that fi is not used. */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
static int
s3fs_read(
    const char            *path,
    char                  *buf,
    size_t                size,
    off_t                 offset,
    struct fuse_file_info *fi
	  )
{
    int    status;
    size_t actuallyRead;

    status = S3ReadFile( path, buf, size, offset, &actuallyRead );
    if( status == 0 )
    {
        return( actuallyRead );
    }
    return( status );
}
#pragma GCC diagnostic pop



/**
 * Get attributes from a file whose file information is available. Similar to
 * \a getattr, but the information is already known.
 * @param path [in] Path and filename of the file.
 * @param stat [out] File stat structure.
 * @param fi [in] FUSE file info.
 * @return 0 on success, or \a -errno on failure.
 */
/* Disable warning that path and fi are not used. */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
static int
s3fs_fgetattr(
    const char            *path,
    struct stat           *stat,
    struct fuse_file_info *fi
            )
{
    struct S3FileInfo *fileInfo;
    int               status;

    printf( "s3fs_fgetattr %s, fh = %d\n", path, (int)fi->fh );

    /* Stat the file. */
    fileInfo = fileDescriptors[ fi->fh ];
    status = 0;

    /* Update the stat structure with file information. */
    CopyFileInfoToFileStat( fileInfo, stat );

    return( status );
}
#pragma GCC diagnostic pop



/**
 * Commit any read/write buffers associated with the file.
 * @param path [in] File path.
 * @param fi [in] FUSE file info.
 * @return 0 on success, or \a -errno on failure.
 */
/* Disable warning that fi is not used. */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
static int
s3fs_flush(
    const char            *path,
    struct fuse_file_info *fi
	   )
{
    int status;

    printf( "s3fs_flush %s\n", path );

    status = S3FlushBuffers( path );

    return( status );
}
#pragma GCC diagnostic pop



/**
 * Indicate that a file's file descriptors should be closed and that all
 * memory mappings for the file are unmapped.
 * @param path [in] File path.
 * @param fi [in] FUSE file info.
 * @return 0 on success, or \a -errno on failure.
 */
static int
s3fs_release(
    const char            *path,
    struct fuse_file_info *fi
	     )
{
    int status;

    status = S3FileClose( path );

    /* Deallocate the file handle. */
    fileDescriptors[ fi->fh ] = NULL;

    return( status );
}



/**
 * Create a symbolic link.
 * @param target [in] Link target.
 * @param path [in] File path of the link.
 * @return 0 on success, or \a -errno on failure.
 */
static int
s3fs_symlink(
    const char *target,
    const char *path
	     )
{
    int status;

    printf( "s3fs_symlink: link %s -> %s\n", path, target );
    status = S3CreateLink( path, target );
    return( status );
}



/**
 * Resolve a symbolic link.
 * @param linkname [in] File path of the link.
 * @param path [out] Buffer that receives the link target.
 * @param length [in] Maximum link target length.
 * @return 0 on success, or \a -errno on failure.
 */
static int
s3fs_readlink(
    const char *linkname,
    char       *path,
    size_t     length
	     )
{
    int  status;
    char *target;

    status = S3ReadLink( linkname, &target );
    if( status == 0 )
    {
	/* Truncate the link target if necessary. */
        if( length < strlen( target ) + 1 )
	{
	    target[ length - 1 ] = '\0';
	}
	strcpy( path, target );
	free( target );
    }
    return( status );
}



/**
 * Modify the atime and mtime settings for a file.
 * @param file [in] File whose timestamps are to be updated.
 * @param tv [in] atime and mtime timestamps.
 * @return 0 on success, or \a -errno otherwise.
 */
static int
s3fs_utimens(
    const char            *file,
    const struct timespec tv[ 2 ]
	     )
{
    const time_t atime = tv[ 0 ].tv_sec;
    const time_t mtime = tv[ 0 ].tv_sec;
    int status;

    status = S3ModifyTimeStamps( file, atime, mtime );
    return( status );
}



/* Disable warning that data is not used. What is it, anyway? */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
void
s3fs_destroy(
    void *data
	     )
{
    /* Shutdown the S3IF. This also deletes all the fileinfo structures in
       the filehandles array. */
    S3Destroy( );
}
#pragma GCC diagnostic pop



static int
s3fs_mkdir(
    const char *dirname,
    mode_t mode
	   )
{
    int status;

    status = S3Mkdir( dirname, mode );
    return( status );
}


static int
s3fs_unlink(
    const char *file
	    )
{
    int status;

    status = S3Unlink( file );
    return( status );
}



static int
s3fs_rmdir(
     const char *dirname
	   )
{
    int status;

    status = S3Rmdir( dirname );
    return( status );
}



static int
s3fs_chmod(
    const char *path,
    mode_t     mode
	   )
{
    return( S3Chmod( path, mode ) );
}



static int
s3fs_chown(
    const char *path,
    uid_t      uid,
    gid_t      gid
	   )
{
    return( S3Chown( path, uid, gid ) );
}



    /* Create a list of gid groups that the current user is a member of. */
