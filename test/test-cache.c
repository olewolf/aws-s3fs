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

#pragma GCC diagnostic ignored "-Wunused-parameter"

struct Configuration globalConfig; /*unused*/


static void test_AddEntry( const char *parms );
static void test_FindEntry( const char *parms );
static void test_Overfill( const char *parms );
static void test_DeleteEntry( const char *parms );


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
    InitializeLoggingModule( );
    InitLog( NULL, log_DEBUG );
    EnableLogging( );
}


void test_AddEntry( const char *parms )
{
    InitLogging( );

    int contents1 = 1;
    const char *filename = "file-1";

    InsertCacheElement( filename, &contents1, NULL );
    CloseLog( );
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

    DisableLogging( );
    InsertCacheElement( filename1, &contents1, NULL );
    InsertCacheElement( filename2, &contents2, NULL );
    InsertCacheElement( filename3, &contents3, NULL );
    InsertCacheElement( filename4, &contents4, NULL );
    EnableLogging( );
    InsertCacheElement( filename5, &contents5, NULL );
    found = SearchStatEntry( filename1 );
    if( found == NULL ) printf( "Found correct value\n" );
    else printf( "Found incorrect value\n" );

    DisableLogging( );
    InsertCacheElement( filename1, &contents1, NULL );
    InsertCacheElement( filename2, &contents2, NULL );
    InsertCacheElement( filename3, &contents3, NULL );
    found = SearchStatEntry( filename1 );
    InsertCacheElement( filename4, &contents4, NULL );
    InsertCacheElement( filename5, &contents5, NULL );
    found = SearchStatEntry( filename1 );
    if( *found == 1 ) printf( "Found correct value\n" );
    else printf( "Found incorrect value\n" );

    CloseLog( );
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

    DisableLogging( );
    InsertCacheElement( filename1, &contents1, NULL );
    InsertCacheElement( filename2, &contents2, NULL );
    InsertCacheElement( filename3, &contents3, NULL );
    EnableLogging( );

    sscanf( parms, "%d", &testNumber );
    switch( testNumber )
    {
        case 1:
            found = SearchStatEntry( filename2 );
	    if( *found == 2 ) printf( "Found correct value\n" );
	    else printf( "Found incorrect value\n" );
	    break;

        case 2:
	    found = SearchStatEntry( filename2 );
	    if( *found == 2 ) printf( "Found correct value\n" );
	    else printf( "Found incorrect value\n" );
	    break;

        case 3:
	    found = SearchStatEntry( "doesn't exist" );
	    if( found == NULL ) printf( "Found correct value\n" );
	    else printf( "Found incorrect value\n" );
	    break;
    }
    CloseLog( );
}



static void test_AutoDelete( )
{
    printf( "Delete function for entry 3 called.\n" );
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

    DisableLogging( );
    InsertCacheElement( filename1, &contents1, NULL );
    InsertCacheElement( filename2, &contents2, NULL );
    InsertCacheElement( filename3, &contents3, &test_AutoDelete );
    EnableLogging( );
    DeleteStatEntry( filename2 );

    sscanf( parms, "%d", &testNumber );
    switch( testNumber )
    {
        case 1:
            found = SearchStatEntry( filename2 );
	    if( found == NULL ) printf( "Found correct value\n" );
	    else printf( "Found incorrect value\n" );
	    break;

        case 2:
	    found = SearchStatEntry( filename1 );
	    if( *found == 1 ) printf( "Found correct value\n" );
	    else printf( "Found incorrect value\n" );
	    break;

        case 3:
	    found = SearchStatEntry( filename3 );
	    if( *found == 3 ) printf( "Found correct value\n" );
	    else printf( "Found incorrect value\n" );
	    break;

        case 4:
	    DeleteStatEntry( filename3 );
	    break;
    }
    CloseLog( );
}

