/**
 * \file s3comms.c
 * \brief Shared S3 communications functions.
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

#ifndef __S3_COMMS_H
#define __S3_COMMS_H

#include <config.h>
#include <curl/curl.h>
#include "base64.h"
#include "digest.h"
#include <pthread.h>

enum bucketRegions
{
    US_STANDARD = 0,
	OREGON,
	NORTHERN_CALIFORNIA,
	IRELAND,
    SINGAPORE,
	TOKYO,
	SAO_PAULO
};


typedef struct
{
	enum bucketRegions region;
	char               *bucket;
	char               *keyId;
	char               *secretKey;
	CURL               *curl;
	pthread_mutex_t    curl_mutex;
} S3COMM;




S3COMM *s3_open( enum bucketRegions region, const char *bucket,
				 const char *keyId, const char *secretKey );
void s3_close( S3COMM *handle );


int s3_SubmitS3Request( S3COMM *handle, const char *httpVerb,
						struct curl_slist *headers, const char *filename,
						void **data, int *dataLength );
int s3_SubmitS3PutRequest( S3COMM *handle, struct curl_slist *headers,
						   const char *filename, void **response,
						   int *responseLength, unsigned char *bodyData,
						   size_t bodyLength );
struct curl_slist*
BuildS3Request( S3COMM *instance, const char *httpMethod,
				const char *hostname, struct curl_slist *additionalHeaders,
				const char *filename );
void DeleteCurlSlistAndContents( struct curl_slist *toDelete );
char* GetS3HostNameByRegion( enum bucketRegions region, const char *bucket );


#endif /* __S3_COMMS_H */
