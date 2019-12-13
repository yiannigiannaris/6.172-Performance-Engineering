/*
 * validator.h - 6.172 Malloc Validator
 *
 * Validates a malloc/free/realloc implementation.
 *
 * Copyright (c) 2010, R. Bryant and D. O'Hallaron, All rights reserved.
 * May not be used, modified, or copied without permission.
 */

#ifndef MM_VALIDATOR_H
#define MM_VALIDATOR_H

#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "./config.h"
#include "./mdriver.h"
#include "./memlib.h"

// Returns true if p is R_ALIGNMENT-byte aligned
#if (__WORDSIZE == 64 )
  #define IS_ALIGNED(p)  ((((uint64_t)(p)) % R_ALIGNMENT) == 0)
#else
  #define IS_ALIGNED(p)  ((((uint32_t)(p)) % R_ALIGNMENT) == 0)
#endif

// Range list data structure

// Records the extent of each block's payload
typedef struct range_t {
  char* lo;              // low payload address
  char* hi;              // high payload address
  struct range_t* next;  // next list element
} range_t;

// The following routines manipulate the range list, which keeps
// track of the extent of every allocated block payload. We use the
// range list to detect any overlapping allocated blocks.


// add_range - As directed by request opnum in trace tracenum,
// we've just called the student's malloc to allocate a block of
// size bytes at addr lo. After checking the block for correctness,
// we create a range struct for this block and add it to the range list.
static int add_range(const malloc_impl_t* impl, range_t** ranges, char* lo,
                     int size, int tracenum, int opnum) {
  char *hi = lo + size - 1;
  range_t *p = NULL;
  char msg[MAXLINE];  // Buffer for sprintf.

  assert(size > 0);

  // Payload addresses must be R_ALIGNMENT-byte aligned
  if (!(IS_ALIGNED(lo))) {
    malloc_error(tracenum, opnum, "block not aligned.");
    return 0;
  }

  // The payload must lie within the extent of the heap
  if ((uintptr_t) lo < (uintptr_t) mem_heap_lo() || (uintptr_t) hi > (uintptr_t) mem_heap_hi()) {
    snprintf(msg, MAXLINE, "block outside heap, lo: %lx, hi: %lx, heap_lo: %lx, heap_hi: %lx.",
             (uintptr_t)lo, (uintptr_t)hi, (uintptr_t)mem_heap_lo(), (uintptr_t)mem_heap_hi());
    malloc_error(tracenum, opnum, msg);
    return 0;
  }

  // The payload must not overlap any other payloads
  assert(ranges != NULL);
  while (*ranges != NULL) {
    range_t *cur_range = *ranges;
    if ((lo <= cur_range->lo && hi >= cur_range->lo) ||
        (lo <= cur_range->hi && hi >= cur_range->hi)) {
      snprintf(msg, MAXLINE,
               "block overlaps with existing, lo: %lx, hi: %lx; existing - lo: %lx, hi: %lx.",
               (uintptr_t)lo, (uintptr_t)hi, (uintptr_t)cur_range->lo, (uintptr_t)cur_range->hi);
      malloc_error(tracenum, opnum, msg);
      return 0;
    }
    ranges = &(cur_range->next);
  }

  // Everything looks OK, so remember the extent of this block by creating a
  // range struct and adding it the range list.
  p = (range_t*) malloc(sizeof(range_t));
  p->lo = lo;
  p->hi = hi;
  p->next = NULL;
  *ranges = p;  // Currently pointing to `next` of last list element.

  return 1;
}

// remove_range - Free the range record of block whose payload starts at lo
static void remove_range(range_t** ranges, char* lo) {
  // Iterate the linked list until you find the range with a matching lo
  // payload and remove it.  Remember to properly handle the case where the
  // payload is in the first node, and to free the node after unlinking it.
  range_t* prev = NULL;
  range_t* curr = *ranges;
  while (curr != NULL) {
    if (curr->lo == lo) {
      if (prev != NULL) {
        prev->next = curr->next;
      } else {
        *ranges = curr->next;
      }
      free(curr);
      break;
    }
    prev = curr;
    curr = curr->next;
  }
}

// clear_ranges - free all of the range records for a trace
static void clear_ranges(range_t** ranges) {
  range_t* p;
  range_t* pnext;

  for (p = *ranges; p != NULL; p = pnext) {
    pnext = p->next;
    free(p);
  }
  *ranges = NULL;
}

// eval_mm_valid - Check the malloc package for correctness
int eval_mm_valid(const malloc_impl_t* impl, trace_t* trace, int tracenum) {
  int i = 0;
  int index = 0;
  int size = 0;
  int oldsize = 0;
  char* newp = NULL;
  char* oldp = NULL;
  char* p = NULL;
  range_t* ranges = NULL;
  uint8_t uId = 0;

  // Reset the heap.
  impl->reset_brk();

  // Call the mm package's init function
  if (impl->init() < 0) {
    malloc_error(tracenum, 0, "impl init failed.");
    return 0;
  }

  // Interpret each operation in the trace in order
  for (i = 0; i < trace->num_ops; i++) {
    if (uId == 255) {
      uId = 0;
    }
    uId++;
    index = trace->ops[i].index;
    size = trace->ops[i].size;

    switch (trace->ops[i].type) {
    case ALLOC:  // malloc

      // Call the student's malloc
      if ((p = (char*) impl->malloc(size)) == NULL) {
        malloc_error(tracenum, i, "impl malloc failed.");
        return 0;
      }

      // Test the range of the new block for correctness and add it
      // to the range list if OK. The block must be  be aligned properly,
      // and must not overlap any currently allocated block.
      if (add_range(impl, &ranges, p, size, tracenum, i) == 0) {
        return 0;
      }

      // Fill the allocated region with some unique data that you can check
      // for if the region is copied via realloc.
      if (size < 4) {
        for (char* q = p; q < p+size; q++) {
          *q = (char)uId;
        }
      } else {
        for (int* q = (int*)p; q < (int*)p+(size/sizeof(int)); q++) {
          *q = i;
        }
      }

      // Remember region
      trace->blocks[index] = p;
      trace->block_sizes[index] = size;
      break;

    case REALLOC:  // realloc

      // Call the student's realloc
      oldp = trace->blocks[index];
      int oldVal_int =  *(int*)oldp;
      uint8_t oldVal_uint8 = *(uint8_t*)oldp;
      if ((newp = (char*) impl->realloc(oldp, size)) == NULL) {
        malloc_error(tracenum, i, "impl realloc failed.");
        return 0;
      }

      // Remove the old region from the range list
      remove_range(&ranges, oldp);

      // Check new block for correctness and add it to range list
      if (add_range(impl, &ranges, newp, size, tracenum, i) == 0) {
        return 0;
      }

      // Make sure that the new block contains the data from the old block,
      // and then fill in the new block with new data that you can use to
      // verify the block was copied if it is resized again.
      oldsize = trace->block_sizes[index];
      if (size < oldsize) {
        oldsize = size;
      }
      // Check id for correctness
      if (oldsize < 4) {
        for (char* q = newp; q < newp+oldsize; q++) {
          if (*q != oldVal_uint8) {
            malloc_error(tracenum, i, "impl realloc failed checking copied bytes 1 byte id");
            return 0;
          }
        }
      } else {
        for (int* q = (int*)newp; q < (int*)newp+(oldsize/sizeof(int)); q++) {
          if (*q != oldVal_int) {
            malloc_error(tracenum, i, "impl realloc failed checking copied bytes index id");
            return 0;
          }
        }
      }
      // Write new id
      if (size < 4) {
        for (char* q = newp; q < newp+size; q++) {
          *q = (char)uId;
        }
      } else {
        for (int* q = (int*)newp; q < (int*)newp+(size/sizeof(int)); q++) {
          *q = index;
        }
      }

      // Remember region
      trace->blocks[index] = newp;
      trace->block_sizes[index] = size;
      break;

    case FREE:  // free

      // Remove region from list and call student's free function
      p = trace->blocks[index];
      remove_range(&ranges, p);
      impl->free(p);
      break;

    case WRITE:  // write

      break;

    default:
      app_error("Nonexistent request type in eval_mm_valid");
    }
  }

  // Free ranges allocated and reset the heap.
  impl->reset_brk();
  clear_ranges(&ranges);

  // As far as we know, this is a valid malloc package
  return 1;
}
#endif  // MM_VALIDATOR_H
