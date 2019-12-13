/**
 * Copyright (c) 2015 MIT License by 6.172 Staff
 *
 * Permission is hereby granted, free of uint8_tge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 **/

#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "./allocator_interface.h"
#include "./memlib.h"

// Don't call libc malloc!
#define malloc(...) (USE_MY_MALLOC)
#define free(...) (USE_MY_FREE)
#define realloc(...) (USE_MY_REALLOC)


// All blocks must have a specified minimum alignment.
// The alignment requirement (from config.h) is >= 8 bytes.
#ifndef ALIGNMENT
  #define ALIGNMENT 8
#endif

#ifndef TRACE
  #define TRACE 0
#endif

#define N_BINS 32

// Rounds up to the nearest multiple of ALIGNMENT.
#define ALIGN(size) (((size) + (ALIGNMENT-1)) & ~(ALIGNMENT-1))

// The smallest aligned size that will hold a byte.
#define SIZE_T_SIZE (ALIGN(sizeof(size_t)))

typedef struct free_list_t {
  struct free_list_t* next;
  struct free_list_t* prev;
} free_list_t;

#define MIN_SIZE sizeof(free_list_t)

free_list_t* bins[N_BINS];
uint8_t* used_heap_end;
size_t largest_free_bin;

// check - This checks our invariant that the size_t header before every
// block points to either the beginning of the next block, or the end of the
// heap.
int my_check() {
  uint8_t* p;
  uint8_t* lo = (uint8_t*)mem_heap_lo();
  uint8_t* hi = used_heap_end + 1;
  size_t size = 0;

  p = lo;
  while (lo <= p && p < hi) {
    size = *(size_t*)p;
    p += size + 2*SIZE_T_SIZE;
  }

  if (p != hi) {
    printf("Bad headers did not end at heap_hi!\n");
    printf("heap_lo: %p, heap_hi: %p, size: %lu, p: %p\n", lo, hi, size, p);
    return -1;
  }

  return 0;
}

// init - Initialize the malloc package.  Called once before any other
// calls are made.  Since this is a very simple implementation, we just
// return success.
int my_init() {
  memset(bins, (uint32_t) NULL, N_BINS * sizeof(free_list_t*));
  // Add buffer for coalescing
  size_t* buffer =  (size_t*)mem_sbrk(SIZE_T_SIZE);
  *buffer = 0;
  // Keep track of highest used address in heap
  used_heap_end = (uint8_t*) mem_heap_hi();
  largest_free_bin = 0;
  return 0;
}

// get_bin - Returns the index into the bins array for an appropriately
// sized bin.


// source for DeBruijn bit hack https://graphics.stanford.edu/~seander/bithacks.html#IntegerLogObvious
static const int MultiplyDeBruijnBitPosition[32] = 
{
  0, 9, 1, 10, 13, 21, 2, 29, 11, 14, 16, 18, 22, 25, 3, 30,
  8, 12, 20, 28, 15, 17, 24, 7, 19, 27, 23, 6, 26, 5, 4, 31
};

size_t get_bin(size_t alloc_size) {
  uint32_t size = (uint32_t)alloc_size;
  size |= size >> 1; // first round down to one less than a power of 2 
  size |= size >> 2;
  size |= size >> 4;
  size |= size >> 8;
  size |= size >> 16;
  size_t bin = MultiplyDeBruijnBitPosition[(uint32_t)(size * 0x07C4ACDDU) >> 27];
  return bin >= N_BINS ? N_BINS - 1 : bin;
}

// split_block - Splits a memory block into two, marking the left as used
// and the right as free. Returns a pointer to the right block.
free_list_t* split_block(free_list_t* block, size_t left_size, size_t right_size) {
  // Update header and footer of left block.
  size_t* left_header = (size_t*)((uint8_t*)block - SIZE_T_SIZE);
  size_t* left_footer = (size_t*)((uint8_t*)block + left_size);
  *left_header = left_size;
  *left_footer = 0;

  // Write header and footer of right block.
  size_t* right_header = (size_t*)((uint8_t*)left_footer + SIZE_T_SIZE);
  free_list_t* right_block = (free_list_t*)((uint8_t*)right_header + SIZE_T_SIZE);
  size_t* right_footer = (size_t*)((uint8_t*)right_block + right_size);
  *right_header = right_size;
  *right_footer = right_size;
  assert(right_size % 8 == 0);
  return right_block;
}

// remove_free_block - Remove block from free list.
void remove_free_block(free_list_t* block) {
  size_t bin = get_bin(*(size_t*)((uint8_t*)block - SIZE_T_SIZE));
  if (block->prev != NULL) {
    block->prev->next = block->next;
  } else {
    bins[bin] = block->next;
  }
  if (block->next != NULL) {
    block->next->prev = block->prev;
  }
  if (bin == largest_free_bin && bins[bin] == NULL) {
    while (bin > 0 && bins[bin] == NULL) {
      bin--;
    }
    largest_free_bin = bin;
  }
}

// add_free_block - Add block to free list.
void add_free_block(free_list_t* block) {
  size_t bin = get_bin(*(size_t*)((uint8_t*)block - SIZE_T_SIZE));
  block->next = bins[bin];
  block->prev = NULL;
  if (bins[bin] != NULL) {
    bins[bin]->prev = block;
  }
  bins[bin] = block;
  if (bin > largest_free_bin) {
    largest_free_bin = bin;
  }
}

// get_free_block - Get a block from the free list.
void* get_free_block(size_t size){
  free_list_t* current = NULL;
  // Finds block in smallest bin that can fit size
  for (size_t i = get_bin(size); i <= largest_free_bin; i++) {
    current = bins[i]; 
    while (current != NULL) {
      if (*(size_t*)((uint8_t*)current - SIZE_T_SIZE) >= size) {
        break;
      }
      current = current->next;
    }
    if (current != NULL) {
      break;
    }
  }

  if (current != NULL){
    remove_free_block(current);
    // If possible, split block and put extra memory back into free list
    size_t current_size = *(size_t*)((uint8_t*)current - SIZE_T_SIZE);
    if (size + 2*SIZE_T_SIZE + MIN_SIZE <= current_size) {
      // Leftover space (without header/footer).
      size_t extra_block_size = current_size - size - 2 * SIZE_T_SIZE;
      assert(extra_block_size % 8 == 0);
      free_list_t* extra_block = split_block((free_list_t*)current, size, extra_block_size);
      // Add new block to free list.
      add_free_block(extra_block);
    } else {
      size_t* current_footer = (size_t*)((uint8_t*)current + current_size);
      *current_footer = 0; 
    }
  }
  assert(size % 8 == 0);
  assert((((uintptr_t)current + (uintptr_t)((uint8_t*)current - SIZE_T_SIZE)) % 8) == 0);
  return (void*)current; 
}

// get_mem - Decide if we have memory at end of heap, or extend heap,
// then return memory.
void* get_mem(size_t size) {
  void* block = NULL;
  size_t alloc_size = size + 2*SIZE_T_SIZE;
  intptr_t needed_extra_mem = (intptr_t)(used_heap_end + alloc_size) - (intptr_t)mem_heap_hi();
  assert(needed_extra_mem % 8 == 0);
  if (needed_extra_mem > 0) {
    // Extend the heap by however much we need to fit.
    void* p = mem_sbrk((size_t)needed_extra_mem);
    if (p == (void*) -1) {
      // Some sort of error occurred,
      // let the client know by returning NULL.
      return NULL;
    }
  }

 // Set block header and footer.
  size_t* block_header = (size_t*)(used_heap_end + 1);
  block = (void*)((uint8_t*)block_header + SIZE_T_SIZE);
  size_t* block_footer = (size_t*)((uint8_t*)block + size);
  *block_header = size;
  *block_footer = 0;
  used_heap_end += size + 2*SIZE_T_SIZE;
  return block;
}

//  malloc - Allocate a block by incrementing the brk pointer.
//  Always allocate a block whose size is a multiple of the alignment.
void* my_malloc(size_t size) {
  if (TRACE) printf("[I] Allocating block of size: %zu\n", size);
  // Short-circuit for zero size.
  if (size == 0) return NULL;

  // Pointer to block we will return (including the header).
  void* p;

  // We allocate a little bit of extra memory so that we can store the
  // size of the block we've allocated.  Take a look at realloc to see
  // one example of a place where this can come in handy.
  // Make sure we have enough space to store the free list node at least.
  size_t aligned_size = ALIGN(size) > MIN_SIZE ? ALIGN(size) : MIN_SIZE;
  if (TRACE) printf("[I] Aligned size: %zu\n", aligned_size);
  p = get_free_block(aligned_size);
  if (p == NULL) {
    // No available free block, get more memory.
    p = get_mem(aligned_size);
  }
  assert(*(size_t*)((uint8_t*)p - SIZE_T_SIZE) % 8 == 0);
  return p;
}

free_list_t* coalesce(free_list_t* block) {
  size_t size = *(size_t*)((uint8_t*)block - SIZE_T_SIZE);
  size_t prev_size = *(size_t*)((uint8_t*)block - 2*SIZE_T_SIZE);
  size_t* header;
  size_t* footer;
  if (prev_size != 0) {
    // Can coalesce to the left (smaller addresses).
    free_list_t* prev_block = (free_list_t*)((uint8_t*)block - prev_size - 2*SIZE_T_SIZE);
    remove_free_block(prev_block);
    header = (size_t*)((uint8_t*)prev_block - SIZE_T_SIZE);
    footer = (size_t*)((uint8_t*)block + size);
    size = size + prev_size + 2*SIZE_T_SIZE;
    *header = size;
    *footer = *header;
    block = prev_block;
  } 
  if (((uintptr_t)((uint8_t*)block + size + SIZE_T_SIZE) < (uintptr_t)used_heap_end + 1)) {
    size_t next_size = *(size_t*)((uint8_t*)block + size + SIZE_T_SIZE);
    free_list_t* next_block = (free_list_t*)((uint8_t*)block + size + 2*SIZE_T_SIZE);
    size_t next_footer_size = *(size_t*)((uint8_t*)next_block + next_size);
    if (next_footer_size != 0) {
      // free_list_t* new_block = block;
      // Can coalesce to the right (higher addresses).
      assert((uintptr_t)((uint8_t*)block + size + SIZE_T_SIZE) <= (uintptr_t)((uint8_t*)used_heap_end + 1));
      assert(next_size == next_footer_size);                  
      remove_free_block(next_block);
      header = (size_t*)((uint8_t*)block - SIZE_T_SIZE);
      footer = (size_t*)((uint8_t*)next_block + next_size);
      *header = size + next_size + 2*SIZE_T_SIZE;
      *footer = *header;
    }
  }
  return block;
}

// free - Freeing a block.
void my_free(void* ptr) {
  if (TRACE) printf("[I] Freeing block\n");
  // Edge case - freeing NULL pointer doesn't do anything.
  if (ptr == NULL) {
    return;
  }

  size_t size = *(size_t*)((uint8_t*)ptr - SIZE_T_SIZE);
  assert((uintptr_t)((uint8_t*)ptr + size + SIZE_T_SIZE) <= (uintptr_t)(used_heap_end + 1));
  if ((uint8_t*)ptr + size + SIZE_T_SIZE == used_heap_end + 1) {
    // Block is the last one used, merge into program heap.
    used_heap_end -= size + 2*SIZE_T_SIZE;
    size_t prev_size = *(size_t*)(used_heap_end - SIZE_T_SIZE + 1);
    if (prev_size != 0){
      used_heap_end -= prev_size + 2*SIZE_T_SIZE;
      remove_free_block((free_list_t*)(used_heap_end + SIZE_T_SIZE + 1));
    }
  } else {
    // Add to free list.
    size_t* footer = (size_t*)((uint8_t*)ptr + size);
    *footer = size;
    add_free_block(coalesce((free_list_t*)ptr));
  }
  assert((uintptr_t)((uint8_t*)used_heap_end + 1) % 8 == 0);
  return;
}

void* simple_realloc(void* ptr, size_t old_size, size_t new_size) {
  // Allocate a new chunk of memory, and fail if that allocation fails.
  void* newptr = my_malloc(new_size);
  if (NULL == newptr) {
    return NULL;
  }

  size_t copy_size = old_size;
  // If the new block is smaller than the old one, we have to stop copying
  // early so that we don't write off the end of the new block of memory.
  if (new_size < old_size) {
    copy_size = new_size;
  }

  // This is a standard library call that performs a simple memory copy.
  memcpy(newptr, ptr, copy_size);

  my_free(ptr);

  return newptr;
}

// realloc - Implemented simply in terms of malloc and free
void* my_realloc(void* ptr, size_t size) {
  if (TRACE) printf("[I] Reallocating block to size: %zu\n", size);
  size_t old_size;

  // Edge case: realloc'ing NULL pointer is the same as malloc.
  if (ptr == NULL) {
    return my_malloc(size);
  }

  // Edge case: realloc'ing to size 0 is the same as freeing.
  if (size == 0) {
    my_free(ptr);
    return NULL;
  }

  // Get the size of the old block of memory stored in the SIZE_T_SIZE bytes
  // preceding the block itself.
  old_size = *(size_t*)((uint8_t*)ptr - SIZE_T_SIZE);

  if (size == old_size) {
    // Same size, just return original.
    return ptr;
  } else if (size > old_size) {
    // Size increase.
    // If block is at the end of heap, get more memory from there.
    if ((uint8_t*)ptr + old_size + SIZE_T_SIZE == used_heap_end + 1) {
      void* extra_ptr = get_mem(size - old_size - 2*SIZE_T_SIZE);
      if (extra_ptr == NULL) return NULL;
      size_t* header = (size_t*)((uint8_t*)ptr - SIZE_T_SIZE);
      *header = size;
      return ptr;
    }
    // If block has adjacent free block that is big enough, expand into it.
    size_t next_block_size = *(size_t*)((uint8_t*)ptr + old_size + SIZE_T_SIZE);
    free_list_t* next_block = (free_list_t*)((uint8_t*)ptr + old_size + 2*SIZE_T_SIZE);
    size_t next_block_footer = *(size_t*)((uint8_t*)next_block + next_block_size);
    if (next_block_footer != 0) {
      // Next block is free.
      if (old_size + next_block_size >= size + MIN_SIZE + 2*SIZE_T_SIZE) {
        // Next block can be split in two.
        remove_free_block(next_block);
        free_list_t* new_free_block = split_block(next_block, size - old_size - 2*SIZE_T_SIZE,
                                                  next_block_size - (size - old_size));
        add_free_block(new_free_block);
        size_t* header = (size_t*)((uint8_t*)ptr - SIZE_T_SIZE);
        size_t* footer = (size_t*)((uint8_t*)ptr + size);
        *header = size;
        *footer = 0;
        return ptr;
      } else if (next_block_footer != 0 && old_size + next_block_size + 2*SIZE_T_SIZE == size) {
        // Two blocks together match the new size exactly.
        remove_free_block(next_block);
        size_t* header = (size_t*)((uint8_t*)ptr - SIZE_T_SIZE);
        size_t* footer = (size_t*)((uint8_t*)ptr + size);
        *header = size;
        *footer = 0;
        return ptr;
      }
    }
  } else if (size + MIN_SIZE + 2*SIZE_T_SIZE <= old_size) {
    // Shrinking block, can free leftover.
    size_t* header = (size_t*)((uint8_t*)ptr - SIZE_T_SIZE);
    size_t* footer = (size_t*)((uint8_t*)ptr + size);
    size_t* extra_header = (size_t*)((uint8_t*)footer + SIZE_T_SIZE);
    free_list_t* extra_block = (free_list_t*)((uint8_t*)extra_header + SIZE_T_SIZE);
    size_t* extra_footer = (size_t*)((uint8_t*)ptr + old_size);
    *header = size;
    *footer = 0;
    *extra_header = old_size - size - 2*SIZE_T_SIZE;
    *extra_footer = *extra_header;
    my_free(extra_block);
    return ptr;
  }

  // Last resort.
  return simple_realloc(ptr, old_size, size);
}