/**
 * \file test_common.c
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
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include "aws-s3fs.h"
#include "testfunctions.h"



extern const FILE *TestFileReadable( const char * );

void test_TestFileReadable( const char * );


const struct dispatchTable dispatchTable[ ] =
{
    { "TestFileReadable", &test_TestFileReadable },
    { NULL, NULL }
};




void test_TestFileReadable( const char *parms )
{
    int testNumber;
    const FILE *fp;
    char filename[ 200 ];

    sscanf( parms, "%d", &testNumber );
    sprintf( filename, "../../testdata/common-%d.conf", testNumber );

    fp = TestFileReadable( filename );
    if( fp == NULL )
    {
        printf( "%d: NULL\n", testNumber );
    }
    else
    {
        printf( "%d: File is readable\n", testNumber );
	fclose( (FILE*) fp );
    }
}
