/**
 * \file test_configfile.c
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
#include <libconfig.h>
#include "aws-s3fs.h"
#include "testfunctions.h"


#pragma GCC diagnostic ignored "-Wunused-parameter"

extern const char *LookupConfigString( const struct config_t *, const char * );
extern void ConfigSetBoolean( struct ConfigurationBoolean *,
			      const struct config_t *, const char * );
extern bool ReadConfigFile( const char *, struct Configuration * );

void test_LookupConfigString( const char * );

void DoNotDaemonize( void ) { }


const struct dispatchTable dispatchTable[ ] =
{
    { "LookupConfigString", &test_LookupConfigString },
    /*
    { "ConfigSetBoolean", &test_ConfigSetBoolean },
    */
    { NULL, NULL }
};



void test_LookupConfigString( const char *param )
{
    struct config_t config;
    const char *configRegion;
    int readSuccess;

    config_init( &config );
    if( ( readSuccess = config_read_file( &config, "../../testdata/lookupconfigstring-1.conf" ) ) != CONFIG_TRUE )
    {
      printf( "Read failure\n" );
	exit( EXIT_FAILURE );
    }
    configRegion = LookupConfigString( &config, "region" );
    printf( "1: %s\n", configRegion );
    configRegion = LookupConfigString( &config, "nonexistent" );
    printf( "2: %s\n", configRegion );
    config_destroy( &config );

    config_init( &config );
    if( ( readSuccess = config_read_file( &config, "../../testdata/doesnotexist" ) ) != CONFIG_TRUE )
    {
        printf( "3: Does not exist\n" );
    }
    config_destroy( &config );

    config_init( &config );
    if( ( readSuccess = config_read_file( &config, "../../testdata/lookupconfigstring-2.conf" ) ) != CONFIG_TRUE )
    {
        printf( "4: Bad file format\n" );
    }
}
