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


#include <config.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <signal.h>
#include <stdbool.h>
#include "aws-s3fs.h"


#define RUN_DIR "/tmp"


static bool daemonizeFlag = true;


/**
 * Call this function to instruct the daemonization routine not to fork this
 * process.
 * @return Nothing.
 */
void
DoNotDaemonize(
    void
	       )
{
    daemonizeFlag = false;
}



/**
 * Respond to HUP and TERM signals.
 * @param sig [in] Signal from the operating system.
 * @return Nothing.
 */
static void
SignalHandler(
	      int sig
	      )
{
    switch( sig )
    {
        /* Restart log if a HUP is received. */
        case SIGHUP:
	    CloseLog( );
	    InitLog( LogFilename( ) );
	    break;

        case SIGTERM:
	    exit( EXIT_SUCCESS );
	    break;

        default:
	    break;
    }
}




/**
 * Fork the process into the background and prepare signal responses. If the
 * function \a DoNotDaemonize has been called, the process stays in the
 * foreground.
 * @return Nothing.
 */
void
Daemonize(
	  void
	  )
{
    int i;
#ifdef USE_LOCK
    int lockFp;
    char pid[ 10 ];
#endif /* USE_LOCK */

    if( daemonizeFlag )
    {
        /* Already a daemon */
        if( getppid( ) == 1 )
	{
	    return;
	}

	/* Attempt to fork into background. */
	i = fork( );
	/* Fork error. */
	if( i < 0 )
	{
	    exit( EXIT_FAILURE );
	}
	/* Parent exits. */
	else if( i > 0 )
	{
	    exit( EXIT_SUCCESS );
	}
	/* Child (daemon) continues. */
	else
	{
	    /* Obtain a new process group. */
	    setsid( ); 
	    /* Close all descriptors. */
	    for( i = getdtablesize( ); i >= 0; --i )
	    {
		close(i);
	    }
	    /* Handle standard I/O. */
	    i = open( "/dev/null", O_RDWR );
	    if( dup( i ) < 0 )
	    {
		exit( EXIT_FAILURE );
	    }
	    if( dup( i ) < 0 )
	    {
		exit( EXIT_FAILURE );
	    }
	    /* Set default file permissions for created files. */
	    umask(027);
	    /* Change running directory. */
	    if( chdir( RUN_DIR ) != 0 )
	    {
		Syslog( LOG_WARNING, "Cannot change to directory %s", RUN_DIR );
	    }
#ifdef USE_LOCK
	    /* Create lock file. */
	    lockFp = open( LOCK_FILE, O_RDWR | O_CREAT, 0640 );
	    if( lockFp < 0 )
	    {
		/* Cannot create lock file. */
	        exit( EXIT_FAILURE );
	    }
	    else if( lockf( lockFp, F_TLOCK, 0 ) < 0 )
	    {
		/* Cannot create lock. */
		exit( EXIT_FAILURE );
	    }
	    else
	    {
		/* First instance continues. */
		/* Record pid in lockfile. */
		sprintf( pid, "%d\n", getpid( ) );
		write( lockFp, pid, strlen( pid ) );
#endif /* USE_LOCK */
		/* Ignore child. */
		signal( SIGCHLD, SIG_IGN);
		/* Ignore tty signals. */
		signal( SIGTSTP, SIG_IGN);
		signal( SIGTTOU, SIG_IGN);
		signal( SIGTTIN, SIG_IGN);
		/* Respond to SIGHUP. */
		signal( SIGHUP, SignalHandler );
		/* Respond to SIGTERM. */
		signal( SIGTERM, SignalHandler );
#ifdef USE_LOCK
	    }
#endif /* USE_LOCK */
	}
    }
}

