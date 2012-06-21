/**
 * \file aws-s3fs.c
 * \brief Main entry for fuse mounting the S3 filesystem.
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
#include <string.h>
#include <unistd.h>
#include <fuse/fuse.h>
#include "aws-s3fs.h"
#include "fuseif.h"
#include "s3if.h"


struct Configuration globalConfig;



/**
 * Determine whether required, external programs are available, and prints
 * an error to stderr for each missing program.
 * @return \a true if the required applications support is available, or
 *         \a false otherwise.
 */
static bool CheckAppsSupport( void )
{
    const char *appslist[ ] =
    {
        "curl",
	"aws"
    };
    unsigned int i;
    char syscommand[ 50 ];
    char missingApps[ 1000 ];
    const char *command;

    /* Search for applications from the appslist, and append any missing
     * ones to the missingApps string. */
    missingApps[ 0 ] = (char) 0;
    for( i = 0; i < sizeof( appslist ) / sizeof( char* ); i++ )
    {
        command = appslist[ i ];
        sprintf( syscommand, "which %s >/dev/null 2>&1", command );
	if( system( syscommand ) != EXIT_SUCCESS )
	{
	    if( missingApps[ 0 ] != (char) 0 )
	    {
	        strcat( missingApps, ", " );
	    }
	    strcat (missingApps, command );
	}
    }
    /* Return with status and/or error message. */
    if( missingApps[ 0 ] == (char) 0 )
    {
        return( true );
    }
    else
    {
        fprintf( stderr, "Please install the following missing programs before using aws-s3fs:\n" );
        fprintf( stderr, "  [ %s ]\n", missingApps );
	return( false );
    }
}



/**
 * The entry function decodes the command-line switches and invokes the
 * associated functions.
 * @param argc [in] The number of input arguments, used by the getopt library.
 * @param argv [in] The input argument strings, used by the getopt library.
 * @return EXIT_SUCCESS if no errors were encountered, EXIT_FAILURE otherwise.
 */
int
main( int argc, char **argv )
{
    int         fuseStatus;
    struct stat st;
    int         fuseArgc;
    char        *fuseArgv[ ] = { NULL, NULL, NULL, NULL };

    if( CheckAppsSupport( ) != true )
    {
	exit( EXIT_FAILURE );
    }

    /* Initialize modules. */
    InitializeConfiguration( &globalConfig );
    InitializeLoggingModule( );
    InitializeS3If( );
    /* Read configuration settings. */
    Configure( &globalConfig, argc, (const char * const *) argv );
    /* Initialize the logging module. */
    InitLog( globalConfig.logfile, globalConfig.logLevel );

    stat( globalConfig.mountPoint, &st );
    if( ( st.st_mode & S_IFDIR ) == 0 )
    {
        fprintf( stderr, "Bad mount point \"%s\": no such directory\n",
		 globalConfig.mountPoint );
	exit( EXIT_FAILURE );
    }

    /* Daemonize unless foreground is requested. */
    /*
    if( globalConfig.daemonize )
    {
        Daemonize( );
	printf( "Demonized!\n" );
    }
    */
    /* Connect to FUSE. */
    fuseArgc = 2;
    if( ! globalConfig.daemonize )
    {
        fuseArgc++;
	fuseArgv[ 2 ] = "-f";
    }
    /*
    fuseArgc++;
    fuseArgv[ 3 ] = "-d";
    */
    fuseArgv[ 0] = globalConfig.bucketName;
    fuseArgv[ 1 ] = globalConfig.mountPoint;
    fuseStatus = fuse_main( fuseArgc, fuseArgv, &s3fsOperations, NULL );
    return( fuseStatus );

    return( EXIT_SUCCESS );
}

