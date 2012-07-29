/**
 * \file dispatch.c
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
#include <string.h>
#include <stdlib.h>
#include "testfunctions.h"



extern const struct dispatchTable dispatchTable[ ];


/* Called with the name of the test script as the first argument and
   optional parameters as the second argument. */
int
main( int argc, char **argv )
{
    const char *script     = argv[ 1 ];
    const char *parameters = argv[ 2 ];
    int i = 0;

    if( ( argc != 2 ) && ( argc != 3 ) )
    {
        printf( "Error: Must be invoked with a script name\n" );
		exit( EXIT_FAILURE );
    }

    do
    {
        if( strcasecmp( dispatchTable[ i ].script, script ) == 0 )
		{
			(*dispatchTable[ i ].function)( parameters );
			exit( EXIT_SUCCESS );
		}
        i++;
    } while( dispatchTable[ i ].script != NULL );

	printf( "Error: Invalid script name\n" );
    exit( EXIT_FAILURE );
}

