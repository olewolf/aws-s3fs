/**
 * \file logger.c
 * \brief Log messages via the system log or to a file.
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
#include <syslog.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdarg.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>
#include <limits.h>
#include <time.h>
#include "aws-s3fs.h"


#define MAX_LOG_ENTRY_LENGTH 1024



/**
 * Initialize the process context for the logging module.
 * @param logging [out] Context for the logging module.
 * @return Nothing.
 */
void InitializeLoggingModule(
    struct ThreadsafeLogging *logging
			     )
{
    logging->loggingEnabled = true;
    logging->logToSyslog    = false;
    logging->logFh          = NULL;
    logging->hostname       = NULL;
    logging->logFilename    = NULL;
    logging->stdoutDisabled = false;
}



/**
 * Disable logging of any messages.
 * @param logging [out] Thread state.
 * @return Nothing.
 */
void
DisableLogging(
    struct ThreadsafeLogging *logging
	       )
{
    logging->loggingEnabled = false;
}


/**
 * Enable logging of all messages.
 * @param logging [out] Thread state.
 * @return Nothing.
 */
void
EnableLogging(
    struct ThreadsafeLogging *logging
	      )
{
    logging->loggingEnabled = true;
}


/**
 * Return the filename of the log.
 * @param logging [in] Thread state.
 * @return Name of the log file.
 */
const char *LogFilename(
    const struct ThreadsafeLogging *logging
			)
{
    return logging->logFilename;
}


/**
 * Initialize the logging to use syslog, a specified filename, or stdout.
 * @param logging [in/out] Thread state.
 * @param logfile [in] The name of the logfile: syslog, a filename, or NULL.
 * @return Nothing.
 */
void
InitLog(
    struct ThreadsafeLogging *logging,
    const char               *logfile
	)
{
    char hostnameBuf[ HOST_NAME_MAX + 1 ];
    char *hostname;

    if( logging->loggingEnabled != true )
    {
        logging->logToSyslog = false;
	logging->logFh = NULL;
        return;
    }

    logging->logFilename = logfile;

    /* Get the hostname. */
    gethostname( hostnameBuf, HOST_NAME_MAX );
    hostnameBuf[ HOST_NAME_MAX ] = '\0';
    
    hostname = malloc( strlen( hostnameBuf ) + sizeof( char ) );
    strcpy( hostname, hostnameBuf );
    logging->hostname = hostname;

    /* Open the log file. */
    if( logfile != NULL )
    {
	if( strcmp( logfile, "syslog" ) == 0 )
        {
	    logging->logToSyslog = true;
	    openlog( "aws-s3fs", LOG_CONS, LOG_DAEMON );
	}
	else
	{
	    logging->logToSyslog = false;
	    logging->logFh = fopen( logfile, "a" );
	    if( logging->logFh == NULL )
	    {
	      Syslog( logging, LOG_ERR,
		      "Cannot open %s logfile for writing", logfile );
	    }
	}
    }
}


/**
 * Close the log file for now.
 * @param logging [in/out] Thread state.
 * @return Nothing.
 */
void
CloseLog(
    struct ThreadsafeLogging *logging
	 )
{
    if( logging->logFh != NULL )
    {
        fclose( logging->logFh );
	logging->logFh = NULL;
    }
    else if( ( logging->logFilename ) &&
	     ( strcmp( LogFilename( logging ), "syslog" ) == 0 ) )
    {
        closelog( ); /* Is this thread-safe? */
    }
}


/**
 * Commit a parsed message string to the log.
 * @param logFh [in] File handle of the log file, or NULL if there's no
 *        log file.
 * @param loggingEnabled [in] State whether logging is enabled at all.
 * @param logToSyslog [in] Indicate that logs go to the syslog.
 * @param stdoutDisabled [in] If \true, don't write to stdout. This flag must
 *        be set during signal handling.
 * @param priority [in] Whether the message is a LOG_ERR, LOG_WARNING, etc.
 * @param message [in] String to append to the log.
 * @return Nothing.
 */
static void
LogMessage(
    FILE       *logFh,
    bool       loggingEnabled,
    bool       logToSyslog,
    bool       stdoutDisabled,
    int        priority,
    const char *hostname,
    const char *message
	   )
{
    time_t t                           = time( NULL );
    struct tm tm                       = *localtime( &t );
    static const char const *months[ ] =
    {
        "Jan", "Feb", "Mar", "Apr", "May", "Jun",
	"Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
    };
    char logmessage[ MAX_LOG_ENTRY_LENGTH + 1 ];

    if( loggingEnabled )
    {
        if( logToSyslog )
	{
	    syslog( priority, "%s", message );
	}
	else
	{
	    /* Generate log message with preamble. */
	    sprintf( logmessage, "%s %2d %02d:%02d:%02d %s aws-s3fs: %s",
		     months[ tm.tm_mon + 1 ], tm.tm_mday,
		     tm.tm_hour, tm.tm_min, tm.tm_sec,
		     hostname,
		     message );
	    if( logFh != NULL )
	    {
	        fputs( logmessage, logFh );
	    }
	    else
	    {
	        if( ! stdoutDisabled )
		{
		    fputs( logmessage, stdout );
		}
	    }
	}
    }
}



/**
 * Log a message to file, stdout, or syslog, depending on the log
 * initialization.
 * @param logging [in] Thread state.
 * @param priority [in] Whether the message is a LOG_ERR, LOG_WARNING, etc.
 * @param format [in] Formatting string for the message.
 * @param ... [in] Variable number of arguments for the formatting string.
 * @return Nothing.
 */
void
Syslog(
    const struct ThreadsafeLogging *logging,
    int                            priority,
    const char                     *format,
                                   ...
       )
{
    int ch;
    int idx;
    union {
        int   d;
        char  *s;
    } printable;
    char compile[ MAX_LOG_ENTRY_LENGTH + 1 ];
    int  outIdx = 0;
    char argument[ MAX_LOG_ENTRY_LENGTH + 1 ];
    va_list v1;

    va_start( v1, format );

    /* Copy from the format string until a parameter is encountered. */
    idx = 0;
    while( ( ch = format[ idx++ ] ) != '\0' )
    {
        assert( outIdx < MAX_LOG_ENTRY_LENGTH );
        if( ch != '%' )
	{
	    compile[ outIdx++ ] = ch;
        }
        else
	{
	    if( format[ idx ] == '%' )
	    {
	        compile[ outIdx++ ] = ch;
	    }
	    else
	    {
	        switch( format[ idx ] )
		{
		    case 'd':
		        printable.d = va_arg( v1, int );
			sprintf( argument, "%d", printable.d );
		        break;
		    case 's':
		        printable.s = va_arg( v1, char* );
			sprintf( argument, "%s", printable.s );
		        break;
		    default:
		        argument[ 0 ] = '\0';
		        break;
		}
		assert( outIdx + strlen( argument ) < MAX_LOG_ENTRY_LENGTH );
		strcpy( &compile[ outIdx ], argument );
		outIdx += strlen( argument );
	    }
	    idx++;
	}
    }
    compile[ outIdx ] = '\0';

    va_end( v1 );

    LogMessage( logging->logFh, logging->loggingEnabled, logging->logToSyslog,
		logging->stdoutDisabled,
		priority, logging->hostname, compile );
}

