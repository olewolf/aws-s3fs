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
#include "aws-s3fs.h"


/** Flag that indicates whether verbose output is on. The flag is set via
    the command-line switches. */
bool verboseOutput = false;

struct configuration configuration =
{
    US_STANDARD,  /* region */
    NULL,         /* bucketName */
    NULL,         /* path */
    NULL,         /* keyId */
    NULL,         /* secretKey */
    NULL,         /* logfile */
    {
        false,    /* value */
	false     /* isset */
    }             /* verbose */
};



/**
 * The entry function decodes the command-line switches and invokes the
 * associated functions.
 * @param argc [in] The number of input arguments, used by the getopt library.
 * @param argv [in] The input argument strings, used by the getopt library.
 * @return 0 if no errors were encountered, 1 otherwise.
 */
int
main( int argc, char **argv )
{
    char *mountPoint;

    Configure( &configuration, &mountPoint, argc, (const char * const *) argv );

    return( 0 );
}
