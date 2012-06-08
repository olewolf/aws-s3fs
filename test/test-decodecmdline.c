/**
 * \file test_decodecmdline.c
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



extern void SplitS3MountPath( const char *, char **, char ** );

void test_SplitS3MountPath( const char * );
void test_DecodeCommandLine( const char * );

void DoNotDaemonize( void )
{
    printf( "Will run in foreground\n" );
}


const struct dispatchTable dispatchTable[ ] =
{
    { "SplitS3MountPath", &test_SplitS3MountPath },
    { "DecodeCommandLine", &test_DecodeCommandLine },
    { NULL, NULL }
};



void test_DecodeCommandLine( const char *parms )
{
    struct cmdlineConfiguration cmdlineConfig =
    {
        {
	    US_STANDARD,
	    NULL,
	    NULL,
	    NULL,
	    NULL,
	    NULL,
	    {
	        false,
		false
	    }
	},
	NULL,
	false,
	false,
	false,
	false,
	false,
	false
    };

    const char *const cmdline[ ] =
    {
        /* argc = 13 */
        "aws-s3fs",
	"-b", "bucketname:pathname",
	"-r", "Northern California",
	"-v",
	"-l", "syslog",
	"-k", "key:secret",
	"-c", "configfile",
	"mountdir/dir",

        /* argc = 11 */
	"aws-s3fs",
	"--bucket=bucketname2:",
	"-r", "Northern California",
	"-l", "syslog",
	"-k", "key:secret",
	"-c", "configfile",
	"mountdir/dir",

	/* argc = 3 */
	"aws-s3fs",
	"--bucket=bucketname3",
	"mountdir/dir",

	/* argc = 6 */
	"aws-s3fs",
	"-b", "bucketname4",
	"-p", "path4",
	"mountdir/dir",

	/* argc = 4 */
	"aws-s3fs",
	"-b", ":path5",
	"mountdir/dir",

	/* argc = 6 */
	"aws-s3fs",
	"-b", ":path6",
	"-p", "override",
	"mountdir/dir",

	/* argc = 7 */
	"aws-s3fs",
	"-b", "bucket7:path7",
	"-p", "override1",
	"overridebucket7:overridepath7",
	"mountdir/dir",

	/* argc = 7 */
	"aws-s3fs",
	"-b", "bucket8",
	"-p", "override1",
	"overridebucket8:overridepath8",
	"mountdir/dir",

	/* argc = 7 */
	"aws-s3fs",
	"-b", ":path9",
	"-p", "override1",
	"overridebucket9:overridepath9",
	"mountdir/dir",

	/* argc = 7 */
	"aws-s3fs",
	"-b", "bucket10:path10",
	"-p", "override1",
	"overridebucket10",
	"mountdir/dir",

	/* argc = 7 */
	"aws-s3fs",
	"-b", "bucket11:path11",
	"-p", "override1",
	":overridepath11",
	"mountdir/dir",

	/* argc = 5 */
	"aws-s3fs",
	"-b", "bucket11:path11",
	"-f",
	"mountdir/dir",
    };

    int testNumber;
    char *mountPoint;
    int argcounts[ ] = { 0, 13, 11, 3, 6, 4, 6, 7, 7, 7, 7, 7, 5 };
    int argc;
    int argvbegins;
    int i;

    sscanf( parms, "%d", &testNumber );
    argc       = argcounts[ testNumber ];
    argvbegins = 0;
    for( i = 0; i < testNumber; i++ )
    {
        argvbegins = argvbegins + argcounts[ i ];
    }
    DecodeCommandLine( &cmdlineConfig, &mountPoint, argc, &cmdline[ argvbegins ] );
    PrintConfig( testNumber, &cmdlineConfig, mountPoint, cmdlineConfig.configuration.verbose.value );
    ReleaseConfig( &cmdlineConfig );
}



void test_SplitS3MountPath( const char *parms )
{
    const char *paths[ ] =
    {
        "bucket:path",
	"\"bucket:path\"",
	"bucket:path\"",
	"\"bucket:path",
	"\"bucket\":path",
	" bucket : path ",
	NULL,
	" bucketpath ",
	"bucketpath: ",
	":bucketpath",
	" bucket\\:path ",
	"bucket\\::path"
    };

    const char *s3Path;
    char *bucket = NULL;
    char *path = NULL;
    int  i;

    for( i = 0; i < 12; i++ )
    {
	printf( "%d: ", i );
	s3Path = paths[ i ];

	if( s3Path != NULL ) printf( "%s = ", s3Path );
	else                  printf( "NULL = " );
	SplitS3MountPath( s3Path, &bucket, &path );
	if( bucket != NULL ) printf( "(%s)-", bucket );
	else                 printf( "(NULL)-" );
	if( path != NULL ) printf( "(%s)\n", path );
	else               printf( "(NULL)\n" );
	if( bucket != NULL ) free( bucket );
	if( path   != NULL ) free( path );
    }
}

