/**
 * \file fix-i386-cc.c
 * \brief Fix a cross-compilation issue.
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


#include <stdlib.h>


/* Fix for a cross-compilation issue where compilation of i386 code on an
   amd64 machine complains that rpl_malloc is undefined. */
/*@out@*/ void *rpl_malloc( size_t n )
{
    /* Allocate an N-byte block of memory from the heap.
       If N is zero, allocate a 1-byte block.  */
    if( n == 0 )
    {
        n = 1;
    }
    /*@-nullret@*/
    return malloc( n );
    /*@+nullret@*/
}
