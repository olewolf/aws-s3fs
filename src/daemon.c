/**
 * \file daemon.c
 * \brief Functions for daemonizing aws-s3fs.
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

/* See also <http://www.cplusplus.com/forum/unices/6452/> */

#include <config.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <signal.h>
#include <stdbool.h>
#include <memory.h>
#include <string.h>
#include "aws-s3fs.h"

/*
#define USE_LOCKFILE
*/

static struct ThreadsafeLogging *logger;
static struct configuration     *configuration;

#ifdef USE_LOCKFILE
static int                      lockFp;
#endif /* USE_LOCKFILE */


/**
 * Respond to HUP and TERM signals.
 * @param sig [in] Signal from the operating system.
 * @return Nothing.
*/
static void
SignalHandler(
    int       sig,
    siginfo_t *sigInfo,
    void      *context
	      )
{
    const char *logfile;

    switch( sigInfo->si_signo )
    {
        /* Restart log if a HUP is received. */
        case SIGHUP:
 	    Syslog( logger, LOG_INFO, "SIGHUP received\n" );
	    logfile = strdup( LogFilename( logger ) );
	    CloseLog( logger );
	    InitLog( logger, logfile, logger->logLevel );
	    free( (char*) logfile );
 	    Syslog( logger, LOG_INFO, "Restarting log\n" );
	    break;
        case SIGTERM:
 	    Syslog( logger, LOG_INFO, "SIGTERM received, stopping daemon\n" );
#ifdef USE_LOCKFILE
	    unlink( LOCK_FILE );
	    close( lockFp );
	    Syslog( logger, LOG_INFO, "Lock file removed\n" );
#endif /* USE_LOCKFILE */
	    exit( EXIT_SUCCESS );
	    break;

        default:
	    break;
    }
}



/**
 * Fork the process into the background and prepare signal responses. If the
 * \a parameter daemonize is false, the process stays in the foreground.
 * @param logging [in] Logging facility.
 * @param config [in] Configuration.
 * @return Nothing.
 */
void
Daemonize(
    struct ThreadsafeLogging *logging,
    struct configuration     *config
	  )
{
    int        fileDesc;
    pid_t      forkPid;
    int        stdIO;
    const char *runDir;
#ifdef USE_LOCKFILE
    char       pidStr[ 10 ];
#endif /* USE_LOCKFILE */
    struct sigaction sigAction;

    /* Already a daemon */
    if( getppid( ) == 1 )
    {
	return;
    }

    /* Attempt to fork into background. */
    forkPid = fork( );
    /* Fork error. */
    if( forkPid < 0 )
    {
	Syslog( logging, LOG_ERR, "Could not spawn daemon process\n" );
	    exit( EXIT_FAILURE );
    }
    /* We're the parent, so now exit. */
    else if( forkPid > 0 )
    {
	exit( EXIT_SUCCESS );
    }
    /* Child (daemon) continues. */
    else
    {
	/* Obtain a new process group. */
        setsid( ); 

	/* Close all descriptors. */
	for( fileDesc = getdtablesize( ); fileDesc >= 0; --fileDesc )
	{
	    close( fileDesc );
	}
	/* Handle standard I/O. */
	stdIO = open( "/dev/null", O_RDWR );
	if( dup( stdIO ) < 0 )
	{
	    Syslog( logging, LOG_WARNING,
		    "Cannot redirect stdout to /dev/null\n" );
	    exit( EXIT_FAILURE );
	}
	if( dup( stdIO ) < 0 )
	{
	    Syslog( logging, LOG_WARNING,
		    "Cannot redirect stderr to /dev/null\n" );
	    exit( EXIT_FAILURE );
	}
	/* Set default file permissions for created files. */
	umask(027);
	/* Change running directory. */
	runDir = getenv( "TMPDIR" );
	if( runDir == NULL )
	{
	    runDir = DEFAULT_TMP_DIR;
	}
	if( chdir( runDir ) != 0 )
	{
	    Syslog( logging, LOG_WARNING,
		    "Cannot change to directory %s\n", runDir );
	}

#ifdef USE_LOCKFILE
	/* Create lock file. */
	lockFp = open( LOCK_FILE, O_RDWR | O_CREAT, 0640 );
	if( lockFp < 0 )
	{
	    /* Cannot create lock file. */
	    Syslog( logging, LOG_ERR, "Unable to create lock file\n" );
	    exit( EXIT_FAILURE );
	}
	else if( lockf( lockFp, F_TLOCK, 0 ) < 0 )
	{
	    /* Cannot create lock. */
	    Syslog( logging, LOG_ERR, "Unable to create lock file\n" );
	    exit( EXIT_FAILURE );
	}
	else
        {
	    /* First instance continues. */
	    /* Record pid in lockfile. */
	    sprintf( &pidStr[ 0 ], "%d\n", getpid( ) );
	    if( write( lockFp, pidStr, strlen( pidStr ) ) < 0 )
	    {
	        Syslog( logging, LOG_ERR, "Unable to write to lock file\n" );
		exit( EXIT_FAILURE );
	    }
#endif /* USE_LOCKFILE */

	    /* Register handling of the HUP and the TERM signals. */
	    memset( &sigAction, 0, sizeof( sigAction ) );
	    sigAction.sa_sigaction = &SignalHandler;
	    /* Use sa_sigaction to handle the signals. */
	    sigAction.sa_flags = SA_SIGINFO;

	    if( sigaction( SIGHUP, &sigAction, NULL ) < 0 )
	    {  
		perror( "Cannot register SIGHUP handler" );
		exit( EXIT_FAILURE );
	    }
	    if( sigaction( SIGTERM, &sigAction, NULL ) < 0)
	    {  
		perror( "Cannot register SIGTERM handler" );
		exit( EXIT_FAILURE );
	    }
	    /* Ignore TTY signals. */
	    sigAction.sa_flags = 0;
	    sigAction.sa_handler = SIG_IGN;
	    sigaction( SIGTSTP, &sigAction, NULL );
	    sigaction( SIGTTOU, &sigAction, NULL );
	    sigaction( SIGTTIN, &sigAction, NULL );

	    Syslog( logging, LOG_INFO,
		    "Forking into background with PID = %d\n", getpid( ) );
#ifdef USE_LOCKFILE
	}
#endif /* USE_LOCKFILE */
    }

    logger        = logging;
    configuration = config;
}
