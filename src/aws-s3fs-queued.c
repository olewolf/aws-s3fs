/**
 * \file aws-s3fs-queued.c
 * \brief Main entry for the aws-s3fs download queue.
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
#include <signal.h>
#include <fcntl.h>
#include <pthread.h>
#include <socket.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include "aws-s3fs.h"
#include "filecache.h"
#include "socket.h"


/* If the parent process is terminated, terminate also this pid, if > 0. */
static pid_t killPid = -1;

/* Used for testing only. */
extern int testSocket;


/**
 * Respond to HUP and TERM signals.
 * Test: none.
 */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
void
SignalHandler(
	int       signal,
	siginfo_t *sigInfo,
	void      *context
	          )
{
	int exitStatus;

    switch( sigInfo->si_signo )
    {
        case SIGHUP:
			printf( "HUP signal received\n" );
            break;
	    case SIGCHLD:
			printf( "Child terminated\n" );
			wait( &exitStatus );
			printf( "Terminating\n" );
            exit( EXIT_SUCCESS );
			break;
	    case SIGINT:
        case SIGTERM:
			if( killPid >= 0)
			{
				kill( killPid, SIGTERM );
				wait( &exitStatus );
			}
			printf( "Terminating\n" );
            exit( EXIT_SUCCESS );
            break;

	    default:
			break;
	}
}
#pragma GCC diagnostic pop



/**
 * Start two processes: the file cache module together with the download queue,
 * and the permissions grant module.  The permissions grant module keeps its
 * superuser privileges.
 * @return Nothing.
 * Test: none.
 */
/* While developing, disable -Wunused-variable */
#pragma GCC diagnostic ignored "-Wunused-variable"
static void
StartProcesses(
	void
	           )
{
	int              socketPair[ 2 ];
	char             *runDir;
	pid_t            forkPid;
	int              fileDesc;
	int              stdIO;
	struct sigaction sigAction;
	int              enabled = 1;
	static pthread_t transferQueue;


	/* Setup a socket pair for communication between the two processes. */
	if( socketpair( PF_UNIX, SOCK_DGRAM, 0, socketPair ) < 0 )
	{
		fprintf( stderr, "Could not create socket pairs\n" );
		exit( EXIT_FAILURE );
	}
	/* Start a child process. */
	if( ( forkPid = fork( ) ) < 0 )
	{
		fprintf( stderr, "Could not fork process\n" );
		exit( EXIT_FAILURE );
	}
	/* Set working directory, file permissions, and signal handling for both
	   the child process and the parent process. */
	else
	{
        /* Set default file permissions for created files. */
        umask(027);
/* Don't change directory while developing */
#if 0
        /* Change running directory. */
        runDir = getenv( "TMPDIR" );
        if( runDir == NULL )
        {
            runDir = DEFAULT_TMP_DIR;
        }
        if( chdir( runDir ) != 0 )
        {
            fprintf( stderr, "Cannot change to directory %s\n", runDir );
        }
#endif

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
		if( sigaction( SIGCHLD, &sigAction, NULL ) < 0 )
		{
			perror( "Cannot register SIGCHLD handler" );
			exit( EXIT_FAILURE );
		}
		if( sigaction( SIGTERM, &sigAction, NULL ) < 0)
		{
			perror( "Cannot register SIGTERM handler" );
			exit( EXIT_FAILURE );
		}
		if( sigaction( SIGINT, &sigAction, NULL ) < 0)
		{
			perror( "Cannot register SIGINT handler" );
			exit( EXIT_FAILURE );
		}
		/* Ignore TTY signals. */
		sigAction.sa_flags = 0;
		sigAction.sa_handler = SIG_IGN;
		sigaction( SIGTSTP, &sigAction, NULL );
		sigaction( SIGTTOU, &sigAction, NULL );
		sigaction( SIGTTIN, &sigAction, NULL );

		/* Initialize the child process, which runs the file cache module. */
		if( forkPid == 0 )
		{
			close( socketPair[ 1 ] );

			/* Obtain a new process group. */
			setsid( );
/* Don't close descriptors or redirect stdio to /dev/null while developing */
#if 0
			/* Close all descriptors. */
			for( fileDesc = getdtablesize( ); fileDesc >= 0; --fileDesc )
			{
				close( fileDesc );
			}
			/* Handle standard I/O. */
			stdIO = open( "/dev/null", O_RDWR );
			if( dup( stdIO ) < 0 )
			{
				fprintf( stderr, "Cannot redirect stdout to /dev/null\n" );
				exit( EXIT_FAILURE );
			}
			if( dup( stdIO ) < 0 )
			{
				fprintf( stderr, "Cannot redirect stderr to /dev/null\n" );
				exit( EXIT_FAILURE );
			}
#endif
			testSocket = socketPair[ 0 ];
			/* Start the download queue as a thread. */
			if( pthread_create( &transferQueue, NULL,
								ProcessTransferQueues, &socketPair[ 0 ] ) != 0 )
			{
				fprintf( stderr, "Couldn't start download queue thread" );
				exit( 1 );
			}
			/* Start the file cache server. */
			InitializeFileCache( );
			while( 1 )
			{
				sleep( 5 );
			}
			ShutdownFileCache( );
		}
		/* Start the permissions grant module. */
		else
		{
			/* Set the child pid to kill on termination. */
			killPid = forkPid;
			/* Turn on credentials passing. */
			close( socketPair[ 0 ] );
			setsockopt( socketPair[ 1 ], SOL_SOCKET, SO_PASSCRED,
						&enabled, sizeof( enabled ) );

			InitializePermissionsGrant( forkPid, socketPair[ 1 ] );
		}
	}
}



/**
 * 
 * @param argc [in] The number of input arguments, used by the getopt library.
 * @param argv [in] The input argument strings, used by the getopt library.
 * @return EXIT_SUCCESS if no errors were encountered, EXIT_FAILURE otherwise.
 */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
int
main( int argc, char **argv )
{
	/* Start the processes. */
	StartProcesses( );
}
#pragma GCC diagnostic pop

