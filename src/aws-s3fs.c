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

    /* Initialize modules. */
    InitializeConfiguration( &globalConfig );
    /* Initialize the logging module. */
    InitializeLoggingModule( );
    /* Read configuration settings. */
    Configure( &globalConfig, argc, (const char * const *) argv );
    InitializeS3If( );
    InitLog( globalConfig.logfile, globalConfig.logLevel );

    stat( globalConfig.mountPoint, &st );
    if( ( st.st_mode & S_IFDIR ) == 0 )
    {
        fprintf( stderr, "Bad mount point \"%s\": no such directory\n",
		 globalConfig.mountPoint );
	exit( EXIT_FAILURE );
    }

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
}

