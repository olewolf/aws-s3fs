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

static void hdl (int sig, siginfo_t *siginfo, void *context)
{
	printf ("Sending PID: %ld, UID: %ld\n",
			(long)siginfo->si_pid, (long)siginfo->si_uid);
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
	  /*
	    CloseLog( );
	    InitLog( LogFilename( ) );
	  */
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
    bool                           daemonize,
    const struct ThreadsafeLogging *logging
	  )
{
    int i;
#ifdef USE_LOCK
    int lockFp;
    char pid[ 10 ];
#endif /* USE_LOCK */
    struct sigaction sigAction;

    if( daemonize )
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
	        Syslog( logging, LOG_WARNING,
			"Cannot change to directory %s", RUN_DIR );
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

		/* Use sigaction instead of signal. */
		memset( &sigAction, 0, sizeof( sigAction ) );
 
	/* Use the sa_sigaction field because the handles has two additional parameters */
	act.sa_sigaction = &hdl;
 
	/* The SA_SIGINFO flag tells sigaction() to use the sa_sigaction field, not sa_handler. */
	act.sa_flags = SA_SIGINFO;
 
	if (sigaction(SIGTERM, &act, NULL) < 0) {
		perror ("sigaction");
		return 1;
	}


		/* Ignore child. */
/*		signal( SIGCHLD, SIG_IGN); */
		/* Ignore tty signals. */
/*		signal( SIGTSTP, SIG_IGN); */
/*		signal( SIGTTOU, SIG_IGN); */
/*		signal( SIGTTIN, SIG_IGN); */
		/* Respond to SIGHUP. */
/*		signal( SIGHUP, SignalHandler ); */
		/* Respond to SIGTERM. */
/*		signal( SIGTERM, SignalHandler ); */
#ifdef USE_LOCK
	    }
#endif /* USE_LOCK */
	}
    }
}






static volatile sig_atomic_t doneflag = 0;  
  
static void setdoneflag(int sig, siginfo_t *siginfo, void *context)  
{  
  printf ("Signo     -  %d\n",siginfo->si_signo);  
  printf ("SigCode   -  %d\n",siginfo->si_code);  
  doneflag = 1;  
}  
  
int main (int argc, char *argv[])  
{  
  struct sigaction act;  
  
  memset (&act, '\0', sizeof(act));  
  
  act.sa_sigaction = &setdoneflag;  
  
  act.sa_flags = SA_SIGINFO;  
  
  if (sigaction(SIGINT, &act, NULL) < 0) {  
      perror ("sigaction");  
      return 1;  
  }  
  
  while (!doneflag) {  
      printf("press CTRL+C to kill the Loop\n");  
      sleep(1);  
  }  
  
  printf("Program terminating ...\n");  
  return 0;  
}  
  
Output:  
  
$ ./a.out  
press CTRL+C to kill the Loop  
press CTRL+C to kill the Loop  
^C  
Signo     -  2  
SigCode   -  128  
Program terminating ...  
$ 


http://www.cplusplus.com/forum/unices/6452/

