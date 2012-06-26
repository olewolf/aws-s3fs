/**
 * \file dircache.h
 * \brief Maintain a directory cache.
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


#ifndef __DIR_CACHE_H
#define __DIR_CACHE_H


/* Cache 5 directories. The directory cache is only effective for a small
   number of directories, in the cache because of its brute-force search
   and update mechanism. */
#define DIR_CACHE_SIZE  5


void InitializeDirectoryCache( void );
void InsertInDirectoryCache( const char *dirname, int size,
			     const char **contents );
const char **LookupInDirectoryCache( const char *dirname, int *size );
void InvalidateDirectoryCacheElement( const char *dirname );
void ShutdownDirectoryCache( void );


#endif /* __DIR_CACHE_H */
