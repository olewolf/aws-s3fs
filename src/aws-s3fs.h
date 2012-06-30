/**
 * \file aws-s3fs.h
 * \brief Definitions shared among the aws-s3fs source code files.
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

#ifndef __AWS_S3FS_H
#define __AWS_S3FS_H

#include <config.h>
#include <assert.h>
#include <stdio.h>
#include <stdbool.h>
#include <syslog.h>
#include "sysdirs.h"
#include "s3comms.h"


#define DEFAULT_TMP_DIR "/tmp"

/* Maximum number of open files. */
#define MAX_FILE_DESCRIPTORS 16

#define SOCKET_NAME LOCALSTATEDIR "/aws-s3fs.sock"


/* Make room for 5,000 files in the stat cache. */
#define MAX_STAT_CACHE_SIZE 5000l

/* Default, system-wide aws-s3fs.conf file. */
#define DEFAULT_CONFIG_FILENAME SYSCONFDIR "/aws-s3fs.conf"

/* Lock dir. */
#define LOCK_DIR LOCALSTATEDIR "/lock"

/* Chache dir. */
#define CACHE_DIR LOCALSTATEDIR "/cache/aws-s3fs"

/* Name of the shared library. */
#define AWS_S3FS_LIB "aws-s3fs.so"


/** Default configuration values. */
#define DEFAULT_REGION     "US Standard"
#define DEFAULT_BUCKETNAME "bucket"
#define DEFAULT_PATH       "/"
#define DEFAULT_KEY_ID     "accesskeyid"
#define DEFAULT_SECRET_KEY "secretkey"
#define DEFAULT_LOG_FILE   "/var/log/aws-s3fs.log"
#define DEFAULT_VERBOSE    false


struct ConfigurationBoolean {
    bool value;
    bool isset;
};

enum LogLevels {
    log_ERR     = LOG_ERR,
    log_WARNING = LOG_WARNING,
    log_NOTICE  = LOG_NOTICE,
    log_INFO    = LOG_INFO,
    log_DEBUG   = LOG_DEBUG
};


struct Configuration {
    enum bucketRegions          region;
    /*@null@*/ char             *mountPoint;
    /*@null@*/ char             *bucketName;
    /*@null@*/ char             *path;
    /*@null@*/ char             *keyId;
    /*@null@*/ char             *secretKey;
    /*@null@*/ char             *logfile;
    struct ConfigurationBoolean verbose;
    enum LogLevels              logLevel;
    bool                        daemonize;
};

struct CmdlineConfiguration {
    struct Configuration configuration;
    /*@null@*/ char      *configFile;
    bool                 regionSpecified;
    bool                 bucketNameSpecified;
    bool                 pathSpecified;
    bool                 keyIdSpecified;
    bool                 secretKeySpecified;
    bool                 logfileSpecified;
    bool                 loglevelSpecified;
};

#define bool_equal( a, b ) ( (a) ? (b) : !(b) )

/* In logger.c */
void InitializeLoggingModule( );
void Syslog( int priority, const char *format, ... );
const char *LogFilename( );
enum LogLevels LogLevel( );
void EnableLogging( );
void DisableLogging( );
void InitLog( const char *logfile, enum LogLevels );
void CloseLog( );

/* In config.c */
void InitializeConfiguration( struct Configuration* );

/* In common.c */
void VerboseOutput( bool, const char *format, ... );

/* In aws-s3fs.c. */
extern struct Configuration globalConfig;

void
Configure(
    struct Configuration *configuration,
    int                  argc,
    const char * const   *argv
);


bool
ReadConfigFile(
    const char           *configFilename,
    struct Configuration *configuration
);

void
ConfigSetKey(
    char       **keyId,
    char       **secretKey,
    const char *configValue,
    bool       *configError
);

void
ConfigSetLoglevel(
    enum LogLevels *loglevel,
    const char     *configValue,
    bool           *configError
);

void
ConfigSetRegion(
    enum bucketRegions *region,
    const char         *configName,
    bool               *configError
);

void
ConfigSetPath(
    char       **path,
    const char *configPath
);


/* In common.c. */

/*@null@*/ /*@dependent@*/ const FILE*
TestFileReadable(
    const char *filename
);


/* In decodecmdline.c. */
bool
DecodeCommandLine(
    struct CmdlineConfiguration *cmdlineConfiguration,
    int                         argc,
    const char * const          *argv
);



#endif /* __AWS_S3FS_H */
