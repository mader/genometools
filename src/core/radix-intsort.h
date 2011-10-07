/*
  Copyright (c) 2011 Stefan Kurtz <kurtz@zbh.uni-hamburg.de>
  Copyright (c) 2011 Center for Bioinformatics, University of Hamburg

  Permission to use, copy, modify, and distribute this software for any
  purpose with or without fee is hereby granted, provided that the above
  copyright notice and this permission notice appear in all copies.

  THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WA(RR)ANTIES
  WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WA(RR)ANTIES OF
  MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
  ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
  WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
  ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
  OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/

#ifndef RADIX_INTSORT_H
#define RADIX_INTSORT_H

#include "core/types_api.h"

#define GT_RADIXREADER_NEXT(VALUE,RR,STOPSTATEMENT)\
        if ((RR)->ptr1 < (RR)->end1)\
        {\
          if ((RR)->ptr2 < (RR)->end2)\
          {\
            if (*(RR)->ptr1 <= *(RR)->ptr2)\
            {\
              VALUE = *(RR)->ptr1++;\
            } else\
            {\
              VALUE = *(RR)->ptr2++;\
            }\
          } else\
          {\
            VALUE = *(RR)->ptr1++;\
          }\
        } else\
        {\
          if ((RR)->ptr2 < (RR)->end2)\
          {\
            VALUE = *(RR)->ptr2++;\
          } else\
          {\
            STOPSTATEMENT;\
          }\
        }

typedef struct
{
  GtUlong *ptr1, *ptr2, *end1, *end2;
} GtRadixreader;

typedef struct GtRadixsortinfo GtRadixsortinfo;

GtRadixsortinfo *gt_radixsort_new(bool pair,
                                  bool smalltables,
                                  unsigned long maxlen,
                                  unsigned int parts,
                                  void *arr);

unsigned long gt_radixsort_entries(bool pair,unsigned int parts,
                                   size_t memlimit);

GtUlong *gt_radixsort_arr(GtRadixsortinfo *radixsort);

GtUlongPair *gt_radixsort_arrpair(GtRadixsortinfo *radixsort);

size_t gt_radixsort_size(const GtRadixsortinfo *radixsort);

void gt_radixsort_delete(GtRadixsortinfo *radixsort);

void gt_radixsort_linear(GtRadixsortinfo *radixsort,unsigned long len);

void gt_radixsort_linear_rr(GtRadixreader *rr,
                            GtRadixsortinfo *radixsort,unsigned long len);

void gt_radixsort_GtUlong_divide(GtUlong *source,
                                 GtUlong *dest,
                                 unsigned long len);

void gt_radixsort_GtUlong_recursive(GtUlong *source,
                                    GtUlong *dest,
                                    unsigned long len);

#endif
