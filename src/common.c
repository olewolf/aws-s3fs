/**
 * \file common.c
 * \brief Various functions that are used by several unrelated functions.
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
#include <stdio.h>
#include "aws-s3fs.h"



/*@null@*/ /*@dependent@*/ const FILE*
TestFileReadable(
    const char *filename
		 )
{
    const FILE *fp;
    /* Test whether the specified file exists and is readable. Return NULL
       if it doesn't, or a pointer to the file (to lock it until it is
       closed) if it is readable. */
    fp =  fopen( filename, "r" );
    return( fp );
}
