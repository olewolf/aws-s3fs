/**
 * \file digest.h
 * \brief Functions to compute MD5/SHA1 message digests without using openssl.
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

#ifndef __DIGEST_H
#define __DIGEST_H

#include <config.h>
#include <stdio.h>


enum HashFunctions { HASH_MD5, HASH_SHA1 };
enum HashEncodings { HASHENC_BASE64, HASHENC_BIN, HASHENC_HEX };


/* Compute the MD5 or SHA1 message digest for bytes read from stream. The
   resulting message digest number will be written into the 16 bytes
   (for MD5) or 20 bytes (for SHA1) of ascDigest. */
int DigestStream( FILE *stream, char *ascDigest, enum HashFunctions function,
		  enum HashEncodings encoding );

/* Compute MD5 or SHA1 message digest for len bytes stored in buffer. The
   resulting message digest number will be written into the 16 bytes
   (for MD5) or 20 bytes (for SHA1) of ascDigest. */
void DigestBuffer( const unsigned char *buffer, size_t len, char *ascDigest,
		   enum HashFunctions function, enum HashEncodings encoding );

/* Compute the HMAC-hash signature for an in-memory message. */
const char* HMAC( const unsigned char *message, int length,
		  const char *key, enum HashFunctions function,
		  enum HashEncodings encoding );


#endif /* __DIGEST_H */
