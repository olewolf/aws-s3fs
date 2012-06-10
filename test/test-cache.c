/**
 * \file test_cache.c
 * \brief .
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
#include <stdio.h>
#include "aws-s3fs.h"
#include "statcache.h"
#include "testfunctions.h"



static void test_AddEntry( const char *parms );
static void test_FindEntry( const char *parms );
static void test_Overfill( const char *parms );
static void test_DeleteEntry( const char *parms );


static struct ThreadsafeLogging logging;


const struct dispatchTable dispatchTable[ ] =
{
    { "AddEntry", test_AddEntry },
    { "FindEntry", test_FindEntry },
    { "Overfill", test_Overfill },
    { "DeleteEntry", test_DeleteEntry },
    { NULL, NULL }
};


static void InitLogging( void )
{
    InitializeLoggingModule( &logging );
    InitLog( &logging, NULL, log_DEBUG );
    EnableLogging( &logging );
}


void test_AddEntry( const char *parms )
{
    InitLogging( );

    int contents1 = 1;
    const char *filename = "file-1";

    InsertCacheElement( &logging, filename, &contents1 );
    CloseLog( &logging );
}


void test_Overfill( const char *parms )
{
    InitLogging( );

    int contents1 = 1;
    int contents2 = 2;
    int contents3 = 3;
    int contents4 = 4;
    int contents5 = 5;
    const char *filename1 = "file-1";
    const char *filename2 = "file-2";
    const char *filename3 = "file-3";
    const char *filename4 = "file-4";
    const char *filename5 = "file-5";

    int *found;

    DisableLogging( &logging );
    InsertCacheElement( &logging, filename1, &contents1 );
    InsertCacheElement( &logging, filename2, &contents2 );
    InsertCacheElement( &logging, filename3, &contents3 );
    InsertCacheElement( &logging, filename4, &contents4 );
    EnableLogging( &logging );
    InsertCacheElement( &logging, filename5, &contents5 );
    found = SearchStatEntry( &logging, filename1 );
    if( found == NULL ) printf( "Found correct value\n" );
    else printf( "Found incorrect value\n" );

    DisableLogging( &logging );
    InsertCacheElement( &logging, filename1, &contents1 );
    InsertCacheElement( &logging, filename2, &contents2 );
    InsertCacheElement( &logging, filename3, &contents3 );
    found = SearchStatEntry( &logging, filename1 );
    InsertCacheElement( &logging, filename4, &contents4 );
    InsertCacheElement( &logging, filename5, &contents5 );
    found = SearchStatEntry( &logging, filename1 );
    if( *found == 1 ) printf( "Found correct value\n" );
    else printf( "Found incorrect value\n" );

    CloseLog( &logging );
}



void test_FindEntry( const char *parms )
{
    int testNumber;

    InitLogging( );

    int contents1 = 1;
    int contents2 = 2;
    int contents3 = 3;
    const char *filename1 = "file-1";
    const char *filename2 = "file-2";
    const char *filename3 = "file-3";

    int *found;

    DisableLogging( &logging );
    InsertCacheElement( &logging, filename1, &contents1 );
    InsertCacheElement( &logging, filename2, &contents2 );
    InsertCacheElement( &logging, filename3, &contents3 );
    EnableLogging( &logging );

    sscanf( parms, "%d", &testNumber );
    switch( testNumber )
    {
        case 1:
            found = SearchStatEntry( &logging, filename2 );
	    if( *found == 2 ) printf( "Found correct value\n" );
	    else printf( "Found incorrect value\n" );
	    break;

        case 2:
	    found = SearchStatEntry( &logging, filename2 );
	    if( *found == 2 ) printf( "Found correct value\n" );
	    else printf( "Found incorrect value\n" );
	    break;

        case 3:
	    found = SearchStatEntry( &logging, "doesn't exist" );
	    if( found == NULL ) printf( "Found correct value\n" );
	    else printf( "Found incorrect value\n" );
	    break;
    }
    CloseLog( &logging );
}



void test_DeleteEntry( const char *parms )
{
    int testNumber;

    InitLogging( );

    int contents1 = 1;
    int contents2 = 2;
    int contents3 = 3;
    const char *filename1 = "file-1";
    const char *filename2 = "file-2";
    const char *filename3 = "file-3";

    int *found;

    DisableLogging( &logging );
    InsertCacheElement( &logging, filename1, &contents1 );
    InsertCacheElement( &logging, filename2, &contents2 );
    InsertCacheElement( &logging, filename3, &contents3 );
    EnableLogging( &logging );
    DeleteStatEntry( &logging, filename2 );

    sscanf( parms, "%d", &testNumber );
    switch( testNumber )
    {
        case 1:
            found = SearchStatEntry( &logging, filename2 );
	    if( found == NULL ) printf( "Found correct value\n" );
	    else printf( "Found incorrect value\n" );
	    break;

        case 2:
	    found = SearchStatEntry( &logging, filename1 );
	    if( *found == 1 ) printf( "Found correct value\n" );
	    else printf( "Found incorrect value\n" );
	    break;

        case 3:
	    found = SearchStatEntry( &logging, filename3 );
	    if( *found == 3 ) printf( "Found correct value\n" );
	    else printf( "Found incorrect value\n" );
	    break;
    }
    CloseLog( &logging );
}


