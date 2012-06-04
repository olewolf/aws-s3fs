/**
 * \file config.c
 * \brief Functions for decoding and setting configuration options.
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
#include <ctype.h>
#include <string.h>
#include "aws-s3fs.h"





void
ConfigSetRegion(
    enum bucketRegions *region,
    const char         *configValue,
    bool               *configError
	  )
{
    bool               found = false;
    int                nameIdx;
    static const char  *regionNames[ ] = {
        "US Standard", "Oregon", "Northern California", "Ireland",
	"Singapore", "Tokyo", "Sao Paulo"
    };

    if( configValue != NULL )
    {
        /* Look up the specified region name in the region enum. */
        for( nameIdx = 0;
	     nameIdx < (int)( sizeof( regionNames ) / sizeof( char* ) );
	     nameIdx++ )
	{
	    /*@-unrecog@*/
	    if( strcasecmp( regionNames[ nameIdx ], configValue ) != 0 )
	    /*@+unrecog@*/
	    {
		found = true;
		*region = nameIdx;
	    }
	}
	if( found == false )
	{
	    /* If the name wasn't found, flag it as a config file error. */
	    fprintf( stderr, "Invalid region name: %s\n", configValue );
	    *configError = true;
	}
    }
}



void
ConfigSetPath(
    char       **path,
    const char *configPath
	      )
{
    if( configPath != NULL )
    {
        CopyDefaultString( path, configPath );
    }
}



static int
ExtractKey(
    /*@out@*/ char **key,
    int            index,
    const char     *configValue
	   )
{
    char *tempBuffer;
    char *bufferPtr;
    char character;
    char *newKey;

    tempBuffer = malloc( strlen( &configValue[ index ] ) + sizeof( char ) );
    if( tempBuffer != NULL )
    {
        bufferPtr  = tempBuffer;
	while( ( character = configValue[ index ] ) != (char) 0 )
	{
	    /* Skip all whitespace. */
	    if( ! isspace( character ) )
	    {
		/* Copy the character unless the character is a ':' or 0. */
	      if( ( character == ':' ) || ( character == (char) 0 ) )
	      {
		  break;
	      }
	      else
	      {
		  *bufferPtr++ = character;
	      }
	    }
	    index++;
	}
	/* Terminate the buffer. */
	*bufferPtr = (char) 0;
	bufferPtr  = NULL;
	/* Copy the extracted key to its target storage. */
	newKey = malloc( strlen( tempBuffer ) + sizeof( char ) );
	if( newKey != NULL )
	{
	    strcpy( newKey, tempBuffer );
	    *key = newKey;
	}
	free( tempBuffer );

	/* Return the end position, where another key may be stored. */
	if( character != (char) 0 )
	{
	    return( index );
	}
    }

    *key = NULL;
    return( 0 );
}



void
ConfigSetKey(
    char       **keyId,
    char       **secretKey,
    const char *configValue,
    bool       *configError
	     )
{
    int  nextKeyIdx;
    char *invalidSecretKey;
    char *currentKeyId;
    char *currentSecretKey;
    char *newKeyId;
    char *newSecretKey;

    /* Allocate a default, invalid secret key and return if this isn't
       possible. (It will always be possible to malloc this little memory,
       and it is only really necessary to work around a lint warning that
       the new secret key may become null.) */
    invalidSecretKey = malloc( strlen( "*" ) + sizeof( char ) );
    if( invalidSecretKey == NULL )
    {
        return;
    }
    strcpy( invalidSecretKey, "*" );

    /* Release the current keys. */
    currentKeyId = *keyId;
    if( currentKeyId != NULL )
    {
        free( currentKeyId );
	*keyId = currentKeyId = NULL;
    }
    if( *secretKey != NULL )
    {
        currentSecretKey = *secretKey;
        free( currentSecretKey );
	*secretKey = currentSecretKey = NULL;
    }

    /* Extract the Access Key. */
    nextKeyIdx = ExtractKey( &newKeyId, 0, configValue );
    *keyId = newKeyId;
    /* Extract the Secret Key. */
    if( nextKeyIdx != 0 )
    {
	(void)ExtractKey( &newSecretKey, nextKeyIdx, configValue );
	*secretKey = newSecretKey;
	free( invalidSecretKey );
    }
    else
    {
        *secretKey = invalidSecretKey;
	*configError = true;
	fprintf( stderr, "Secret key not found.\n" );
    }
}



void CopyDefaultString(
    char       **key,
    const char *value
		       )
{
    char *currentKey;
    char *newKey;

    assert( key != NULL );

    /* Delete the current string. */
    if( *key != NULL )
    {
	currentKey = *key;
	free( currentKey );
    }
    *key = NULL;

    /* Copy the new string to the key. */
    newKey = malloc( strlen( value ) + sizeof( char ) );
    assert( newKey != NULL );
    strcpy( newKey, value );
    *key = newKey;
}



static void
InitializeConfiguration(
    struct configuration *configuration
			)
{
    assert( configuration != NULL );

    /* All pointers in the configuration are expected to be NULL before
       allocating new contents. */
    /*@-null@*/
    CopyDefaultString( &configuration->bucketName, DEFAULT_BUCKETNAME );
    CopyDefaultString( &configuration->path, DEFAULT_PATH );
    CopyDefaultString( &configuration->keyId, DEFAULT_KEY_ID );
    CopyDefaultString( &configuration->secretKey, DEFAULT_SECRET_KEY );
    CopyDefaultString( &configuration->logfile, DEFAULT_LOG_FILE );
    /*@+null@*/
}



void
Configure(
    struct configuration *configuration,
    int                  argc,
    const char * const   *argv
	  )
{
    struct cmdlineConfiguration cmdlineConfiguration =
    {
	{                 /* configuration */
	    US_STANDARD,  /* region */
	    NULL,         /* bucketName */
	    NULL,         /* path */
	    NULL,         /* keyId */
	    NULL,         /* secretKey */
	    NULL,         /* logfile */
	    {             /* verbose */
  	        false,    /* value */
		false     /* isset */
	    }
	},
        NULL              /* configFile */
    };

    char                        *configFile;
    const FILE                  *configFp;
    const char                  *homedir;
    const char                  *accessKeys;
    char                        *mountPoint;
    bool                        keyError;

    /* Decode the command line options, which may include an override of
       the configuration file. */
    InitializeConfiguration( &cmdlineConfiguration.configuration );
    DecodeCommandLine( &cmdlineConfiguration, &mountPoint, argc, argv );

    /* Read the configuration files in the following priority:
       (1) command-line specified override
       (2) /etc/aws-s3fs.conf
       (3) ~/.aws-s3fs.conf
    */

    InitializeConfiguration( configuration );

    /* Attempt to read the configuration file override, if specified. */
    configFile = cmdlineConfiguration.configFile;
    if( configFile != NULL )
    {
	configFp = TestFileReadable( configFile );
	if( configFp != NULL )
	{
	    if( verboseOutput )
	    {
    	        printf( "Reading configuration from %s.", configFile );
	    }
	    ReadConfigFile( configFp, configFile, configuration );
	}
	else
	{
	    fprintf( stderr, "Cannot open %s for reading.\n", configFile );
	    exit( EXIT_FAILURE );
	}
    }
    else
    {
        /* Otherwise, attempt "~/.aws-s3fs". */
        configFp = NULL;
        homedir = getenv( "HOME" );
	if( homedir != NULL )
	{
	    /* Build the path for "~/.aws-s3fs". */
	    configFile = malloc( strlen( homedir ) + sizeof( "/.aws-s3fs" )
				 + sizeof( char ) );
	    assert( configFile != NULL );
	    strcpy( configFile, homedir );
	    strcpy( &configFile[ strlen( configFile ) ], "/.aws-s3fs" );
	    configFp = TestFileReadable( configFile );
	    if( configFp != NULL )
	    {
	        if( verboseOutput )
		{
    	            printf( "Reading configuration from %s.", configFile );
		}
	        ReadConfigFile( configFp, configFile, configuration );
	    }
	    free( configFile );
	}

	/* Still unsuccessful, attempt DEFAULT_CONFIG_FILENAME. */
	if( configFp == NULL )
	{
	    if( configFp == NULL )
	    {
		configFp = TestFileReadable( DEFAULT_CONFIG_FILENAME );
		if( configFp != NULL )
		{
		    if( verboseOutput )
		    {
			printf( "Reading configuration from %s.",
				DEFAULT_CONFIG_FILENAME );
		    }
		    ReadConfigFile( configFp, DEFAULT_CONFIG_FILENAME,
				    configuration );
		}
	    }
	}
    }

    /* Having read the config file (if any of them existed), overwrite any
       configuration setting that is specified by the secret key environment
       variable. */
    accessKeys = getenv( "AWS_S3FS_KEY" );
    if( accessKeys != NULL )
    {
        if( verboseOutput )
	{
	    printf( "AWS_S3FS_KEY variable found.\n" );
	}
        keyError = false;
	/*@-null@*/
	ConfigSetKey( &configuration->keyId, &configuration->secretKey,
		      accessKeys, &keyError );
	/*@+null@*/
	if( bool_equal( keyError, true ) )
	{
	    fprintf( stderr, "AWS_S3FS_KEY: invalid format.\n" );
	    exit( EXIT_FAILURE );
	}
    }

    /* Finally, override any configuration setting that was specified on the
       command-line. */
    /*@-null@*/
    ConfigSetPath( &configuration->bucketName,
		   cmdlineConfiguration.configuration.bucketName );
    ConfigSetPath( &configuration->path, 
		   cmdlineConfiguration.configuration.path );
    ConfigSetPath( &configuration->keyId, 
		   cmdlineConfiguration.configuration.keyId );
    ConfigSetPath( &configuration->secretKey, 
		   cmdlineConfiguration.configuration.secretKey );
    ConfigSetPath( &configuration->logfile, 
		   cmdlineConfiguration.configuration.logfile );
    /*@+null@*/
    if( cmdlineConfiguration.configuration.verbose.isset )
    {
	configuration->verbose.isset = true;
	configuration->verbose.value = cmdlineConfiguration.configuration.verbose.value;
    }

    /* Destroy the temporary configuration structure holding the command-line
       options. */
    free( cmdlineConfiguration.configuration.bucketName );
    free( cmdlineConfiguration.configuration.path );
    free( cmdlineConfiguration.configuration.keyId );
    free( cmdlineConfiguration.configuration.secretKey );
    free( cmdlineConfiguration.configuration.logfile );
    /*@-usedef@*/
    free( cmdlineConfiguration.configFile );
    /*@+usedef@*/
}
