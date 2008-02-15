/*
  Copyright (c) 2007 Thomas Jahns <Thomas.Jahns@gmx.net>

  Permission to use, copy, modify, and distribute this software for any
  purpose with or without fee is hereby granted, provided that the above
  copyright notice and this permission notice appear in all copies.

  THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
  WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
  MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
  ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
  WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
  ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
  OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/

#include <assert.h>
#include <string.h>

#include "libgtcore/dataalign.h"
#include "libgtcore/error.h"
#include "libgtcore/str.h"
#include "libgtcore/unused.h"
#include "libgtmatch/sarr-def.h"
#include "libgtmatch/esa-map.pr"
#include "libgtmatch/seqpos-def.h"

#include "libgtmatch/eis-bitpackseqpos.h"
#include "libgtmatch/eis-bwtseq.h"
#include "libgtmatch/eis-bwtseqconstruct.h"
#include "libgtmatch/eis-bwtconstruct_params.h"
#include "libgtmatch/eis-bwtseqpriv.h"
#include "libgtmatch/eis-bwtseqcreate.h"
#include "libgtmatch/eis-encidxseq.h"
#include "libgtmatch/eis-encidxseqconstruct.h"
#include "libgtmatch/eis-mrangealphabet.h"
#include "libgtmatch/eis-suffixerator-interface.h"
#include "libgtmatch/eis-suffixarray-interface.h"

static int
initBWTSeqFromEncSeqIdx(struct BWTSeq *bwtSeq, struct encIdxSeq *baseSeqIdx,
                        MRAEnc *alphabet, Seqpos *counts);

static BWTSeq *
newBWTSeq(struct encIdxSeq *seqIdx, MRAEnc *alphabet);

extern BWTSeq *
availBWTSeq(const struct bwtParam *params, Error *err)
{
  struct BWTSeq *bwtSeq = NULL;
  Suffixarray suffixArray;
  Seqpos len;
  Verboseinfo *verbosity;
  assert(params && err);
  error_check(err);
  /* FIXME: handle verbosity in a more sane fashion */
  verbosity = newverboseinfo(false);
  if (streamsuffixarray(&suffixArray, &len, SARR_SUFTAB | SARR_BWTTAB,
                        params->projectName, verbosity, err))
  {
    freeverboseinfo(&verbosity);
    return NULL;
  }
  ++len;
  bwtSeq = availBWTSeqFromSA(params, &suffixArray, len, err);
  freesuffixarray(&suffixArray);
  freeverboseinfo(&verbosity);
  return bwtSeq;
}

extern BWTSeq *
availBWTSeqFromSA(const struct bwtParam *params, Suffixarray *sa,
                  Seqpos totalLen, Error *err)
{
  BWTSeq *bwtSeq;
  assert(sa && params && err);
  error_check(err);
  /* try loading index */
  bwtSeq = loadBWTSeqForSA(params->projectName, params->baseType,
                           params->seqParams.blockEnc.EISFeatureSet,
                           sa, totalLen, err);
  /* if loading didn't work try on-demand creation */
  if (!bwtSeq)
  {
    error_unset(err);
    bwtSeq = createBWTSeqFromSA(params, sa, totalLen, err);
  }
  return bwtSeq;
}

static int GTAlphabetRangeHandling[] = { NORMAL_RANGE, SPECIAL_RANGE };

extern BWTSeq *
loadBWTSeq(const Str *projectName, int BWTOptFlags, Error *err)
{
  struct BWTSeq *bwtSeq = NULL;
  Suffixarray suffixArray;
  Seqpos len;
  Verboseinfo *verbosity;
  assert(projectName && err);
  error_check(err);
  /* FIXME: handle verbosity in a more sane fashion */
  verbosity = newverboseinfo(false);
  if (mapsuffixarray(&suffixArray, &len, 0, projectName, verbosity, err))
  {
    freeverboseinfo(&verbosity);
    return NULL;
  }
  ++len;
  bwtSeq = loadBWTSeqForSA(projectName, BWT_ON_BLOCK_ENC, BWTOptFlags,
                           &suffixArray, len, err);
  freesuffixarray(&suffixArray);
  freeverboseinfo(&verbosity);
  return bwtSeq;
}

extern BWTSeq *
loadBWTSeqForSA(const Str *projectName, enum seqBaseEncoding baseType,
                int BWTOptFlags, const Suffixarray *sa,
                Seqpos totalLen, Error *err)
{
  struct BWTSeq *bwtSeq = NULL;
  EISeq *seqIdx = NULL;
  MRAEnc *alphabet = NULL;
  assert(projectName && sa && err);
  alphabet = newMRAEncFromSA(sa);
  switch (baseType)
  {
  case BWT_ON_BLOCK_ENC:
    if ((seqIdx = loadBlockEncIdxSeqForSA(
           sa, totalLen, projectName,
           convertBWTOptFlags2EISFeatures(BWTOptFlags), err)))
    {
      if (!(bwtSeq = newBWTSeq(seqIdx, alphabet)))
        break;
      fputs("Using pre-computed sequence index.\n", stderr);
    }
    break;
  default:
    error_set(err, "Illegal/unknown/unimplemented encoding requested!");
    break;
  }
  if (!bwtSeq && seqIdx)
    deleteEncIdxSeq(seqIdx);
  return bwtSeq;
}

extern BWTSeq *
createBWTSeqFromSA(const struct bwtParam *params, Suffixarray *sa,
                   Seqpos totalLen, Error *err)
{
  BWTSeq *bwtSeq = NULL;
  MRAEnc *alphabet = NULL;
  if (params->locateInterval && !sa->suftabstream.fp)
  {
    fprintf(stderr, "error: locate sampling requested but not available"
            " for project %s\n", str_get(params->projectName));
  }
  else if (!sa->longest.defined)
  {
    fprintf(stderr,
            "error: position of null-rotation/longest suffix not available"
            " for project %s\n", str_get(params->projectName));
  }
  else
  {
    EISeq *seqIdx = NULL;
    alphabet = newMRAEncFromSA(sa);
    switch (params->baseType)
    {
    case BWT_ON_BLOCK_ENC:
      seqIdx =
        createBWTSeqGeneric(
          params, (indexCreateFunc)newBlockEncIdxSeqFromSA, sa, totalLen,
          alphabet, GTAlphabetRangeHandling, saGetOrigSeqSym, sa, saReadSeqpos,
          sa, reportSALongest, sa, err);
      break;
    default:
      error_set(err, "Illegal/unknown/unimplemented encoding requested!");
      break;
    }
    if (seqIdx)
      bwtSeq = newBWTSeq(seqIdx, alphabet);
    if (!bwtSeq)
    {
      if (seqIdx)
        deleteEncIdxSeq(seqIdx);
      if (alphabet)
        MRAEncDelete(alphabet);
    }
  }
  return bwtSeq;
}

struct sfxIReadInfo
{
  sfxInterface *si;
  listenerID id;
};

static int
sfxIReadSeqpos(void *src, Seqpos *dest, size_t len, Error *err)
{
  return readSfxISufTabRange(((struct sfxIReadInfo *)src)->si,
                             ((struct sfxIReadInfo *)src)->id,
                             len, dest, err) == len;
}

#if 0
static int
sfxIReadBWTSym(void *src, Symbol *dest, size_t len, Error *err)
{
  return readSfxIBWTRange(((struct sfxIReadInfo *)src)->si,
                          ((struct sfxIReadInfo *)src)->id,
                          len, dest, err) == len;
}
#endif

extern BWTSeq *
createBWTSeqFromSfxI(const struct bwtParam *params, sfxInterface *si,
                     Seqpos totalLen, Error *err)
{
  struct sfxIReadInfo siriSeqpos;
  EISeq *seqIdx = NULL;
  BWTSeq *bwtSeq = NULL;
  MRAEnc *alphabet = NULL;
  assert(si && params && err);

  if (params->locateInterval)
  {
    siriSeqpos.si = si;
    if (!SfxIRegisterReader(si, &siriSeqpos.id, SFX_REQUEST_SUFTAB))
      return NULL;
  }
  alphabet = newMRAEncFromSfxI(si);
  seqIdx= createBWTSeqGeneric(
    params, (indexCreateFunc)newBlockEncIdxSeqFromSfxI, si, totalLen,
    alphabet, GTAlphabetRangeHandling,
    SfxIGetOrigSeq, si, sfxIReadSeqpos, &siriSeqpos,
    (reportLongest)getSfxILongestPos, si, err);
  if (seqIdx)
  {
    bwtSeq = newBWTSeq(seqIdx, alphabet);
  }
  if (!bwtSeq && seqIdx)
    deleteEncIdxSeq(seqIdx);
  return bwtSeq;
}

/**
 * @param alphabet ownership of alphabet is with the newly produced
 * sequence object if return value is not 0
 */
static int
initBWTSeqFromEncSeqIdx(BWTSeq *bwtSeq, struct encIdxSeq *seqIdx,
                        MRAEnc *alphabet, Seqpos *counts)
{
  size_t alphabetSize;
  Symbol bwtTerminatorFlat;
  EISHint hint;
  assert(bwtSeq && seqIdx);
  bwtSeq->alphabet = alphabet;
  alphabetSize = MRAEncGetSize(alphabet);
  if (!alphabetSize)
    /* weird error, shouldn't happen, but I prefer error return to
     * segfault in case someone tampered with the input */
    return 0;
  /* FIXME: this should probably be handled in chardef.h to have a
   * unique mapping */
  MRAEncAddSymbolToRange(alphabet, SEPARATOR - 3, 1);
  assert(MRAEncGetSize(alphabet) ==  alphabetSize + 1);
  alphabetSize = MRAEncGetSize(alphabet);
  bwtSeq->bwtTerminatorFallback = bwtTerminatorFlat =
    MRAEncMapSymbol(alphabet, UNDEFBWTCHAR);

  bwtSeq->count = counts;
  bwtSeq->seqIdx = seqIdx;
  bwtSeq->alphabetSize = alphabetSize;
  {
    struct locateHeader header;
    if (!readLocateInfoHeader(seqIdx, &header)
        || !header.locateInterval)
    {
      fputs("Index does not contain locate information.\n"
            "Localization of matches will not be supported!\n", stderr);
      bwtSeq->locateSampleInterval = 0;
    }
    else
    {
      bwtSeq->locateSampleInterval = header.locateInterval;
      bwtSeq->longest = header.longest;
      /* FIXME: this really deserves its own header */
      bwtSeq->featureToggles = header.featureToggles;
    }
  }
  bwtSeq->hint = hint = newEISHint(seqIdx);
  {
    Symbol i;
    Seqpos len = EISLength(seqIdx), *count = bwtSeq->count;
    count[0] = 0;
    for (i = 0; i < bwtTerminatorFlat; ++i)
      count[i + 1] = count[i]
        + EISSymTransformedRank(seqIdx, i, len, hint);
    /* handle character which the terminator has been mapped to specially */
    count[i + 1] = count[i]
      + EISSymTransformedRank(seqIdx, i, len, hint) - 1;
    assert(count[i + 1] >= count[i]);
    /* now we can finish the rest of the symbols */
    for (i += 2; i < alphabetSize; ++i)
      count[i] = count[i - 1]
        + EISSymTransformedRank(seqIdx, i - 1, len, hint);
    /* and finally place the 1-count for the terminator */
    count[i] = count[i - 1] + 1;
#ifdef DEBUG
    fprintf(stderr, "count[alphabetSize]="FormatSeqpos
            ", len="FormatSeqpos"\n", count[alphabetSize], len);
    for (i = 0; i <= alphabetSize; ++i)
      fprintf(stderr, "count[%u]="FormatSeqpos"\n", (unsigned)i, count[i]);
#endif
    assert(count[alphabetSize] == len);
  }
  return 1;
}

/**
 * @param alphabet ownership of alphabet is with the newly produced
 * sequence object if return value is non-NULL
 */
static BWTSeq *
newBWTSeq(EISeq *seqIdx, MRAEnc *alphabet)
{
  BWTSeq *bwtSeq;
  Seqpos *counts;
  unsigned alphabetSize;
  assert(seqIdx);
  /* alphabetSize is increased by one to handle the flattened
   * terminator symbol correctly */
  alphabetSize = MRAEncGetSize(alphabet) + 1;
  bwtSeq = ma_malloc(offsetAlign(sizeof (struct BWTSeq), sizeof (Seqpos))
                     + sizeof (Seqpos) * (alphabetSize + 1));
  counts = (Seqpos *)((char  *)bwtSeq
                      + offsetAlign(sizeof (struct BWTSeq),
                                    sizeof (Seqpos)));
  if (!initBWTSeqFromEncSeqIdx(bwtSeq, seqIdx, alphabet, counts))
  {
    ma_free(bwtSeq);
    bwtSeq = NULL;
  }
  return bwtSeq;
}

void
deleteBWTSeq(BWTSeq *bwtSeq)
{
  MRAEncDelete(bwtSeq->alphabet);
  deleteEISHint(bwtSeq->seqIdx, bwtSeq->hint);
  deleteEncIdxSeq(bwtSeq->seqIdx);
  ma_free(bwtSeq);
}

static inline void
getMatchBound(const BWTSeq *bwtSeq, const Symbol *query, size_t queryLen,
              struct matchBound *match)
{
  size_t i = queryLen;
  const Seqpos *count;
  Symbol curSym;
  const MRAEnc *alphabet;
  assert(bwtSeq && query);
  count = bwtSeq->count;
  alphabet = BWTSeqGetAlphabet(bwtSeq);
  curSym = MRAEncMapSymbol(alphabet, query[--i]);
  /* XXX Thomas: fix the following: lower is always the first index */
  match->upper = count[curSym];
  match->lower = count[curSym + 1];
  while ((match->upper <= match->lower) && (i > 0))
  {
    curSym = MRAEncMapSymbol(alphabet, query[--i]);
    match->upper = count[curSym]
      + BWTSeqOcc(bwtSeq, curSym, match->upper);
    match->lower = count[curSym]
      + BWTSeqOcc(bwtSeq, curSym, match->lower);
  }
}

unsigned long packedindexuniqueforward(const void *genericindex,
                                       /*@unused@*/ unsigned long offset,
                                       /*@unused@*/ Seqpos left,
                                       /*@unused@*/ Seqpos right,
                                       /*@unused@*/ Seqpos *witnessposition,
                                       const Uchar *qstart,
                                       const Uchar *qend)
{
  Uchar cc;
  const Uchar *qptr;
  struct matchBound bwtbound;
  const BWTSeq *bwtSeq = (BWTSeq *) genericindex;
  Symbol curSym;
  const MRAEnc *alphabet;

  assert(bwtSeq && qstart);
  alphabet = BWTSeqGetAlphabet(bwtSeq);
  qptr = qstart;
  cc = *qptr++;
#undef mydebug
#ifdef mydebug
  printf("# start cc=%u\n",cc);
#endif
  if (ISSPECIAL(cc))
  {
    return 0;
  }
  curSym = MRAEncMapSymbol(alphabet, cc);
  bwtbound.lower = bwtSeq->count[curSym];
  bwtbound.upper = bwtSeq->count[curSym+1];
#ifdef mydebug
  printf("# bounds=" FormatSeqpos "," FormatSeqpos " = " FormatSeqos
          "occurrences\n",
         PRINTSeqposcast(bwtbound.lower),
         PRINTSeqposcast(bwtbound.upper),
         PRINTSeqposcast(bwtbound.upper - bwtbound.lower));
#endif
  while (qptr < qend && bwtbound.lower + 1 < bwtbound.upper)
  {
    cc = *qptr;
#ifdef mydebug
    printf("# cc=%u\n",cc);
#endif
    if (ISSPECIAL (cc))
    {
      return 0;
    }
    curSym = MRAEncMapSymbol(alphabet, cc);

    bwtbound.lower = bwtSeq->count[curSym] +
                     BWTSeqOcc(bwtSeq, curSym, bwtbound.lower);
    bwtbound.upper = bwtSeq->count[curSym] +
                     BWTSeqOcc(bwtSeq, curSym, bwtbound.upper);
#ifdef mydebug
    printf("# bounds=" FormatSeqpos "," FormatSeqpos " = " FormatSeqos
            "occurrences\n",
           PRINTSeqposcast(bwtbound.lower),
           PRINTSeqposcast(bwtbound.upper),
           PRINTSeqposcast(bwtbound.upper - bwtbound.lower));
#endif
    qptr++;
  }
  if (bwtbound.lower + 1 == bwtbound.upper)
  {
    return (unsigned long) (qptr - qstart);
  }
  return 0;
}

unsigned long packedindexmstatsforward(const void *genericindex,
                                       /*@unused@*/ unsigned long offset,
                                       /*@unused@*/ Seqpos left,
                                       /*@unused@*/ Seqpos right,
                                       Seqpos *witnessposition,
                                       const Uchar *qstart,
                                       const Uchar *qend)
{
  Uchar cc;
  const Uchar *qptr;
  Seqpos prevlbound;
  struct matchBound bwtbound;
  const BWTSeq *bwtSeq = (BWTSeq *) genericindex;
  Symbol curSym;
  unsigned long matchlength;
  const MRAEnc *alphabet;

  assert(bwtSeq && qstart && qstart < qend);
  alphabet = BWTSeqGetAlphabet(bwtSeq);
  qptr = qstart;
  cc = *qptr;
#undef mydebug
#ifdef mydebug
  printf("# start cc=%u\n",cc);
#endif
  if (ISSPECIAL(cc))
  {
    return 0;
  }
  curSym = MRAEncMapSymbol(alphabet, cc);
  bwtbound.lower = bwtSeq->count[curSym];
  bwtbound.upper = bwtSeq->count[curSym+1];
  if (bwtbound.lower >= bwtbound.upper)
  {
    return 0;
  }
#ifdef mydebug
  printf("# bounds=" FormatSeqpos "," FormatSeqpos " = " FormatSeqos
          "occurrences\n",
         PRINTSeqposcast(bwtbound.lower),
         PRINTSeqposcast(bwtbound.upper),
         PRINTSeqposcast(bwtbound.upper - bwtbound.lower));
#endif
  prevlbound = bwtbound.lower;
  for (qptr++; qptr < qend; qptr++)
  {
    cc = *qptr;
#ifdef mydebug
    printf("# cc=%u\n",cc);
#endif
    if (ISSPECIAL (cc))
    {
      break;
    }
    curSym = MRAEncMapSymbol(alphabet, cc);
    bwtbound.lower = bwtSeq->count[curSym] +
                     BWTSeqOcc(bwtSeq, curSym, bwtbound.lower);
    bwtbound.upper = bwtSeq->count[curSym] +
                     BWTSeqOcc(bwtSeq, curSym, bwtbound.upper);
#ifdef mydebug
    printf("# bounds=" FormatSeqpos "," FormatSeqpos " = " FormatSeqos
            "occurrences\n",
           PRINTSeqposcast(bwtbound.lower),
           PRINTSeqposcast(bwtbound.upper),
           PRINTSeqposcast(bwtbound.upper - bwtbound.lower));
#endif
    if (bwtbound.lower >= bwtbound.upper)
    {
      break;
    }
    prevlbound = bwtbound.lower;
  }
  matchlength = (unsigned long) (qptr - qstart);
  if (witnessposition != NULL)
  {
    Seqpos startpos = pckfindfirstmatch(bwtSeq,prevlbound);
    assert((bwtSeq->seqIdx->seqLen-1) >= (startpos + matchlength));
    *witnessposition = (bwtSeq->seqIdx->seqLen - 1) - (startpos + matchlength);
  }
  return matchlength;
}

extern Seqpos
BWTSeqMatchCount(const BWTSeq *bwtSeq, const Symbol *query, size_t queryLen)
{
  struct matchBound match;
  assert(bwtSeq && query);
  getMatchBound(bwtSeq, query, queryLen, &match);
  if (match.lower < match.upper)
    return 0;
  else
    return match.lower - match.upper;
}

struct BWTSeqExactMatchesIterator *
newEMIterator(const BWTSeq *bwtSeq, const Symbol *query, size_t queryLen)
{
  struct BWTSeqExactMatchesIterator *newIter;
  assert(bwtSeq && query);
  if (!bwtSeq->locateSampleInterval)
  {
    fputs("Index does not contain locate information.\n"
          "Localization of matches impossible!", stderr);
    return NULL;
  }
  newIter = ma_malloc(sizeof (*newIter));
  getMatchBound(bwtSeq, query, queryLen, &newIter->bounds);
  newIter->nextMatchBWTPos = newIter->bounds.upper;
  initExtBitsRetrieval(&newIter->extBits);
  return newIter;
}

void
deleteEMIterator(struct BWTSeqExactMatchesIterator *iter)
{
  destructExtBitsRetrieval(&iter->extBits);
  ma_free(iter);
}

Seqpos
EMINumMatchesTotal(const struct BWTSeqExactMatchesIterator *iter)
{
  assert(iter);
  if (iter->bounds.upper > iter->bounds.lower)
    return 0;
  else
    return iter->bounds.lower - iter->bounds.upper;
}

extern Seqpos
EMINumMatchesLeft(const struct BWTSeqExactMatchesIterator *iter)
{
  assert(iter);
  if (iter->nextMatchBWTPos > iter->bounds.lower)
    return 0;
  else
    return iter->bounds.lower - iter->bounds.upper;
}

extern int
BWTSeqVerifyIntegrity(BWTSeq *bwtSeq, const Str *projectName,
                      UNUSED unsigned long tickPrint, UNUSED FILE *fp,
                      Error *err)
{
  Suffixarray suffixArray;
  struct extBitsRetrieval extBits;
  bool suffixArrayIsInitialized = false, extBitsAreInitialized = false;
  Verboseinfo *verbosity = NULL;
  enum verifyBWTSeqErrCode retval = VERIFY_BWTSEQ_NO_ERROR;
  do
  {
    Seqpos len;
    assert(bwtSeq && projectName && err);
    error_check(err);

    verbosity = newverboseinfo(true);
    initExtBitsRetrieval(&extBits);
    if (mapsuffixarray(&suffixArray, &len,
                       SARR_SUFTAB | SARR_ESQTAB, projectName, verbosity, err))
    {
      error_set(err, "Cannot load reference suffix array project with"
                    " demand for suffix table file and encoded sequence"
                    " for project: %s", str_get(projectName));
      freeverboseinfo(&verbosity);
      retval = VERIFY_BWTSEQ_REFLOAD_ERROR;
      break;
    }
    suffixArrayIsInitialized = true;
    ++len;
    if (BWTSeqLength(bwtSeq) != len)
    {
      error_set(err, "length mismatch for suffix array project %s and "
                    "bwt sequence index", str_get(projectName));
      retval = VERIFY_BWTSEQ_LENCOMPARE_ERROR;
      break;
    }

    if (BWTSeqHasLocateInformation(bwtSeq))
    {
      Seqpos i;
      for (i = 0; i < len && retval == VERIFY_BWTSEQ_NO_ERROR; ++i)
        if (BWTSeqPosHasLocateInfo(bwtSeq, i, &extBits))
        {
          Seqpos sfxArrayValue = BWTSeqLocateMatch(bwtSeq, i, &extBits);
          if (sfxArrayValue != suffixArray.suftab[i])
          {
            error_set(err, "Failed suffixarray value comparison"
                          " at position "FormatSeqpos": "FormatSeqpos" != "
                          FormatSeqpos,
                          i, sfxArrayValue, suffixArray.suftab[i]);
            retval = VERIFY_BWTSEQ_SUFVAL_ERROR;
            break;
          }
        }
      if (retval != VERIFY_BWTSEQ_NO_ERROR)
        break;
    }
    else
    {
      fputs("Not checking suftab values (no locate information present)!\n",
            stderr);
    }
    if ((bwtSeq->featureToggles & BWTProperlySorted)
        && suffixArray.longest.defined && len)
    {
      Seqpos nextLocate = suffixArray.longest.valueseqpos,
        i = len;
      /* handle first symbol specially because the encodedsequence
       * will not return the terminator symbol */
      {
        Symbol sym = EISGetSym(bwtSeq->seqIdx, nextLocate, bwtSeq->hint);
        if (sym != UNDEFBWTCHAR)
        {
          error_set(err, "symbol mismatch at position "FormatSeqpos": "
                        "%d vs. reference symbol %d", i - 1, sym,
                        UNDEFBWTCHAR);
          retval = VERIFY_BWTSEQ_LFMAPWALK_ERROR;
          break;
        }
        --i;
        nextLocate = BWTSeqLFMap(bwtSeq, nextLocate);
      }
      while (i > 0)
      {
        Symbol symRef = getencodedchar(suffixArray.encseq,
                                       --i, suffixArray.readmode);
        Symbol symCmp = EISGetSym(bwtSeq->seqIdx, nextLocate, bwtSeq->hint);
        if (symCmp != symRef)
        {
          error_set(err, "symbol mismatch at position "FormatSeqpos": "
                        "%d vs. reference symbol %d", i, symCmp, symRef);
          retval = VERIFY_BWTSEQ_LFMAPWALK_ERROR;
          break;
        }
        nextLocate = BWTSeqLFMap(bwtSeq, nextLocate);
      }
    }
  } while (0);
  if (suffixArrayIsInitialized)
    freesuffixarray(&suffixArray);
  if (verbosity)
    freeverboseinfo(&verbosity);
  if (extBitsAreInitialized)
    destructExtBitsRetrieval(&extBits);
  return retval;
}
