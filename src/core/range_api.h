/*
  Copyright (c) 2005-2008 Gordon Gremme <gremme@zbh.uni-hamburg.de>
  Copyright (c) 2005-2008 Center for Bioinformatics, University of Hamburg

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

#ifndef RANGE_API_H
#define RANGE_API_H

#include <stdbool.h>
#include "core/array_api.h"

/* The <GtRange> class is used to represent genomic ranges in __GenomeTools__.
   Thereby, the <start> must ___always___ be smaller or equal than the <end>. */
typedef struct GtRange GtRange;

struct GtRange {
  unsigned long start,
                end;
};

/* Compare <range_a> with <range_b>. Returns 0 if <range_a> equals <range_b>, -1
   if <range_a> starts before <range_b> or (for equal starts) <range_a> ends
   before <range_b>, and 1 else. */
int           gt_range_compare(const GtRange *range_a, const GtRange *range_b);
/* XXX */
int           gt_range_compare_with_delta(GtRange, GtRange,
                                          unsigned long delta);
/* XXX */
bool          gt_range_overlap(GtRange, GtRange);
/* XXX */
bool          gt_range_contains(GtRange, GtRange);
/* XXX */
bool          gt_range_within(GtRange, unsigned long);
/* XXX */
GtRange       gt_range_join(GtRange, GtRange);
/* XXX */
GtRange       gt_range_offset(GtRange, long offset);
/* XXX */
unsigned long gt_range_length(GtRange);

#endif
