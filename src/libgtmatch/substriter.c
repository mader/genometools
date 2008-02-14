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

#include <inttypes.h>
#include "libgtcore/strarray.h"
#include "libgtcore/seqiterator.h"
#include "libgtcore/chardef.h"
#include "substriter.h"
#include "spacedef.h"

#include "initbasepower.pr"

struct Substriter
{
  uint64_t unitnum;
  unsigned int qvalue,
               numofchars;
  bool newseq;
  Codetype **multimappower;
  SeqIterator *seqit;
};

Substriter *substriter_new(const StrArray *queryfilenames,
                           const Alphabet *alphabet,
                           unsigned int qvalue)
{
  Substriter *substriter;
  ALLOCASSIGNSPACE(substriter,NULL,Substriter,1);
  substriter->unitnum = 0;
  substriter->qvalue = qvalue;
  substriter->numofchars = getnumofcharsAlphabet(alphabet);
  substriter->seqit = seqiterator_new(queryfilenames,
                                      getsymbolmapAlphabet(alphabet),
                                      true);
  substriter->multimappower = initmultimappower(substriter->numofchars,qvalue);
  substriter->newseq = true;
  return substriter;
}

static unsigned int qgram2code(Codetype *code,
                               const Codetype **multimappower,
                               unsigned int qvalue,
                               const Uchar *qgram)
{
  int i;
  Codetype tmpcode = 0;
  Uchar a;

  for (i=(int) (qvalue-1); i>=0; i--)
  {
    a = qgram[i];
    if (ISSPECIAL(a))
    {
      return (unsigned int) i;
    }
    tmpcode += multimappower[i][a];
  }
  *code = tmpcode;
  return qvalue;
}

int substriter_next(Substring *substring,Substriter *substriter,Error *err)
{
  unsigned int firstspecial;

  while (true)
  {
    if (substriter->newseq)
    {
      int retval;

      retval = seqiterator_next(substriter->seqit,
                                &substring->start,
                                &substring->remaining,
                                &substring->desc,
                                err);
      if (retval <= 0)
      {
        FREESPACE(substring->desc);
        return retval;
      }
      substring->currentptr = substring->start;
      assert(substring->remaining > 0);
      if (substring->remaining >= (unsigned long) substriter->qvalue)
      {
        firstspecial = qgram2code(&substring->currentcode,
                                  (const Codetype **) substriter->multimappower,
                                  substriter->qvalue,
                                  substring->currentptr);
        substriter->newseq = false;
        if (firstspecial == substriter->qvalue)
        {
          break;
        }
      }
    } else
    {
      assert(substring->remaining > 0);
      substring->remaining--;
      substring->currentptr++;
      if (substring->remaining >= (unsigned long) substriter->qvalue)
      {
        firstspecial = qgram2code(&substring->currentcode,
                                  (const Codetype **) substriter->multimappower,
                                  substriter->qvalue,
                                  substring->currentptr);
        if (firstspecial == substriter->qvalue)
        {
          break;
        }
      } else
      {
        substriter->newseq = true;
        substriter->unitnum++;
        FREESPACE(substring->desc);
      }
    }
  }
  return 1;
}

uint64_t substriter_unitnum(const Substriter *substriter)
{
  return substriter->unitnum;
}

void substriter_delete(Substriter **substriter)
{
  seqiterator_delete((*substriter)->seqit);
  multimappowerfree((*substriter)->multimappower);
  FREESPACE(*substriter);
}
