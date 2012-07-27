/**
 * \file 
 * \brief Shared functions for testing.
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
#include <ctype.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "aws-s3fs.h"
#include "filecache.h"
#include "testfunctions.h"

extern struct Configuration globalConfig;

extern void InitializeFileCacheDatabase( void );


void PrintConfig( int testNo, const struct CmdlineConfiguration *config, bool verbose )
{
    printf( "%d: R %d ", testNo, config->configuration.region );
    printf( "B %s ", config->configuration.bucketName );
    printf( "P %s ", config->configuration.path );
    printf( "k %s:%s ", config->configuration.keyId, config->configuration.secretKey );
    printf( "l %s ", config->configuration.logfile );
    printf( "v %d ", verbose );
    printf( "m %s ", config->configuration.mountPoint );
    printf( "c %s ", config->configFile );
    printf( "d %d\n", config->configuration.daemonize );
}


void ReleaseConfig( struct CmdlineConfiguration *config )
{
    struct Configuration *conf = &config->configuration;

    conf->region = US_STANDARD;
    free( conf->bucketName );
    conf->bucketName = NULL;
    free( conf->path );
    conf->path = NULL;
    free( conf->keyId );
    conf->keyId = NULL;
    free( conf->secretKey );
    conf->secretKey = NULL;
    free( conf->logfile );
    conf->logfile = NULL;
    conf->verbose.value = false;
    conf->verbose.isset = false;
    conf->daemonize = true;
    free( config->configFile );
    config->configFile = NULL;
    config->regionSpecified = false;
    config->bucketNameSpecified = false;
    config->pathSpecified = false;
    config->keyIdSpecified = false;
    config->secretKeySpecified = false;
    config->logfileSpecified = false;
}



void ReadLiveConfig( const char *param )
{
    FILE               *conf;
    char               buf[ 100 ];
    int                i;
    char               *configKey   = NULL;
    char               *configValue = NULL;

    globalConfig.bucketName = NULL;
    globalConfig.keyId      = NULL;
    globalConfig.secretKey  = NULL;

    if( param == NULL )
    {
        printf( "Config file with authentication data required for live tests.\n" );
	exit( 77 );
    }

    /* Read the test config file. */
    conf = fopen( param, "r" );
    if( ! conf )
    {
        printf( "Cannot open config file \"%s\".\n", param );
		exit( 77 );
    }
    while( ! feof( conf ) )
    {
        if( fgets( buf, sizeof( buf ), conf ) != NULL )
		{
			if( ( buf[ 0 ] != '#' ) && ( ! isspace( buf[ 0 ] ) ) )
			{
				configKey = &buf[ 0 ];
				configValue = NULL;
				for( i = 0; ( buf[ i ] != '\0' ) && ( buf[ i ] != '\n' ); i++ )
				{
					if( buf[ i ] == ':' )
					{
						configValue = &buf[ i + 1 ];
						break;
					}
				}
				if( configValue != NULL )
				{
					while( configValue[ i ] != '\0' )
					{
						if( configValue[ i ] == '\n' )
						{
							configValue[ i ] = '\0';
						}
						i++;
					}
				}
				if( strncmp( configKey, "key", 3 ) == 0 )
	        {
				i = 0;
				while( configValue[ i ] != ':' ) i++;
				globalConfig.keyId = malloc( i + 1 );
				strncpy( globalConfig.keyId, configValue, i );
				i++;
				globalConfig.secretKey = malloc( strlen( &configValue[ i ] ) + 1 );
				strcpy( globalConfig.secretKey, &configValue[ i ] );
			}
				else if( strncmp( configKey, "bucket", 6 ) == 0 )
				{
					globalConfig.bucketName = strdup( configValue );
				}
				else if( strncmp( configKey, "region", 6 ) == 0 )
				{
					globalConfig.region = atoi( configValue );
				}
				else
				{
					printf( "Config key not recognized.\n" );
				}
			}
		}
    }
}



void CheckSQLiteUtil( void )
{
#ifndef HAVE_SQLITE_UTIL
	printf( "sqlite3 not found; skipping test.\n" );
	exit( 77 );
#endif
}


#ifdef AUTOTEST_WITH_FILECACHE
void CreateDatabase( void )
{
	unlink( CACHE_DATABASE );
	mkdir( CACHE_DIR, 0700 );
	mkdir( CACHE_FILES, 0755 );
	mkdir( CACHE_INPROGRESS, 0700 );
	InitializeFileCacheDatabase( );
}


void FillDatabase( void )
{
	CheckSQLiteUtil( );
	CreateDatabase( );
	if( system( "echo \"PRAGMA foreign_keys = ON;\n.read ../../testdata/cache.sql\" | sqlite3 " CACHE_DATABASE ) != 0 )
	{
		exit( EXIT_FAILURE );
	}
}
#endif /* AUTOTEST_WITH_FILECACHE */

