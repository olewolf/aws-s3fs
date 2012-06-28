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
#include "aws-s3fs.h"


void s3fs_InitializeLibrary( void );

int s3fs_SubmitS3Request( const char *httpVerb, enum bucketRegions region,
						  const char *bucket, struct curl_slist *headers,
						  const char *filename, void **data, int *dataLength );
int s3fs_SubmitS3PutRequest( struct curl_slist *headers,
							 enum bucketRegions region, const char *bucket,
							 const char *filename, void **response,
							 int *responseLength, unsigned char *bodyData,
							 size_t bodyLength );


#endif /* __S3_COMMS_H */
