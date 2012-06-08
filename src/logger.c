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

#include <stdlib.h>


static bool       loggingEnabled = true;
static bool       logToSyslog    = false;
static FILE       *logFh         = NULL;
static char       hostname[ HOST_NAME_MAX + 1 ];
static const char *logFilename   = NULL;



void
Syslog(
    int        priority,
    const char *format,
    ...
       );



/**
 * Disable logging of any messages.
 * @return Nothing.
 */
void
DisableLogging(
    void
	       )
{
    loggingEnabled = false;
}


/**
 * Enable logging of all messages.
 * @return Nothing.
 */
void
EnableLogging(
    void
	       )
{
    loggingEnabled = true;
}


const char *LogFilename(
    void
			)
{
    return logFilename;
}


/**
 * Initialize the logging to use syslog, a specified filename, or stdout.
 * @param logfile [in] The name of the logfile: syslog, a filename, or NULL.
 * @return Nothing.
 */
void
InitLog(
    const char *logfile
	)
{
    if( loggingEnabled != true )
    {
        logToSyslog = false;
	logFh = NULL;
        return;
    }

    logFilename = logfile;

    /* Get the hostname. */
    gethostname( hostname, HOST_NAME_MAX );
    hostname[ HOST_NAME_MAX ] = '\0';

    /* Open the log file. */
    if( logfile != NULL )
    {
	if( strcmp( logfile, "syslog" ) == 0 )
        {
	    logToSyslog = true;
	    openlog( "aws-s3fs", LOG_CONS, LOG_DAEMON );
	}
	else
	{
	    logToSyslog = false;
	    logFh = fopen( logfile, "a" );
	    if( logFh == NULL )
	    {
		Syslog( LOG_ERR, "Cannot open %s logfile for writing", logfile );
	    }
	}
    }
}


/**
 * Close the log file for now.
 * @return Nothing.
 */
void
CloseLog(
    void
	 )
{
    if( logFh != NULL )
    {
        fclose( logFh );
	logFh = NULL;
    }
    else if( ( logFilename ) && ( strcmp( LogFilename( ), "syslog" ) == 0 ) )
    {
        closelog( );
    }
}


/**
 * Commit a parsed message string to the log.
 * @param priority [in] Whether the message is a LOG_ERR, LOG_WARNING, etc.
 * @param message [in] String to append to the log.
 * @return Nothing.
 */
static void
LogMessage( 
    int        priority,
    const char *message
	    )
{
    time_t t     = time( NULL );
    struct tm tm = *localtime( &t );
    static const char const *months[ ] =
    {
        "Jan", "Feb", "Mar", "Apr", "May", "Jun",
	"Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
    };
    char logmessage[ 1024 ];

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
	        fputs( logmessage, stdout );
	    }
	}
    }
}



/**
 * Log a message to file, stdout, or syslog, depending on the log
 * initialization.
 * @param priority [in] Whether the message is a LOG_ERR, LOG_WARNING, etc.
 * @param format [in] Formatting string for the message.
 * @param ... [in] Variable number of arguments for the formatting string.
 * @return Nothing.
 */
void
Syslog(
    int        priority,
    const char *format,
    ...
       )
{
    int ch;
    int idx;
    union {
        int   d;
        char  *s;
    } printable;
    char compile[ 1024 ];
    char *outString = compile;
    char argument[ 256 ];
    va_list v1;

    va_start( v1, format );

    /* Copy from the format string until a parameter is encountered. */
    idx = 0;
    while( ( ch = format[ idx++ ] ) != '\0' )
    {
        if( ch != '%' )
	{
	    *outString++ = ch;
        }
        else
	{
	    if( format[ idx ] == '%' )
	    {
	        *outString++ = ch;
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
		strcpy( outString, argument );
		outString += strlen( argument );
	    }
	    idx++;
	}
    }
    *outString = '\0';

    va_end( v1 );

    LogMessage( priority, compile );
}

