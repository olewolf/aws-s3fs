/**
 * \file test_md5.c
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
#include "testfunctions.h"

int MD5DigestStream( FILE *stream, char *md5Result );
void MD5DigestBuffer( const char *buffer, size_t len, char *md5Result );

static void test_MD5DigestBuffer( const char *parms );
static void test_MD5DigestStream( const char *parms );


const struct dispatchTable dispatchTable[ ] =
{
    { "MD5DigestBuffer", test_MD5DigestBuffer },
    { "MD5DigestStream", test_MD5DigestStream },
    { "MD5DigestBuffer", test_MD5DigestBuffer },
    { NULL, NULL }
};


static void test_MD5DigestBuffer( const char *parms )
{
    char md5sum[ 33 ];
    FILE* fh;
    int nBytes;
    char filebuf[ 8192 ];

    fh = fopen( parms, "r" );
    if( fh == NULL ) exit( EXIT_FAILURE );
    nBytes = fread( &filebuf, 1, 4096, fh );
    if( nBytes == 0 ) exit( EXIT_FAILURE );
    printf( "%d\n", nBytes );
    MD5DigestBuffer( &filebuf[ 0 ], nBytes, &md5sum[ 0 ] );
    md5sum[ 32 ] = '\0';
    printf( "MD5 Digest: %s\n", md5sum );
}



static void test_MD5DigestStream( const char *parms )
{
    char md5sum[ 33 ];
    FILE* fh;
    int success;

    fh = fopen( parms, "r" );
    if( fh == NULL ) exit( EXIT_FAILURE );
    success = MD5DigestStream( fh, &md5sum[ 0 ] );
    if( success != 0 ) exit( EXIT_FAILURE );
    md5sum[ 32 ] = '\0';
    printf( "MD5 Digest: %s\n", md5sum );
}
