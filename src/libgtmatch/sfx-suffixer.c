/*
  Copyright (c) 2007 Stefan Kurtz <kurtz@zbh.uni-hamburg.de>
  Copyright (c) 2007 Center for Bioinformatics, University of Hamburg

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

#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <limits.h>
#include <errno.h>
#include "libgtcore/arraydef.h"
#include "libgtcore/error.h"
#include "spacedef.h"
#include "intbits.h"
#include "divmodmul.h"
#include "measure-time-if.h"
#include "intcode-def.h"
#include "encseq-def.h"
#include "safecast-gen.h"
#include "sfx-codespec.h"
#include "sfx-partssuf-def.h"
#include "sfx-suffixer.h"
#include "sfx-outlcp.h"
#include "stamp.h"

#include "sfx-mappedstr.pr"
#include "initbasepower.pr"
#include "kmer2string.pr"

#define CODEBITS        (32-PREFIXLENBITS)
#define MAXPREFIXLENGTH ((1U << PREFIXLENBITS) - 1)
#define MAXCODEVALUE    ((1U << CODEBITS) - 1)

typedef struct
{
  unsigned int maxprefixlen:PREFIXLENBITS;
  unsigned int code:CODEBITS;
  Seqpos position; /* get rid of this by using information from encseq */
} Codeatposition;

DECLAREARRAYSTRUCT(Codeatposition);

#define LONGOUTPUT
#undef LONGOUTPUT

DECLAREARRAYSTRUCT(Seqpos);

 struct Sfxiterator
{
  bool storespecials;
  Codetype currentmincode,
           currentmaxcode;
  Seqpos specialcharacters,
         *suftab,
         *suftabptr;
  unsigned long nextfreeCodeatposition;
  Codeatposition *spaceCodeatposition;
  Suftabparts *suftabparts;
  const Encodedsequence *encseq;
  Readmode readmode;
  Seqpos widthofpart,
         totallength;
  Outlcpinfo *outlcpinfo;
  unsigned int part,
               numofchars,
               prefixlength;
  const Uchar *characters;
  ArraySeqpos fusp;
  Specialrangeiterator *sri;
  Sequencerange overhang;
  Seqpos previoussuffix;
  bool exhausted;
  Bcktab bcktab;
};

static void updatekmercount(void *processinfo,
                            Codetype code,
                            Seqpos position,
                            const Firstspecialpos *firstspecial)
{
  Sfxiterator *sfi = (Sfxiterator *) processinfo;

  if (firstspecial->defined)
  {
    if (sfi->storespecials)
    {
      if (firstspecial->specialpos > 0)
      {
        Codeatposition *cp;

        cp = sfi->spaceCodeatposition + sfi->nextfreeCodeatposition++;
        cp->code = code;
        cp->maxprefixlen = firstspecial->specialpos;
        cp->position = position + firstspecial->specialpos;
        sfi->storespecials = false;
        sfi->bcktab.leftborder[code]++;
      }
    } else
    {
      if (firstspecial->specialpos > 0)
      {
        sfi->bcktab.leftborder[code]++;
      } else
      {
        sfi->storespecials = true;
      }
    }
  } else
  {
    sfi->bcktab.leftborder[code]++;
  }
}

static void insertwithoutspecial(void *processinfo,
                                 Codetype code,
                                 Seqpos position,
                                 const Firstspecialpos *firstspecial)
{
  if (!firstspecial->defined)
  {
    Sfxiterator *sfi = (Sfxiterator *) processinfo;

    if (code >= sfi->currentmincode && code <= sfi->currentmaxcode)
    {
      Seqpos stidx;

      stidx = --sfi->bcktab.leftborder[code];
#ifdef LONGOUTPUT
      printf("insert suffix " FormatSeqpos " at location " FormatSeqpos "\n",
              PRINTSeqposcast(position),
              PRINTSeqposcast(stidx));
#endif
      sfi->suftabptr[stidx] = position;
    }
  }
}

static void reversespecialcodes(Codeatposition *spaceCodeatposition,
                                unsigned long nextfreeCodeatposition)
{
  Codeatposition *front, *back, tmp;

  for (front = spaceCodeatposition,
       back = spaceCodeatposition + nextfreeCodeatposition - 1;
       front < back; front++, back--)
  {
    tmp = *front;
    *front = *back;
    *back = tmp;
  }
}

static Codetype codedownscale(const unsigned int *filltable,
                              const unsigned int *basepower,
                              Codetype code,
                              unsigned int prefixindex,
                              unsigned int maxprefixlen)
{
  unsigned int remain;

  code -= filltable[maxprefixlen];
  remain = maxprefixlen-prefixindex;
  code %= (filltable[remain]+1);
  code *= basepower[remain];
  code += filltable[prefixindex];
  return code;
}

static void derivespecialcodes(Sfxiterator *sfi,bool deletevalues)
{
  Codetype code, ordercode, divider;
  unsigned int prefixindex;
  unsigned long insertindex, j;
  Seqpos stidx;

#ifdef LONGOUTPUT
  char buffer[MAXPREFIXLENGTH+1];
#endif

  for (prefixindex=1U; prefixindex < sfi->prefixlength; prefixindex++)
  {
    divider = sfi->bcktab.filltable[prefixindex]+1;
    for (j=0, insertindex = 0; j < sfi->nextfreeCodeatposition; j++)
    {
      if (prefixindex <= sfi->spaceCodeatposition[j].maxprefixlen)
      {
        code = codedownscale(sfi->bcktab.filltable,
                             sfi->bcktab.basepower,
                             sfi->spaceCodeatposition[j].code,
                             prefixindex,
                             sfi->spaceCodeatposition[j].maxprefixlen);
        if (code >= sfi->currentmincode && code <= sfi->currentmaxcode)
        {
          if (prefixindex < sfi->prefixlength-1)
          {
            ordercode = (code - sfi->bcktab.filltable[prefixindex])/divider;
            sfi->bcktab.distpfxidx[prefixindex-1][ordercode]++;
          }
          sfi->bcktab.countspecialcodes[
               FROMCODE2SPECIALCODE(code,sfi->numofchars)]++;
          stidx = --sfi->bcktab.leftborder[code];
#ifdef LONGOUTPUT
          kmercode2string(buffer,
                          code,
                          sfi->numofchars,
                          sfi->prefixlength,
                          sfi->characters);
          printf("insert special_suffix_for_prefixindex %u " FormatSeqpos
                 " (code %u,\"%s\",ordercode=%u) at location "
                 FormatSeqpos "\n",
                 prefixindex,
                 PRINTSeqposcast(sfi->spaceCodeatposition[j].position -
                                 prefixindex),
                 (unsigned int) code,
                 buffer,
                 (unsigned int) (code - sfi->bcktab.filltable[prefixindex])/
                                divider,
                 PRINTSeqposcast(stidx));
#endif
          sfi->suftabptr[stidx] = sfi->spaceCodeatposition[j].position -
                                  prefixindex;
        }
      }
      if (deletevalues)
      {
        if (prefixindex < sfi->prefixlength - 1 &&
            prefixindex < sfi->spaceCodeatposition[j].maxprefixlen)
        {
          if (insertindex < j)
          {
            sfi->spaceCodeatposition[insertindex] =
              sfi->spaceCodeatposition[j];
          }
          insertindex++;
        }
      }
    }
    if (deletevalues)
    {
      sfi->nextfreeCodeatposition = insertindex;
    }
  }
}

void freeSfxiterator(Sfxiterator **sfi)
{
  Codetype specialcode;

  specialcode = FROMCODE2SPECIALCODE((*sfi)->bcktab.filltable[0],
                                     (*sfi)->numofchars);
  (*sfi)->bcktab.countspecialcodes[specialcode]
    += ((*sfi)->specialcharacters + 1);
  if ((*sfi)->sri != NULL)
  {
    freespecialrangeiterator(&(*sfi)->sri);
  }
  FREESPACE((*sfi)->spaceCodeatposition);
  FREESPACE((*sfi)->bcktab.filltable);
  FREESPACE((*sfi)->bcktab.basepower);
  FREESPACE((*sfi)->bcktab.leftborder);
  FREESPACE((*sfi)->bcktab.countspecialcodes);
  FREESPACE((*sfi)->suftab);
  if ((*sfi)->bcktab.distpfxidx != NULL)
  {
    FREESPACE((*sfi)->bcktab.distpfxidx[0]);
    FREESPACE((*sfi)->bcktab.distpfxidx);
  }
  freesuftabparts((*sfi)->suftabparts);
  FREESPACE(*sfi);
}

static unsigned long **initdistprefixindexcounts(const Codetype *basepower,
                                                 unsigned int prefixlength)
{

  if (prefixlength > 2U)
  {
    unsigned int idx;
    unsigned long *counters, numofcounters, **distpfxidx;

    for (numofcounters = 0, idx=1U; idx <= prefixlength-2; idx++)
    {
      numofcounters += basepower[idx];
    }
    assert(numofcounters > 0);
    ALLOCASSIGNSPACE(distpfxidx,NULL,unsigned long *,prefixlength-2);
    ALLOCASSIGNSPACE(counters,NULL,unsigned long,numofcounters);
    memset(counters,0,(size_t) sizeof (*counters) * numofcounters);
    distpfxidx[0] = counters;
    for (idx=1U; idx<prefixlength-2; idx++)
    {
      distpfxidx[idx] = distpfxidx[idx-1] + basepower[idx];
    }
    return distpfxidx;
  } else
  {
    return NULL;
  }
}

 DECLARESAFECASTFUNCTION(Seqpos,Seqpos,unsigned long,unsigned_long)

Sfxiterator *newSfxiterator(Seqpos specialcharacters,
                            Seqpos specialranges,
                            const Encodedsequence *encseq,
                            Readmode readmode,
                            unsigned int numofchars,
                            const Uchar *characters,
                            unsigned int prefixlength,
                            unsigned int numofparts,
                            Outlcpinfo *outlcpinfo,
                            Measuretime *mtime,
                            Verboseinfo *verboseinfo,
                            Error *err)
{
  Sfxiterator *sfi = NULL;
  Seqpos *optr;
  bool haserr = false;

  error_check(err);
  if (prefixlength == 0 || prefixlength > MAXPREFIXLENGTH)
  {
    error_set(err,"argument for option -pl must be in the range [1,%u]",
                  MAXPREFIXLENGTH);
    haserr = true;
  } else
  {
    ALLOCASSIGNSPACE(sfi,NULL,Sfxiterator,1);
    ALLOCASSIGNSPACE(sfi->spaceCodeatposition,NULL,
                     Codeatposition,specialranges+1);
    sfi->nextfreeCodeatposition = 0;
    sfi->suftab = NULL;
    sfi->suftabptr = NULL;
    sfi->suftabparts = NULL;
    sfi->encseq = encseq;
    sfi->readmode = readmode;
    sfi->numofchars = numofchars;
    sfi->characters = characters;
    sfi->prefixlength = prefixlength;
    sfi->totallength = getencseqtotallength(encseq);
    sfi->specialcharacters = specialcharacters;
    sfi->previoussuffix = 0;
    sfi->outlcpinfo = outlcpinfo;
    sfi->sri = NULL;
    sfi->part = 0;
    sfi->exhausted = false;

    sfi->bcktab.distpfxidx = NULL;
    sfi->bcktab.filltable = NULL;
    sfi->bcktab.basepower = NULL;
    sfi->bcktab.leftborder = NULL;
    sfi->bcktab.countspecialcodes = NULL;
    sfi->bcktab.basepower = initbasepower(numofchars,prefixlength);
    sfi->bcktab.filltable = initfilltable(sfi->bcktab.basepower,prefixlength);
    sfi->bcktab.numofallcodes = sfi->bcktab.basepower[prefixlength];
    sfi->bcktab.numofspecialcodes = sfi->bcktab.basepower[prefixlength-1];
    if (sfi->bcktab.numofallcodes-1 > MAXCODEVALUE)
    {
      error_set(err,"alphasize^prefixlength-1 = %u does not fit into "
                    " %u bits: choose smaller value for prefixlength",
                    sfi->bcktab.numofallcodes-1,
                    (unsigned int) CODEBITS);
      haserr = true;
    } else
    {
      sfi->bcktab.distpfxidx = initdistprefixindexcounts(sfi->bcktab.basepower,
                                                         sfi->prefixlength);
    }
  }
  if (!haserr)
  {
    assert(sfi != NULL);
    ALLOCASSIGNSPACE(sfi->bcktab.leftborder,NULL,Seqpos,
                     sfi->bcktab.numofallcodes+1);
    memset(sfi->bcktab.leftborder,0,
           sizeof (*sfi->bcktab.leftborder) *
           (size_t) sfi->bcktab.numofallcodes);
    ALLOCASSIGNSPACE(sfi->bcktab.countspecialcodes,NULL,Seqpos,
                     sfi->bcktab.numofspecialcodes);
    memset(sfi->bcktab.countspecialcodes,0,
           sizeof (*sfi->bcktab.countspecialcodes) *
                  (size_t) sfi->bcktab.numofspecialcodes);
    sfi->storespecials = true;
    if (mtime != NULL)
    {
      deliverthetime(stdout,mtime,"counting prefix distribution");
    }
    getencseqkmers(encseq,
                   readmode,
                   updatekmercount,
                   sfi,
                   numofchars,
                   prefixlength,
                   err);
    assert(specialranges+1 >= (Seqpos) sfi->nextfreeCodeatposition);
    assert(sfi->bcktab.leftborder != NULL);
    /* printf("leftborder[0]=%u\n",sfi.leftborder[0]); */
    for (optr = sfi->bcktab.leftborder + 1;
         optr < sfi->bcktab.leftborder + sfi->bcktab.numofallcodes; optr++)
    {
      /* printf("leftborder[%u]=%u\n",(unsigned int) (optr - sfi->leftborder),
                                   *optr); */
      *optr += *(optr-1);
    }
    sfi->bcktab.leftborder[sfi->bcktab.numofallcodes]
      = sfi->totallength - specialcharacters;
    sfi->suftabparts = newsuftabparts(numofparts,
                                      sfi->bcktab.leftborder,
                                      sfi->bcktab.numofallcodes,
                                      sfi->totallength - specialcharacters,
                                      specialcharacters + 1,
                                      verboseinfo);
    assert(sfi->suftabparts != NULL);
    ALLOCASSIGNSPACE(sfi->suftab,NULL,Seqpos,
                     stpgetlargestwidth(sfi->suftabparts));
    reversespecialcodes(sfi->spaceCodeatposition,sfi->nextfreeCodeatposition);
    if (hasspecialranges(sfi->encseq))
    {
      sfi->sri = newspecialrangeiterator(sfi->encseq,
                                         ISDIRREVERSE(sfi->readmode)
                                           ? false : true);
    } else
    {
      sfi->sri = NULL;
    }
    sfi->fusp.spaceSeqpos = sfi->suftab;
    sfi->fusp.allocatedSeqpos
      = CALLCASTFUNC(Seqpos,unsigned_long,
                     stpgetlargestwidth(sfi->suftabparts));
    sfi->overhang.leftpos = sfi->overhang.rightpos = 0;
  }
  if (haserr)
  {
    if (sfi != NULL)
    {
      freeSfxiterator(&sfi);
    }
    return NULL;
  }
  return sfi;
}

static void preparethispart(Sfxiterator *sfi,
                            Measuretime *mtime,
                            Error *err)
{
  Seqpos totalwidth;

  sfi->currentmincode = stpgetcurrentmincode(sfi->part,sfi->suftabparts);
  sfi->currentmaxcode = stpgetcurrentmaxcode(sfi->part,sfi->suftabparts);
  sfi->widthofpart = stpgetcurrentwidthofpart(sfi->part,sfi->suftabparts);
  sfi->suftabptr = sfi->suftab -
                   stpgetcurrentsuftaboffset(sfi->part,sfi->suftabparts);
  derivespecialcodes(sfi,
                     (stpgetnumofparts(sfi->suftabparts) == 1U)
                       ? true : false);
  if (mtime != NULL)
  {
    deliverthetime(stdout,mtime,"inserting suffixes into buckets");
  }
  getencseqkmers(sfi->encseq,
                 sfi->readmode,
                 insertwithoutspecial,
                 sfi,
                 sfi->numofchars,
                 sfi->prefixlength,
                 err);
  if (mtime != NULL)
  {
    deliverthetime(stdout,mtime,"sorting the buckets");
  }
  totalwidth = stpgetcurrentsumofwdith(sfi->part,sfi->suftabparts);
  sortallbuckets(sfi->suftabptr,
                 sfi->encseq,
                 sfi->readmode,
                 sfi->currentmincode,
                 sfi->currentmaxcode,
                 totalwidth,
                 sfi->previoussuffix,
                 &sfi->bcktab,
                 sfi->numofchars,
                 sfi->prefixlength,
                 sfi->outlcpinfo);
  assert(totalwidth > 0);
  sfi->previoussuffix = sfi->suftab[sfi->widthofpart-1];
  sfi->part++;
}

static void insertfullspecialrange(Sfxiterator *sfi,
                                   Seqpos leftpos,
                                   Seqpos rightpos)
{
  Seqpos pos;

  assert(leftpos < rightpos);
  if (ISDIRREVERSE(sfi->readmode))
  {
    pos = rightpos - 1;
  } else
  {
    pos = leftpos;
  }
  while (true)
  {
    if (ISDIRREVERSE(sfi->readmode))
    {
      sfi->fusp.spaceSeqpos[sfi->fusp.nextfreeSeqpos++]
        = REVERSEPOS(sfi->totallength,pos);
      if (pos == leftpos)
      {
        break;
      }
      pos--;
    } else
    {
      sfi->fusp.spaceSeqpos[sfi->fusp.nextfreeSeqpos++] = pos;
      if (pos == rightpos-1)
      {
        break;
      }
      pos++;
    }
  }
}

static void fillspecialnextpage(Sfxiterator *sfi)
{
  Sequencerange range;
  Seqpos width;

  while (true)
  {
    if (sfi->overhang.leftpos < sfi->overhang.rightpos)
    {
      width = sfi->overhang.rightpos - sfi->overhang.leftpos;
      if (sfi->fusp.nextfreeSeqpos + width > sfi->fusp.allocatedSeqpos)
      {
        /* does not fit into the buffer, so only output a part */
        unsigned long rest = sfi->fusp.nextfreeSeqpos +
                             width - sfi->fusp.allocatedSeqpos;
        assert(rest > 0);
        if (ISDIRREVERSE(sfi->readmode))
        {
          insertfullspecialrange(sfi,sfi->overhang.leftpos + rest,
                                 sfi->overhang.rightpos);
          sfi->overhang.rightpos = sfi->overhang.leftpos + rest;
        } else
        {
          insertfullspecialrange(sfi,sfi->overhang.leftpos,
                                     sfi->overhang.rightpos - rest);
          sfi->overhang.leftpos = sfi->overhang.rightpos - rest;
        }
        break;
      }
      if (sfi->fusp.nextfreeSeqpos + width == sfi->fusp.allocatedSeqpos)
      { /* overhang fits into the buffer and buffer is full */
        insertfullspecialrange(sfi,sfi->overhang.leftpos,
                               sfi->overhang.rightpos);
        sfi->overhang.leftpos = sfi->overhang.rightpos = 0;
        break;
      }
      /* overhang fits into the buffer and buffer is not full */
      insertfullspecialrange(sfi,sfi->overhang.leftpos,
                             sfi->overhang.rightpos);
      sfi->overhang.leftpos = sfi->overhang.rightpos = 0;
    } else
    {
      if (sfi->sri != NULL && nextspecialrangeiterator(&range,sfi->sri))
      {
        width = range.rightpos - range.leftpos;
        assert(width > 0);
        if (sfi->fusp.nextfreeSeqpos + width > sfi->fusp.allocatedSeqpos)
        { /* does not fit into the buffer, so only output a part */
          unsigned long rest = sfi->fusp.nextfreeSeqpos +
                               width - sfi->fusp.allocatedSeqpos;
          if (ISDIRREVERSE(sfi->readmode))
          {
            insertfullspecialrange(sfi,range.leftpos + rest,
                                   range.rightpos);
            sfi->overhang.leftpos = range.leftpos;
            sfi->overhang.rightpos = range.leftpos + rest;
          } else
          {
            insertfullspecialrange(sfi,range.leftpos,range.rightpos - rest);
            sfi->overhang.leftpos = range.rightpos - rest;
            sfi->overhang.rightpos = range.rightpos;
          }
          break;
        }
        if (sfi->fusp.nextfreeSeqpos + width == sfi->fusp.allocatedSeqpos)
        { /* overhang fits into the buffer and buffer is full */
          insertfullspecialrange(sfi,range.leftpos,range.rightpos);
          sfi->overhang.leftpos = sfi->overhang.rightpos = 0;
          break;
        }
        insertfullspecialrange(sfi,range.leftpos,range.rightpos);
        sfi->overhang.leftpos = sfi->overhang.rightpos = 0;
      } else
      {
        if (sfi->fusp.nextfreeSeqpos < sfi->fusp.allocatedSeqpos)
        {
          sfi->fusp.spaceSeqpos[sfi->fusp.nextfreeSeqpos++] = sfi->totallength;
          sfi->exhausted = true;
        }
        break;
      }
    }
  }
}

const Seqpos *nextSfxiterator(Seqpos *numberofsuffixes,bool *specialsuffixes,
                              Measuretime *mtime,Sfxiterator *sfi,Error *err)
{
  error_check(err);
  if (sfi->part < stpgetnumofparts(sfi->suftabparts))
  {
    preparethispart(sfi,mtime,err);
    *numberofsuffixes = sfi->widthofpart;
    *specialsuffixes = false;
    return sfi->suftab;
  }
  if (sfi->exhausted)
  {
    return NULL;
  }
  sfi->fusp.nextfreeSeqpos = 0;
  fillspecialnextpage(sfi);
  assert(sfi->fusp.nextfreeSeqpos > 0);
  *numberofsuffixes = (Seqpos) sfi->fusp.nextfreeSeqpos;
  *specialsuffixes = true;
  return sfi->suftab;
}

int sfibcktab2file(FILE *fp,
                   const Sfxiterator *sfi,
                   Error *err)
{
  return bcktab2file(fp,&sfi->bcktab,err);
}
