/**
 * \file common.c
 * \brief Various functions that are used by several unrelated functions.
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
#include <stdarg.h>
#include <string.h>
#include "aws-s3fs.h"


/* Maximum message for each verbose output string. */
#define MAX_OUTPUT_STRING 1024


void
InitializeThreadConfiguration(
    struct configuration *configuration
			      )
{
    configuration->region     = US_STANDARD;
    configuration->bucketName = NULL;
    configuration->path       = NULL;
    configuration->keyId      = NULL;
    configuration->secretKey  = NULL;
    configuration->logfile    = NULL;
    configuration->verbose.value = false;
    configuration->verbose.isset = false;
    configuration->daemonize  = true;
}



/**
 * Write verbose output if the verbose output option has been specified.
 * @param verboseOutput [in] \a true if verbose output is enabled; \a false
 *        otherwise.
 * @param format [in] Formatting string for the output.
 * @param ... [in] Parameters for the format string.
 * @return FILE* pointer, or NULL if the file cannot be read.
 */
void
VerboseOutput(
    bool       verboseOutput,
    const char *format,
    ...
	      )
{
    int ch;
    int idx;
    union {
        int   d;
        float f;
        char  c;
        char  *s;
    } printable;
    char compile[ MAX_OUTPUT_STRING ];
    int  outIdx = 0;
    char argument[ MAX_OUTPUT_STRING ];
    va_list v1;

    va_start( v1, format );

    /* Copy from the format string until a parameter is encountered. */
    idx = 0;
    while( ( ch = format[ idx++ ] ) != '\0' )
    {
        assert( outIdx < MAX_OUTPUT_STRING - 1 );
        if( ch != '%' )
	{
	    compile[ outIdx++ ] = ch;
        }
        else
	{
	    if( format[ idx ] == '%' )
	    {
	        compile[ outIdx++ ] = ch;
	    }
	    else
	    {
	        switch( format[ idx ] )
		{
		    case 'd':
		        printable.d = va_arg( v1, int );
			sprintf( argument, "%d", printable.d );
		        break;
		    case 's':
		        printable.s = va_arg( v1, char* );
			assert( strlen( printable.s ) < MAX_OUTPUT_STRING );
			sprintf( argument, "%s", printable.s );
		        break;
		    case 'f':
		        printable.f = va_arg( v1, double );
			sprintf( argument, "%f", printable.f );
		        break;
		    case 'c':
		      printable.c = (char) va_arg( v1, int );
			sprintf( argument, "%c", printable.c );
		        break;
		    default:
		        argument[ 0 ] = '\0';
		        break;
		}
		assert( outIdx + strlen( argument ) < MAX_OUTPUT_STRING );
		strcpy( &compile[ outIdx ], argument );
		outIdx += strlen( argument );
	    }
	    idx++;
	}
    }
    compile[ outIdx ] = '\0';

    va_end( v1 );

    if( verboseOutput )
    {
	printf( "%s", compile );
    }
}




/**
 * Open a file for reading, and keep the \a FILE* pointer. If the file cannot
 * be opened for reading, the \a FILE* pointer is NULL.
 * @param filename [in] Name of the file that is opened for reading.
 * @return FILE* pointer, or NULL if the file cannot be read.
 */
/*@null@*/ /*@dependent@*/ const FILE*
TestFileReadable(
    const char *filename
		 )
{
    const FILE *fp;
    /* Test whether the specified file exists and is readable. Return NULL
       if it doesn't, or a pointer to the file (to lock it until it is
       closed) if it is readable. */
    fp =  fopen( filename, "r" );
    return( fp );
}
