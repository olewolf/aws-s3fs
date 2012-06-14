/**
 * \file base64.c
 * \brief Functions for encoding and decoding base64 (MIME).
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


static const char const *validBase64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
                                       "abcdefghijklmnopqrstuvwxyz"
                                       "0123456789"
                                       "+/";


char*
EncodeBase64(
    const unsigned char *source,
    int                 length
	     )
{
    int           ch;
    unsigned char array3[ 3 ];
    unsigned char array4[ 4 ];
    int           a3Idx = 0;
    int           i;

#ifdef AUTOTEST
    char buildBuffer[ 100 ];
#else
    char buildBuffer[ 2048 ];
#endif
    int  buildIdx = 0;
    char *result;
    int  allocCount;
    char *toReturn;

    result     = malloc( sizeof( buildBuffer ) );
    allocCount = 1;
    *result    = '\0';

    /* Expand input string to 4/3. */
    for( ch = 0; ch < length; ch++ )
    {
        array3[ a3Idx++ ] = source[ ch ];

	if( a3Idx == 3 )
	{
	    /* Expand 3 characters into 4. */
	    array4[ 0 ] = (   array3[ 0 ] & 0xfc ) >> 2;
	    array4[ 1 ] = ( ( array3[ 0 ] & 0x03 ) << 4 ) |
	                  ( ( array3[ 1 ] & 0xf0 ) >> 4 );
	    array4[ 2 ] = ( ( array3[ 1 ] & 0x0f ) << 2 ) |
	                  ( ( array3[ 2 ] & 0xc0 ) >> 6 );
	    array4[ 3 ] = (   array3[ 2 ] & 0x3f );
	    /* Change to BASE64 character representation. */
	    for( i = 0; i < 4; i++ )
	    {
	        buildBuffer[ buildIdx++ ] = validBase64[ array4[ i ] ];
	    }
	    /* Allocate a new block if the current block is full. "-6" because
	      we want to leave room for another four characters. sizeof-1 is
	      the last item, and we also need room for a '\0'. */
	    if( buildIdx >= sizeof( buildBuffer ) - 6 )
	    {
	        buildBuffer[ buildIdx ] = '\0';
		strcat( result, buildBuffer );
		result = realloc( result,
				  ( ++allocCount ) * sizeof( buildBuffer ) );
		buildIdx = 0;
		buildBuffer[ buildIdx ] = '\0';
	    }
	    a3Idx = 0;
	}
    }

    /* Process remaining array3[ ] entries. */
    if( a3Idx > 0 )
    {
        /* Clear last one/two characters in array3. */
        for( i = a3Idx; i < 3; i++ )
	{
  	    array3[ i ] = '\0';
	}

	/* Expand 3 characters into 4. */
	array4[ 0 ] = (   array3[ 0 ] & 0xfc ) >> 2;
	array4[ 1 ] = ( ( array3[ 0 ] & 0x03 ) << 4 ) |
	              ( ( array3[ 1 ] & 0xf0 ) >> 4 );
	array4[ 2 ] = ( ( array3[ 1 ] & 0x0f ) << 2 ) |
	              ( ( array3[ 2 ] & 0xc0 ) >> 6 );
	array4[ 3 ] = (   array3[ 2 ] & 0x3f );

	for( i = 0; i <= a3Idx; i++ )
	{
	    buildBuffer[ buildIdx++ ] = validBase64[ array4[ i ] ];
	}
	/* "Zero"-pad until the length is divisible by 3. */
	for( ; i < 4; i++ )
	{
	    buildBuffer[ buildIdx++ ] = '=';
	}
	buildBuffer[ buildIdx ] = '\0';

	strcat( result, buildBuffer );
    }

    /* Copy the working buffer to a result, which probably requires less
       memory. */
    toReturn = malloc( strlen( result ) + sizeof( char ) );
    strcpy( toReturn, result );
    free( result );
    return toReturn;
}

