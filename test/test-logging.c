/**
 * \file test_logging.c
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

#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include "aws-s3fs.h"
#include "testfunctions.h"



extern void Syslog( int, const char *, ... );

void test_Syslog( const char * );


const struct dispatchTable dispatchTable[ ] =
{
    { "Syslog", &test_Syslog },
    { NULL, NULL }
};




void test_Syslog( const char *parms )
{
    int testNumber;
    sscanf( parms, "%d", &testNumber );

    switch( testNumber )
    {
        case 1:
	    InitLog( NULL );
	    EnableLogging( );
	    Syslog( LOG_INFO, "Message %d: %d %s\n", testNumber, 42, "Test" );
	    CloseLog( );
	    break;

        case 2:
	    InitLog( "syslog" );
	    EnableLogging( );
	    Syslog( LOG_INFO, "Message %d: %d %s\n", testNumber, 42, "Test" );
	    CloseLog( );
	    break;

        case 3:
	    InitLog( "test-log.log" );
	    EnableLogging( );
	    Syslog( LOG_INFO, "Message %d: %d %s\n", testNumber, 42, "Test" );
	    CloseLog( );
	    break;
    }
}

