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
    sqlite3_stmt *isCached;
    sqlite3_stmt *incrementSubscription;
    sqlite3_stmt *decrementSubscription;

} cacheDatabase;


struct CacheFileStat
{

};


static void CreateDatabase( sqlite3* cacheDb; );
static void CompileStandardQueries( sqlite3 *cacheDb );
static bool CompileSqlStatement( sqlite3 *db, const char *const sql,
				 sqlite3_stmt **query );
bool IsFileCached( const char *filename );


/* Convenience macro for creating a series of nested "if OK" clauses
   without cluttering the code. */
#define BIND_QUERY( rc, stmt, next ) rc = stmt;   \
                                     if( rc == SQLITE_OK ) { next; }

/* Compile an SQL source entry and add the byte-code to the cacheDatabase
   structure. */
#define CREATESTRUCTELEMENT( x, y ) x.y
#define COMPILESQL( stmt ) CompileSqlStatement( cacheDb, stmt##Sql,   \
                           CREATESTRUCTELEMENT( &cacheDatabase, stmt ) )




void
InitializeFileCacheDatabase(
    void
	                       )
{
    sqlite3 *cacheDb;
    int rc;

    /* Open the database. */
    rc = sqlite3_open( CACHE_DATABASE, &cacheDb );
    if( rc != SQLITE_OK )
    {
        fprintf( stderr, "Cannot open database: %s\n",
				 sqlite3_errmsg( cacheDb ) );
		exit( 1 );
    }
    /* Create tables if necessary. */
    cacheDatabase.cacheDb = cacheDb;
    CreateDatabase( cacheDb );

    /* Compile queries that are often used. */
    CompileStandardQueries( cacheDb );

    /* Create the files directory. Ignore any errors for now (such as that
       the directory already exists), because we only need to react to the
       fact that the local files won't be created unless the directory
       exists. */
    (void) mkdir( CACHE_FILES, S_IRWXU );
}



void
ShutdownFileCacheDatabase(
    void
		                 )
{
    sqlite3_close( cacheDatabase.cacheDb );
}



/**
 * Create the database tables if they do not already exist.
 * @param cacheDb [in] Opened SQLite database.
 * @return Nothing.
 */
static void
CreateDatabase(
    sqlite3 *cacheDb
	           )
{
    char *errMsg;
    int  rc;

    static const char *createSql =
        /* The `files` table maps the remote S3 file path to the local,
		   temporary filename, and maintains the number of subscriptions to
		   the filename.
		   `statcacheinsync` indicates that the file cache has not changed
		   its own file stats since last time the file stat was synchronized
		   with that of the stat cache.
		   `fileinsync` indicates that the local file has not changed since
		   the last time the file was synchronized with the remote host. */
        "CREATE TABLE IF NOT EXISTS files(                   \
            id INTEGER PRIMARY KEY,                          \
            remotename VARCHAR( 4096 ) NOT NULL UNIQUE,      \
            localname VARCHAR( 6 ) NOT NULL,                 \
            subscriptions INT NOT NULL DEFAULT \'1\',        \
            uid INTEGER NOT NULL,                            \
            gid INTEGER NOT NULL,                            \
            permissions INTEGER NOT NULL,                    \
            atime DATETIME NULL,                             \
            mtime DATETIME NULL,                             \
            statcacheinsync BOOLEAN NOT NULL DEFAULT \'1\',  \
            filechanged BOOLEAN NOT NULL DEFAULT \'0\'       \
        );                                                   \
        CREATE INDEX IF NOT EXISTS remotename_id ON files( remotename ); "

		/* The `users` table contains users and their keys. */
		"CREATE TABLE IF NOT EXISTS users(                 \
            uid INTEGER UNIQUE NOT NULL,                   \
            key VARCHAR( 61 ) NOT NULL                     \
        ); "
        "CREATE INDEX IF NOT EXISTS id ON users( uid ); "

        /* The `transfers` table contains a list of current uploads or
		   downloads. */
	    "CREATE TABLE IF NOT EXISTS transfers(                     \
            id INTEGER PRIMARY KEY,                                \
            owner INTEGER NOT NULL REFERENCES users( uid ),        \
            file INTEGER NOT NULL REFERENCES files( id ),          \
            direction CHARACTER( 1 )                               \
                CONSTRAINT dir_chk                                 \
                CHECK( direction = \'u\' OR direction = \'d\' ),   \
	        uploadId VARCHAR( 57 )								   \
        ); "

        /* Transferparts lists the parts of an S3 multipart upload. */
        "CREATE TABLE IF NOT EXISTS transferparts(                  \
	        id INTEGER PRIMARY KEY,                                 \
	        transfer INTEGER REFERENCES transfers( id ) NOT NULL,	\
            part INTEGER NOT NULL,                                  \
	        inprogress BOOLEAN NOT NULL,                            \
            etag VARCHAR( 32 ) NULL                                 \
	    ); "
		"";

    rc = sqlite3_exec( cacheDb, createSql, NULL, NULL, &errMsg );
    if( rc != SQLITE_OK )
    {
        fprintf( stderr, "Cannot create table (%i): %s\n", rc, errMsg );
		sqlite3_free( errMsg );
		sqlite3_close( cacheDb );
		exit( 1 );
    }
}



/**
 * Precompile the often-used SQL queries and place them in the cacheDatabase
 * structure.
 * @param cacheDb [in] Opened SQLite database.
 * @return Nothing.
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
        "INSERT INTO files( uid, gid, permissions, mtime,  \
                            remotename, localname )        \
        VALUES( ?, ?, ?, ?, ?, ? );";

    const char *const isCachedSql =
        "SELECT COUNT( remotename ) AS cached FROM files WHERE remotename = ?;";

    const char *const incrementSubscriptionSql = 
        "UPDATE files SET subscriptions = subscriptions + 1   \
         WHERE remotename = ?;";

    const char *const decrementSubscriptionSql = 
        "UPDATE files SET subscriptions = subscriptions - 1   \
         WHERE remotename = ?;";

    COMPILESQL( fileStat );
    COMPILESQL( newFile );
    COMPILESQL( isCached );
    COMPILESQL( incrementSubscription );
    COMPILESQL( decrementSubscription );

}



/**
 * Compile the specified SQL source query to sqlite byte code. Note that this
 * function does not support multi-entry queries.
 * @param db [in] Opened SQLite database.
 * @param sql [in] Source SQL query.
 * @param query [out] Destination for the byte code.
 * @return \true if the query was successfully compiled, or \a false otherwise.
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
 * @return \a true if the file is cached, or \a false otherwise.
 */
bool IsFileCached(
    const char *path
		                )
{
    int rc;
    int isCached;

    /* Search for the remote filename in the database. */
    LockCache( );
    BIND_QUERY( rc, sqlite3_bind_text( cacheDatabase.isCached,
									   1, path, -1, NULL ), );
    if( rc == SQLITE_OK )
    {
        fprintf( stderr, "Can't prepare select query (%i): %s\n",
				 rc, sqlite3_errmsg( cacheDatabase.cacheDb ) );
    }
    /* Get the boolean value for the search for the path. */
    while( ( rc = sqlite3_step( cacheDatabase.isCached ) ) == SQLITE_ROW )
    {
        isCached = sqlite3_column_int( cacheDatabase.isCached, 0 );
    }
    UnlockCache( );

    if( rc != SQLITE_DONE )
    {
        fprintf( stderr, "Select statement didn't finish with DONE (%i): %s\n",
				 rc, sqlite3_errmsg( cacheDatabase.cacheDb ) );
    }

    return( isCached != 0 ? true : false );
}



/**
 * Create a local filename for the S3 file and create a database entry
 * with the two file names and the S3 file attributes.
 * @param path [in] Full S3 pathname for the file.
 * @param uid [in] Ownership of the S3 file.
 * @param gid [in] Ownership of the S3 file.
 * @param permissions [in] Permissions for the S3 file.
 * @param mtime [in] Last modification time of the S3 file.
 * @param localfile [out] Filename of the local file.
 * @return ID for the database entry, or \a -1 if an error occurred.
 */
sqlite3_int64
Query_CreateLocalFile(
    const char *path,
    int        uid,
    int        gid,
    int        permissions,
    time_t     mtime,
    char       *localfile
		)
{
    int           rc;
    sqlite3_stmt  *query = cacheDatabase.newFile;
    sqlite3_int64 id;


    /* Insert the filename combo into the database. */
    BIND_QUERY( rc, sqlite3_bind_int( query, 1, uid ),
    BIND_QUERY( rc, sqlite3_bind_int( query, 2, gid ),
    BIND_QUERY( rc, sqlite3_bind_int( query, 3, permissions ),
    BIND_QUERY( rc, sqlite3_bind_int( query, 4, mtime ),
    BIND_QUERY( rc, sqlite3_bind_text( query, 5, path, -1, NULL ),
    BIND_QUERY( rc, sqlite3_bind_text( query, 6, localfile, -1, NULL ),
		) ) ) ) ) );
    if( rc != SQLITE_OK )
    {
        fprintf( stderr, "Error binding value in insert (%i): %s\n", rc,
				 sqlite3_errmsg( cacheDatabase.cacheDb ) );
		exit( 1 );
    }
    rc = sqlite3_step( query );
    if( rc != SQLITE_DONE )
    {
        fprintf( stderr, "Insert statement failed (%i): %s\n", rc,
				 sqlite3_errmsg( cacheDatabase.cacheDb) );
    }
    id = sqlite3_last_insert_rowid( cacheDatabase.cacheDb );
    sqlite3_finalize( query );

    return( id );
}


