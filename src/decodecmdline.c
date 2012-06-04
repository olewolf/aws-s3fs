/**
 * \file decodecmdline.c
 * \brief Decode the command line, setting configurations or printing info.
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
#include <stdio.h>
#include <string.h>
#include <getopt.h>
#include "aws-s3fs.h"


/**
 * Print the version of this software and exit.
 * @param commandName [in] The filename of this file's executable.
 * @return Nothing.
 */
static void
PrintSoftwareVersion( const char *commandName )
{
    printf( "%s version %s\n", commandName, VERSION );
}



/**
 * Print a help screen for this software and exit.
 * @param commandName [in] The filename of this file's executable.
 * @return Nothing.
 */
static void
PrintSoftwareHelp( const char *commandName )
{
    printf( "Usage:\n    %s [options] [bucket:[path]] dir\n\n", commandName );
    printf( "Options:\n" );
    printf( "      -h, --help            Print help and exit\n" );
    printf( "      -V, --version         Print version and exit\n" );
    printf( "      -r, --region=region   Set the region for the S3 bucket\n" );
    printf( "      -b, --bucket=bucket   Set the name of the S3 bucket\n" );
    printf( "      -p, --path=path       Set the path relative to the root of the S3 bucket\n" );
    printf( "      -k, --key=xxxx:yyyy   Set the access key and secret key\n" );
    printf( "      -c, --config=file     Specify an alternative configuration file\n" );
    printf( "      -l, --log=file|syslog Specify a log file, or syslog\n" );
    printf( "      -v, --verbose         Generate verbose output\n" );
    printf( "      -L, --license         Print licensing information and exit\n" );
    printf( "\nFor further help, see the man page for aws-s3fs(1).\n" );
}



/**
 * Print license information for this software.
 * @param commandName [in] The filename of this file's executable.
 * @return Nothing.
 */
static void
PrintSoftwareLicense( const char *commandName )
{
    printf( "%s Copyright (C) 2012 Ole Wolf\n", commandName );

    printf( "This program comes with ABSOLUTELY NO WARRANTY. This is free software, and you\n" );
    printf( "are welcome to redistribute it under the conditions of the GNU General Public\n" );
    printf( "License. See <http://www.gnu.org/licenses/> for details.\n\n" );
}



/**
 * Split an S3 path 
 */
static void
SplitS3MountPath(
    const char     *s3Path,
    /*@out@*/ char **bucket,
    /*@out@*/ char **path
		      )
{
    bool quoted  = false;
    bool escaped = false;
    int  s3PathIdx = 0;
    char character;
    int    bucketStart;
    int    bucketEnd;
    size_t bucketStrLength;
    int    pathStart;
    int    pathEnd;
    size_t pathStrLength;

    /* Determine if the path is quoted. */
    if( s3Path[ s3PathIdx ] == '"' )
    {
        quoted = true;
	s3PathIdx++;
    }
    bucketStart = s3PathIdx;
    /* Determine the beginning and the end of the bucket name part of the
       S3 path. */
    do {
        character = s3Path[ s3PathIdx ];

	/* The ':' marks the delimiter between the bucket and the path
	   unless it has been escaped by a backslash. */
	if( ( character == ':' ) && ( ! escaped ) )
	{
	    bucketEnd = s3PathIdx - 1 ;
	    break;
	}
	/* The end of the string also marks a delimiter between the bucket
	   and the path. */
	else if( character == (char) 0 )
	{
	    bucketEnd = s3PathIdx - 1 ;
	    break;
	}
	/* If the character is a backslash, it begins an escape character
	   unless it is already escaped. */
	else if( ( character == '\\' ) && ( ! escaped ) )
	{
	    escaped = true;
	}
	/* Anything else ends an escape sequence. */
	else
	{
	    escaped = false;
	}
	s3PathIdx++;
    } while( character != (char) 0 );

    /* Skip the ':'. */
    if( s3Path[ s3PathIdx ] == ':' )
    {
        s3PathIdx++;
    }

    /* Determine the beginning and the end of the path part of the S3 path. */
    pathStart = s3PathIdx;
    do {
        character = s3Path[ s3PathIdx ];

        /* If the path is quoted, a non-escaped quote sign marks the end of
	   the path. */
	if( ( quoted ) && ( ( character == '"' ) && ( ! escaped ) ) )
	{
	    pathEnd = s3PathIdx - 1 ;
	    break;
	}
	/* The end of the string also marks the end. */
	else if( character == (char) 0 )
	{
	    pathEnd = s3PathIdx - 1 ;
	    break;
	}
	/* If the character is a backslash, it begins an escape character
	   unless it is already escaped. */
	else if( ( character == '\\' ) && ( ! escaped ) )
	{
	    escaped = true;
	}
	/* Anything else ends an escape sequence. */
	else
	{
	    escaped = false;
	}
	s3PathIdx++;
    } while( character != (char) 0 );

    /* We now have the beginning and the end of the bucket name and the path,
       so copy them to the output. */
    /*@-usedef@ bucketEnd is guaranteed to be valid. */
    bucketStrLength = (size_t)( bucketEnd - bucketStart );
    /*@+usedef@*/
    *bucket = malloc( sizeof( char ) * ( bucketStrLength + 1 ) );
    assert( *bucket != NULL );
    strncpy( *bucket, &s3Path[ bucketStart ], bucketStrLength );
    *bucket[ bucketStrLength ] = (char) 0;

    /*@-usedef@ pathEnd is guaranteed to be valid. */
    pathStrLength = (size_t)( pathEnd - pathStart );
    /*@+usedef@*/
    *path = malloc( sizeof( char ) * ( pathStrLength + 1 ) );
    assert( *path != NULL );
    strncpy( *path, &s3Path[ pathStart ], pathStrLength );
    *path[ pathStrLength ] = (char) 0;
}



void
DecodeCommandLine(
    struct cmdlineConfiguration *cmdlineConfiguration,
    /*@out@*/ char              **mountPoint,
    int                         argc,
    const char * const          *argv
)
{
    /* Use the stdopt library to parse long and short options. */
    /*@-null@*/
    static struct option longOptions[ ] =
    {
        { "help",      no_argument,       NULL, (int)'h' },
        { "version",   no_argument,       NULL, (int)'V' },
        { "license",   no_argument,       NULL, (int)'L' },
	{ "region",    required_argument, NULL, (int)'r' },
	{ "bucket",    required_argument, NULL, (int)'b' },
	{ "path",      required_argument, NULL, (int)'p' },
	{ "logfile",   required_argument, NULL, (int)'l' },
	{ "key",       required_argument, NULL, (int)'k' },
        { "verbose",   no_argument,       NULL, (int)'v' },
        { "config",    required_argument, NULL, (int)'c' },
	{ NULL, 0, NULL, 0 }
    };
    /*@+null@*/
    int  optionCharacter;
    int  optionIndex = 0;
    bool optionError = 0;

    int  remainingArguments;
    char *localMountPoint;
    int  mountArgc;

    assert( cmdlineConfiguration != NULL );

    /* If called without arguments, print the help screen and exit. */
    if( argc == 1 )
    {
        PrintSoftwareHelp( argv[ 0 ] );
	exit( EXIT_SUCCESS );
    }

    /* Decode the command-line switches. */
    while( ( optionCharacter = getopt_long( argc, (char * const *) argv,
	    "hVLr:b:p:l:k:vc:", longOptions, &optionIndex ) )
	   != -1 )
    {
        switch( optionCharacter )
	{
	/* End of options. */
	case 0:
	    break;
	/* Print help information. */
	case 'h':
	    PrintSoftwareHelp( argv[ 0 ] );
	    exit( EXIT_SUCCESS );
	/* Print version information. */
	case 'V':
	    PrintSoftwareVersion( argv[ 0 ] );
	    exit( EXIT_SUCCESS );
	/* Print licensing information. */
	case 'L':
	    PrintSoftwareLicense( argv[ 0 ] );
	    exit( EXIT_SUCCESS );
	/* Set bucket region. */
	case 'r':
	    ConfigSetRegion( &cmdlineConfiguration->configuration.region, optarg, &optionError );
   	    break;
	/* Set bucket name. */
	case 'b':
	    /*@-null@*/
	    ConfigSetPath( &cmdlineConfiguration->configuration.bucketName, optarg );
	    /*@+null@*/
   	    break;
	/* Set path. */
	case 'p':
	    /*@-null@*/
	    ConfigSetPath( &cmdlineConfiguration->configuration.path, optarg );
	    /*@+null@*/
   	    break;
	/* Set log file. */
	case 'l':
	  /*	    ConfigSetPath( &cmdlineConfiguration->configuration.logfile, optarg );*/
	    /*@-null@*/
	    ConfigSetPath( &cmdlineConfiguration->configuration.logfile, "hello" );
	    /*@+null@*/
	    break;
	/* Set access key and secret key. */
	case 'k':
	    /*@-null@*/
	    ConfigSetKey( &cmdlineConfiguration->configuration.keyId,
			  &cmdlineConfiguration->configuration.secretKey,
			  optarg, &optionError );
	    /*@+null@*/
	    break;
	/* Set verbosity. */
	case 'v':
	    verboseOutput = true;
	    break;
	/* Set config file. */
	case 'c':
	    /*@-null@*/
	    ConfigSetPath( &cmdlineConfiguration->configFile, optarg );
	    /*@+null@*/
   	    break;

	case '?':
	    optionError = true;
	    break;

	default:
	    abort( );
	}
    }

    /* Show the help screen if an error is encountered. */
    if( bool_equal( optionError, true ) ) /* i.e., if( optionError == true ) */
    {
        PrintSoftwareHelp( argv[ 0 ] );
	exit(EXIT_SUCCESS );
    }

    /* Parse command line arguments (not options). */
    remainingArguments = argc - optind;

    /* One argument: mount point. The bucket name must be specified in a
       configuration file or as a command-line option. */
    mountArgc = optind;
    if( remainingArguments == 1 )
    {
        /* The bucket name and path stay as they were in the
	   cmdlineConfiguration structure. */
    }
    /* Two arguments: bucket[:path] and mount point. The bucket overrides any
       configuration or command-line options, as does the path if specified. */
    else if ( remainingArguments == 2 )
    {
        SplitS3MountPath( argv[ optind ],
			  &cmdlineConfiguration->configuration.bucketName,
			  &cmdlineConfiguration->configuration.path );
        mountArgc = optind + 1;
    }
    else
    {
        PrintSoftwareHelp( argv[ 0 ] );
    }
    /* Copy the mount path. */
    localMountPoint = malloc( strlen( argv[ mountArgc ] ) + sizeof( char ) );
    assert( localMountPoint != NULL );
    strcpy( localMountPoint, argv[ mountArgc ] );
    *mountPoint = localMountPoint;
}

