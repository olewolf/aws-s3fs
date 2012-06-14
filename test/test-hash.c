/**
 * \file test-hash.c
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
#include "digest.h"


static void test_MD5DigestBuffer( const char *parms );
static void test_MD5DigestStream( const char *parms );
#ifdef MAKE_OPENSSL_TESTS
static void test_MD5Signature( const char *parms );
#endif
static void test_SHA1DigestBuffer( const char *parms );
static void test_SHA1DigestStream( const char *parms );
#ifdef MAKE_OPENSSL_TESTS
static void test_SHA1Signature( const char *parms );
#endif

#ifdef MAKE_OPENSSL_TESTS
#define MD5Signature     test_MD5Signature
#define SHA1Signature    test_SHA1Signature
#else
#define MD5Signature     SkipTest
#define SHA1Signature    SkipTest

static void SkipTest( const char *parms ) { exit( 77 ); }
#endif




const struct dispatchTable dispatchTable[ ] =
{
    { "MD5DigestBuffer", test_MD5DigestBuffer },
    { "MD5DigestStream", test_MD5DigestStream },
    { "MD5Signature", MD5Signature },
    { "SHA1DigestBuffer", test_SHA1DigestBuffer },
    { "SHA1DigestStream", test_SHA1DigestStream },
    { "SHA1Signature", SHA1Signature },
    { NULL, NULL }
};


static void test_MD5DigestBuffer( const char *parms )
{
    char md5sum[ 33 ];
    FILE* fh;
    int nBytes;
    unsigned char filebuf[ 8192 ];

    fh = fopen( parms, "r" );
    if( fh == NULL ) exit( EXIT_FAILURE );
    nBytes = fread( &filebuf, 1, 4096, fh );
    if( nBytes == 0 ) exit( EXIT_FAILURE );
    DigestBuffer( &filebuf[ 0 ], nBytes, &md5sum[ 0 ], HASH_MD5 );
    md5sum[ 32 ] = '\0';
    printf( "%s  %s\n", md5sum, parms );
}



static void test_MD5DigestStream( const char *parms )
{
    char md5sum[ 33 ];
    FILE* fh;
    int success;

    fh = fopen( parms, "r" );
    if( fh == NULL ) exit( EXIT_FAILURE );
    success = DigestStream( fh, &md5sum[ 0 ], HASH_MD5 );
    if( success != 0 ) exit( EXIT_FAILURE );
    md5sum[ 32 ] = '\0';
    printf( "%s  %s\n", md5sum, parms );
}



static void test_SHA1DigestBuffer( const char *parms )
{
    char sha1sum[ 41 ];
    FILE* fh;
    int nBytes;
    unsigned char filebuf[ 8192 ];

    fh = fopen( parms, "r" );
    if( fh == NULL ) exit( EXIT_FAILURE );
    nBytes = fread( &filebuf, 1, 4096, fh );
    if( nBytes == 0 ) exit( EXIT_FAILURE );
    DigestBuffer( &filebuf[ 0 ], nBytes, &sha1sum[ 0 ], HASH_SHA1 );
    sha1sum[ 40 ] = '\0';
    printf( "%s  %s\n", sha1sum, parms );
}



static void test_SHA1DigestStream( const char *parms )
{
    char sha1sum[ 41 ];
    FILE* fh;
    int success;

    fh = fopen( parms, "r" );
    if( fh == NULL ) exit( EXIT_FAILURE );
    success = DigestStream( fh, &sha1sum[ 0 ], HASH_SHA1 );
    if( success != 0 ) exit( EXIT_FAILURE );
    sha1sum[ 40 ] = '\0';
    printf( "%s  %s\n", sha1sum, parms );
}



#ifdef MAKE_OPENSSL_TESTS
static void test_SHA1Signature( const char *parms )
{
    /* Test against:
         openssl sha1 -hmac TestSecretKey ../README
     */

    FILE* fh;
    int nBytes;
    unsigned char filebuf[ 8192 ];
    const char *signature;

    fh = fopen( parms, "r" );
    if( fh == NULL ) exit( EXIT_FAILURE );
    nBytes = fread( &filebuf, 1, 4096, fh );
    if( nBytes == 0 ) exit( EXIT_FAILURE );
    signature = HMAC( filebuf, nBytes, "TestSecretKey", HASH_SHA1 );
    printf( "HMAC-SHA1(%s)= %s\n", parms, signature );

    free( (char*)signature );
}
#endif



#ifdef MAKE_OPENSSL_TESTS
static void test_MD5Signature( const char *parms )
{
    /* Test against:
         openssl md5 -hmac TestSecretKey ../README
     */

    FILE* fh;
    int nBytes;
    unsigned char filebuf[ 8192 ];
    const char *signature;

    fh = fopen( parms, "r" );
    if( fh == NULL ) exit( EXIT_FAILURE );
    nBytes = fread( &filebuf, 1, 4096, fh );
    if( nBytes == 0 ) exit( EXIT_FAILURE );
    signature = HMAC( filebuf, nBytes, "TestSecretKey", HASH_MD5 );
    printf( "HMAC-MD5(%s)= %s\n", parms, signature );

    free( (char*)signature );
}
#endif
