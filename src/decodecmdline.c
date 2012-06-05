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
#include <ctype.h>
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
PrintSoftwareHelp( const char *commandName, bool showOptions )
{
    printf( "Usage:\n    %s [options] [bucket:[path]] dir\n\n", commandName );
    if( showOptions )
    {
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
 * Split an S3 bucket:path.
 */
/*@-exportlocal@*//* for testing purposes*/ void
SplitS3MountPath(
    const char     *s3Path,
    /*@out@*/ char **bucket,
    /*@out@*/ char **path
		 )
/*@+exportlocal@*/
{
    char *bucketName;
    char *pathName;
    int  idx;
    int  stringBeginIdx;
    char character;
    bool escaped;

    if( s3Path == NULL )
    {
        *bucket = NULL;
	*path   = NULL;
	return;
    }

    /* Skip leading whitespace. */
    idx = 0;
    while( isspace( s3Path[ idx ] ) )
    {
        idx++;
    }
    stringBeginIdx = idx;

    /* Search until either a ':' or whitespace is encountered or the
     * end of the string is found. */
    escaped = false;
    do {
        character = s3Path[ idx++ ];
	/* If the character is ':' or whitespace and it isn't escaped, end. */
	if( ( ( character == ':' ) || isspace( character ) )
	    && ( ! escaped ) )
	{
	    break;
	}
	/* If the character is 0, end. */
	if( character == (char) 0 )
	{
	    break;
	}
	/* If the character is a '\', then an escape sequence begins
	   unless the current character is escaped, in which case the
	   escape sequence ends. */
	if( character == '\\' )
	{
	    escaped = ! escaped;
	}
	else
	{
	    escaped = false;
	}
    } while( true );

    /* Copy the string to the bucket name. */
    bucketName = malloc( ( idx - stringBeginIdx ) * sizeof( char ) );
    assert( bucketName != NULL );
    strncpy( bucketName, &s3Path[ stringBeginIdx ],
	     (size_t)( idx - stringBeginIdx - 1 ) );
    bucketName[ idx - stringBeginIdx - 1 ] = (char) 0;
    *bucket = bucketName;

    if( character == (char) 0 )
    {
        pathName = malloc( ( strlen( "" ) + 1 ) * sizeof( char ) );
	assert( pathName != NULL );
	strcpy( pathName, "" );
    }
    else
    {
        /* Skip past the ':' and any whitespace. */
        while( ( ( character = s3Path[ idx ] ) == ':' ) || isspace( character ) )
	{
	    idx++;
	}

	/* Copy the remainder of the string to the path name. */
	pathName = malloc( ( strlen( &s3Path[ idx ] ) + 1 ) * sizeof( char ) );
	assert( pathName != NULL );
	strcpy( pathName, &s3Path[ idx ] );
    }
    *path = pathName;
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
    char *bucket    = NULL;
    char *path      = NULL;

    assert( cmdlineConfiguration != NULL );

    /* If called without arguments, print the help screen and exit. */
    if( argc == 1 )
    {
        PrintSoftwareHelp( argv[ 0 ], true );
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
	    PrintSoftwareHelp( argv[ 0 ], true );
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
	    SplitS3MountPath( optarg, &bucket, &path );
	    /*@-null@*/
	    ConfigSetPath( &cmdlineConfiguration->configuration.bucketName, bucket );
	    /*@+null@*/
	    free( bucket );
	    bucket = NULL;
	    /* If -b bucket:path includes the path, then any -p overrides that
	       path. */
	    if( cmdlineConfiguration->configuration.path == NULL )
	    {
	        /*@-null@*/
	        ConfigSetPath( &cmdlineConfiguration->configuration.path, path );
		/*@+null@*/
	    }
	    free( path );
	    path = NULL;
   	    break;
	/* Set path. */
	case 'p':
	    /*@-null@*/
	    ConfigSetPath( &cmdlineConfiguration->configuration.path, optarg );
	    /*@+null@*/
   	    break;
	/* Set log file. */
	case 'l':
	    /*@-null@*/
	   ConfigSetPath( &cmdlineConfiguration->configuration.logfile, optarg );
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
        PrintSoftwareHelp( argv[ 0 ], false );
	exit(EXIT_FAILURE );
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
    else if( remainingArguments == 2 )
    {
        SplitS3MountPath( argv[ optind ], &bucket, &path );
	if( ( bucket != NULL ) && ( strcmp( "", bucket ) != 0 ) )
        {
	    /*@-null@*/
	    ConfigSetPath( &cmdlineConfiguration->configuration.bucketName, bucket );
	    /*@+null@*/
	    free( bucket );
	}

	if( ( path != NULL ) && ( strcmp( "", path ) != 0 ) )
	{
	    /*@-null@*/
	    ConfigSetPath( &cmdlineConfiguration->configuration.path, path );
	    /*@+null@*/
	}
	free( path );

        mountArgc = optind + 1;
    }
    else
    {
        PrintSoftwareHelp( argv[ 0 ], false );
	exit(EXIT_FAILURE );
    }

    /* Copy the mount path. */
    localMountPoint = malloc( strlen( argv[ mountArgc ] ) + sizeof( char ) );
    assert( localMountPoint != NULL );
    strcpy( localMountPoint, argv[ mountArgc ] );
    *mountPoint = localMountPoint;
}

