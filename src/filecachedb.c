/**
 * \file filecachedb.c
 * \brief Database for the file cache server.
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
#include <sys/stat.h>
#include <assert.h>
#include <pthread.h>
#include <glib-2.0/glib.h>
#include "aws-s3fs.h"
#include "socket.h"
#include "filecache.h"


static pthread_mutex_t cacheDatabase_mutex = PTHREAD_MUTEX_INITIALIZER;


static struct
{
    sqlite3 *cacheDb;

    /* Compiled queries. */
    sqlite3_stmt *fileStat;
    sqlite3_stmt *newFile;
	sqlite3_stmt *newParent;
    sqlite3_stmt *findFile;
	sqlite3_stmt *findParent;
	sqlite3_stmt *getLocalpath;
    sqlite3_stmt *incrementSubscription;
    sqlite3_stmt *decrementSubscription;
	sqlite3_stmt *download;
	sqlite3_stmt *upload;
	sqlite3_stmt *allOwners;
	sqlite3_stmt *deleteTransfer;
	sqlite3_stmt *addDownload;
	sqlite3_stmt *addUpload;
	sqlite3_stmt *addUser;
	sqlite3_stmt *setCachedFlag;
	sqlite3_stmt *checkCacheStatus;
	sqlite3_stmt *createMultipart;
	sqlite3_stmt *getUpload;
	sqlite3_stmt *setUploadId;
	sqlite3_stmt *allPartsComplete;
	sqlite3_stmt *getEtag;
	sqlite3_stmt *setEtag;
	sqlite3_stmt *findUploadRequest;
	sqlite3_stmt *deleteUploadTransfer;
} cacheDatabase;


struct CacheFileStat
{

};


static void CreateDatabase( sqlite3* cacheDb; );
static void CompileStandardQueries( sqlite3 *cacheDb );
static bool CompileSqlStatement( sqlite3 *db, const char *const sql,
								 sqlite3_stmt **query );
STATIC sqlite3_int64 FindParent( const char *path, char *localname );


/* Convenience macro for creating a series of nested "if OK" clauses
   without cluttering the source code. */
#define BIND_QUERY( rc, stmt, next ) rc = sqlite3_bind_##stmt;   \
                                     if( rc == SQLITE_OK ) { next; }
#define RESET_QUERY( query ) sqlite3_reset( cacheDatabase.query )
/* Compile an SQL source entry and add the byte-code to the cacheDatabase
   structure. */
#define COMPILESQL( stmt ) CompileSqlStatement( cacheDb, stmt##Sql,   \
												&cacheDatabase.stmt )



#ifdef AUTOTEST
sqlite3*
GetCacheDatabase(
	void
                 )
{
	return( cacheDatabase.cacheDb );
}
#endif



/**
 * Initialize the database access, and create the database if necessary.
 * @return Nothing.
 * Test: unit test (test-filecache.c).
 */
void
InitializeFileCacheDatabase(
    void
	                       )
{
    sqlite3 *cacheDb;
    int     rc;

    /* Open the database. */
    rc = sqlite3_config( SQLITE_CONFIG_SERIALIZED );
    if( rc != SQLITE_OK )
    {
        fprintf( stderr, "Cannot open database: %s\n",
				 sqlite3_errmsg( cacheDb ) );
		exit( EXIT_FAILURE );
    }
    rc = sqlite3_initialize( );
    if( rc != SQLITE_OK )
    {
        fprintf( stderr, "Cannot open database: %s\n",
				 sqlite3_errmsg( cacheDb ) );
		exit( EXIT_FAILURE );
    }
    rc = sqlite3_enable_shared_cache( 1 );
    if( rc != SQLITE_OK )
    {
        fprintf( stderr, "Cannot open database: %s\n",
				 sqlite3_errmsg( cacheDb ) );
		exit( EXIT_FAILURE );
    }
    rc = sqlite3_open( CACHE_DATABASE, &cacheDb );
    if( rc != SQLITE_OK )
    {
        fprintf( stderr, "Cannot open database: %s\n",
				 sqlite3_errmsg( cacheDb ) );
		exit( EXIT_FAILURE );
    }
    cacheDatabase.cacheDb = cacheDb;

    /* Create tables if necessary. */
    CreateDatabase( cacheDb );

    /* Compile queries that are often used. */
    CompileStandardQueries( cacheDb );
}




/**
 * Release the database and compiled queries.
 * @return Nothing.
 * Test: none.
 */
void
ShutdownFileCacheDatabase(
    void
		                 )
{
    #define CLEAR_QUERY( query ) sqlite3_finalize( cacheDatabase.query )
	CLEAR_QUERY( fileStat );
	CLEAR_QUERY( newFile );
	CLEAR_QUERY( newParent );
	CLEAR_QUERY( getLocalpath );
	CLEAR_QUERY( findParent );
	CLEAR_QUERY( findFile );
	CLEAR_QUERY( incrementSubscription );
	CLEAR_QUERY( decrementSubscription );
	CLEAR_QUERY( download );
	CLEAR_QUERY( allOwners );
	CLEAR_QUERY( deleteTransfer );
	CLEAR_QUERY( addDownload );
	CLEAR_QUERY( addUpload );
	CLEAR_QUERY( addUser );
	CLEAR_QUERY( setCachedFlag );
	CLEAR_QUERY( checkCacheStatus );
	CLEAR_QUERY( createMultipart );
	CLEAR_QUERY( getUpload );
	CLEAR_QUERY( setUploadId );
	CLEAR_QUERY( allPartsComplete );
	CLEAR_QUERY( getEtag );
	CLEAR_QUERY( setEtag );
	CLEAR_QUERY( findUploadRequest );
	CLEAR_QUERY( deleteUploadTransfer );

    sqlite3_close( cacheDatabase.cacheDb );
	sqlite3_shutdown( );
}



/**
 * Create the database tables if they do not already exist.
 * @param cacheDb [in] Opened SQLite database.
 * @return Nothing.
 * Test: implicit blackbox (test-filecache.c).
 */
static void
CreateDatabase(
    sqlite3 *cacheDb
	           )
{
    char *errMsg;
    int  rc;

    static const char *createSql =
		"PRAGMA foreign_keys = ON; "

		/* Parent directories are required because users must be able to
		   create files locally for upload. The parent directories determine
		   access rights that are governed by the system. */
		"CREATE TABLE IF NOT EXISTS parents(                 \
            id INTEGER PRIMARY KEY,                          \
            remotename VARCHAR( 4096 ) NOT NULL,             \
            localname VARCHAR( 6 ) NOT NULL,                 \
            uid INTEGER NUL NULL,                            \
            gid INTEGER NOT NULL,                            \
            permissions INTEGER NOT NULL                     \
        );                                                   \
        CREATE INDEX IF NOT EXISTS dirname_id ON parents( remotename ); "

        /* The `files` table maps the remote S3 file path to the local,
		   temporary filename, and maintains the number of subscriptions to
		   the filename.
		   `statcacheinsync` indicates that the file cache has not changed
		   its own file stats since last time the file stat was synchronized
		   with that of the stat cache.
		   `fileinsync` indicates that the local file has not changed since
		   the last time the file was synchronized with the remote host. */
        "CREATE TABLE IF NOT EXISTS files(                     \
            id INTEGER PRIMARY KEY,                            \
            bucket VARCHAR( 128 ) NOT NULL,                    \
            remotename VARCHAR( 4096 ) NOT NULL UNIQUE,        \
            localname VARCHAR( 6 ) NOT NULL,                   \
            filesize INTEGER,                                  \
            subscriptions INT NOT NULL DEFAULT \'1\',          \
            parent INTEGER NOT NULL,                           \
            uid INTEGER NOT NULL,                              \
            gid INTEGER NOT NULL,                              \
            permissions INTEGER NOT NULL,                      \
            atime DATETIME NULL,                               \
            mtime DATETIME NULL,                               \
            iscached BOOLEAN NOT NULL DEFAULT \'0\',           \
            statcacheinsync BOOLEAN NOT NULL DEFAULT \'1\',    \
            filechanged BOOLEAN NOT NULL DEFAULT \'0\',        \
            FOREIGN KEY( parent ) REFERENCES parents( id )     \
        ); "
        "CREATE INDEX IF NOT EXISTS remotename_id ON files( remotename ); "

		/* The `users` table contains users and their keys. */
		"CREATE TABLE IF NOT EXISTS users(                 \
            uid INTEGER UNIQUE NOT NULL,                   \
            keyid VARCHAR( 21 ) NOT NULL,                  \
            secretkey VARCHAR( 41 ) NOT NULL               \
        ); "
        "CREATE INDEX IF NOT EXISTS id ON users( uid ); "

        /* The `transfers` table contains a list of current uploads or
		   downloads. */
	    "CREATE TABLE IF NOT EXISTS transfers(                     \
            id INTEGER PRIMARY KEY,                                \
            owner INTEGER NOT NULL,                                \
            file INTEGER UNIQUE NOT NULL,                          \
            filesize INTEGER,                                      \
            direction CHARACTER( 1 )                               \
                CONSTRAINT dir_chk                                 \
                CHECK( direction = \'u\' OR direction = \'d\' ),   \
	        uploadid VARCHAR( 57 ),                                \
            FOREIGN KEY( owner ) REFERENCES users( uid ),          \
            FOREIGN KEY( file ) REFERENCES files( id )             \
        ); "

        /* Transferparts lists the parts of an S3 multipart upload. */
        "CREATE TABLE IF NOT EXISTS transferparts(                  \
	        id INTEGER PRIMARY KEY,                                 \
	        transfer INTEGER NOT NULL,	                            \
            part INTEGER                                            \
                CONSTRAINT part_chk                                 \
                CHECK( part > \'0\' AND part < \'10001\' ),         \
	        inprogress BOOLEAN NOT NULL DEFAULT \'0\',              \
	        completed  BOOLEAN NOT NULL DEFAULT \'0\',              \
            etag VARCHAR( 32 ) NULL,                                \
            FOREIGN KEY( transfer ) REFERENCES transfers( id )      \
                ON DELETE CASCADE				                    \
	    ); "
		"";


    rc = sqlite3_exec( cacheDb, createSql, NULL, NULL, &errMsg );
    if( rc != SQLITE_OK )
    {
        fprintf( stderr, "Cannot create table (%i): %s\n", rc, errMsg );
		sqlite3_free( errMsg );
		sqlite3_close( cacheDb );
		exit( EXIT_FAILURE );
    }
}



/**
 * Precompile the often-used SQL queries and place them in the cacheDatabase
 * structure.
 * @param cacheDb [in] Opened SQLite database.
 * @return Nothing.
 * Test: implicit blackbox (test-filecache.c).
 */
static void
CompileStandardQueries(
    sqlite3 *cacheDb
	                  )
{

    const char *const fileStatSql =
        "SELECT localname, uid, gid, permissions, mtime FROM files \
        WHERE remotename = ?;";

    const char *const newFileSql =
        "INSERT INTO files( bucket, uid, gid, permissions, mtime,  \
                            parent, remotename, localname )        \
        VALUES( ?, ?, ?, ?, ?, ?, ?, ? );";

    const char *const newParentSql =
        "INSERT INTO parents( uid, gid, permissions, remotename, localname ) \
        VALUES( ?, ?, ?, ?, ? );";

	const char *findParentSql =
		"SELECT id, localname FROM parents WHERE remotename = ?;";

    const char *const findFileSql =
        "SELECT id, localname FROM files WHERE remotename = ?;";

	const char *const getLocalpathSql =
		"SELECT files.localname, parents.localname           \
         FROM parents                                        \
             LEFT JOIN files ON files.parent = parents.id    \
         WHERE files.remotename = ?;";

    const char *const incrementSubscriptionSql = 
        "UPDATE files SET subscriptions = subscriptions + 1 WHERE id = ?;";

    const char *const decrementSubscriptionSql = 
        "UPDATE files SET subscriptions = subscriptions - 1 WHERE id = ?;";

	const char *const downloadSql =
		"SELECT files.bucket, files.remotename, files.localname,  \
                users.keyid, users.secretkey                      \
        FROM transfers                                            \
            LEFT JOIN files ON transfers.file = files.id          \
            LEFT JOIN users ON transfers.owner = users.uid        \
        WHERE files.id = ?;";
/*
		"SELECT files.bucket, files.remotename, files.localname,  \
                users.keyid, users.secretkey                      \
        FROM files, users, transfers                              \
            WHERE transfers.file = files.id          \
            AND   transfers.owner = users.uid        \
            AND   files.id = ?;";
*/

	const char *const uploadSql =
		"SELECT files.bucket, files.remotename, files.localname,             \
                files.uid, files.gid, files.permissions, files.filesize,     \
                transfers.uploadid, users.keyid, users.secretkey             \
        FROM transfers                                                       \
            LEFT JOIN files ON transfers.file = files.id                     \
            LEFT JOIN users ON transfers.owner = users.uid                   \
            LEFT JOIN transferparts ON transfers.id = transferparts.transfer \
        WHERE files.id = ?                                                   \
            AND transferparts.completed = \'1\'                              \
            AND transferparts.inprogress = \'0\';";

	const char *const allOwnersSql =
		"SELECT files.uid, files.gid, files.permissions, files.localname,  \
                parents.uid, parents.gid, parents.localname                \
         FROM parents                                                      \
             LEFT JOIN files ON files.parent = parents.id                  \
         WHERE files.id = ?;";

	const char *const deleteTransferSql =
		"DELETE FROM transfers WHERE file = ?;";

	const char *const addDownloadSql =
		"INSERT INTO transfers( file, owner, direction ) VALUES( ?, ?, 'd' );";

	const char *const addUploadSql =
		"INSERT INTO transfers( file, owner, filesize, direction )  \
        VALUES( ?, ?, ?, 'u' );";

	const char *const addUserSql =
		"INSERT INTO users( uid, keyid, secretkey ) VALUES( ?, ?, ? );";

	const char *const setCachedFlagSql =
		"UPDATE files SET iscached = '1' WHERE id = ?;";

	const char *const checkCacheStatusSql =
		"SELECT iscached FROM files WHERE id = ?;";

	const char *const createMultipartSql =
		"INSERT INTO transferparts( transfer, part ) "
		"VALUES( "
		"    (SELECT id FROM transfers WHERE direction = 'u' AND file = ?), "
        "    ? "
		"); ";

	const char *const getUploadSql =
		"SELECT files.bucket, files.remotename, "
		"    transfers.uploadid, transferparts.part, "
		"    files.uid, files.gid, files.permissions, "
		"    files.filesize, files.localname,"
		"    parents.localname, "
		"    users.keyid, users.secretkey "
		"FROM files "
		"INNER JOIN transfers ON files.id = transfers.file "
		"INNER JOIN transferparts ON transferparts.transfer = transfers.id "
		"INNER JOIN parents ON parents.id = files.parent "
		"INNER JOIN users ON users.uid = transfers.owner "
        "WHERE transferparts.inprogress = \'0\' "
		"AND   transferparts.completed  = \'0\' "
		"AND   transfers.direction      = \'u\' "
	    "AND   files.id                 = ? "
		"LIMIT 1;";

	const char *const setUploadIdSql =
		"UPDATE transfers SET uploadid = ? WHERE file = ?;";

	const char *const allPartsCompleteSql =
		"SELECT COUNT( part ) - SUM( completed ) + SUM( inprogress ) "
		"FROM transferparts "
		"INNER JOIN transfers ON transferparts.transfer = transfers.id "
		"WHERE transfers.file = ?;";

	const char *const getEtagSql =
		"SELECT etag FROM transferparts "
		"LEFT JOIN transfers ON transfers.id = transferparts.transfer "
		"WHERE transfers.file = ? AND part = ?;";

	/* Regrettably, sqlite doesn't support update joins. Otherwise:
	       "UPDATE p "
		   "SET p.etag = ? "
		   "FROM transferparts p "
		   "INNER JOIN transfers t ON t.id = p.transfer "
		   "WHERE t.file = ? AND p.part = ?; "; */
	const char *const setEtagSql =
		"UPDATE transferparts "
		"SET etag = ? "
		"WHERE id IN "
		"( "
		"    SELECT transferparts.id FROM transferparts "
		"    INNER JOIN transfers "
		"        ON transferparts.transfer = transfers.id "
		"    WHERE transfers.file = ? "
		"    AND   transferparts.part = ? "
		"); ";

	const char *const findUploadRequestSql =
		"SELECT file FROM transferparts "
		"INNER JOIN transfers ON transferparts.transfer = transfers.id "
		"WHERE transfers.direction = \'u\' "
		"AND   transferparts.inprogress = \'0\' "
		"AND   transferparts.completed  = \'0\' "
		"GROUP BY file LIMIT 1;";

	/* This query cascade deletes all transferparts. */
	const char *const deleteUploadTransferSql =
		"DELETE FROM transfers WHERE transfers.file = ?;";

/*
		"DELETE transfers, transferparts "
		"FROM transfers INNER JOIN transferparts "
		"ON    transferparts.transfer = transfers.id "
		"WHERE transfers.file = ?;";
*/

    COMPILESQL( fileStat );
    COMPILESQL( newFile );
    COMPILESQL( newParent );
    COMPILESQL( findParent );
    COMPILESQL( findFile );
    COMPILESQL( getLocalpath );
    COMPILESQL( incrementSubscription );
    COMPILESQL( decrementSubscription );
	COMPILESQL( download );
	COMPILESQL( upload );
	COMPILESQL( allOwners );
	COMPILESQL( deleteTransfer );
	COMPILESQL( addDownload );
	COMPILESQL( addUpload );
	COMPILESQL( addUser );
	COMPILESQL( setCachedFlag );
	COMPILESQL( checkCacheStatus );
	COMPILESQL( createMultipart );
	COMPILESQL( getUpload );
	COMPILESQL( setUploadId );
	COMPILESQL( allPartsComplete );
	COMPILESQL( getEtag );
	COMPILESQL( setEtag );
	COMPILESQL( findUploadRequest );
	COMPILESQL( deleteUploadTransfer );
}



/**
 * Compile the specified SQL source query to sqlite byte code. Note that this
 * function does not support multi-entry queries.
 * @param db [in] Opened SQLite database.
 * @param sql [in] Source SQL query.
 * @param query [out] Destination for the byte code.
 * @return \a true if the query was successfully compiled, or \a false
 *         otherwise.
 * Test: implicit blackbox (test-filecache.c).
 */
static bool
CompileSqlStatement(
    sqlite3           *db,
    const char *const sql,
    sqlite3_stmt      **query
	                )
{
    int rc;

    /* Compile the query. Note: it only works for single-query statements
       because of the NULL at the end. */
    rc = sqlite3_prepare_v2( db, sql, -1, query, NULL );
    if( rc != SQLITE_OK )
    {
        fprintf( stderr, "Can't prepare select query %s (%i): %s\n",
		 sql, rc, sqlite3_errmsg( db ) );
		sqlite3_close( db );
		exit(1);
    }
    return( true );
}



/**
 * Prevent other threads from accessing the cache.
 * @return Nothing.
 * Test: implicit blackbox (test-filecache.c).
 */
static inline void
LockCache(
    void
	      )
{
    pthread_mutex_lock( &cacheDatabase_mutex );
}



/**
 * Allow other threads to access the cache.
 * @return Nothing.
 * Test: implicit blackbox (test-filecache.c).
 */
static inline void
UnlockCache(
    void
	       )
{
    pthread_mutex_unlock( &cacheDatabase_mutex );
}



/**
 * Determine if the specified file is cached.
 * @param path [in] Remote filename.
 * @param localname [out] The basename of the local file.
 * @return ID of the file if it is cached, or 0 otherwise.
 * Test: unit test (test-filecache.c).
 */
sqlite3_int64
FindFile(
    const char *path,
	char       *localname
         )
{
    int           rc;
    sqlite3_stmt  *searchQuery = cacheDatabase.findFile;
    sqlite3_int64 fileId = 0;
	const char    *basename;

    /* Search for the remote filename in the database. */
    LockCache( );
    BIND_QUERY( rc, text( searchQuery, 1, path, -1, NULL ), );
    if( rc == SQLITE_OK )
	{
		/* Read the `id` for the path. */
		while( ( rc = sqlite3_step( searchQuery ) ) == SQLITE_ROW )
		{
			fileId = sqlite3_column_int64( searchQuery, 0 );
			basename = (const char*) sqlite3_column_text( searchQuery, 1 );
			strncpy( localname, basename, 6 );
		}
		if( rc != SQLITE_DONE )
		{
			fprintf( stderr,
					 "Select statement didn't finish with DONE (%i): %s\n",
					 rc, sqlite3_errmsg( cacheDatabase.cacheDb ) );
		}
	}
	else
    {
        fprintf( stderr, "Can't prepare select query (%i): %s\n",
				 rc, sqlite3_errmsg( cacheDatabase.cacheDb ) );
    }
	RESET_QUERY( findFile );
    UnlockCache( );

    return( fileId );
}



/**
 * Return the local path (directory and filename) for a local file.
 * @param remotename [in] Remote filename.
 * @return Path name or \a NULL if the file is not known by the file cache.
 * Test: unit test (test-filecache.c).
 */
const char*
Query_GetLocalPath(
    const char *remotename
                   )
{
    int           rc;
    sqlite3_stmt  *searchQuery = cacheDatabase.getLocalpath;
	const char    *dirname;
	const char    *basename;
	char          localpath[ 14 ];
	const char    *toReturn    = NULL;
	bool          found        = false;

    /* Search for the remote filename in the database. */
    LockCache( );
    BIND_QUERY( rc, text( searchQuery, 1, remotename, -1, NULL ), );
    if( rc == SQLITE_OK )
	{
		while( ( rc = sqlite3_step( searchQuery ) ) == SQLITE_ROW )
		{
			/* Get the directory and the basename. */
			basename = (const char*) sqlite3_column_text( searchQuery, 0 );
			dirname  = (const char*) sqlite3_column_text( searchQuery, 1 );
			if( ( dirname != NULL ) && ( basename != NULL ) )
			{
				found = true;
				strcpy( localpath, dirname );
				strcat( localpath, "/" );
				strcat( localpath, basename );
				found = true;
			}
		}
		if( rc != SQLITE_DONE )
		{
			fprintf( stderr,
					 "Select statement didn't finish with DONE (%i): %s\n",
					 rc, sqlite3_errmsg( cacheDatabase.cacheDb ) );
		}
	}
	else
    {
        fprintf( stderr, "Can't prepare select query (%i): %s\n",
				 rc, sqlite3_errmsg( cacheDatabase.cacheDb ) );
    }
	RESET_QUERY( getLocalpath );
    UnlockCache( );

	if( found )
	{
		toReturn = strdup( localpath );
	}
    return( toReturn );
}



/**
 * Create a local filename for the S3 file and create a database entry
 * with the two file names and the S3 file attributes.
 * @param bucket [in] Bucket name for the S3 file storage.
 * @param path [in] Full S3 pathname for the file.
 * @param uid [in] Ownership of the S3 file.
 * @param gid [in] Ownership of the S3 file.
 * @param permissions [in] Permissions for the S3 file.
 * @param mtime [in] Last modification time of the S3 file.
 * @param parentId [in] ID of the parent directory.
 * @param localfile [in/out] Filename of the local file.  If the file already
 *        exists in the database, \a localfile is overwritten with a copy of
 *        the filename.
 * @param alreadyExists [out] Set to true if the file already exists in the
 *        database.
 * @return ID for the database entry, or \a -1 if an error occurred.
 * Test: unit test (test-filecache.c).
 */
sqlite3_int64
Query_CreateLocalFile(
	const char    *bucket,
    const char    *path,
    int           uid,
    int           gid,
    int           permissions,
    time_t        mtime,
	sqlite3_int64 parentId,
    char          *localfile,
	bool          *alreadyExists
	                  )
{
    int           rc;

    sqlite3_stmt  *newFileQuery = cacheDatabase.newFile;
    sqlite3_int64 id = 0;
	char          localname[ 7 ];

	/* Return the file ID if the file is already cached. */
	if( ( id = FindFile( path, localname ) ) != 0 )
	{
	    Query_IncrementSubscriptionCount( id );
		strncpy( localfile, localname, 7 );
		*alreadyExists = true;
		return( id );
	}

	LockCache( );
    BIND_QUERY( rc, text( newFileQuery, 1, bucket, -1, NULL ),
	BIND_QUERY( rc, int( newFileQuery, 2, uid ),
	BIND_QUERY( rc, int( newFileQuery, 3, gid ),
	BIND_QUERY( rc, int( newFileQuery, 4, permissions ),
    BIND_QUERY( rc, int( newFileQuery, 5, mtime ),
    BIND_QUERY( rc, int64( newFileQuery, 6, parentId ),
    BIND_QUERY( rc, text( newFileQuery, 7, path, -1, NULL ),
    BIND_QUERY( rc, text( newFileQuery, 8, localfile, -1, NULL ),
		) ) ) ) ) ) ) );

    if( rc != SQLITE_OK )
	{
		fprintf( stderr, "Error binding value in insert (%i): %s\n", rc,
				 sqlite3_errmsg( cacheDatabase.cacheDb ) );
		id = 0;
	}
	else
	{
		rc = sqlite3_step( newFileQuery );
		if( rc != SQLITE_DONE )
		{
			fprintf( stderr, "Insert statement failed (%i): %s\n", rc,
					 sqlite3_errmsg( cacheDatabase.cacheDb) );
			id = 0;
		}
		else
		{
			id = sqlite3_last_insert_rowid( cacheDatabase.cacheDb );
		}
	}

	RESET_QUERY( newFile );
	UnlockCache( );

    return( id );
}



/**
 * Create a local directory for S3 files and create a database entry
 * with the two file names and the directory owner and permissions.
 * @param path [in] Full S3 pathname for the directory.
 * @param uid [in] Ownership of the S3 directory.
 * @param gid [in] Ownership of the S3 directory.
 * @param permissions [in] Permissions for the S3 directory.
 * @param localdir [out] Filename of the local directory.
 * @param alreadyExists [out] Set to \a true if the parent already exists
 *        in the database.
 * @return ID for the database entry, or \a -1 if an error occurred.
 * Test: unit test (test-filecache.c).
 */
sqlite3_int64
Query_CreateLocalDir(
    const char *path,
    int        uid,
    int        gid,
    int        permissions,
    char       *localdir,
	bool       *alreadyExists
		             )
{
    int           rc;
    sqlite3_stmt  *newDirQuery = cacheDatabase.newParent;
    sqlite3_int64 id;
	char          parentName[ 7 ];

	/* Return the file ID if the file is already cached. */
	if( ( id = FindParent( path, parentName ) ) != 0 )
	{
		strncpy( localdir, parentName, 7 );
		*alreadyExists = true;
		return( id );
	}
	*alreadyExists = false;

	LockCache( );

	/* Insert the directory into the database. */
	BIND_QUERY( rc, int( newDirQuery, 1, uid ),
	BIND_QUERY( rc, int( newDirQuery, 2, gid ),
	BIND_QUERY( rc, int( newDirQuery, 3, permissions ),
    BIND_QUERY( rc, text( newDirQuery, 4, path, -1, NULL ),
    BIND_QUERY( rc, text( newDirQuery, 5, localdir, -1, NULL ),
		) ) ) ) );
	if( rc != SQLITE_OK )
	{
		fprintf( stderr, "Error binding value in insert (%i): %s\n", rc,
				 sqlite3_errmsg( cacheDatabase.cacheDb ) );
		id = 0;
	}
	else
	{
		rc = sqlite3_step( newDirQuery );
		if( rc != SQLITE_DONE )
		{
			fprintf( stderr, "Insert statement failed (%i): %s\n", rc,
					 sqlite3_errmsg( cacheDatabase.cacheDb) );
			id = 0;
		}
		else
		{
			id = sqlite3_last_insert_rowid( cacheDatabase.cacheDb );
		}
	}

	RESET_QUERY( newParent );
	UnlockCache( );
    return( id );
}



/**
 * Determine if the specified directory is cached.
 * @param parent [in] Remote directory name.
 * @param localname [in/out] The local directory name.  If the directory
 *        already exists, \a localname is overwritten with the local directory
 *        name.
 * @return ID of the directory if it is cached, or 0 otherwise.
 * Test: unit test (test-filecache.c).
 */
STATIC sqlite3_int64
FindParent(
	const char *parent,
	char       *localname
	       )
{
	int           rc;
    sqlite3_stmt  *parentQuery = cacheDatabase.findParent;
	sqlite3_int64 parentId = 0;
	const char    *basename;

	LockCache( );
    BIND_QUERY( rc, text( parentQuery, 1, parent, -1, NULL ), );
	if( rc == SQLITE_OK )
    {
		while( ( rc = sqlite3_step( parentQuery ) ) == SQLITE_ROW )
		{
			parentId = sqlite3_column_int64( parentQuery, 0 );
			basename = (const char*) sqlite3_column_text( parentQuery, 1 );
			strncpy( localname, basename, 6 );
		}

		if( rc != SQLITE_DONE )
		{
			fprintf( stderr,
					 "Select statement didn't finish with DONE (%i): %s\n",
					 rc, sqlite3_errmsg( cacheDatabase.cacheDb ) );
		}
	}
	else
	{
        fprintf( stderr, "Can't prepare select query (%i): %s\n",
				 rc, sqlite3_errmsg( cacheDatabase.cacheDb ) );
    }
	RESET_QUERY( findParent );
	UnlockCache( );

	return( parentId );
}



/**
 * Read the information that is required in order to create a download request
 * for the Amazon S3.
 * @param fileId [in] ID of the file in the files table.
 * @param bucket [out] Pointer to the file's bucket name string.
 * @param remotePath [out] Pointer to the file's S3 path name string.
 * @param localPath [out] Pointer to the file's local path name string.
 * @param keyId [out] Pointer to the user's Amazon Key ID string.
 * @param secretKey [out] Pointer to the user's Secret Key string.
 * @return \a true if the information was found, or \a false otherwise.
 * Test: unit test (filecache.c).
 */
bool
Query_GetDownload(
	sqlite3_int64 fileId,
	char          **bucket,
	char          **remotePath,
	char          **localPath,
	char          **keyId,
    char          **secretKey
	              )
{
	bool         status = false;
	int          rc;
    sqlite3_stmt *filenamesQuery = cacheDatabase.download;
	int          rows = 0;

	LockCache( );
    BIND_QUERY( rc, int64( filenamesQuery, 1, fileId ), );
	if( rc == SQLITE_OK )
    {
		while( ( rc = sqlite3_step( filenamesQuery ) ) == SQLITE_ROW )
		{
			if( rows++ != 0 )
			{
				fprintf( stderr, "WARNING: shouldn't return multiple rows" );
				free( *bucket );
				free( *remotePath );
				free( *localPath );
				free( *keyId );
				free( *secretKey );
			}

			*bucket     = strdup( (const char*)
								  sqlite3_column_text( filenamesQuery, 0 ) );
			*remotePath = strdup( (const char*)
								  sqlite3_column_text( filenamesQuery, 1 ) );
			*localPath  = strdup( (const char*)
								  sqlite3_column_text( filenamesQuery, 2 ) );
			*keyId      = strdup( (const char*)
								  sqlite3_column_text( filenamesQuery, 3 ) );
			*secretKey  = strdup( (const char*)
								  sqlite3_column_text( filenamesQuery, 4 ) );
			status = true;
		}
		if( rc != SQLITE_DONE )
		{
			fprintf( stderr,
					 "Select statement didn't finish with DONE (%i): %s\n",
					 rc, sqlite3_errmsg( cacheDatabase.cacheDb ) );
		}
	}
	else
	{
        fprintf( stderr, "Can't prepare select query (%i): %s\n",
				 rc, sqlite3_errmsg( cacheDatabase.cacheDb ) );
    }

	#ifdef AUTOTEST
	printf( "%s, %s, %s\n", *bucket, *remotePath, *localPath );
	#endif

	if( status != true )
	{
		*bucket = NULL;
		*remotePath = NULL;
		*localPath  = NULL;
		*keyId      = NULL;
		*secretKey  = NULL;
	}

	RESET_QUERY( download );
	UnlockCache( );

	return( status );
}



/**
 * Read the ownership and permissions of a file and its parent directory.
 * @param fileId [in] ID of the file whose access requirements are requested.
 * @param parentname [out] Pointer to the parent directory name string.
 * @param parentUid [out] Pointer to the parent directory uid.
 * @param parentGid [out] Pointer to the parent directory gid.
 * @param filename [out] Pointer to the file name string.
 * @param uid [out] Pointer to the file uid.
 * @param gid [out] Pointer to the file gid.
 * @param permissions [out] Pointer to the file permissions.
 * @return \a true if the access requirements could be retrieved, or \a false
 *         otherwise.
 * Test: unit test (test-filecache.c).
 */
bool
Query_GetOwners(
	sqlite3_int64 fileId,
	char          **parentname,
	uid_t         *parentUid,
	gid_t         *parentGid,
	char          **filename,
	uid_t         *uid,
	gid_t         *gid,
	int           *permissions
	            )
{
	bool         status = false;
	int          rc;
    sqlite3_stmt *ownersQuery   = cacheDatabase.allOwners;
	char         *resParentname = "";
	char         *resFilename   = "";

	LockCache( );
    BIND_QUERY( rc, int64( ownersQuery, 1, fileId ), );
	if( rc == SQLITE_OK )
    {
		while( ( rc = sqlite3_step( ownersQuery ) ) == SQLITE_ROW )
		{
			*uid          = sqlite3_column_int( ownersQuery, 0 );
			*gid          = sqlite3_column_int( ownersQuery, 1 );
			*permissions  = sqlite3_column_int( ownersQuery, 2 );
			resFilename   = (char*) sqlite3_column_text( ownersQuery, 3 );
			*parentUid    = sqlite3_column_int( ownersQuery, 4 );
			*parentGid    = sqlite3_column_int( ownersQuery, 5 );
			resParentname = (char*) sqlite3_column_text( ownersQuery, 6 );

			*parentname   = strdup( resParentname );
			*filename     = strdup( resFilename );
			status        = true;
		}
		if( rc != SQLITE_DONE )
		{
			status = false;
			fprintf( stderr,
					 "Select statement didn't finish with DONE (%i): %s\n",
					 rc, sqlite3_errmsg( cacheDatabase.cacheDb ) );
		}
	}
	else
	{
        fprintf( stderr, "Can't prepare select query (%i): %s\n",
				 rc, sqlite3_errmsg( cacheDatabase.cacheDb ) );
    }

	if( status != true )
	{
		*parentname = NULL;
		*filename   = NULL;
	}

	RESET_QUERY( allOwners );
	UnlockCache( );

	return( status );
}



/**
 * Delete a transfer from the queue of transfers.
 * @param fileId [in] ID of the file that should be deleted from the transfer
 *        queue.
 * @return \a true if the file was deleted, or \a false otherwise.
 */
bool
Query_DeleteTransfer(
	sqlite3_int64 fileId
	                       )
{
	bool         status = false;
	int          deleted;
	int          rc;
    sqlite3_stmt *deleteQuery = cacheDatabase.deleteTransfer;

	LockCache( );
    BIND_QUERY( rc, int64( deleteQuery, 1, fileId ), );
	if( rc == SQLITE_OK )
    {
		if( ( rc = sqlite3_step( deleteQuery ) ) == SQLITE_DONE )
		{
			if( ( deleted = sqlite3_changes( cacheDatabase.cacheDb ) ) == 1 )
			{
				status = true;
			}
			else
			{
				fprintf( stderr, "Invalid number of records deleted (%d)\n",
						 deleted );
			}
		}
		else
		{
			fprintf( stderr,
					 "Select statement didn't finish with DONE (%i): %s\n",
					 rc, sqlite3_errmsg( cacheDatabase.cacheDb ) );
		}
	}
	else
	{
        fprintf( stderr, "Can't prepare select query (%i): %s\n",
				 rc, sqlite3_errmsg( cacheDatabase.cacheDb ) );
    }
	RESET_QUERY( deleteTransfer );
	UnlockCache( );

	return( status );
}



/**
 * Increment or decrement the number of subscribers to a file.
 * @param fileId [in] ID of the file whose subscription count shall be
 *        incremented or decremented.
 * @param increment [in] \a true if the subscription count shall be incremented,
 *        or \a false if it shall be decremented.
 * @return \a true if the query succeeded, or \a false otherwise.
 * Test: implied blackbox (test-downloadcache.c).
 */
static bool
Query_IncrementOrDecrement(
	sqlite3_int64 fileId,
	bool          increment
	                 )
{
	bool         status = false;
	int          rc;
    sqlite3_stmt *countQuery;

	if( increment) countQuery = cacheDatabase.incrementSubscription;
	else           countQuery = cacheDatabase.decrementSubscription;

	LockCache( );
    BIND_QUERY( rc, int64( countQuery, 1, fileId ), );
	if( rc == SQLITE_OK )
    {
		if( ( rc = sqlite3_step( countQuery ) ) == SQLITE_DONE )
		{
			if( sqlite3_changes( cacheDatabase.cacheDb ) == 1 )
			{
				status = true;
			}
			else
			{
				fprintf( stderr, "Invalid number of records changed\n" );
			}
		}
		else
		{
			fprintf( stderr,
					 "Select statement didn't finish with DONE (%i): %s\n",
					 rc, sqlite3_errmsg( cacheDatabase.cacheDb ) );
		}
	}
	else
	{
        fprintf( stderr, "Can't prepare select query (%i): %s\n",
				 rc, sqlite3_errmsg( cacheDatabase.cacheDb ) );
    }

	if( increment) RESET_QUERY( incrementSubscription );
	else           RESET_QUERY( decrementSubscription );
	UnlockCache( );

	return( status );
}



/**
 * Increment the number of subscribers to a file.
 * @param fileId [in] ID of the file whose subscription count shall be
 *        incremented.
 * @return \a true if the query succeeded, or \a false otherwise.
 * Test: implied blackbox (test-downloadcache.c).
 */
bool
Query_IncrementSubscriptionCount(
	sqlite3_int64 fileId
	                             )
{
	return( Query_IncrementOrDecrement( fileId, true ) );
}



/**
 * Decrement the number of subscribers to a file.
 * @param fileId [in] ID of the file whose subscription count shall be
 *        decremented.
 * @return \a true if the query succeeded, or \a false otherwise.
 * Test: implied blackbox (test-downloadcache.c).
 */
bool
Query_DecrementSubscriptionCount(
	sqlite3_int64 fileId
	                             )
{
	return( Query_IncrementOrDecrement( fileId, false ) );
}



/**
 * Add a download transfer to the database.
 * @param fileId [in] ID of the file which is about to be downloaded.
 * @param owner [in] uid of the user that made the download request.
 * @return \a true if the query succeeded, or \a false otherwise.
 * Test: implied blackbox (test-downloadcache.c).
 */
bool
Query_AddDownload(
	sqlite3_int64 fileId,
	uid_t         owner
                  )
{
    int           rc;
	bool          status    = false;
    sqlite3_stmt  *addQuery = cacheDatabase.addDownload;

    LockCache( );
    BIND_QUERY( rc, int64( addQuery, 1, fileId ),
	BIND_QUERY( rc, int( addQuery, 2, (int) owner ),
		) );
    if( rc == SQLITE_OK )
	{
		while( ( rc = sqlite3_step( addQuery ) ) == SQLITE_ROW )
		{
			status = true;
		}
		if( rc != SQLITE_DONE )
		{
			fprintf( stderr,
					 "Select statement didn't finish with DONE (%i): %s\n",
					 rc, sqlite3_errmsg( cacheDatabase.cacheDb ) );
			status = false;
		}
	}
	else
    {
        fprintf( stderr, "Can't prepare select query (%i): %s\n",
				 rc, sqlite3_errmsg( cacheDatabase.cacheDb ) );
    }

	RESET_QUERY( addDownload );
    UnlockCache( );

    return( status );
}



/**
 * Add a user to the database.
 * @param uid [in] The user's uid.
 * @param keyId [in] The user's Amazon Access ID.
 * @param secretKey [in] The user's secret key.
 * @return \a true if the query succeeded, or \a false otherwise.
 */
bool
Query_AddUser(
	uid_t uid,
	char  keyId[ 21 ],
	char  secretKey[ 41 ]
	          )
{
	bool          status;
    int           rc;
    sqlite3_stmt  *addQuery = cacheDatabase.addUser;

    LockCache( );
    BIND_QUERY( rc, int( addQuery, 1, (int) uid ),
    BIND_QUERY( rc, text( addQuery, 2, keyId, -1, NULL ),
    BIND_QUERY( rc, text( addQuery, 3, secretKey, -1, NULL ),
		) ) );
    if( rc == SQLITE_OK )
	{
		while( ( rc = sqlite3_step( addQuery ) ) == SQLITE_ROW )
		{
			status = true;
		}
		if( rc != SQLITE_DONE )
		{
			fprintf( stderr,
					 "Select statement didn't finish with DONE (%i): %s\n",
					 rc, sqlite3_errmsg( cacheDatabase.cacheDb ) );
			status = false;
		}
	}
	else
    {
        fprintf( stderr, "Can't prepare select query (%i): %s\n",
				 rc, sqlite3_errmsg( cacheDatabase.cacheDb ) );
    }

	RESET_QUERY( addUser );
    UnlockCache( );

    return( status );
}



/**
 * Mark a file as cached.
 * @param fileId [in] ID of the file.
 * @return Nothing.
 */
void
Query_MarkFileAsCached(
	sqlite3_int64 fileId
	          )
{
    int           rc;
    sqlite3_stmt  *setQuery = cacheDatabase.setCachedFlag;

    LockCache( );
    BIND_QUERY( rc, int64( setQuery, 1, fileId ), );
    if( rc == SQLITE_OK )
	{
		while( ( rc = sqlite3_step( setQuery ) ) == SQLITE_ROW )
		{
			/* No action. */
		}
		if( rc != SQLITE_DONE )
		{
			fprintf( stderr,
					 "Select statement didn't finish with DONE (%i): %s\n",
					 rc, sqlite3_errmsg( cacheDatabase.cacheDb ) );
		}
	}
	else
    {
        fprintf( stderr, "Can't prepare select query (%i): %s\n",
				 rc, sqlite3_errmsg( cacheDatabase.cacheDb ) );
    }

	RESET_QUERY( setCachedFlag );
    UnlockCache( );
}



/**
 * Determine whether a file is cached.
 * @param fileId [in] ID of the file.
 * @return \a true if the file is cached, or \a false otherwise.
 */
bool
Query_IsFileCached(
	sqlite3_int64 fileId
	               )
{
    int           rc;
    sqlite3_stmt  *checkQuery = cacheDatabase.checkCacheStatus;
	int           cached;

    LockCache( );
    BIND_QUERY( rc, int64( checkQuery, 1, fileId ), );
    if( rc == SQLITE_OK )
	{
		while( ( rc = sqlite3_step( checkQuery ) ) == SQLITE_ROW )
		{
			cached = sqlite3_column_int( checkQuery, 0 );
		}
		if( rc != SQLITE_DONE )
		{
			fprintf( stderr,
					 "Select statement didn't finish with DONE (%i): %s\n",
					 rc, sqlite3_errmsg( cacheDatabase.cacheDb ) );
		}
	}
	else
    {
        fprintf( stderr, "Can't prepare select query (%i): %s\n",
				 rc, sqlite3_errmsg( cacheDatabase.cacheDb ) );
    }
	RESET_QUERY( checkCacheStatus );
    UnlockCache( );

	return( cached ? true : false );
}



/**
 * Prepare multipart uploads for a file.
 * @param fileId [in] ID of the file.
 * @param parts [in] Number up parts.
 * @return Nothing.
 * Test: unit test (in test-filecache.c).
 */
void
Query_CreateMultiparts(
	sqlite3_int64 fileId,
	int           parts
	                   )
{
	int           i;
	int           check;
    int           rc;
    sqlite3_stmt  *setMultipartQuery = cacheDatabase.createMultipart;

    LockCache( );

	/* Create "parts" entries in the database, each representing a multipart
	   upload section. */
	check = 0;
	for( i = 0; i < parts; i++ )
	{
		BIND_QUERY( rc, int64( setMultipartQuery, 1, fileId ),
		BIND_QUERY( rc, int( setMultipartQuery, 2, i + 1 ),
			) );
		if( rc == SQLITE_OK )
		{
			if( ( rc = sqlite3_step( setMultipartQuery ) ) == SQLITE_DONE )
			{
				check++;
			}
			else
			{
				fprintf( stderr,
						 "Select statement didn't finish with DONE (%i): %s\n",
						 rc, sqlite3_errmsg( cacheDatabase.cacheDb ) );
			}
		}
		else
		{
			fprintf( stderr, "Can't prepare select query (%i): %s\n",
					 rc, sqlite3_errmsg( cacheDatabase.cacheDb ) );
		}

		RESET_QUERY( createMultipart );
	}
	if( check != i )
	{
		fprintf( stderr, "Couldn't write all the parts to the database\n" );
	}

    UnlockCache( );
}



/**
 * Add an upload transfer to the database.
 * @param fileId [in] ID of the file should be uploaded.
 * @param owner [in] uid of the user that made the upload request.
 * @param filesize [in] Size of the file in bytes.
 * @return \a true if the query succeeded, or \a false otherwise.
 * Test: unit test (in test-filecache.c).
 */
bool
Query_AddUpload(
	sqlite3_int64 fileId,
	uid_t         owner,
	long long int filesize
                )
{
    int           rc;
	bool          status    = false;
    sqlite3_stmt  *addQuery = cacheDatabase.addUpload;

    LockCache( );
    BIND_QUERY( rc, int64( addQuery, 1, fileId ),
	BIND_QUERY( rc, int( addQuery, 2, (int) owner ),
	BIND_QUERY( rc, int64( addQuery, 3, filesize ),
		) ) );
    if( rc == SQLITE_OK )
	{
		rc = sqlite3_step( addQuery );
		if( rc == SQLITE_DONE )
		{
			status = true;
		}
		else
		{
			fprintf( stderr,
					 "Select statement didn't finish with DONE (%i): %s\n",
					 rc, sqlite3_errmsg( cacheDatabase.cacheDb ) );
			status = false;
		}
	}
	else
    {
        fprintf( stderr, "Can't prepare select query (%i): %s\n",
				 rc, sqlite3_errmsg( cacheDatabase.cacheDb ) );
    }

	RESET_QUERY( addUpload );
    UnlockCache( );

    return( status );
}



/**
 * Fetch file information for a file in the upload queue.
 * @param fileId [in] ID of the file.
 * @param part [out] Part number that is available for upload.
 * @param bucket [out] Name of the S3 bucket.
 * @param remotePath [out] File path on the S3 storage.
 * @param uploadId [out] Upload for the file, or \a "NULL" if no upload ID has
 *        been assigned yet.
 * @param uid [out] uid owner of the file.
 * @param gid [out] gid owner of the file.
 * @param permissions [out] Access permissions of the file.
 * @param filesize [out] Size of the file in bytes.
 * @param localPath [out] Local file name relative to the cache directory.
 * @param keyId [out] The user's Amazon Access ID.
 * @param secretKey [out] The user's secret key.
 * @return \a true if a file or a file part is ready for upload, or \a false
 *         if no file is currently available.
 * Test: unit test (in test-filecache.c).
 */
bool
Query_GetUpload(
	sqlite3_int64 fileId,
	int           *part,
	char          **bucket,
	char          **remotePath,
	char          **uploadId,
	uid_t         *uid,
	gid_t         *gid,
	int           *permissions,
	long long int *filesize,
	char          **localPath,
	char          **keyId,
	char          **secretKey
	            )
{
    int           rc;
    sqlite3_stmt  *getQuery = cacheDatabase.getUpload;
	int           count;

	const char *query_bucket;
	const char *query_remotePath;
	const char *query_uploadId;
	const char *query_localpath;
	const char *query_localname;
	const char *query_keyId;
	const char *query_secretKey;
	char       *localfilepath;

	*bucket     = NULL;
	*remotePath = NULL;
	*uploadId   = NULL;
	*localPath  = NULL;
	*keyId      = NULL;
	*secretKey  = NULL;

	count = 0;
    LockCache( );
    BIND_QUERY( rc, int64( getQuery, 1, fileId ), );
    if( rc == SQLITE_OK )
	{
		while( ( rc = sqlite3_step( getQuery ) ) == SQLITE_ROW )
		{
			query_bucket     = (const char*) sqlite3_column_text( getQuery, 0 );
			query_remotePath = (const char*) sqlite3_column_text( getQuery, 1 );
			query_uploadId   = (const char*) sqlite3_column_text( getQuery, 2 );
			*part            = sqlite3_column_int( getQuery, 3 );
			*uid             = sqlite3_column_int( getQuery, 4 );
			*gid             = sqlite3_column_int( getQuery, 5 );
			*permissions     = sqlite3_column_int( getQuery, 6 );
			*filesize        = sqlite3_column_int64( getQuery, 7 );
			query_localpath  = (const char*)sqlite3_column_text( getQuery, 8 );
			query_localname  = (const char*)sqlite3_column_text( getQuery, 9 );
			query_keyId      = (const char*)sqlite3_column_text( getQuery, 10 );
			query_secretKey  = (const char*)sqlite3_column_text( getQuery, 11 );

			/* We expect no more than one row. */
			count++;
			if( 1 < count )
			{
				fprintf( stderr, "Multiple rows returned.\n" );
				free( *bucket );
				free( *remotePath );
				free( *uploadId );
				free( *localPath );
				free( *keyId );
				free( *secretKey );
			}

			/* Copy string values to the caller. */
			*bucket     = strdup( query_bucket );
			*remotePath = strdup( query_remotePath );
			if( query_uploadId != NULL )
			{
				*uploadId = strdup( query_uploadId );
			}
			else
			{
				*uploadId = NULL;
			}
			*keyId      = strdup( query_keyId );
			*secretKey  = strdup( query_secretKey );
			/* Create the local file path. */
			localfilepath = malloc( strlen( query_localname )
									+ strlen( query_localpath )
									+ sizeof( char ) * 2 );
			strcpy( localfilepath, query_localname );
			strcat( localfilepath, "/" );
			strcat( localfilepath, query_localpath );
			*localPath = localfilepath;
		}
		if( rc != SQLITE_DONE )
		{
			fprintf( stderr,
					 "Select statement didn't finish with DONE (%i): %s\n",
					 rc, sqlite3_errmsg( cacheDatabase.cacheDb ) );
		}
	}
	else
    {
        fprintf( stderr, "Can't prepare select query (%i): %s\n",
				 rc, sqlite3_errmsg( cacheDatabase.cacheDb ) );
    }
	RESET_QUERY( getUpload );
    UnlockCache( );

	return( count ? true : false );
}



/**
 * Add the upload ID for a multipart upload to the transfer information.
 * @param fileId [in] ID of the file.
 * @param uploadId [in] The upload ID.
 * @return Nothing.
 * Test: unit test (in test-filecache.c).
 */
void
Query_SetUploadId(
	sqlite3_int64 fileId,
	char          *uploadId
	              )
{
    int           rc;
    sqlite3_stmt  *setQuery = cacheDatabase.setUploadId;

    LockCache( );
	BIND_QUERY( rc, text( setQuery, 1, uploadId, -1, NULL ),
    BIND_QUERY( rc, int64( setQuery, 2, fileId ),
		) );
    if( rc == SQLITE_OK )
	{
		if( ( rc = sqlite3_step( setQuery ) ) == SQLITE_DONE )
		{
			/* No action. */
		}
		else
		{
			fprintf( stderr,
					 "Select statement didn't finish with DONE (%i): %s\n",
					 rc, sqlite3_errmsg( cacheDatabase.cacheDb ) );
		}
	}
	else
    {
        fprintf( stderr, "Can't prepare select query (%i): %s\n",
				 rc, sqlite3_errmsg( cacheDatabase.cacheDb ) );
    }

	RESET_QUERY( setUploadId );
    UnlockCache( );
}



/**
 * Determine whether all the parts belonging to a multipart upload have been
 * delivered.
 * @param fileId [in] The ID of the file.
 * @return \a true if all parts have been uploaded, or \a false otherwise.
 */
bool
Query_AllPartsUploaded(
	sqlite3_int64 fileId
	                   )
{
    int           rc;
    sqlite3_stmt  *checkQuery = cacheDatabase.allPartsComplete;
	int           partCount;

    LockCache( );
    BIND_QUERY( rc, int64( checkQuery, 1, fileId ), );
    if( rc == SQLITE_OK )
	{
		while( ( rc = sqlite3_step( checkQuery ) ) == SQLITE_ROW )
		{
			partCount = sqlite3_column_int( checkQuery, 0 );
		}
		if( rc != SQLITE_DONE )
		{
			fprintf( stderr,
					 "Select statement didn't finish with DONE (%i): %s\n",
					 rc, sqlite3_errmsg( cacheDatabase.cacheDb ) );
		}
	}
	else
	{
		fprintf( stderr, "Can't prepare select query (%i): %s\n",
				 rc, sqlite3_errmsg( cacheDatabase.cacheDb ) );
	}

    RESET_QUERY( allPartsComplete );
    UnlockCache( );

	return( partCount == 0 ? true : false );
}



/**
 * Get the ETag for a multipart upload part.
 * @param fileId [in] File ID for the file with multipart uploads.
 * @param part [in] Part number.
 * @return The multipart's ETag or \a "NULL" if no ETag was assigned.
 * Test: unit test (in test-filecache.c).
 */
const char*
Query_GetPartETag(
	sqlite3_int64 fileId,
	int           part
	              )
{
    int           rc;
    sqlite3_stmt  *etagQuery = cacheDatabase.getEtag;
	const char    *query_etag;
	const char    *etag = NULL;

    LockCache( );
    BIND_QUERY( rc, int64( etagQuery, 1, fileId ),;
	BIND_QUERY( rc, int( etagQuery, 2, part ),
		) );
    if( rc == SQLITE_OK )
	{
		while( ( rc = sqlite3_step( etagQuery ) ) == SQLITE_ROW )
		{
			if( etag != NULL )
			{
				fprintf( stderr, "Multiple rows returned." );
				free( (char*) etag );
			}
			query_etag = (const char*) sqlite3_column_text( etagQuery, 0 );
			if( query_etag != NULL )
			{
				etag = strdup( query_etag );
			}
			else
			{
				etag = NULL;
			}
		}
		if( rc != SQLITE_DONE )
		{
			fprintf( stderr,
					 "Select statement didn't finish with DONE (%i): %s\n",
					 rc, sqlite3_errmsg( cacheDatabase.cacheDb ) );
		}
	}
	else
    {
        fprintf( stderr, "Can't prepare select query (%i): %s\n",
				 rc, sqlite3_errmsg( cacheDatabase.cacheDb ) );
    }
	RESET_QUERY( getEtag );
    UnlockCache( );

	return( etag );
}



/**
 * Set the ETag for a multipart upload part.
 * @param fileId [in] File ID for the file with multipart uploads.
 * @param part [in] Part number.
 * @param etag [in] ETag for the part number.
 * @return Nothing.
 * Test: unit test (in test-filecache.c).
 */
void
Query_SetPartETag(
	sqlite3_int64 fileId,
	int           part,
	const char    *etag
	              )
{
    int          rc;
    sqlite3_stmt *etagQuery = cacheDatabase.setEtag;
	int          changes;

    LockCache( );
    BIND_QUERY( rc, text( etagQuery, 1, etag, -1, NULL ),
    BIND_QUERY( rc, int64( etagQuery, 2, fileId ),
    BIND_QUERY( rc, int( etagQuery, 3, part ),
		) ) );
    if( rc == SQLITE_OK )
	{
		if( ( rc = sqlite3_step( etagQuery ) ) == SQLITE_DONE )
		{
			if( ( changes = sqlite3_changes( cacheDatabase.cacheDb ) ) != 1 )
			{
				fprintf( stderr, "Invalid number of records changed (%d)\n",
						 changes );
			}
		}
		else
		{
			fprintf( stderr,
					 "Select statement didn't finish with DONE (%i): %s\n",
					 rc, sqlite3_errmsg( cacheDatabase.cacheDb ) );
		}
	}
	else
    {
        fprintf( stderr, "Can't prepare select query (%i): %s\n",
				 rc, sqlite3_errmsg( cacheDatabase.cacheDb ) );
    }
	RESET_QUERY( setEtag );
    UnlockCache( );
}



/**
 * Get the file ID of the next file that is waiting in the upload queue.
 * @return The ID of the next file in the upload queue, or \a 0 if no files
 * are pending.
 * Test: unit test (in test-filecache.c).
 */
sqlite3_int64
Query_FindPendingUpload(
	void
	                    )
{
    int           rc;
    sqlite3_stmt  *findQuery = cacheDatabase.findUploadRequest;
	sqlite3_int64 fileId;

    LockCache( );
	if( ( rc = sqlite3_step( findQuery ) ) == SQLITE_ROW )
	{
		fileId = sqlite3_column_int( findQuery, 0 );

		if( ( rc != SQLITE_DONE ) && ( rc != SQLITE_ROW ) )
		{
			fprintf( stderr,
					 "Select statement didn't finish with DONE (%i): %s\n",
					 rc, sqlite3_errmsg( cacheDatabase.cacheDb ) );
		}
	}
	else
	{
		fileId = 0;
	}
	RESET_QUERY( findUploadRequest );
    UnlockCache( );

	return( fileId );
}



/**
 * Delete an upload transfer and all of its associated transfer parts from
 * the queue of transfers.
 * @param fileId [in] ID of the file that should be deleted from the transfer
 *        queue.
 * @return \a true if the file was deleted, or \a false otherwise.
 * Test: implied blackbox (in GetSubscriptionFromUploadQueue in
 *       test-uploadqueue.c and in test_FindPendingUpload in test-filecache.c).
 */
bool
Query_DeleteUploadTransfer(
	sqlite3_int64 fileId
	                       )
{
	bool         status = false;
	int          rc;
    sqlite3_stmt *deleteQuery = cacheDatabase.deleteUploadTransfer;

	LockCache( );
    BIND_QUERY( rc, int64( deleteQuery, 1, fileId ), );
	if( rc == SQLITE_OK )
    {
		if( ( rc = sqlite3_step( deleteQuery ) ) == SQLITE_DONE )
		{
			status = true;
		}
		else
		{
			fprintf( stderr,
					 "Select statement didn't finish with DONE (%i): %s\n",
					 rc, sqlite3_errmsg( cacheDatabase.cacheDb ) );
		}
	}
	else
	{
        fprintf( stderr, "Can't prepare select query (%i): %s\n",
				 rc, sqlite3_errmsg( cacheDatabase.cacheDb ) );
    }
	RESET_QUERY( deleteUploadTransfer );
	UnlockCache( );

	return( status );
}
