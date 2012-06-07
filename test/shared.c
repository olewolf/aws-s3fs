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
#include <string.h>
#include "aws-s3fs.h"
#include "testfunctions.h"



void PrintConfig( int testNo, const struct cmdlineConfiguration *config, const char *mountPoint, bool verbose )
{
    printf( "%d: R %d ", testNo, config->configuration.region );
    printf( "B %s ", config->configuration.bucketName );
    printf( "P %s ", config->configuration.path );
    printf( "k %s:%s ", config->configuration.keyId, config->configuration.secretKey );
    printf( "l %s ", config->configuration.logfile );
    printf( "v %d ", verbose );
    printf( "m %s ", mountPoint );
    printf( "c %s\n", config->configFile );
}


void ReleaseConfig( struct cmdlineConfiguration *config )
{
    struct configuration *conf = &config->configuration;

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
    free( config->configFile );
    config->configFile = NULL;
    config->regionSpecified = false;
    configuration.verbose.value = false;
}

