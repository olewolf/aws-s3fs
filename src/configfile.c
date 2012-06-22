/**
 * \file configfile.c
 * \brief Read default values from config files.
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
#include <stdio.h>
#include <libconfig.h>
#include "aws-s3fs.h"



/**
 * Read a boolean value from a libopt configuration file, and set a
 * \a configBoolean value to that value. (A \a configBoolean in this software
 * is a value that is true, false, or not set.)
 * @param [out] configBoolean Output for the .value and .isset values of the
 *              boolean configuration value.
 * @param [in] config libopt config structure.
 * @param [in] key Key of the configuration file value.
 * @return Nothing.
 */
#ifndef AUTOTEST
static
#endif
void
ConfigSetBoolean(
    struct ConfigurationBoolean *configBoolean,
    bool                        configValue
		)
{
    configBoolean->isset = true;
    configBoolean->value = ( configValue ? true : false );
}



/**
 * Read settings from the specified config file.
 * @param lockFp [in] File handle for an opened file. This handle is released
 *                    when the function returns.
 * @param configFilename [in] Path to the configuration file.
 * @param configuration [out] Configuration structure, which is filled with
 *                            the configuration file contents.
 * @return \a true if the configuration file was read correctly, or \a false
 *         otherwise.
 */
bool
ReadConfigFile(
    const char           *configFilename,
    struct Configuration *configuration
	      )
{
    struct config_t config;
    int             readSuccess;
    bool            configError = false;
    const char      *configRegion;
    const char      *configBucket;
    const char      *configPath;
    const char      *configKey;
    const char      *configLogfile;
    int             configVerbose;

    /* Open the config file. */
    /*@-compdef@*/
    config_init( &config );

    readSuccess = config_read_file( &config, configFilename );
    /*@+compdef@*/
    if( readSuccess != CONFIG_TRUE )
    {
        /* Report any errors beyond the file not being present. */
        /*@-usedef@*/
        fprintf( stderr, "Config file error in line %d: %s\n",
		 config_error_line( &config ), config_error_text( &config ) );
        /*@+usedef@*/
	configError = true;
	exit( EXIT_FAILURE );
    }
    else
    {
        /* Read the region from the config file. */
        /*@-compdef@*/
        if( config_lookup_string( &config, "region", &configRegion ) )
	{
	    ConfigSetRegion( &configuration->region, configRegion,
			     &configError );
	}
	/* Read the bucket name from the config file. */
        /*@-compdef@*/
	if( config_lookup_string( &config, "bucket", &configBucket ) )
        /*@+compdef@*/
	{
	    ConfigSetPath( &configuration->bucketName, configBucket );
	}
	/* Read the path from the config file. */
        /*@-compdef@*/
	if( config_lookup_string( &config, "path", &configPath ) )
        /*@+compdef@*/
	{
	    /*@-null@*/
	    ConfigSetPath( &configuration->path, configPath );
	    /*@+null@*/
	}
	/* Read the authentication key from the config file. */
        /*@-compdef@*/
	if( config_lookup_string( &config, "key", &configKey ) )
        /*@+compdef@*/
	{
	    /*@-null@*/
	    ConfigSetKey( &configuration->keyId, &configuration->secretKey,
			  configKey, &configError );
	    /*@+null@*/
	}
	/* Read the logfile name from the config file. */
        /*@-compdef@*/
	if( config_lookup_string( &config, "logfile", &configLogfile ) )
        /*@+compdef@*/
	{	
	    ConfigSetPath( &configuration->logfile, configLogfile );
	}
	/* Read the verbosity setting from the config file. */
	if( config_lookup_bool( &config, "verbose", &configVerbose ) )
	{
	     ConfigSetBoolean( &configuration->verbose, configVerbose );
	}
    }
    config_destroy( &config );

    return( ! configError );
}
