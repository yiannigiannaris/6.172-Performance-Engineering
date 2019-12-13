/**
 * Copyright (c) 2012 MIT License by 6.172 Staff
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
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

// Implements the ADT specified in bitarray.h as a packed array of bits; a bit
// array containing bit_sz bits will consume roughly bit_sz/8 bytes of
// memory.


#include "./bitarray.h"

#include <assert.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>

#include <sys/types.h>


// ********************************* Types **********************************

// Concrete data type representing an array of bits.
struct bitarray {
  // The number of bits represented by this bit array.
  // Need not be divisible by 8.
  size_t bit_sz;

  // The underlying memory buffer that stores the bits in
  // packed form (8 per byte).
  char* buf;
};


// ******************** Prototypes for static functions *********************

// Rotates a subarray left by an arbitrary number of bits.
//
// bit_offset is the index of the start of the subarray
// bit_length is the length of the subarray, in bits
// bit_right_amount is the number of places to rotate the
//                    subarray left
//
// The subarray spans the half-open interval
// [bit_offset, bit_offset + bit_length)
// That is, the start is inclusive, but the end is exclusive.
// static void bitarray_rotate_left(bitarray_t* const bitarray,
//                                  const size_t bit_offset,
//                                  const size_t bit_length,
//                                  const size_t bit_right_amount);

// Rotates a subarray left by one bit.
//
// bit_offset is the index of the start of the subarray
// bit_length is the length of the subarray, in bits
//
// The subarray spans the half-open interval
// [bit_offset, bit_offset + bit_length)
// That is, the start is inclusive, but the end is exclusive.
// static void bitarray_rotate_left_one(bitarray_t* const bitarray,
//                                      const size_t bit_offset,
//                                      const size_t bit_length);

// Portable modulo operation that supports negative dividends.
//
// Many programming languages define modulo in a manner incompatible with its
// widely-accepted mathematical definition.
// http://stackoverflow.com/questions/1907565/c-python-different-behaviour-of-the-modulo-operation
// provides details; in particular, C's modulo
// operator (which the standard calls a "remainder" operator) yields a result
// signed identically to the dividend e.g., -1 % 10 yields -1.
// This is obviously unacceptable for a function which returns size_t, so we
// define our own.
//
// n is the dividend and m is the divisor
//
// Returns a positive integer r = n (mod m), in the range
// 0 <= r < m.
static size_t modulo(const ssize_t n, const size_t m);

// Produces a mask which, when ANDed with a byte, retains only the
// bit_index th byte.
//
// Example: bitmask(5) produces the byte 0b00100000.
//
// (Note that here the index is counted from right
// to left, which is different from how we represent bitarrays in the
// tests.  This function is only used by bitarray_get and bitarray_set,
// however, so as long as you always use bitarray_get and bitarray_set
// to access bits in your bitarray, this reverse representation should
// not matter.
static char bitmask(const size_t bit_index);

// Gets a uint64 pointer to a p bitarray. Start bit must be 8 byte aligned
static uint64_t* get_int_addr(bitarray_t* const bitarray, size_t offset);

// Moves the bitarray starting at old_location with num_words words into new_location
// while also shifting the array to the right by left_shift_amount
static void move_and_shift(uint64_t* old_location, uint64_t* new_location, 
                          ssize_t num_words, ssize_t left_shift_amount);


static size_t nearest_aligned_bit(bitarray_t* const bitarray, size_t index, bool right);


// ******************************* Functions ********************************

bitarray_t* bitarray_new(const size_t bit_sz) {
  // Allocate an underlying buffer of ceil(bit_sz/8) bytes.
  char* const buf = calloc(1, (bit_sz+7) / 8);
  if (buf == NULL) {
    return NULL;
  }

  // Allocate space for the struct.
  bitarray_t* const bitarray = malloc(sizeof(struct bitarray));
  if (bitarray == NULL) {
    free(buf);
    return NULL;
  }

  bitarray->buf = buf;
  bitarray->bit_sz = bit_sz;
  return bitarray;
}

void bitarray_free(bitarray_t* const bitarray) {
  if (bitarray == NULL) {
    return;
  }
  free(bitarray->buf);
  bitarray->buf = NULL;
  free(bitarray);
}

static void move_and_shift(uint64_t* old_location, uint64_t* new_location, 
                          ssize_t num_words, ssize_t left_shift_amount){
  uint64_t* end_of_new = new_location + num_words;
  if (left_shift_amount == 0){
    while(new_location < end_of_new){
      *(new_location) = *(old_location);
      new_location++;
      old_location++;
    }
  }
  else if(left_shift_amount > 0){
    uint64_t right_amount = 64-left_shift_amount;
    while(new_location < end_of_new - 1){
     *new_location = ((*old_location) >> left_shift_amount) + ((*(old_location + 1)) << right_amount);
      new_location++;
      old_location++;
    }
    *new_location = (*old_location) >> left_shift_amount;
  }
  else{
    size_t right_shift_amount = labs(left_shift_amount);
    size_t left_amount = 64-right_shift_amount;
    uint64_t leftover = 0;
    while(new_location < end_of_new){
      *new_location = ((*old_location) << right_shift_amount) + leftover;
      leftover = (*old_location) >> left_amount;
      new_location++;
      old_location++;
    }
    *new_location = leftover;
  }
}

void bitarray_rotate(bitarray_t* const bitarray,
                     const size_t bit_offset,
                     const size_t bit_length,
                     const ssize_t bit_right_amount) {
  assert(bit_offset + bit_length <= bitarray->bit_sz);
  if (bit_length == 0) {
    return;
  }
  size_t split_index = modulo(-bit_right_amount, bit_length);
  // printf("\n");
  // printf("SPLIT INDEX: %zu \n", split_index);
  // printf("\n");
  if (split_index == 0 || split_index == bit_length){
    return;
  }
  size_t chunk_a_start = nearest_aligned_bit(bitarray, bit_offset, true);
  size_t chunk_a_end = nearest_aligned_bit(bitarray, bit_offset + split_index, false);
  size_t chunk_b_start = nearest_aligned_bit(bitarray, bit_offset + split_index, true);
  size_t chunk_b_end = nearest_aligned_bit(bitarray, bit_offset + bit_length, false);
  // printf("\n");
  // printf("CHUNK_A_START: %zu \n", chunk_a_start);
  // printf("\n");
  // printf("CHUNK_A_END: %zu \n", chunk_a_end);
  // printf("\n");
  // printf("CHUNK_B_START: %zu \n", chunk_b_start);
  // printf("\n");
  // printf("CHUNK_B_END: %zu \n", chunk_b_end);
  // printf("\n");
  size_t left_size = split_index;
  size_t right_size = bit_length - left_size;
  size_t edge_lengths[4] = {128, 128, 128, 128};
  if (left_size < 128){
    edge_lengths[0] = left_size;
    edge_lengths[1] = left_size;
  }
  size_t b_edge_lengths = 128;
  if (right_size < 128){
    edge_lengths[2] = right_size;
    edge_lengths[3] = right_size;
  }
  size_t edge_starts[4] = {bit_offset, bit_offset + split_index - edge_lengths[1], bit_offset + split_index, bit_offset + bit_length - edge_lengths[3]};
  // printf("E0 start: %zu\n", edge_starts[0]);
  // printf("E1 start: %zu\n", edge_starts[1]);
  // printf("E2 start: %zu\n", edge_starts[2]);
  // printf("E3 start: %zu\n", edge_starts[3]);
  bool edges[4][128];
  for (int i = 0; i < 4; i++){
    for (int j = 0; j < edge_lengths[i]; j++){
      edges[i][j] = bitarray_get(bitarray, edge_starts[i] + j);
    }
  }
  ssize_t chunk_a_length = chunk_a_end - chunk_a_start;
  ssize_t chunk_b_length = chunk_b_end - chunk_b_start;
  uint64_t* chunk_a = get_int_addr(bitarray, chunk_a_start);
  uint64_t* chunk_b = get_int_addr(bitarray, chunk_b_start);
  ssize_t words_in_a = chunk_a_length / 64;
  ssize_t words_in_b = chunk_b_length / 64;
  ssize_t a_front_overhang = chunk_a_start - bit_offset;
  ssize_t a_end_overhang = split_index + bit_offset - chunk_a_end;
  ssize_t b_front_overhang = chunk_b_start - (split_index + bit_offset);
  ssize_t b_end_overhang = bit_offset + bit_length - chunk_b_end;
  uint64_t* temp = NULL;
  if (chunk_a_length >= 64 && chunk_b_length >= 64){
    temp  = malloc(sizeof(uint64_t) * (words_in_a)+1);       
    move_and_shift(chunk_a, temp, words_in_a, -(b_end_overhang - a_end_overhang));
    move_and_shift(chunk_b, chunk_a, words_in_b, -(b_front_overhang - a_front_overhang));
    move_and_shift(temp, chunk_b + (words_in_b - words_in_a), words_in_a, 0);
  }
  else if (chunk_a_length >= 64 && chunk_b_length < 64){
    temp  = malloc(sizeof(uint64_t) * (words_in_a)+1);
    move_and_shift(chunk_a, temp, words_in_a, -(b_end_overhang - a_end_overhang));
    // for (uint64_t* i = temp + words_in_a-1; i >= temp; i--){
    //   for (int j = 63; j >= 0; j--){
    //     uint64_t bit = ((*i) >> j) << 63;
    //     printf("%c", bit & 0b1111111111111111111111111111111111111111111111111111111111111111 ? '1':'0');
    //   }
    //   printf("\n");
    // }
    // printf("AFTER\n");
    move_and_shift(temp, chunk_b + (words_in_b - words_in_a), words_in_a, 0);
  }
  else if (chunk_a_length < 64 && chunk_b_length >= 64){
    temp  = malloc(sizeof(uint64_t) * (words_in_b)+1);
    move_and_shift(chunk_b, temp, words_in_b, -(b_front_overhang - a_front_overhang));
    move_and_shift(temp, chunk_a, words_in_b, 0); // Check to make sure correct
  }
  free(temp);
  size_t new_edge_starts[4] = {bit_offset+bit_length-split_index, 
                              bit_offset+bit_length-edge_lengths[1],
                              bit_offset,
                              bit_offset+bit_length-split_index-edge_lengths[3]};
  for (int k = 0; k < 4; k++){
    size_t new_start = new_edge_starts[k];
    for (int h = 0; h < edge_lengths[k]; h++){
      bitarray_set(bitarray, new_start+h, edges[k][h]);
    }
  }
}

static size_t nearest_aligned_bit(bitarray_t* const bitarray, size_t index, bool right){
  uint64_t bitarray_start = (uint64_t) (bitarray->buf);
  if (right){
    while(true){
      if (index % 8 == 0 && ((bitarray_start + index/8) % 8 == 0)){
        return index;
      }
      index++;
    }
  }
  else{
    while(true){
      if (index % 8 == 0 && ((bitarray_start + index/8) % 8 == 0)){
        return index;
      }
      index--;
    }
  }
}


static uint64_t* get_int_addr(bitarray_t* const bitarray, size_t offset){
  return (uint64_t*)(((uint64_t)bitarray->buf) + offset / 8);
}

size_t bitarray_get_bit_sz(const bitarray_t* const bitarray) {
  return bitarray->bit_sz;
}

bool bitarray_get(const bitarray_t* const bitarray, const size_t bit_index) {
  assert(bit_index < bitarray->bit_sz);

  // We're storing bits in packed form, 8 per byte.  So to get the nth
  // bit, we want to look at the (n mod 8)th bit of the (floor(n/8)th)
  // byte.
  //
  // In C, integer division is floored explicitly, so we can just do it to
  // get the byte; we then bitwise-and the byte with an appropriate mask
  // to produce either a zero byte (if the bit was 0) or a nonzero byte
  // (if it wasn't).  Finally, we convert that to a boolean.
  return (bitarray->buf[bit_index >> 3] & bitmask(bit_index)) ?
         true : false;
}

void bitarray_set(bitarray_t* const bitarray,
                  const size_t bit_index,
                  const bool value) {
  assert(bit_index < bitarray->bit_sz);

  // We're storing bits in packed form, 8 per byte.  So to set the nth
  // bit, we want to set the (n mod 8)th bit of the (floor(n/8)th) byte.
  //
  // In C, integer division is floored explicitly, so we can just do it to
  // get the byte; we then bitwise-and the byte with an appropriate mask
  // to clear out the bit we're about to set.  We bitwise-or the result
  // with a byte that has either a 1 or a 0 in the correct place.
  bitarray->buf[bit_index >> 3] =
    (bitarray->buf[bit_index >> 3] & ~bitmask(bit_index)) |
    (value ? bitmask(bit_index) : 0);
}

void bitarray_randfill(bitarray_t* const bitarray){
  // Do big fills
  int64_t *wptr = (int64_t *)bitarray->buf;
  for (int64_t i = 0; i < bitarray->bit_sz/64; i++){
    wptr[i] = (int64_t)&wptr[i];
  }

  // Fill remaining bytes
  int8_t *bptr = (int8_t *)bitarray->buf;
  int64_t filled_bytes = bitarray->bit_sz/64*8;
  int64_t total_bytes = (bitarray->bit_sz+7)/8;
  for (int64_t j = filled_bytes; j < total_bytes; j++){
    bptr[j] = (int8_t)&bptr[j]; // Truncate
  }
}

void bitarray_small_rotate(bitarray_t* const bitarray,
                     const size_t bit_offset,
                     const size_t bit_length,
                     const ssize_t bit_right_amount) {
  assert(bit_offset + bit_length <= bitarray->bit_sz);
  if (bit_length == 0) {
    return;
  }

  size_t right_shift_amount = modulo(-bit_right_amount, bit_length);
  bitarray_reverse(bitarray, bit_offset, right_shift_amount);
  bitarray_reverse(bitarray, bit_offset + right_shift_amount, bit_length - right_shift_amount);
  bitarray_reverse(bitarray, bit_offset, bit_length);
}

void bitarray_reverse(bitarray_t* const bitarray,
                     const size_t bit_offset,
                     const size_t bit_length) {
  bool temp;
  int middle = bit_offset + (bit_length >> 1);
  for (size_t i = bit_offset; i < middle; i++){
    temp = bitarray_get(bitarray, i);
    bitarray_set(bitarray, i, bitarray_get(bitarray, bit_offset + bit_length - 1 - (i - bit_offset)));
    bitarray_set(bitarray, bit_offset + bit_length - 1 - (i- bit_offset), temp);
  }
}

// void bitarray_rotate(bitarray_t* const bitarray,
//                      const size_t bit_offset,
//                      const size_t bit_length,
//                      const ssize_t bit_right_amount) {
//   assert(bit_offset + bit_length <= bitarray->bit_sz);

//   if (bit_length == 0) {
//     return;
//   }

//   // Convert a rotate left or right to a left rotate only, and eliminate
//   // multiple full rotations.
//   bitarray_rotate_left(bitarray, bit_offset, bit_length,
//                        modulo(-bit_right_amount, bit_length));
// }


// static void bitarray_rotate_left(bitarray_t* const bitarray,
//                                  const size_t bit_offset,
//                                  const size_t bit_length,
//                                  const size_t bit_right_amount) {
//   for (size_t i = 0; i < bit_right_amount; i++) {
//     bitarray_rotate_left_one(bitarray, bit_offset, bit_length);
//   }
// }

// static void bitarray_rotate_left_one(bitarray_t* const bitarray,
//                                      const size_t bit_offset,
//                                      const size_t bit_length) {
//   // Grab the first bit in the range, shift everything left by one, and
//   // then stick the first bit at the end.
//   const bool first_bit = bitarray_get(bitarray, bit_offset);
//   size_t i;
//   for (i = bit_offset; i + 1 < bit_offset + bit_length; i++) {
//     bitarray_set(bitarray, i, bitarray_get(bitarray, i + 1));
//   }
//   bitarray_set(bitarray, i, first_bit);
// }

static size_t modulo(const ssize_t n, const size_t m) {
  const ssize_t signed_m = (ssize_t)m;
  assert(signed_m > 0);
  const ssize_t result = ((n % signed_m) + signed_m) % signed_m;
  assert(result >= 0);
  return (size_t)result;
}

static char bitmask(const size_t bit_index) {
  return 1 << (bit_index & 7);
}

