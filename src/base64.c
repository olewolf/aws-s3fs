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
#include <ctype.h>

#include <stdio.h>

static const char const *toBase64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
                                    "abcdefghijklmnopqrstuvwxyz"
                                    "0123456789"
                                    "+/";

static const char const fromBase64_idx43[ ] =
{
    /* '+' */  62,
               0, 0, 0,
    /* '/' */  63, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 0,
               0, 0, 0, 0, 0, 0,
    /* 'A' */  0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14,
               15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25,
               0, 0, 0, 0, 0, 0,
    /* 'a' */  26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38,
               39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51
};



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
	        buildBuffer[ buildIdx++ ] = toBase64[ array4[ i ] ];
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
	    buildBuffer[ buildIdx++ ] = toBase64[ array4[ i ] ];
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



static inline int isBase64( char ch )
{
    if( isalnum( ch ) || ( ch == '/' ) || ( ch == '+' ) )
    {
        return 1;
    }
    else
    {
        return(0 );
    }
}



unsigned char*
DecodeBase64( const char *source, int *length )
{
    int           sourceLength;
    int           sourceIdx = 0;
    unsigned char array4[ 4 ] = { 0 }; /* Initialized to avoid compiler warn. */
    int           a4Idx = 0;
    unsigned char array3[ 3 ];
    int           i;

#ifdef AUTOTEST
    unsigned char buildBuffer[ 100 ];
#else
    unsigned char buildBuffer[ 2048 ];
#endif
    int           buildIdx      = 0;
    int           decodedLength;
    int           blockBegin;
    unsigned char *result;
    int           allocCount;
    unsigned char *toReturn;

    result        = malloc( sizeof( buildBuffer ) );
    allocCount    = 1;
    blockBegin    = 0;
    decodedLength = 0;

    sourceLength = strlen( source );
    while( ( sourceIdx < sourceLength ) && ( source[ sourceIdx ] != '=' ) &&
	   isBase64( source[ sourceIdx ] ) )
    {
        /* Compact 4 characters into 3. */
	array4[ a4Idx++ ] = source[ sourceIdx++ ];
	if( a4Idx == 4 )
	{
	    for( i = 0; i < 4; i++ )
	    {
	        array4[ i ] = fromBase64_idx43[ array4[ i ] - 43 ];
	    }
	    array3[ 0 ] =   ( array4[ 0 ]          << 2 ) |
	                  ( ( array4[ 1 ] & 0x30 ) >> 4 );
	    array3[ 1 ] = ( ( array4[ 1 ] & 0x0f ) << 4 ) |
	                  ( ( array4[ 2 ] & 0x3c ) >> 2 );
	    array3[ 2 ] = ( ( array4[ 2 ] & 0x03 ) << 6 ) |
	                      array4[ 3 ];
	    for( i = 0; i < 3; i++ )
	    {
		buildBuffer[ buildIdx++ ] = array3[ i ];
	    }
	    decodedLength = decodedLength + 3;

	    /* Allocate a new block if the current block is full. "-5" because
	       we want to leave room for another three characters. sizeof-1 is
	       the last item, and we also need room for a '\0'. */
	    if( buildIdx >= sizeof( buildBuffer ) - 5 )
	    {
		memcpy( &result[ blockBegin ], buildBuffer, buildIdx );
		result = realloc( result,
				  ( ++allocCount ) * sizeof( buildBuffer ) );
		blockBegin = blockBegin + buildIdx;
		buildIdx   = 0;
	    }
	    a4Idx = 0;
	}
    }
 
   /* Process remaining characters. */
    if( a4Idx != 0 )
    {
	for( i = 0; i < a4Idx; i++ )
	{
	    array4[ i ] = fromBase64_idx43[ array4[ i ] - 43 ];
	}
        for( ; i < 4; i++ )
	{
	    array4[ i ] = 0;
	}
	array3[ 0 ] =   ( array4[ 0 ]          << 2 ) |
                      ( ( array4[ 1 ] & 0x30 ) >> 4 );
	array3[ 1 ] = ( ( array4[ 1 ] & 0x0f ) << 4 ) |
	              ( ( array4[ 2 ] & 0x3c ) >> 2 );
	array3[ 2 ] = ( ( array4[ 2 ] & 0x03 ) << 6 ) |
	                  array4[ 3 ];

	for( i = 0; i < a4Idx - 1; i++ )
        {
	    buildBuffer[ buildIdx++ ] = array3[ i ];
	    decodedLength++;
	}
	memcpy( &result[ blockBegin ], buildBuffer, buildIdx );
    }

    /* Copy the working buffer to a result, which probably requires less
       memory. */
    toReturn = malloc( decodedLength );
    memcpy( toReturn, result, decodedLength );
    free( result );

    *length = decodedLength;
    return toReturn;
}

