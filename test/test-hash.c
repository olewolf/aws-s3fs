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
#include <string.h>
#include "testfunctions.h"
#include "digest.h"
#include "base64.h"


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
static void test_EncodeBase64( const char *parms );
static void test_DecodeBase64( const char *parms );


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
    { "EncodeBase64", test_EncodeBase64 },
    { "DecodeBase64", test_DecodeBase64 },
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
    fclose( fh );
    if( nBytes == 0 ) exit( EXIT_FAILURE );
    DigestBuffer( &filebuf[ 0 ], nBytes, &md5sum[ 0 ], HASH_MD5, HASHENC_HEX );
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
    success = DigestStream( fh, &md5sum[ 0 ], HASH_MD5, HASHENC_HEX );
    fclose( fh );
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
    fclose( fh );
    if( nBytes == 0 ) exit( EXIT_FAILURE );
    DigestBuffer( &filebuf[ 0 ], nBytes, &sha1sum[ 0 ], HASH_SHA1, HASHENC_HEX );
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
    success = DigestStream( fh, &sha1sum[ 0 ], HASH_SHA1, HASHENC_HEX );
    fclose( fh );
    if( success != 0 ) exit( EXIT_FAILURE );
    sha1sum[ 40 ] = '\0';
    printf( "%s  %s\n", sha1sum, parms );
}



#ifdef MAKE_OPENSSL_TESTS
static void test_SHA1Signature( const char *parms )
{
    FILE* fh;
    int nBytes;
    unsigned char filebuf[ 8192 ];
    const char *signature;

    fh = fopen( parms, "r" );
    if( fh == NULL ) exit( EXIT_FAILURE );
    nBytes = fread( &filebuf, 1, 4096, fh );
    fclose( fh );
    if( nBytes == 0 ) exit( EXIT_FAILURE );
    signature = HMAC( filebuf, nBytes, "TestSecretKey", HASH_SHA1, HASHENC_HEX );
    printf( "HMAC-SHA1(%s)= %s\n", parms, signature );

    free( (char*)signature );
}
#endif



#ifdef MAKE_OPENSSL_TESTS
static void test_MD5Signature( const char *parms )
{
    FILE* fh;
    int nBytes;
    unsigned char filebuf[ 8192 ];
    const char *signature;

    fh = fopen( parms, "r" );
    if( fh == NULL ) exit( EXIT_FAILURE );
    nBytes = fread( &filebuf, 1, 4096, fh );
    fclose( fh );
    if( nBytes == 0 ) exit( EXIT_FAILURE );
    signature = HMAC( filebuf, nBytes, "TestSecretKey", HASH_MD5, HASHENC_HEX );
    printf( "HMAC-MD5(%s)= %s\n", parms, signature );

    free( (char*)signature );
}
#endif



static void test_EncodeBase64( const char *parms )
{
    unsigned char buf[ 256 ];
    int i;
    /*int lf;*/
    char *base64;
    for( i = 0; i < 256; i++ )
    {
        buf[ i ] = i;
    }

    base64 = EncodeBase64( buf, 256 );

    /*    lf = 0;*/
    for( i = 0; i < strlen( base64 ); i++ )
    {
        printf( "%c", base64[ i ] );
	/*
	if( ++lf == 76 )
	{
	    printf( "\n" );
	    lf = 0;
	}
	*/
    }
    printf( "\n" );
}



static void test_DecodeBase64( const char *parms )
{
    const char *base64 = "AAECAwQFBgcICQoLDA0ODxAREhMUFRYXGBkaGxwdHh8gISIjJCUmJygpKissLS4vMDEyMzQ1Njc4OTo7PD0+P0BBQkNERUZHSElKS0xNTk9QUVJTVFVWV1hZWltcXV5fYGFiY2RlZmdoaWprbG1ub3BxcnN0dXZ3eHl6e3x9fn+AgYKDhIWGh4iJiouMjY6PkJGSk5SVlpeYmZqbnJ2en6ChoqOkpaanqKmqq6ytrq+wsbKztLW2t7i5uru8vb6/wMHCw8TFxsfIycrLzM3Oz9DR0tPU1dbX2Nna29zd3t/g4eLj5OXm5+jp6uvs7e7v8PHy8/T19vf4+fr7/P3+/w==";
    int length;
    unsigned char *binary;
    int i;
    int error = 0;

    binary = DecodeBase64( base64, &length );
    printf( "Length: %d\n", length );
    for( i = 0; i < 256; i++ )
    {
        if( binary[ i ] != i )
	{
	    printf( "Binary mismatch at i = %d\n", i );
	    error = 1;
	    break;
	}
    }
    if( error == 0 )
    {
        printf( "Success\n" );
    }
}
