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



bool verboseOutput = false;

extern void SplitS3MountPath( const char *, char **, char ** );
extern void DecodeCommandLine( struct cmdlineConfiguration *, char **,
			       int, const char * const * );

void test_SplitS3MountPath( const char * );
void test_DecodeCommandLine( const char * );


const struct dispatchTable dispatchTable[ ] =
{
    { "SplitS3MountPath", &test_SplitS3MountPath },
    { "DecodeCommandLine", &test_DecodeCommandLine },
    { NULL, NULL }
};



static void PrintConfig( int testNo, struct cmdlineConfiguration *config, char *mountPoint, bool verbose )
{
    printf( "%d: R %d ", testNo, config->configuration.region );
    printf( "B %s ", config->configuration.bucketName );
    printf( "P %s ", config->configuration.path );
    printf( "k %s:%s ", config->configuration.keyId, config->configuration.secretKey );
    printf( "l %s ", config->configuration.logfile );
    printf( "v %d ", verbose );
    printf( "m %s ", mountPoint );
    printf( "c %s\n", config->configFile );
}


static void ReleaseConfig( struct cmdlineConfiguration *config )
{
    struct configuration *conf = &config->configuration;

    conf->region = US_STANDARD;
    free( conf->bucketName );
    conf->bucketName = NULL;
    free( conf->path );
    conf->path = NULL;
    free( conf->keyId );
    conf->keyId = NULL;
    free( conf->secretKey );
    conf->secretKey = NULL;
    free( conf->logfile );
    conf->logfile = NULL;
    conf->verbose.value = false;
    conf->verbose.isset = false;
    free( config->configFile );
    config->configFile = NULL;
    verboseOutput = false;
}


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
	NULL
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
    };

    int testNumber;
    char *mountPoint;
    int argcounts[ ] = { 0, 13, 11, 3, 6, 4, 6, 7, 7, 7, 7, 7 };
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
    /*
    printf( "argc: %d, argvbegins: %d\n", argc, argvbegins );
    for( i = 0; i < argc; i++ )
    {
      printf( "argv[%d] = \"%s\"\n", i, cmdline[argvbegins+i] );
    }
    */
    DecodeCommandLine( &cmdlineConfig, &mountPoint, argc, &cmdline[ argvbegins ] );
    PrintConfig( testNumber, &cmdlineConfig, mountPoint, verboseOutput );
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



