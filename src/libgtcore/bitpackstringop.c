/*
** Copyright (C) 2007 Thomas Jahns <Thomas.Jahns@gmx.net>
**
** See LICENSE file or http://genometools.org/license.html for license details.
**
*/
#include <assert.h>
#include <inttypes.h>
#include <limits.h>
#include <string.h>

#include "bitpackstring.h"

/**
 * \file bitpackstringop.c
 * Involved (i.e. not inlined) operations on bitstrings.
 */

#define MIN(a, b) (((a)<(b))?(a):(b))
#define MIN3(a, b, c) (((a)<(b))?((a)<(c)?(a):(c)):((b)<(c)?(b):(c)))

int
requiredUInt32Bits(uint32_t v)
{
  int r;
  static const int MultiplyDeBruijnBitPosition[32] = {
    1, 2, 29, 3, 30, 15, 25, 4, 31, 23, 21, 16, 26, 18, 5, 9,
    32, 28, 14, 24, 22, 20, 17, 8, 27, 13, 19, 7, 12, 6, 11, 10
  };
  v |= v >> 1; /* first round down to power of 2 */
  v |= v >> 2;
  v |= v >> 4;
  v |= v >> 8;
  v |= v >> 16;
  v = (v >> 1) + 1;
  r = MultiplyDeBruijnBitPosition[(v * (uint32_t)0x077CB531UL) >> 27];
  return r;
}

/* FIXME: this needs some serious rework for 64-bit */
int
requiredUInt64Bits(uint64_t v)
{
  int r;
  static const int MultiplyDeBruijnBitPosition[64] = {
    1, 2, 3, 57, 4, 33, 58, 47, 30, 5, 21, 34, 8, 59, 12, 48,
    63, 31, 19, 6, 17, 22, 35, 24, 54, 9, 60, 37, 26, 13, 49, 40,
    64, 56, 32, 46, 29, 20, 7, 11, 62, 18, 16, 23, 53, 36, 25, 39,
    55, 45, 28, 10, 61, 15, 52, 38, 44, 27, 14, 51, 43, 50, 42, 41
  };
  v |= v >> 1; /* first round down to power of 2 */
  v |= v >> 2;
  v |= v >> 4;
  v |= v >> 8;
  v |= v >> 16;
  v |= v >> 32;
  v = (v >> 1) + 1;
  r = MultiplyDeBruijnBitPosition[(v * (uint64_t)0x26752B916FC7B0DULL) >> 58];
  return r;
}

#if 0
/**
 * This function was used to compute the 64-bit DeBruijn table shown
 * above. The multiplicator can be gathered from diverse web sources.
 */
static void
computeDeBruijn()
{
  unsigned prod, i;
  unsigned long long v;
  int MultiplyDeBruijnBitPosition[64];
  for (v = 1, i = 1; i <= 64; ++i, v <<= 1)
  {
    prod = ((unsigned long long)v * 0x26752B916FC7B0DULL) >> 58;
    printf("v = %llu, i = %u, prod = %u\n", v, i, prod);
    assert(prod < 64);
    MultiplyDeBruijnBitPosition[prod] = i;
  }
  fputs("int MultiplyDeBruijnBitPosition[64] = { ", stdout);
  for (i = 0; i < 63; ++i)
    printf("%d, ", MultiplyDeBruijnBitPosition[i]);
  printf("%d };\n", MultiplyDeBruijnBitPosition[i]);
}
#endif

/**
 * \brief Compares substrings of bitstrings, starting at respective offsets,
 * both of lenght numBits.
 *
 * The bitstrings are treated as MSB-first encodings (which they are
 * if produced by above functions) and thus after the first
 * most-significant bit set in only one of the strings the comparision
 * can terminate.
 *
 * This function treats two sub-bitstrings as
 * @param a first bitstring to compare
 * @param offsetA corresponding start position
 * @param numBitsA length of substring \f$a'\f$ in a to use for comparison.
 * @param b second bitstring
 * @param offsetB corresponding start position
 * @param numBitsB length of substring \f$b'\f$ in b to use for comparison.
 * @return 0 for equality, \f$-1\f$ if \f$a < b\f$, \f$1\f$ if \f$b > a\f$
 */
extern int
bsCompare(const BitString a, BitOffset offsetA, BitOffset numBitsA,
          const BitString b, BitOffset offsetB, BitOffset numBitsB)
{
  BitOffset totalBitsLeftA = numBitsA, totalBitsLeftB = numBitsB;
  size_t elemStartA = offsetA/bitElemBits, elemStartB = offsetB/bitElemBits;
  unsigned bitTopA = offsetA%bitElemBits, bitTopB = offsetB%bitElemBits;
  const BitElem *pA = a + elemStartA, *pB = b + elemStartB;
  uint_fast32_t accumA = 0, accumB = 0;
  unsigned bitsInAccumA, bitsInAccumB;
  assert(a && b);
  /* user requested zero length comparison, treat as equality */
  if (!numBitsA && !numBitsB)
  {
    return 0;
  }
  if (numBitsA > numBitsB)
    return -1 * bsCompare(b, offsetB, numBitsB, a, offsetA, numBitsA);
  if (numBitsB > numBitsA)
  {
    /* B is longer and thus compared with virtual zeros in A */
    unsigned comparePreBits = numBitsB - numBitsA;
    do {
      bitsInAccumB = 0;
      if (bitTopB)
      {
        uint_fast32_t mask; /*< all of the bits we want to get from *pB */
        unsigned bits2Read = MIN(bitElemBits - bitTopB, comparePreBits);
        unsigned unreadRightBits = (bitElemBits - bitTopB - bits2Read);
        mask = (~((~(uint_fast32_t)0) << bits2Read)) << unreadRightBits;
        accumB = ((*pB++) & mask) >> unreadRightBits;
        bitsInAccumB += bits2Read;
        totalBitsLeftB -= bits2Read;
        comparePreBits -= bits2Read;
      }
      while (bitsInAccumB < CHAR_BIT * sizeof (accumB) && comparePreBits)
      {
        unsigned bits2Read,
          bitsFree = (CHAR_BIT * sizeof (accumA)) - bitsInAccumB;
        uint_fast32_t mask;
        bits2Read = MIN3(bitsFree, bitElemBits, comparePreBits);
        mask = ~((~(uint_fast32_t)0) << bits2Read);
        accumB = accumB << bits2Read
          | (((*pB) >> (bitElemBits - bits2Read)) & mask);
        bitsInAccumB += bits2Read;
        totalBitsLeftB -= bits2Read;
        comparePreBits -= bits2Read;
        /* all of *pB consumed? */
        if (bits2Read == bitElemBits)
          ++pB, bitTopB = 0;
        else
          bitTopB = bits2Read;
      }
    } while (accumB == 0 && comparePreBits);
    if (accumB > 0)
      return -1;
  }
  do {
    bitsInAccumB = bitsInAccumA = 0;
    /* get bits of first element if not aligned */
    if (bitTopA)
    {
      uint_fast32_t mask; /*< all of the bits we want to get from *pA */
      unsigned bits2Read = MIN(bitElemBits - bitTopA, totalBitsLeftA);
      unsigned unreadRightBits = (bitElemBits - bitTopA - bits2Read);
      mask = (~((~(uint_fast32_t)0) << bits2Read)) << unreadRightBits;
      accumA = ((*pA++) & mask) >> unreadRightBits;
      bitsInAccumA += bits2Read;
      totalBitsLeftA -= bits2Read;
    }
    /* get bits of first element if not aligned */
    if (bitTopB)
    {
      uint_fast32_t mask; /*< all of the bits we want to get from *pB */
      unsigned bits2Read = MIN(bitElemBits - bitTopB, totalBitsLeftB);
      unsigned unreadRightBits = (bitElemBits - bitTopB - bits2Read);
      mask = (~((~(uint_fast32_t)0) << bits2Read)) << unreadRightBits;
      accumB = ((*pB++) & mask) >> unreadRightBits;
      bitsInAccumB += bits2Read;
      totalBitsLeftB -= bits2Read;
    }
    while (bitsInAccumA < (CHAR_BIT * sizeof (accumA)) && totalBitsLeftA)
    {
      unsigned bits2Read,
               bitsFree = (CHAR_BIT * sizeof (accumA)) - bitsInAccumA;
      uint_fast32_t mask;
      bits2Read = MIN3(bitsFree, bitElemBits, totalBitsLeftA);
      mask = (~((~(uint_fast32_t)0) << bits2Read));
      accumA = accumA << bits2Read
        | (((*pA) >> (bitElemBits - bits2Read)) & mask);
      bitsInAccumA += bits2Read;
      totalBitsLeftA -= bits2Read;
      /* all of *pA consumed? */
      if (bits2Read == bitElemBits)
        ++pA, bitTopA = 0;
      else
        bitTopA = bits2Read;
    }
    while (bitsInAccumB < (CHAR_BIT * sizeof (accumA)) && totalBitsLeftB)
    {
      unsigned bits2Read,
        bitsFree = (CHAR_BIT * sizeof (accumA)) - bitsInAccumB;
      uint_fast32_t mask;
      bits2Read = MIN3(bitsFree, bitElemBits, totalBitsLeftB);
      mask = (~((~(uint_fast32_t)0) << bits2Read));
      accumB = accumB << bits2Read
        | (((*pB) >> (bitElemBits - bits2Read)) & mask);
      bitsInAccumB += bits2Read;
      totalBitsLeftB -= bits2Read;
      /* all of *pB consumed? */
      if (bits2Read == bitElemBits)
        ++pB, bitTopB = 0;
      else
        bitTopB = bits2Read;
    }
  } while (accumA == accumB && totalBitsLeftA);
  return accumA > accumB?1:(accumA < accumB?-1:0);
}

void
bsCopy(const BitString src, BitOffset offsetSrc,
       const BitString dest, BitOffset offsetDest, BitOffset numBits)
{
  size_t elemStartSrc = offsetSrc/bitElemBits,
    elemStartDest = offsetDest/bitElemBits;
  unsigned bitTopSrc = offsetSrc%bitElemBits,
    bitTopDest = offsetDest%bitElemBits;
  BitOffset bitsLeft = numBits;
  BitElem *p = src + elemStartSrc, *q = dest + elemStartDest;
  assert(src && dest);
  /* special optimization if equally aligned data will be copied */
  if (bitTopSrc == bitTopDest)
  {
    if (bitTopSrc)
    {
      BitElem mask = (~(BitElem)0) >> bitTopSrc;
      if (numBits < bitElemBits - bitTopSrc)
      {
        unsigned backShift = bitElemBits - numBits - bitTopSrc;
        mask &= ~(BitElem)0 << backShift;
        *q = (*q & ~mask) | (*p & mask);
        /* TODO: try wether  r = a ^ ((a ^ b) & mask) is faster, see above */
        return;
      }
      else
      {
        *q = (*q & ~mask) | (*p & mask);
        ++p, ++q;
        bitsLeft -= bitElemBits - bitTopSrc;
      }
    }
    if (bitsLeft)
    {
      size_t completeElems = bitsLeft/bitElemBits;
      memcpy(q, p, completeElems);
      p += completeElems, q += completeElems;
      bitsLeft %= bitElemBits;
    }
    if (bitsLeft)
    {
      BitElem mask = (~(BitElem)0) << (bitElemBits - bitsLeft);
      *q = (*q & ~mask) | (*p & mask);
    }
  }
  else
  {
    uint_fast32_t accum = 0;
    unsigned bitsInAccum = 0;
    while (bitsLeft && (bitTopSrc || bitTopDest))
    {
      if (bitTopSrc)
      {
        uint_fast32_t mask = (~(BitElem)0) >> bitTopSrc;
        unsigned bits2Read = MIN3(bitElemBits - bitTopSrc, bitsLeft,
                                  sizeof (accum) * CHAR_BIT - bitsInAccum);
        unsigned unreadRightBits = (bitElemBits - bitTopSrc - bits2Read);
        mask = (~((~(uint_fast32_t)0) << bits2Read));
        accum = (accum << bits2Read) | (((*p) >> unreadRightBits) & mask);
        bitsLeft -= bits2Read;
        bitsInAccum += bits2Read;
        if ((bitTopSrc += bits2Read) == bitElemBits)
          bitTopSrc = 0, ++p;
      }
      if (bitTopDest)
      {
        while (bitsLeft >= bitElemBits
               && sizeof (accum) - bitsInAccum > bitElemBits)
        {
          accum = accum << bitElemBits | (*p++);
          bitsLeft -= bitElemBits;
          bitsInAccum += bitElemBits;
        }
        if (bitElemBits - bitTopDest > bitsInAccum)
        {
          /* very inconvinient: we have to take all the bits we can get
           * just to fill the first incomplete element */
          {
            uint_fast32_t mask;
            unsigned bits2Read = MIN(sizeof (accum)*CHAR_BIT - bitsInAccum,
                                     bitsLeft);
            unsigned unreadRightBits = (bitElemBits - bits2Read);
            mask = (~((~(uint_fast32_t)0) << bits2Read)) << unreadRightBits;
            accum = (accum << bits2Read) | ((*p) & mask) >> unreadRightBits;
            bitsLeft -= bits2Read;
            bitsInAccum += bits2Read;
            bitTopSrc = bits2Read;
          }
          {
            unsigned bits2Write = bitElemBits - bitTopDest;
            uint_fast32_t mask =
              ~((~(uint_fast32_t)0) << bits2Write);
            *q = (*q & ~mask) | ((accum >> (bitsInAccum - bits2Write)) & mask);
            bitsInAccum -= bits2Write;
          }
        }
        else
        {
          /* accum holds enough bits to fill incomplete region at dest start */
          unsigned bits2Write = bitElemBits - bitTopDest;
          uint_fast32_t mask =
            ~((~(uint_fast32_t)0) << bits2Write);
          *q = (*q & ~mask) | ((accum >> (bitsInAccum - bits2Write)) & mask);
          ++q;
          bitsInAccum -= bits2Write;
          bitTopDest = 0;
        }
      }
    }
    /* all reads and writes are aligned on BitElems in this loop */
    do
    {
      /* fill accumulator */
      while (bitsLeft >= bitElemBits
             && sizeof (accum) - bitsInAccum > bitElemBits)
      {
        accum = accum << bitElemBits | (*p++);
        bitsInAccum += bitElemBits;
        bitsLeft -= bitElemBits;
      }
      /* write out accum */
      while (bitsInAccum >= bitElemBits)
      {
        *q++ = accum >> (bitsInAccum -= bitElemBits);
      }
    }
    while (bitsLeft >= bitElemBits);
    /* write remaining (trailing) bits */
    while (bitsLeft)
    {
      {
        unsigned bits2Read = MIN(bitsLeft,
                                 sizeof (accum) * CHAR_BIT - bitsInAccum);
        unsigned unreadRightBits = (bitElemBits - bits2Read);
        uint_fast32_t mask =
          (~((~(uint_fast32_t)0) << bits2Read)) << unreadRightBits;
        accum = (accum << bits2Read) | ((*p) & mask) >> unreadRightBits;
        bitsLeft -= bits2Read;
        bitsInAccum += bits2Read;
        bitTopSrc = bits2Read;
      }
      while (bitsInAccum)
      {
        unsigned bits2Write = MIN(bitsInAccum, bitElemBits - bitTopDest),
          unreadRightBits = bitElemBits - bits2Write - bitTopDest;
        uint_fast32_t mask = (~(uint_fast32_t)0);
        if (bits2Write != bitElemBits)
        {
          mask = ~(mask << bits2Write);
        }
        *q = (*q & ~mask) |
          (((accum >> (bitsInAccum - bits2Write)) & mask) << unreadRightBits);
        if ((bitTopDest += bits2Write) == bitElemBits)
          bitTopDest = 0, ++q;
      }
    }
  }
}
