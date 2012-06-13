/**
 * \file md5digest.c
 * \brief Functions to compute MD5 message digest without using openssl.
 *
 * Copyright (C) 2012 Ole Wolf <wolf@blazingangles.com>
 *
 * Original code copyright (C) 1995 Ulrich Drepper <drepper@gnu.ai.mit.edu>
 * licensed under GPLv2+.
 *
 * Modifications to the original code:
 *   * Functional modifications:
 *     - Made the code re-entrant.
 *     - Changed the output to 32-byte ASCII hex digest.
 *   * Structural modifications:
 *     - Changed all macros to inline functions, believing that compilers today
 *       are capable of choosing the best option.
 *     - Got rid of extensive 32-bit checking, which isn't necessary nowadays.
 *   * Cosmetic modifications:
 *     - Changed naming and indentation convention to match the remainder of
 *       the code in aws-s3fs.
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
#include <stdint.h>
#include <malloc.h>
#include <memory.h>
#include <assert.h>
#include <endian.h>
#include "md5digest.h"


/* Set to define MD5 functions as inline functions rather than macros. */
#define NO_MACRO_FUNCTIONS


/* It is unfortunate that C does not provide an operator for
   cyclic rotation.  Hope the C compiler is smart enough.  */
#ifdef NO_MACRO_FUNCTIONS

static inline uint32_t
ROTATE(
    register uint32_t w,
    register uint32_t s
       )
{
    return( w = ( w << s ) | ( w >> ( 32 - s ) ) );
}

#else
#define ROTATE( w, s ) ( w = ( w << s ) | ( w >> ( 32 - s ) ) )
#endif /* NO_MACRO_FUNCTIONS */


#ifdef NO_MACRO_FUNCTIONS

#if __BYTE_ORDER == __BIG_ENDIAN
static inline uint32_t
MAKE_LITTLE_ENDIAN(
    register uint32_t n
		   )
{
    return(   ( (n)            << 24 )             |
	    ( ( (n) & 0xff00 ) <<  8 )             |
	    ( ( (n)            >>  8 ) & 0xff00 )  |
	    (   (n)            >> 24 ) );

}

#else

static inline uint32_t
MAKE_LITTLE_ENDIAN(
    register uint32_t n
		   )
{
    return( n );
}

#endif /* __BYTE_ORDER == __BIG_ENDIAN */


#else

#if __BYTE_ORDER == __BIG_ENDIAN
#define MAKE_LITTLE_ENDIAN( n )	                     \
    ( ( (n)            << 24 ) |                     \
    ( ( (n) & 0xff00 ) <<  8 ) |                     \
    ( ( (n)            >>  8 ) & 0xff00 )  |         \
    (   (n)            >> 24) )
#else
#define MAKE_LITTLE_ENDIAN( n ) ( n )
#endif

#endif /* NO_MACRO_FUNCTIONS */


/* These are the four functions used in the four steps of the MD5 algorithm
   and defined in the RFC 1321.  The first function is a little bit optimized
   (as found in Colin Plumbs public domain implementation).
   Original function: #define FF(b, c, d) ((b & c) | (~b & d)) */

/* The functions have been changed from macros to inline functions, assuming
   that the compiler is capable of performing proper optimization. */
#ifdef NO_MACRO_FUNCTIONS
static inline uint32_t
FF(
     register uint32_t b,
     register uint32_t c,
     register uint32_t d
   )
{
    return( d ^ ( b & ( c ^ d ) ) );
}


static inline uint32_t
FG(
     register uint32_t b,
     register uint32_t c,
     register uint32_t d
   )
{
    return( FF( d, b, c ) );
}


static inline uint32_t
FH(
     register uint32_t b,
     register uint32_t c,
     register uint32_t d
   )
{
    return( b ^ c ^ d );
}


static inline uint32_t
FI(
     register uint32_t b,
     register uint32_t c,
     register uint32_t d
   )
{
    return(  c ^ ( b | ~d ) );
}

/* First round: using the given function, the context and a constant
   the next context is computed.  Because the algorithms processing
   unit is a 32-bit word and it is determined to work on words in
   little endian byte order we perhaps have to change the byte order
   before the computation.  To reduce the work for the next steps
   we store the swapped words in the array CORRECT_WORDS.  */
static inline void
OP(
    register                uint32_t *a,
    register                uint32_t b,
    register                uint32_t c,
    register                uint32_t d,
    register                uint32_t s,
    register                uint32_t T,
    register                uint32_t **cwp,
    register const uint32_t **words
   )
{
    *a = *a + FF( b, c, d ) + ( **cwp++ = MAKE_LITTLE_ENDIAN( **words ) ) + T;
    *words = *words + 1;
    ROTATE( *a, s );
    *a = *a +b;
}


static inline void
OP_fn(
    register uint32_t (*f)( uint32_t, uint32_t, uint32_t ),
    register uint32_t *a,
    register uint32_t b,
    register uint32_t c,
    register uint32_t d,
    register uint32_t s,
    register uint32_t T,
    register uint32_t correctWord
   )
{
    *a = *a + f( b, c, d ) + correctWord + T;
    ROTATE( *a, s );
    *a = *a + b;
}


#else
#define FF( b, c, d ) ( d ^ ( b & ( c ^ d ) ) )
#define FG( b, c, d ) FF( d, b, c )
#define FH( b, c, d ) ( b ^ c ^ d )
#define FI( b, c, d ) ( c ^ ( b | ~d ) )
/* First round: using the given function, the context and a constant
   the next context is computed.  Because the algorithms processing
   unit is a 32-bit word and it is determined to work on words in
   little endian byte order we perhaps have to change the byte order
   before the computation.  To reduce the work for the next steps
   we store the swapped words in the array CORRECT_WORDS.  */
#define OP(a, b, c, d, s, T, dummy1, dummy2)				   \
    *a = *a + FF( b, c, d ) + ( *cwp++ = MAKE_LITTLE_ENDIAN( *words ) ) + T; \
    words = words + 1;				                           \
    ROTATE( *a, s );							   \
    *a = *a + b

/* For the second to fourth round we have the possibly swapped words
   in CORRECT_WORDS. Define a similar macro that takes an additional
   first argument specifying the function to use. */
#define OP_fn( f, a, b, c, d, s, T, correctWord )			\
    *a = *a + f( b, c, d ) + correctWord + T;				\
    ROTATE( *a, s );							\
    *a = *a + b							

#endif /* NO_MACRO_FUNCTIONS */



/* Structure to save state of computation between the steps. */
struct MD5State
{
    uint32_t A;
    uint32_t B;
    uint32_t C;
    uint32_t D;
    uint32_t total[ 2 ];
    uint32_t buflen;
    char     buffer[ 128 ];
    /* This array contains is used to pad the buffer to the next
       64-byte boundary.  (RFC 1321, 3.1: Step 1)  */
    unsigned char fillbuf[ 64 ];
};


/* Starting with the result of former calls of this function (or the
   initialization function update the context for the next LEN bytes
   starting at BUFFER.
   The length must be a multiple of 64! */
static void MD5ProcessBlock( const char*, size_t, struct MD5State* );

/* Compute the MD5 message digest for bytes read from STREAM.  The
   resulting message digest number will be written into the 16 bytes
   beginning at RESBLOCK.  */
int MD5DigestStream( FILE *stream, char *md5digest );

/* Compute MD5 message digest for LEN bytes beginning at BUFFER.  The
   result is always in little endian byte order, so that a byte-wise
   output yields to the wanted ASCII representation of the message
   digest.  */
void MD5DigestBuffer( const char *buffer, size_t len, char *md5digest );



/**
 * Convert a 16-byte binary representation of an MD5 message digest to a
 * 32-byte ASCII hex representation.
 * @param binary [in] 16-byte binary MD5 digest.
 * @param md5result [out] 32-byte ASCII MD5 digest.
 * @return Nothing.
 */
static void
BinMD5toHexMD5(
    const char *binary,
    char       *md5result
	       )
{
    int i;
    for( i = 0; i < 16; i++ )
    {
        sprintf( &md5result[ i * 2 ], "%02x", binary[ i ] & 0x00ff );
    }
}



/**
 * Initialize the MD5 state.
 * @param state [out] MD5 state variables.
 * @return Nothing.
 */
static void
InitializeMD5State(
    struct MD5State *state
		   )
{
    /* Initialize structure containing state of computation.
       (RFC 1321, 3.3: Step 3)  */
    state->A = 0x67452301;
    state->B = 0xefcdab89;
    state->C = 0x98badcfe;
    state->D = 0x10325476;

    state->total[ 0 ] = 0;
    state->total[ 1 ] = 0;
    state->buflen = 0;

    memset( &state->fillbuf[ 0 ], 0, 64 );
}



/**
 * Write the MD5 state from the previous computations into resbuf (in little
 * endian byte order).
 * @param state [in/out] MD5 state variables.
 * @param resbuf [out] 16 byte buffer where the MD5 digest is eventually stored.
 * @return Nothing.
 */
static char*
RestoreState(
    const struct MD5State *state,
    char                  *resbuf
	     )
{
    ( (uint32_t*) resbuf )[ 0 ] = MAKE_LITTLE_ENDIAN( state->A );
    ( (uint32_t*) resbuf )[ 1 ] = MAKE_LITTLE_ENDIAN( state->B );
    ( (uint32_t*) resbuf )[ 2 ] = MAKE_LITTLE_ENDIAN( state->C );
    ( (uint32_t*) resbuf )[ 3 ] = MAKE_LITTLE_ENDIAN( state->D );

    return resbuf;
}



/**
 * Process the remaining bytes in the internal buffer to produce the final
 * MD5 digest.
 * @param state [in/out] MD5 state variables.
 * @param resbuf [out] 16 byte buffer where the MD5 digest is eventually stored.
 * @return Nothing.
 */
static void*
FlushMD5State(
    struct MD5State *state,
    char            *resbuf
	      )
{
    /* Take yet unprocessed bytes into account.  */
    uint32_t bytes   = state->buflen;
    size_t   padding;

    /* Now count remaining bytes.  */
    state->total[ 0 ] = state->total[ 0 ] + bytes;
    if( state->total[ 0 ] < bytes )
    {
        state->total[ 1 ] = state->total[ 1 ] + 1;
    }

    if( bytes >= 56 )
    {
	padding = 64 + 56 - bytes;
    }
    else
    {
	padding =      56 - bytes;
    }
    memcpy( &state->buffer[ bytes ], state->fillbuf, padding );

    /* Put the 64-bit file length in *bits* at the end of the buffer.  */
    *(uint32_t*) &state->buffer[ bytes + padding    ] =
        MAKE_LITTLE_ENDIAN(   state->total[ 0 ] << 3 );
    *(uint32_t*) &state->buffer[ bytes + padding + 4] =
        MAKE_LITTLE_ENDIAN( ( state->total[ 1 ] << 3) |
                            ( state->total[ 0 ] >> 29 ) );

    /* Process last bytes.  */
    MD5ProcessBlock( state->buffer, bytes + padding + 8, state );

    return( RestoreState( state, resbuf ) );
}


/**
 * Process all but the last "length mod 64" bytes in a buffer.
 * @param buffer [in] Buffer with the remaining bytes.
 * @param len [in] Number of bytes in the buffer.
 * @param state [in/out] MD5 state.
 * @return Nothing.
 */
static void
MD5ProcessMessage(
    const char      *buffer,
    size_t          len,
    struct MD5State *state
		  )
{
    size_t leftOver;
    size_t toAdd;

    /* When we already have some bits in our internal buffer concatenate
       both inputs first.  */
    if( state->buflen != 0 )
    {
        leftOver = state->buflen;
	if( 128 - leftOver > len )
	{
	    toAdd = len;
	}
	else
        {
	    toAdd = 128 - leftOver;
	}

	memcpy( &state->buffer[ leftOver ], buffer, toAdd );
	state->buflen = state->buflen + toAdd;

	if( leftOver + toAdd > 64 )
	{
	    MD5ProcessBlock(state->buffer, (leftOver + toAdd) & ~63, state);
	    /* The regions in the following copy operation cannot overlap. */
	    memcpy( state->buffer, &state->buffer[ ( leftOver + toAdd ) & ~63 ],
		    ( leftOver + toAdd ) & 63 );
	    state->buflen = ( leftOver + toAdd ) & 63;
	}

	buffer = buffer + toAdd;
	len = len - toAdd;
    }

    /* Process available complete blocks.  */
    if (len > 64)
    {
        MD5ProcessBlock( buffer, len & ~63, state );
	buffer = buffer + ( len & ~63 );
	len = len & 63;
    }

    /* Move remaining bytes in internal buffer.  */
    if (len > 0)
    {
        memcpy( state->buffer, buffer, len );
	state->buflen = len;
    }
}



/**
 * Process a message blockwise, storing the MD5 state between function calls.
 * This is useful for calculating the MD5 digest of a message that does not
 * fit entirely in memory.
 * @param buffer [in] Buffer with multiples of 64 bytes of the message.
 * @param len [in] Number of bytes in the buffer.
 * @param state [in/out] MD5 state variables.
 * @return Nothing.
 */
static void
MD5ProcessBlock(
    const char      *buffer,
    size_t          len,
    struct MD5State *state
		)
{
    uint32_t       correctWords[ 16 ] = { 0 };
    const uint32_t *words = (uint32_t*) buffer;
    size_t         nWords = len / sizeof( uint32_t );
    const uint32_t *endp  = &words[ nWords ];
    uint32_t       A = state->A;
    uint32_t       B = state->B;
    uint32_t       C = state->C;
    uint32_t       D = state->D;
    uint32_t       *cwp;
    uint32_t       A_save, B_save, C_save, D_save;

    /* First increment the byte count.  RFC 1321 specifies the possible
       length of the file up to 2^64 bits.  Here we only compute the
       number of bytes.  Do a double word increment.  */
    state->total[ 0 ] = state->total[ 0 ] + len;
    if( state->total[ 0 ] < len )
    {
	state->total[ 1 ] = state->total[ 1 ] + 1;
    }

    /* Process all bytes in the buffer with 64 bytes in each round of
       the loop.  */
    while( words < endp )
    {
        cwp = correctWords;
	A_save = A;
	B_save = B;
	C_save = C;
	D_save = D;

	/* Before we start, one word to the strange constants.
	   They are defined in RFC 1321 as

	     T[i] = (int) (4294967296.0 * fabs (sin (i))), i=1..64
	*/

	/* Round 1.  */
	OP( &A, B, C, D,  7, 0xd76aa478, &cwp, &words );
	OP( &D, A, B, C, 12, 0xe8c7b756, &cwp, &words );
	OP( &C, D, A, B, 17, 0x242070db, &cwp, &words );
	OP( &B, C, D, A, 22, 0xc1bdceee, &cwp, &words );
	OP( &A, B, C, D,  7, 0xf57c0faf, &cwp, &words );
	OP( &D, A, B, C, 12, 0x4787c62a, &cwp, &words );
	OP( &C, D, A, B, 17, 0xa8304613, &cwp, &words );
	OP( &B, C, D, A, 22, 0xfd469501, &cwp, &words );
	OP( &A, B, C, D,  7, 0x698098d8, &cwp, &words );
	OP( &D, A, B, C, 12, 0x8b44f7af, &cwp, &words );
	OP( &C, D, A, B, 17, 0xffff5bb1, &cwp, &words );
	OP( &B, C, D, A, 22, 0x895cd7be, &cwp, &words );
	OP( &A, B, C, D,  7, 0x6b901122, &cwp, &words );
	OP( &D, A, B, C, 12, 0xfd987193, &cwp, &words );
	OP( &C, D, A, B, 17, 0xa679438e, &cwp, &words );
	OP( &B, C, D, A, 22, 0x49b40821, &cwp, &words );

	/* Round 2.  */
	OP_fn( FG, &A, B, C, D,  5, 0xf61e2562, correctWords[  1 ] );
	OP_fn( FG, &D, A, B, C,  9, 0xc040b340, correctWords[  6 ] );
	OP_fn( FG, &C, D, A, B, 14, 0x265e5a51, correctWords[ 11 ] );
	OP_fn( FG, &B, C, D, A, 20, 0xe9b6c7aa, correctWords[  0 ] );
	OP_fn( FG, &A, B, C, D,  5, 0xd62f105d, correctWords[  5 ] );
	OP_fn( FG, &D, A, B, C,  9, 0x02441453, correctWords[ 10 ] );
	OP_fn( FG, &C, D, A, B, 14, 0xd8a1e681, correctWords[ 15 ] );
	OP_fn( FG, &B, C, D, A, 20, 0xe7d3fbc8, correctWords[  4 ] );
	OP_fn( FG, &A, B, C, D,  5, 0x21e1cde6, correctWords[  9 ] );
	OP_fn( FG, &D, A, B, C,  9, 0xc33707d6, correctWords[ 14 ] );
	OP_fn( FG, &C, D, A, B, 14, 0xf4d50d87, correctWords[  3 ] );
	OP_fn( FG, &B, C, D, A, 20, 0x455a14ed, correctWords[  8 ] );
	OP_fn( FG, &A, B, C, D,  5, 0xa9e3e905, correctWords[ 13 ] );
	OP_fn( FG, &D, A, B, C,  9, 0xfcefa3f8, correctWords[  2 ] );
	OP_fn( FG, &C, D, A, B, 14, 0x676f02d9, correctWords[  7 ] );
	OP_fn( FG, &B, C, D, A, 20, 0x8d2a4c8a, correctWords[ 12 ] );

	/* Round 3.  */
	OP_fn( FH, &A, B, C, D,  4, 0xfffa3942, correctWords[  5 ] );
	OP_fn( FH, &D, A, B, C, 11, 0x8771f681, correctWords[  8 ] );
	OP_fn( FH, &C, D, A, B, 16, 0x6d9d6122, correctWords[ 11 ] );
	OP_fn( FH, &B, C, D, A, 23, 0xfde5380c, correctWords[ 14 ] );
	OP_fn( FH, &A, B, C, D,  4, 0xa4beea44, correctWords[  1 ] );
	OP_fn( FH, &D, A, B, C, 11, 0x4bdecfa9, correctWords[  4 ] );
	OP_fn( FH, &C, D, A, B, 16, 0xf6bb4b60, correctWords[  7 ] );
	OP_fn( FH, &B, C, D, A, 23, 0xbebfbc70, correctWords[ 10 ] );
	OP_fn( FH, &A, B, C, D,  4, 0x289b7ec6, correctWords[ 13 ] );
	OP_fn( FH, &D, A, B, C, 11, 0xeaa127fa, correctWords[  0 ] );
	OP_fn( FH, &C, D, A, B, 16, 0xd4ef3085, correctWords[  3 ] );
	OP_fn( FH, &B, C, D, A, 23, 0x04881d05, correctWords[  6 ] );
	OP_fn( FH, &A, B, C, D,  4, 0xd9d4d039, correctWords[  9 ] );
	OP_fn( FH, &D, A, B, C, 11, 0xe6db99e5, correctWords[ 12 ] );
	OP_fn( FH, &C, D, A, B, 16, 0x1fa27cf8, correctWords[ 15 ] );
	OP_fn( FH, &B, C, D, A, 23, 0xc4ac5665, correctWords[  2 ] );

	/* Round 4.  */
	OP_fn( FI, &A, B, C, D,  6, 0xf4292244, correctWords[  0 ] );
	OP_fn( FI, &D, A, B, C, 10, 0x432aff97, correctWords[  7 ] );
	OP_fn( FI, &C, D, A, B, 15, 0xab9423a7, correctWords[ 14 ] );
	OP_fn( FI, &B, C, D, A, 21, 0xfc93a039, correctWords[  5 ] );
	OP_fn( FI, &A, B, C, D,  6, 0x655b59c3, correctWords[ 12 ] );
	OP_fn( FI, &D, A, B, C, 10, 0x8f0ccc92, correctWords[  3 ] );
	OP_fn( FI, &C, D, A, B, 15, 0xffeff47d, correctWords[ 10 ] );
	OP_fn( FI, &B, C, D, A, 21, 0x85845dd1, correctWords[  1 ] );
	OP_fn( FI, &A, B, C, D,  6, 0x6fa87e4f, correctWords[  8 ] );
	OP_fn( FI, &D, A, B, C, 10, 0xfe2ce6e0, correctWords[ 15 ] );
	OP_fn( FI, &C, D, A, B, 15, 0xa3014314, correctWords[  6 ] );
	OP_fn( FI, &B, C, D, A, 21, 0x4e0811a1, correctWords[ 13 ] );
	OP_fn( FI, &A, B, C, D,  6, 0xf7537e82, correctWords[  4 ] );
	OP_fn( FI, &D, A, B, C, 10, 0xbd3af235, correctWords[ 11 ] );
	OP_fn( FI, &C, D, A, B, 15, 0x2ad7d2bb, correctWords[  2 ] );
	OP_fn( FI, &B, C, D, A, 21, 0xeb86d391, correctWords[  9 ] );

	/* Add the starting values of the context.  */
	A = A + A_save;
	B = B + B_save;
	C = C + C_save;
	D = D + D_save;
    }

    /* Put checksum in context given as argument.  */
    state->A = A;
    state->B = B;
    state->C = C;
    state->D = D;
}



/**
 * Compute the MD5 message digest for bytes read from a file. The message
 * digest is returned as 32 bytes of hex ASCII code (non-null-terminated).
 * @param stream [in] File to generate the MD5 digest of.
 * @param md5sum [out] Destination string for the MD5 digest.
 * @return 0 if the MD5 digest was computed, or 1 on file errors.
 */
int
MD5DigestStream(
     FILE *stream,
     char *md5sum
		)
{
    /* Compute MD5 message digest for bytes read from STREAM.  The
       resulting message digest number will be written into the 16 bytes
       beginning at RESBLOCK.  */

    /* Important: BLOCKSIZE must be a multiple of 64.  */
    const int       BLOCKSIZE = 4096;
    struct MD5State state;
    char            resblock[ 16 ];
    char            *buffer;
    size_t          sum;

    buffer = malloc( BLOCKSIZE + 72 );
    if( buffer == NULL )
    {
        return( 1 );
    }

    /* Initialize the computation context.  */
    InitializeMD5State( &state );

    /* Iterate over full file contents.  */
    while( 1 )
    {
        /* We read the file in blocks of BLOCKSIZE bytes.  One call of the
	   computation function processes the whole buffer so that with the
	   next round of the loop another block can be read.  */
        size_t bytesRead;
        sum = 0;

	/* Read block.  Take care for partial reads.  */
	do
	{
	    bytesRead = fread( buffer + sum, 1, BLOCKSIZE - sum, stream );
	    sum += bytesRead;
	}
	while( sum < BLOCKSIZE && bytesRead != 0 );
	if( ( bytesRead == 0 ) && ferror( stream ) )
	{
	    return 1;
	}

	/* If end of file is reached, end the loop.  */
	if( bytesRead == 0 )
	break;

	/* Process buffer with BLOCKSIZE bytes. Note that BLOCKSIZE % 64 = 0. */
	MD5ProcessBlock( buffer, BLOCKSIZE, &state );
    }

    /* Add the remaining bytes if necessary, if any.  */
    if( sum > 0 )
    {
	MD5ProcessMessage( buffer, sum, &state );
    }

    /* Construct result in desired memory.  */
    FlushMD5State (&state, resblock);
    BinMD5toHexMD5( resblock, md5sum );

    return( 0 );
}



/**
 * Compute MD5 message digest for an in-memory buffer. The message
 * digest is returned as 32 bytes of hex ASCII code (non-null-terminated).
 * @param stream [in] Buffer with the message to generate the MD5 digest of.
 * @param len [in] Number of bytes in the buffer.
 * @param md5Result [out] Destination string for the MD5 digest.
 * @return Nothing.
 */
void
MD5DigestBuffer( const char *buffer, size_t len, char *md5sum )
{
    /* Compute MD5 message digest for LEN bytes beginning at BUFFER.  The
       result is always in little endian byte order, so that a byte-wise
       output yields to the wanted ASCII representation of the message
       digest. */

    struct MD5State state;
    char resblock[ 16 ];

    /* Initialize the MD5 state variables.  */
    InitializeMD5State( &state );

    /* Process the whole buffer but the last len % 64 bytes.  */
    MD5ProcessMessage( buffer, len, &state );

    /* Finish the remaining bytes. */
    FlushMD5State( &state, resblock );

    /* Put result in desired memory area.  */
    BinMD5toHexMD5( resblock, md5sum );
}

