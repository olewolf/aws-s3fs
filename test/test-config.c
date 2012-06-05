/**
 * \file test_config.c
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
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include "aws-s3fs.h"
#include "testfunctions.h"



bool verboseOutput = false;

extern void InitializeConfiguration( struct configuration * );
extern void ConfigSetRegion( enum bucketRegions *, const char *, bool * );
extern void ConfigSetPath( char **, const char * );
extern void ConfigSetKey( char **, char **, const char *, bool * );
extern int ExtractKey( char **, int, const char * );
extern void Configure( struct configuration *, int, const char * const * );
extern void CopyDefaultString( char **, const char * );

void test_InitializeConfiguration( const char * );
void test_ConfigSetRegion( const char * );
void test_ConfigSetPath( const char * );
void test_ConfigSetKey( const char * );
void test_ExtractKey( const char * );
void test_CopyDefaultString( const char * );
void test_Configure( const char * );


const struct dispatchTable dispatchTable[ ] =
{
    { "InitializeConfiguration", &test_InitializeConfiguration },
    { "ConfigSetRegion", &test_ConfigSetRegion },
    { "ConfigSetPath", &test_ConfigSetPath },
    { "ConfigSetKey", &test_ConfigSetKey },
    { "ExtractKey", &test_ExtractKey },
    { "CopyDefaultString", &test_CopyDefaultString },
    { "Configure", &test_Configure },
    { NULL, NULL }
};



void test_InitializeConfiguration( const char *parms )
{
    struct configuration config =
    {
        TOKYO,        /* region */
	NULL,         /* bucketName */
	NULL,         /* path */
	NULL,         /* keyId */
	NULL,         /* secretKey */
	NULL,         /* logfile */
	{             /* verbose */
	    true,     /* value */
	    true      /* isset */
	}
    };

    InitializeConfiguration( &config );
    printf( "Region: %d vs %d\n", config.region, US_STANDARD );
    printf( "bucketName: %s vs %s\n", config.bucketName, DEFAULT_BUCKETNAME );
    printf( "path: %s vs %s\n", config.path, DEFAULT_PATH );
    printf( "keyId: %s vs %s\n", config.keyId, DEFAULT_KEY_ID );
    printf( "secretKey: %s vs %s\n", config.secretKey, DEFAULT_SECRET_KEY );
    printf( "logfile: %s vs %s\n", config.logfile, DEFAULT_LOG_FILE );
    printf( "verbose.value: %d vs %d\n", config.verbose.value, DEFAULT_VERBOSE );
    printf( "verbose.isset: %d vs %d\n", config.verbose.isset, false );
}


void test_ConfigSetRegion( const char *parms )
{
    enum bucketRegions region;
    bool configError = false;

    ConfigSetRegion( &region, "US Standard", &configError );
    printf( "0: %d %d\n", region, configError );
    ConfigSetRegion( &region, "Northern California", &configError );
    printf( "1: %d %d\n", region, configError );
    ConfigSetRegion( &region, "Unknown", &configError );
    printf( "2: %d %d\n", region, configError );
    configError = false;
    ConfigSetRegion( &region, "orEGON", &configError );
    printf( "3: %d %d\n", region, configError );
    ConfigSetRegion( &region, "Ireland", &configError );
    printf( "4: %d %d\n", region, configError );
    ConfigSetRegion( &region, "Tokyo", &configError );
    printf( "5: %d %d\n", region, configError );
    ConfigSetRegion( &region, "Sao Paulo", &configError );
    printf( "6: %d %d\n", region, configError );
    ConfigSetRegion( &region, "Singapore", &configError );
    printf( "7: %d %d\n", region, configError );
}


void test_ConfigSetPath( const char *parms )
{
    char *path = NULL;

    ConfigSetPath( &path, "/usr/local" );
    printf( "1: %s\n", path );
    ConfigSetPath( &path, "/var/log/syslog" );
    printf( "2: %s\n", path );
    ConfigSetPath( &path, NULL );
    printf( "3: %s\n", path );
}


void test_ConfigSetKey( const char *parms )
{
    char *keyId = NULL;
    char *secretKey = NULL;
    bool configError = false;

    ConfigSetKey( &keyId, &secretKey, "key1:secret1", &configError );
    printf( "1: %s %s %d\n", keyId, secretKey, configError );
    ConfigSetKey( &keyId, &secretKey, "key2: secret2", &configError );
    printf( "2: %s %s %d\n", keyId, secretKey, configError );
    ConfigSetKey( &keyId, &secretKey, "key3:", &configError );
    printf( "3: %s %s %d\n", keyId, secretKey, configError );
    configError = false;
    ConfigSetKey( &keyId, &secretKey, NULL, &configError );
    printf( "4: %s %s %d\n", keyId, secretKey, configError );
}


void test_ExtractKey( const char *parms )
{
    char *key = NULL;
    char *inputKey1 = " accesskeyid1 : secretkey1 ";
    char *inputKey2 = "accesskeyid2:secretkey2";
    char *inputKey3 = NULL;
    char *inputKey4 = "accesskey4 ";
    int  index;

    index = ExtractKey( &key, 0, inputKey1 );
    printf( "1: %s next=%d\n", key, index );
    index = ExtractKey( &key, index, inputKey1 );
    printf( "2: %s next=%d\n", key, index );

    index = ExtractKey( &key, 0, inputKey2 );
    printf( "3: %s next=%d\n", key, index );
    index = ExtractKey( &key, index, inputKey2 );
    printf( "4: %s next=%d\n", key, index );

    index = ExtractKey( &key, 0, inputKey3 );
    printf( "5: %s next=%d\n", key, index );

    index = ExtractKey( &key, 0, inputKey4 );
    printf( "6: %s next=%d\n", key, index );
}



void test_CopyDefaultString( const char *parms )
{
    char *value1 = malloc( 100 );
    char *value2 = NULL;

    strcpy( value1, "**********" );
    CopyDefaultString( &value1, " string 1 " );
    printf( "1: %s\n", value1 );
    CopyDefaultString( &value2, "string 2" );
    printf( "2: %s\n", value2 );
}



void test_Configure( const char *parms )
{
    
}
