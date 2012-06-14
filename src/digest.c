/**
 * \file digest.c
 * \brief Compute MD5 and SHA message digests without using openssl.
 *
 * Copyright (C) 2012 Ole Wolf <wolf@blazingangles.com>
 *
 * Original MD5 code copyright (C) 1995 Ulrich Drepper <drepper@gnu.ai.mit.edu>
 * licensed under GPLv2+.
 *
 * Original SHA1 code copyright (C) 1998, 2009 Paul E. Jones
 * <paulej@packetizer.com> licensed as freeware.
 *
 * Modifications to the original code:
 *   * Functional modifications:
 *     - Made the MD5 code re-entrant.
 *     - Changed the output to 32-byte ASCII hex digest.
 *   * Structural modifications:
 *     - Changed all macros to inline functions, believing that compilers today
 *       are capable of choosing the best option.
 *     - Got rid of extensive 32-bit checking, which isn't necessary nowadays.
 *     - Combined the MD5 digest and the SHA1 digest calculations into the
 *       same outer wrapper.
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
#include "digest.h"



/* Set to define functions as inline functions rather than macros. */
#define NO_MACRO_FUNCTIONS


/* It is unfortunate that C does not provide an operator for
   cyclic rotation. Hope the C compiler is smart enough.  */
#ifdef NO_MACRO_FUNCTIONS
static inline void
ROTATE(
    register uint32_t *w,
    register uint32_t s
       )
{
    *w = ( *w << s ) | ( (uint32_t) ( *w >> ( 32 - s ) ) );
}

static uint32_t inline
SHA1CircularShift(
    register uint32_t bits,
    register uint32_t word
)
{
    return( ( ( word << bits ) & 0xffffffff ) | (word >> ( 32 - bits ) ) );
}

#else
#define ROTATE( w, s ) ( w = ( w << s ) | ( w >> ( 32 - s ) ) )
#define SHA1CircularShift( bits, word )	\
    ( ( ( (word) << (bits) ) & 0xFFFFFFFF ) | ( (word ) >> ( 32 - (bits) ) ) )
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
#endif // __BYTE_ORDER == __BIG_ENDIAN

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
    *a = *a + FF( b, c, d ) + ( *(*cwp)++ = MAKE_LITTLE_ENDIAN( **words ) ) + T;
    *words = *words + 1;
    ROTATE( a, s );
    *a = *a + b;
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
    ROTATE( a, s );
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
    ROTATE( a, s );							   \
    *a = *a + b

/* For the second to fourth round we have the possibly swapped words
   in CORRECT_WORDS. Define a similar macro that takes an additional
   first argument specifying the function to use. */
#define OP_fn( f, a, b, c, d, s, T, correctWord )			\
    *a = *a + f( b, c, d ) + correctWord + T;				\
    ROTATE( a, s );							\
    *a = *a + b							

#endif /* NO_MACRO_FUNCTIONS */


/* This array contains the bytes used to pad the buffer to the next
   64-byte boundary.  (RFC 1321, 3.1: Step 1)  */
static const unsigned char fillbuf[ 64 ] =
{
    0x80, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
       0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
       0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
       0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};


/* Structure to save state of computation between the steps. */
struct DigestState
{
    uint32_t      A;
    uint32_t      B;
    uint32_t      C;
    uint32_t      D;
    uint32_t      E;
    uint32_t      total[ 2 ];
    uint32_t      buflen;
    unsigned char buffer[ 128 ];

    int Computed;
    int Corrupted;
};


/*
 *  Function Prototypes
 */
static void
BinDigestToHexDigest(
    const unsigned char *binary, char *digest, enum HashFunctions function );

static void
MD5ProcessBlock( const unsigned char *buffer, size_t len,
		 struct DigestState *ctx );

static void
MD5ProcessContinuous( const unsigned char *buffer, size_t len,
		      struct DigestState *ctx );

static int SHA1Result( struct DigestState* );
static void SHA1Input( struct DigestState*, const unsigned char*, unsigned );
static void SHA1ProcessMessageBlock(struct DigestState *);
static void SHA1PadMessage(struct DigestState *);



/**
 * Convert a binary representation of an MD5 or SHA1 message digest to an
 * ASCII hex representation.
 * @param binary [in] binary digest.
 * @param digest [out] ASCII digest.
 * @param function [in] Hash function; HASH_MD5 or HASH_SHA1.
 * @return Nothing.
 */
static void
BinDigestToHexDigest(
    const unsigned char *binary,
    char                *digest,
    enum HashFunctions  function
		     )
{
    int i;
    int size;

    switch( function )
    {
        case HASH_MD5:
	    size = 128 / 8;
	    break;
        case HASH_SHA1:
	    size = 160 / 8;
	    break;
        default:
	    size = 0;
	    break;
    }

    for( i = 0; i < size; i++ )
    {
        sprintf( &digest[ i * 2 ], "%02x", binary[ i ] & 0x00ff );
    }
}



/* Initialize structure containing state of computation.
   (RFC 1321, 3.3: Step 3)  */
/**
 * Initialize the digest computation state variables.
 * @param ctx [out] MD5 state variables.
 * @return Nothing.
 */
static void
InitializeDigestState(
    struct DigestState *ctx
		      )
{
    ctx->A = 0x67452301;
    ctx->B = 0xefcdab89;
    ctx->C = 0x98badcfe;
    ctx->D = 0x10325476;
    ctx->E = 0xc3d2e1f0;

    ctx->total[ 0 ] = 0;
    ctx->total[ 1 ] = 0;
    ctx->buflen     = 0;

    ctx->Computed   = 0;
    ctx->Corrupted  = 0;
}

/* Put result from CTX in first 16 bytes following RESBUF.  The result
   must be in little endian byte order.

   IMPORTANT: On some systems it is required that RESBUF is correctly
   aligned for a 32 bits value.  */
/**
 * Write the digest computation state from the previous computations into
 * resbuf (in little endian byte order).
 * @param ctx [in/out] State variables.
 * @param resbuf [out] 16 byte buffer where the MD5 digest is eventually stored.
 * @return Nothing.
 */
static unsigned char*
RestoreState(
    const struct DigestState *ctx,
    unsigned char            *resbuf
	     )
{
  ( (uint32_t *) resbuf )[ 0 ] = MAKE_LITTLE_ENDIAN( ctx->A );
  ( (uint32_t *) resbuf )[ 1 ] = MAKE_LITTLE_ENDIAN( ctx->B );
  ( (uint32_t *) resbuf )[ 2 ] = MAKE_LITTLE_ENDIAN( ctx->C );
  ( (uint32_t *) resbuf )[ 3 ] = MAKE_LITTLE_ENDIAN( ctx->D );

  return resbuf;
}



/* Process the remaining bytes in the internal buffer and the usual
   prolog according to the standard and write the result to RESBUF.

   IMPORTANT: On some systems it is required that RESBUF is correctly
   aligned for a 32 bits value.  */
/**
 * Process the remaining bytes in the internal buffer to produce the final
 * MD5 digest.
 * @param state [in/out] MD5 state variables.
 * @param resbuf [out] 16 byte buffer where the MD5 digest is eventually stored.
 * @return Nothing.
 */
static unsigned char*
MD5FlushState(
    struct DigestState *ctx,
    unsigned char      *resbuf
	      )
{
    /* Take yet unprocessed bytes into account.  */
    uint32_t bytes   = ctx->buflen;
    size_t   padding;

    /* Now count remaining bytes.  */
    ctx->total[ 0 ] = ctx->total[ 0 ] + bytes;
    if( ctx->total[ 0 ] < bytes )
    {
        ctx->total[ 1 ] = ctx->total[ 1 ] + 1;
    }

    if( bytes >= 56 )
    {
        padding = 64 + 56 - bytes;
    }
    else
    {
        padding =      56 - bytes;
    }
    memcpy( &ctx->buffer[ bytes ], fillbuf, padding );

    /* Put the 64-bit file length in *bits* at the end of the buffer.  */
    *(uint32_t*) &ctx->buffer[ bytes + padding    ] =
        MAKE_LITTLE_ENDIAN(   ctx->total[ 0 ] << 3 );
    *(uint32_t*) &ctx->buffer[ bytes + padding + 4] =
        MAKE_LITTLE_ENDIAN( ( ctx->total[ 1 ] << 3) |
                            ( ctx->total[ 0 ] >> 29 ) );

    /* Process last bytes.  */
    MD5ProcessBlock( ctx->buffer, bytes + padding + 8, ctx );

    return( RestoreState( ctx, resbuf ) );
}



/* Compute MD5 message digest for bytes read from STREAM.  The
   resulting message digest number will be written into the 16 bytes
   beginning at RESBLOCK.  */
/**
 * Compute the MD5 message digest for bytes read from a file. The message
 * digest is returned as 32 bytes of hex ASCII code (non-null-terminated).
 * @param stream [in] File to generate the MD5 digest of.
 * @param ascDigest [out] Destination string for the MD5 digest.
 * @return 0 if the MD5 digest was computed, or 1 on file errors.
 */
int
DigestStream(
     FILE               *stream,
     char               *ascDigest,
     enum HashFunctions function
	     )
{
    /* Compute MD5 message digest for bytes read from STREAM.  The
       resulting message digest number will be written into the 16 bytes
       beginning at RESBLOCK.  */

    /* Important: BLOCKSIZE must be a multiple of 64.  */
    const int          BLOCKSIZE = 4096;
    struct DigestState state;
    unsigned char      resblock[ 20 ];
    unsigned char      *buffer;
    size_t             sum;

    buffer = malloc( BLOCKSIZE + 72 );
    if( buffer == NULL )
    {
        return( 1 );
    }

    /* Initialize the computation context.  */
    InitializeDigestState( &state );

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

	if( function == HASH_MD5 )
	{
	    /* Process buffer with BLOCKSIZE bytes. Note that
	       BLOCKSIZE % 64 = 0. */
	    MD5ProcessBlock( buffer, BLOCKSIZE, &state );
	}
	if( function == HASH_SHA1 )
	{
	    SHA1Input( &state, buffer, sum );
	}
    }

    if( function == HASH_MD5 )
    {
        /* Add the remaining bytes if necessary, if any.  */
        if( sum > 0 )
	{
	    MD5ProcessContinuous( buffer, sum, &state );
	}

	/* Construct result in desired memory.  */
	MD5FlushState( &state, resblock );
	BinDigestToHexDigest( resblock, ascDigest, function );
    }
    if( function == HASH_SHA1 )
    {
        /* Add the remaining bytes if necessary, if any.  */
        if( sum > 0 )
	{
	    SHA1Input( &state, buffer, sum );
	}
	
	/* Pad the result. */
	SHA1Result( &state );

	/* Put result in designated memory area.  */
	sprintf( ascDigest, "%08x%08x%08x%08x%08x",
		 state.A, state.B, state.C, state.D, state.E );
    }

    return( 0 );
}



/**
 * Process a continuous buffer.
 * @param buffer [in] Buffer.
 * @param len [in] Number of bytes in the buffer.
 * @param state [in/out] MD5 state.
 * @return Nothing.
 */
static void
MD5ProcessContinuous(
    const unsigned char *buffer,
    size_t              len,
    struct DigestState  *ctx
		     )
{
    size_t leftOver;
    size_t toAdd;

    /* When we already have some bits in our internal buffer concatenate
       both inputs first.  */
    if( ctx->buflen != 0 )
    {
        leftOver = ctx->buflen;
	if( 128 - leftOver > len )
	{
	    toAdd = len;
	}
	else
        {
	    toAdd = 128 - leftOver;
	}

	memcpy( &ctx->buffer[ leftOver ], buffer, toAdd );
	ctx->buflen = ctx->buflen + toAdd;

	if( leftOver + toAdd > 64 )
	{
	    MD5ProcessBlock( ctx->buffer, ( leftOver + toAdd ) & ~63, ctx );
	    /* The regions in the following copy operation cannot overlap. */
	    memcpy( ctx->buffer, &ctx->buffer[ ( leftOver + toAdd ) & ~63 ],
		    ( leftOver + toAdd ) & 63 );
	    ctx->buflen = ( leftOver + toAdd ) & 63;
	}

	buffer = buffer + toAdd;
	len = len - toAdd;
    }

    /* Process available complete blocks.  */
    if (len > 64)
    {
        MD5ProcessBlock( buffer, len & ~63, ctx );
	buffer = buffer + ( len & ~63 );
	len = len & 63;
    }

    /* Move remaining bytes in internal buffer.  */
    if (len > 0)
    {
        memcpy( ctx->buffer, buffer, len );
	ctx->buflen = len;
    }
}



/* Process LEN bytes of BUFFER, accumulating context into CTX.
   It is assumed that LEN % 64 == 0.  */

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
    const unsigned char *buffer,
    size_t              len,
    struct DigestState  *state
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
 * Compute message digest for an in-memory buffer. The message
 * digest is returned as 32 bytes of hex ASCII code (non-null-terminated).
 * @param buffer [in] Buffer with the message to generate the digest of.
 * @param len [in] Number of bytes in the buffer.
 * @param ascDigest [out] Destination string for the digest.
 * @return Nothing.
 */
void
DigestBuffer(
    const unsigned char *buffer,
    size_t              len,
    char                *ascDigest,
    enum HashFunctions  function
		)
{
    /* Compute message digest for LEN bytes beginning at BUFFER.  The
       result is always in little endian byte order, so that a byte-wise
       output yields to the wanted ASCII representation of the message
       digest. */
    struct DigestState state;
    unsigned char      resblock[ 20 ];

    /* Initialize the digest state variables.  */
    InitializeDigestState( &state );

    if( function == HASH_MD5 )
    {
        /* Process the whole buffer but the last len % 64 bytes.  */
        MD5ProcessContinuous( buffer, len, &state );

	/* Finish the remaining bytes. */
	MD5FlushState( &state, resblock );

	/* Put result in designated memory area.  */
	BinDigestToHexDigest( resblock, ascDigest, function );
    }

    if( function == HASH_SHA1 )
    {
        /* Process the whole buffer but the last len % 64 bytes.  */
	SHA1Input( &state, buffer, len );
	/* Pad the result. */
	SHA1Result( &state );

	/* Put result in designated memory area.  */
	sprintf( ascDigest, "%08x%08x%08x%08x%08x",
		 state.A, state.B, state.C, state.D, state.E );
    }
}



/*  
 *  SHA1Result
 *
 *  Description:
 *      This function will return the 160-bit message digest into the
 *      Message_Digest array within the struct DigestState provided
 *
 *  Parameters:
 *      context: [in/out]
 *          The context to use to calculate the SHA-1 hash.
 *
 *  Returns:
 *      1 if successful, 0 if it failed.
 *
 *  Comments:
 *
 */
static int
SHA1Result(
    struct DigestState *context
	   )
{

    if (context->Corrupted)
    {
        return 0;
    }

    if (!context->Computed)
    {
        SHA1PadMessage(context);
        context->Computed = 1;
    }

    return 1;
}



/*  
 *  SHA1Input
 *
 *  Description:
 *      This function accepts an array of octets as the next portion of
 *      the message.
 *
 *  Parameters:
 *      context: [in/out]
 *          The SHA-1 context to update
 *      message_array: [in]
 *          An array of characters representing the next portion of the
 *          message.
 *      length: [in]
 *          The length of the message in message_array
 *
 *  Returns:
 *      Nothing.
 *
 *  Comments:
 *
 */
static void
SHA1Input(
    struct DigestState  *context,
    const unsigned char *message_array,
    unsigned            length
	  )
{
    if (!length)
    {
        return;
    }

    if (context->Computed || context->Corrupted)
    {
        context->Corrupted = 1;
        return;
    }

    while(length-- && !context->Corrupted)
    {
        context->buffer[context->buflen++] =
                                                (*message_array & 0xFF);

        context->total[ 0 ] += 8;
        /* Force it to 32 bits */
        context->total[ 0 ] &= 0xFFFFFFFF;
        if (context->total[ 0 ] == 0)
        {
            context->total[ 1 ]++;
            /* Force it to 32 bits */
            context->total[ 1 ] &= 0xFFFFFFFF;
            if (context->total[ 1 ] == 0)
            {
                /* Message is too long */
                context->Corrupted = 1;
            }
        }

        if (context->buflen == 64)
        {
            SHA1ProcessMessageBlock(context);
        }

        message_array++;
    }
}



/*  
 *  SHA1ProcessMessageBlock
 *
 *  Description:
 *      This function will process the next 512 bits of the message
 *      stored in the Message_Block array.
 *
 *  Parameters:
 *      None.
 *
 *  Returns:
 *      Nothing.
 *
 *  Comments:
 *      Many of the variable names in the SHAContext, especially the
 *      single character names, were used because those were the names
 *      used in the publication.
 *         
 *
 */
static void
SHA1ProcessMessageBlock(
    struct DigestState *context
			)
{
    const unsigned K[] =            /* Constants defined in SHA-1   */      
    {
        0x5A827999,
        0x6ED9EBA1,
        0x8F1BBCDC,
        0xCA62C1D6
    };
    int         t;                  /* Loop counter                 */
    unsigned    temp;               /* Temporary word value         */
    unsigned    W[80];              /* Word sequence                */
    unsigned    A, B, C, D, E;      /* Word buffers                 */

    /*
     *  Initialize the first 16 words in the array W
     */
    for(t = 0; t < 16; t++)
    {
        W[t]  = ((unsigned) context->buffer[t * 4]) << 24;
        W[t] |= ((unsigned) context->buffer[t * 4 + 1]) << 16;
        W[t] |= ((unsigned) context->buffer[t * 4 + 2]) << 8;
        W[t] |= ((unsigned) context->buffer[t * 4 + 3]);
    }

    for(t = 16; t < 80; t++)
    {
       W[t] = SHA1CircularShift(1,W[t-3] ^ W[t-8] ^ W[t-14] ^ W[t-16]);
    }

    A = context->A;
    B = context->B;
    C = context->C;
    D = context->D;
    E = context->E;

    for(t = 0; t < 20; t++)
    {
        temp =  SHA1CircularShift(5,A) +
                ((B & C) | ((~B) & D)) + E + W[t] + K[0];
        temp &= 0xFFFFFFFF;
        E = D;
        D = C;
        C = SHA1CircularShift(30,B);
        B = A;
        A = temp;
    }

    for(t = 20; t < 40; t++)
    {
        temp = SHA1CircularShift(5,A) + (B ^ C ^ D) + E + W[t] + K[1];
        temp &= 0xFFFFFFFF;
        E = D;
        D = C;
        C = SHA1CircularShift(30,B);
        B = A;
        A = temp;
    }

    for(t = 40; t < 60; t++)
    {
        temp = SHA1CircularShift(5,A) +
               ((B & C) | (B & D) | (C & D)) + E + W[t] + K[2];
        temp &= 0xFFFFFFFF;
        E = D;
        D = C;
        C = SHA1CircularShift(30,B);
        B = A;
        A = temp;
    }

    for(t = 60; t < 80; t++)
    {
        temp = SHA1CircularShift(5,A) + (B ^ C ^ D) + E + W[t] + K[3];
        temp &= 0xFFFFFFFF;
        E = D;
        D = C;
        C = SHA1CircularShift(30,B);
        B = A;
        A = temp;
    }

    context->A = ( context->A + A ) & 0xFFFFFFFF;
    context->B = ( context->B + B ) & 0xFFFFFFFF;
    context->C = ( context->C + C ) & 0xFFFFFFFF;
    context->D = ( context->D + D ) & 0xFFFFFFFF;
    context->E = ( context->E + E ) & 0xFFFFFFFF;

    context->buflen = 0;
}



/*  
 *  SHA1PadMessage
 *
 *  Description:
 *      According to the standard, the message must be padded to an even
 *      512 bits.  The first padding bit must be a '1'.  The last 64
 *      bits represent the length of the original message.  All bits in
 *      between should be 0.  This function will pad the message
 *      according to those rules by filling the Message_Block array
 *      accordingly.  It will also call SHA1ProcessMessageBlock()
 *      appropriately.  When it returns, it can be assumed that the
 *      message digest has been computed.
 *
 *  Parameters:
 *      context: [in/out]
 *          The context to pad
 *
 *  Returns:
 *      Nothing.
 *
 *  Comments:
 *
 */
static void
SHA1PadMessage(
    struct DigestState *context
	       )
{
    /*
     *  Check to see if the current message block is too small to hold
     *  the initial padding bits and length.  If so, we will pad the
     *  block, process it, and then continue padding into a second
     *  block.
     */
    if (context->buflen > 55)
    {
        context->buffer[context->buflen++] = 0x80;
        while(context->buflen < 64)
        {
            context->buffer[context->buflen++] = 0;
        }

        SHA1ProcessMessageBlock(context);

        while(context->buflen < 56)
        {
            context->buffer[context->buflen++] = 0;
        }
    }
    else
    {
        context->buffer[context->buflen++] = 0x80;
        while(context->buflen < 56)
        {
            context->buffer[context->buflen++] = 0;
        }
    }

    /*
     *  Store the message length as the last 8 octets
     */
    context->buffer[ 56 ] = ( context->total[ 1 ] >> 24 ) & 0xFF;
    context->buffer[ 57 ] = ( context->total[ 1 ] >> 16 ) & 0xFF;
    context->buffer[ 58 ] = ( context->total[ 1 ] >>  8 ) & 0xFF;
    context->buffer[ 59 ] = ( context->total[ 1 ] )       & 0xFF;
    context->buffer[ 60 ] = ( context->total[ 0 ] >> 24 ) & 0xFF;
    context->buffer[ 61 ] = ( context->total[ 0 ] >> 16 ) & 0xFF;
    context->buffer[ 62 ] = ( context->total[ 0 ] >>  8 ) & 0xFF;
    context->buffer[ 63 ] = ( context->total[ 0 ] )       & 0xFF;

    SHA1ProcessMessageBlock( context );
}



/**
 * Bytewise XOR a section of memory with a constant and store the result in
 * another place.
 * @param dest [out] Destination for the XOR operation.
 * @param src [in] Memory that is to be XOR'ed.
 * @param ch [in] Constant for the XOR operation.
 * @param length [in] Number of bytes to XOR.
 * @return Nothing.
 */
static void
XorMemory(
    unsigned char       *dest,
    const unsigned char *src,
    unsigned char       ch,
    int                 length
	  )
{
    int i;

    for( i = 0; i < length; i++ )
    {
        dest[ i ] = src[ i ] ^ ch;
    }
}



/**
 * Convert an uint32_t to a little-endian binary string.
 * @param [in] statePart The uint32_t value to convert.
 * @param [out] out Four bytes of storage for the string.
 * @return Nothing.
 */
static void
Sha1StatePartToBinDigest(
    uint32_t      statePart,
    unsigned char *out
			 )
{
    out[ 0 ] = ( statePart >> 24 ) & 0x00ff;
    out[ 1 ] = ( statePart >> 16 ) & 0x00ff;
    out[ 2 ] = ( statePart >>  8 ) & 0x00ff;
    out[ 3 ] = statePart & 0x00ff;
}


/**
 * Create a binary representation of the five 32-bit values that hold the
 * SHA1 hash in the digest state variables.
 * @param [in] state Digest state with state variables.
 * @param [out] out Twenty bytes of storage for the binary hash.
 * @return Nothing.
 */
static void
Sha1StateToBinDigest(
    struct DigestState *state,
    unsigned char      *out
		     )
{
    Sha1StatePartToBinDigest( state->A, &out[  0 ] );
    Sha1StatePartToBinDigest( state->B, &out[  4 ] );
    Sha1StatePartToBinDigest( state->C, &out[  8 ] );
    Sha1StatePartToBinDigest( state->D, &out[ 12 ] );
    Sha1StatePartToBinDigest( state->E, &out[ 16 ] );
}



/**
 * HMAC sign a message with an MD5 or SHA1 hash.
 *
 * Let:
 * H(·) be a cryptographic hash function,
 * K    be a secret key padded to the right with extra zeros to the input
 *      block size of the hash function, or the hash of the original key if
 *      it's longer than that block size,
 * m    be the message to be authenticated,
 * ∥   denote concatenation,
 * ⊕    denote exclusive or (XOR),
 *      opad be the outer padding (0x5c5c5c…5c5c, one-block-long hexadecimal
 *      constant), and
 *      ipad be the inner padding (0x363636…3636, one-block-long hexadecimal
 *      constant).
 *
 * Then HMAC(K,m) is mathematically defined by:
 *
 *      HMAC(K,m) = H((K ⊕ opad) ∥ H((K ⊕ ipad) ∥ m)).
 *
 * In pseudo-code:
 *
 * Shorten key if it is longer than blocksize:
 *   if key.length > blocksize then
 *       key = hash(key)
 *   end if
 * Zero-pad key if it is shorter than blocksize:
 *   if key.length < blocksize then
 *       key = key ∥ [0x00 * (blocksize - key.length)]
 *   end if
 *
 * Outer XOR: 
 *   o_key_pad = [0x5c * blocksize] ⊕ key
 * Inner XOR:
 *   i_key_pad = [0x36 * blocksize] ⊕ key
 *
 * Return hash(o_key_pad ∥ hash(i_key_pad ∥ message))
 *
 * @param message [in] Message to sign.
 * @param length [in] Length of the message.
 * @param key [in] Key to sign the message with.
 * @param function [in] Type of hash function to sign with.
 * @return Allocated buffer with signature as a string.
 */

const char*
HMAC(
    const unsigned char *message,
    int                 length,
    const char          *key,
    enum HashFunctions  function
     )
{
    char                workKey[ 64 ];
    int                 padding;
    int                 i;
    unsigned char       outerXor[ 64 ];
    unsigned char       innerXor[ 64 ];
    struct DigestState  state;
    unsigned char       md5hashPass1[ 16 ];
    unsigned char       md5hashPass2[ 16 ];
    unsigned char       sha1hashPass1[ 20 ];
    unsigned char       sha1hashPass2[ 20 ];
    const unsigned char *binSignature;
    const char          *signature = NULL;

    /* Shorten key to its hash if it is longer than blocksize. */
    if( strlen( key ) > 64 )
    {
        DigestBuffer( (const unsigned char*) key, strlen( key ),
		      workKey, function );
    }
    else
    {
        strncpy( workKey, key, 64 );
    }
    /* Zero-pad key if it is shorter than blocksize. */
    padding = 64 - strlen( workKey );
    if( padding > 0 )
    {
        for( i = 64 - padding; i < 64; i++ )
	{
	    workKey[ i ] = 0;
	}
    }

    /* Create outer and inner key XOR. */
    XorMemory( outerXor, ( unsigned char*) workKey, 0x5c, 64 );
    XorMemory( innerXor, ( unsigned char*) workKey, 0x36, 64 );
    /* Overwrite work copy of the key for security. */
    memset( workKey, 0x00, 64 );

    /* Pass 1: hash ( inner XOR concatenated with message ). */
    InitializeDigestState( &state );
    if( function == HASH_MD5 )
    {
        MD5ProcessContinuous( innerXor, 64, &state );
	MD5ProcessContinuous( message, length, &state );
	MD5FlushState( &state, md5hashPass1 );
    }
    if( function == HASH_SHA1 )
    {
        SHA1Input( &state, innerXor, 64 );
	SHA1Input( &state, message, length );
	SHA1Result( &state );
	Sha1StateToBinDigest( &state, sha1hashPass1 );
    }

    /* Pass 2: hash ( outer XOR concatenated with first hash ). */
    InitializeDigestState( &state );
    if( function == HASH_MD5 )
    {
        MD5ProcessContinuous( outerXor, 64, &state );
	MD5ProcessContinuous( md5hashPass1, 16, &state );
	MD5FlushState( &state, md5hashPass2 );
    }
    if( function == HASH_SHA1 )
    {
        SHA1Input( &state, outerXor, 64 );
	SHA1Input( &state, sha1hashPass1, 20 );
	SHA1Result( &state );
	Sha1StateToBinDigest( &state, sha1hashPass2 );
    }

    /* Return an ASCII output. */
    if( function == HASH_MD5 )
    {
        signature = malloc( 33 );
	binSignature = &md5hashPass2[ 0 ];
    }
    if( function == HASH_SHA1 )
    {
        signature = malloc( 41 );
	binSignature = &sha1hashPass2[ 0 ];
    }
    BinDigestToHexDigest( binSignature, ( char* )signature, function );
    return( signature );
}
