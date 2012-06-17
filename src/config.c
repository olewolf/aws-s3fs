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


static const char *regionNames[ ] = {
    "US Standard", "Oregon", "Northern California", "Ireland",
    "Singapore", "Tokyo", "Sao Paulo"
};



/**
 * Copy a string, allocating memory for the destination. If the destination
 * string already exists, its memory is first freed.
 * @param key [out] Pointer to the destination string.
 * @param value [in] Input string to be copied.
 * @return Nothing.
 */
#ifndef AUTOTEST
static
#endif
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
	*key = NULL;
    }

    /* Copy the new string to the key. */
    newKey = strdup( value );
    assert( newKey != NULL );
    *key = newKey;
}



/**
 * Write the bucket region value corresponding to the supplied region name
 * string. If the region name is invalid, the region value is left as is;
 * the \a configError flag is set; and an an error is printed to stdout.
 * @param region [out] Pointer to the region value container.
 * @param configValue [in] Region name.
 * @param configError [out] Flag that indicates whether a configuration
 *        error has occurred.
 * @return Nothing.
 */
void
ConfigSetRegion(
    enum bucketRegions *region,
    const char         *configValue,
    bool               *configError
	  )
{
    bool               found = false;
    int                nameIdx;

    if( configValue != NULL )
    {
        /* Look up the specified region name in the region enum. */
        for( nameIdx = 0;
	     nameIdx < (int)( sizeof( regionNames ) / sizeof( char* ) );
	     nameIdx++ )
	{
	    /*@-unrecog@*/
	    if( strcasecmp( regionNames[ nameIdx ], configValue ) == 0 )
	    /*@+unrecog@*/
	    {
		found = true;
		*region = nameIdx;
		break;
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



/**
 * Copy a path string (or any other) to a destination unless the path string
 * is NULL. The destination string is automatically allocated in memory.
 * @param path [out] Destination for the path name.
 * @param configPath [in] String with path name.
 * @return Nothing.
 */
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



/**
 * Set the log verbosity.
 * @param loglevel [out] One of log_ERR, log_WARNING, log_NOTICE, log_INFO, or
 *        log_DEBUG.
 * @param configValue [in] String value of the above.
 * @param configError [out] Configuration error flag.
 * @return Nothing.
 */
void
ConfigSetLoglevel(
    enum LogLevels *loglevel,
    const char     *configValue,
    bool           *configError
	      )
{
    if( strcasecmp( configValue, "log_err" ) == 0 )
    {
        *loglevel = log_ERR;
    }
    else if( strcasecmp( configValue, "log_warning" ) == 0 )
    {
        *loglevel = log_WARNING;
    }
    else if( strcasecmp( configValue, "log_notice" ) == 0 )
    {
        *loglevel = log_NOTICE;
    }
    else if( strcasecmp( configValue, "log_info" ) == 0 )
    {
        *loglevel = log_NOTICE;
    }
    else if( strcasecmp( configValue, "log_debug" ) == 0 )
    {
        *loglevel = log_NOTICE;
    }
    else
    {
        *configError = true;
    }
}



/**
 * Extract a key string from a string where keys are separated by ':'. The
 * memory for the extracted key string is automatically allocated and written
 * to the \a key parameter.
 * @param key [out] Pointer to the extracted key string.
 * @param index [in] Scan the key string from this position in the string.
 * @param configValue [in] String with ':'-separated keys.
 * @return The position of the next key in the string, or 0 if there are no
 *         more keys in the string.
 */
#ifndef AUTOTEST
static
#endif
int
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

    if( configValue == NULL )
    {
        *key = NULL;
	return 0;
    }

    tempBuffer = malloc( strlen( &configValue[ index ] ) + sizeof( char ) );
    assert( tempBuffer != NULL );
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
    /* Copy the extracted key to its target storage. */
    newKey = malloc( strlen( tempBuffer ) + sizeof( char ) );
    assert( newKey != NULL );
    strcpy( newKey, tempBuffer );
    *key = newKey;
    bufferPtr = NULL;
    free( tempBuffer );

    /* Skip past whitespace and ':'. */
    while( ( character = configValue[ index ] ) != (char) 0 )
    {
        if( isspace( character ) || ( character == ':' ) )
        {
	    index++;
	}
	else
	{
	    break;
	}
    }
    /* Return the end position, where another key may be stored. */
    if( character != (char) 0 )
    {
        return( index );
    }
    /* Or return 0 to indicate that there are no more keys. */
    else
    {
        return( 0 );
    }
}



/**
 * Extract the Access Key ID and the Secret Key from a ':'-separated key
 * string. Set the \a configError flag if the keys cannot be extracted.
 * @param keyId [out] Pointer to the extracted Access Key ID string.
 * @param secretKey [out] Pointer to the extracted Secret Key string.
 * @param configValue [in] String with ':'-separated keys.
 * @param configError [out] Configuration error flag.
 * @return Nothing.
 */
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
    invalidSecretKey = strdup( "*" );
    assert( invalidSecretKey != NULL );

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
    if( newKeyId == NULL )
    {
	fprintf( stderr, "Access key ID not found.\n" );
        *secretKey = invalidSecretKey;
	*configError = true;
    }
    else
    {
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
}



/**
 * Set all values in a \a configuration structure to default values. If
 * memory has been allocated for any of its current contents, this memory
 * must be freed prior to calling this function.
 * @param configuration [out] Pointer to configuration structure that is to
 *        be initialized.
 * @return Nothing.
 */
/**
 * Copy a string, allocating memory for the destination. If the destination
 * string already exists, its memory is first freed.
 * @param key [out] Pointer to the destination string.
 * @param value [in] Input string to be copied.
 * @return Nothing.
 */
void
InitializeConfiguration(
    struct Configuration *configuration
			)
{
    assert( configuration != NULL );

    configuration->region = US_STANDARD;
    configuration->mountPoint = NULL;
    /* All pointers in the configuration are expected to be NULL before
       allocating new contents. */
    /*@-null@*/
    CopyDefaultString( &configuration->bucketName, DEFAULT_BUCKETNAME );
    CopyDefaultString( &configuration->path, DEFAULT_PATH );
    CopyDefaultString( &configuration->keyId, DEFAULT_KEY_ID );
    CopyDefaultString( &configuration->secretKey, DEFAULT_SECRET_KEY );
    CopyDefaultString( &configuration->logfile, DEFAULT_LOG_FILE );
    /*@+null@*/
    configuration->verbose.value = DEFAULT_VERBOSE;
    configuration->verbose.isset = false;
    configuration->daemonize     = true;
}



/**
 * Helper function for the \a Configure function. It returns the string that
 * was passed, except if the string is NULL. In that case, it returns the
 * string "(None)".
 * @param string [in] The string which is also returned.
 * @return The input string or "(None)" if the input string is NULL.
 */
static const char *ShowStringValue( const char *string )
{
    if( string != NULL )
    {
        return( string );
    }
    else
    {
	return( "(None)" );
    }
}


static const char *ShowLogLevel( enum LogLevels logLevel )
{
    static const char *strErr     = "LOG_ERR";
    static const char *strWarning = "LOG_WARNING";
    static const char *strNotice  = "LOG_NOTICE";
    static const char *strInfo    = "LOG_INFO";
    static const char *strDebug   = "LOG_DEBUG";
    static const char *invalid    = "INVALID";

    switch( logLevel )
    {
        case log_ERR:
	    return( strErr );
	    break;
        case log_WARNING:
	    return( strWarning );
	    break;
        case log_NOTICE:
	    return( strNotice );
	    break;
        case log_INFO:
	    return( strInfo );
	    break;
        case log_DEBUG:
	    return( strDebug );
	    break;
        default:
	    return( invalid );
	    break;
    }
}



/**
 * Fill a \a configuration structure according to aws-s3fs.conf file contents
 * and command-line options and parameters, and the AWS_S3FS_KEY.
 * @param configuration [out] Pointer to configuration structure that is to
 *        be filled with values.
 * @param argc [in] The \a argc from the \a main function.
 * @param argv [in] The \a argv from the \a main function.
 * @return Nothing.
 */
void
Configure(
    struct Configuration *configuration,
    int                  argc,
    const char * const   *argv
	  )
{
    struct CmdlineConfiguration cmdlineConfiguration =
    {
	.configuration =
	{
	    .region      = US_STANDARD,
	    .bucketName  = NULL,
	    .path        = NULL,
	    .keyId       = NULL,
	    .secretKey   = NULL,
	    .logfile     = NULL,
	    .verbose     =
	    {
  	        .value = false,
		.isset = false
	    },
	    .logLevel    = log_WARNING,
	    .daemonize   = true
	},
        .configFile          = NULL,
	.regionSpecified     = false,
	.bucketNameSpecified = false,
	.pathSpecified       = false,
	.keyIdSpecified      = false,
	.secretKeySpecified  = false,
	.logfileSpecified    = false,
	.loglevelSpecified   = false
    };

    char                        *configFile = NULL;
    const FILE                  *configFp;
    const char                  *homedir;
    const char                  *accessKeys;
    bool                        keyError;
    bool                        forcedConfigFile = false;
    bool                        optionSuccess;
    bool                        configSuccess = true;

    /* Decode the command line options, which may include an override of
       the configuration file. */
    InitializeConfiguration( &cmdlineConfiguration.configuration );
    optionSuccess = DecodeCommandLine( &cmdlineConfiguration, argc, argv );
    if( ! optionSuccess )
    {
	fprintf( stderr, "Invalid command line options.\n" );
	exit( EXIT_FAILURE );
    }

    /* Read the configuration files in the following priority:
       (1) command-line specified config file
       (2) /etc/aws-s3fs.conf
       (3) ~/.aws-s3fs.conf
    */

    InitializeConfiguration( configuration );

    /* Attempt to read the configuration file override, if specified. */
    if( cmdlineConfiguration.configFile != NULL )
    {
        configFile = strdup( cmdlineConfiguration.configFile );
	assert( configFile != NULL );
        forcedConfigFile = true;
	configFp = TestFileReadable( configFile );
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
	    strcat( configFile, "/.aws-s3fs" );
	    configFp = TestFileReadable( configFile );
	}
	/* Still unsuccessful, attempt DEFAULT_CONFIG_FILENAME. */
	if( configFp == NULL )
	{
	    if( configFile != NULL )
	    {
	        free( configFile );
	    }
	    configFile = strdup( DEFAULT_CONFIG_FILENAME );
	    assert( configFile != NULL );
	    configFp = TestFileReadable( configFile );
	}
    }
    if( configFp != NULL )
    {
	configSuccess = ReadConfigFile( configFp, configFile, configuration );
    }
    if( ( configFp == NULL ) && bool_equal( forcedConfigFile, true ) )
    {
	fprintf( stderr, "Cannot open %s for reading.\n", configFile );
	exit( EXIT_FAILURE );
    }
    free( configFile );
    if( ! configSuccess )
    {
	fprintf( stderr, "Invalid config file settings.\n" );
	exit( EXIT_FAILURE );
    }
    else
    {
        VerboseOutput( configuration->verbose.value,
		       "Read configuration from %s.\n", configFile );
    }

    /* Having read the config file (if any of them existed), overwrite any
       configuration setting that is specified by the secret key environment
       variable. */
    accessKeys = getenv( "AWS_S3FS_KEY" );
    if( accessKeys != NULL )
    {
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
	else
	{
	    VerboseOutput( configuration->verbose.value,
			   "Keys set from AWS_S3FS_KEY variable.\n" );
	}
    }

    /* Finally, override any configuration setting that was specified on the
       command-line. */
    if( cmdlineConfiguration.regionSpecified )
    {
        configuration->region = cmdlineConfiguration.configuration.region;
    }
    if( cmdlineConfiguration.bucketNameSpecified )
    {
	/*@-null@*/
        ConfigSetPath( &configuration->bucketName,
		       cmdlineConfiguration.configuration.bucketName );
	/*@+null@*/
    }
    if( cmdlineConfiguration.pathSpecified )
    {
	/*@-null@*/
        ConfigSetPath( &configuration->path, 
		       cmdlineConfiguration.configuration.path );
	/*@+null@*/
    }
    if( cmdlineConfiguration.keyIdSpecified )
    {
	/*@-null@*/
        ConfigSetPath( &configuration->keyId, 
		       cmdlineConfiguration.configuration.keyId );
	/*@+null@*/
    }
    if( cmdlineConfiguration.secretKeySpecified )
    {
	/*@-null@*/
	ConfigSetPath( &configuration->secretKey, 
		       cmdlineConfiguration.configuration.secretKey );
	/*@+null@*/
    }
    if( cmdlineConfiguration.logfileSpecified )
    {
	/*@-null@*/
        ConfigSetPath( &configuration->logfile, 
		       cmdlineConfiguration.configuration.logfile );
	/*@+null@*/
    }
    if( cmdlineConfiguration.loglevelSpecified )
    {
        configuration->logLevel = cmdlineConfiguration.configuration.logLevel;
    }
    if( cmdlineConfiguration.configuration.verbose.isset )
    {
	configuration->verbose.isset = true;
	configuration->verbose.value = cmdlineConfiguration.configuration.verbose.value;
    }
    configuration->daemonize = cmdlineConfiguration.configuration.daemonize;
    configuration->mountPoint = cmdlineConfiguration.configuration.mountPoint;

    /* Destroy the temporary configuration structure holding the command-line
       options. */
    free( cmdlineConfiguration.configFile );
    free( cmdlineConfiguration.configuration.bucketName );
    free( cmdlineConfiguration.configuration.path );
    free( cmdlineConfiguration.configuration.keyId );
    free( cmdlineConfiguration.configuration.secretKey );
    free( cmdlineConfiguration.configuration.logfile );

    VerboseOutput( configuration->verbose.value,
		   "Configuration:\n  Region: %s\n  Bucket: %s\n"
		   "  Path: %s\n  Syslog: %s\n  Mount point %s\n",
		   regionNames[ configuration->region ],
		   ShowStringValue( configuration->bucketName ),
		   ShowStringValue( configuration->path ),
		   ShowStringValue( configuration->logfile ),
		   ShowLogLevel( configuration->logLevel ),
		   ShowStringValue( configuration->mountPoint ) );
}
