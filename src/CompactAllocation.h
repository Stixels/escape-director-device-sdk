// Copyright 2026 Stixels
// SPDX-License-Identifier: MIT

#pragma once
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

namespace ed {
namespace detail {

// Preserve malloc's alignment after our size prefix, including double/uint64_t
// allocations on 32-bit ARM. The payload starts immediately after this header.
struct alignas(max_align_t) AllocationHeader {
  size_t size;
};
static_assert(sizeof(AllocationHeader) % alignof(max_align_t) == 0,
              "Allocation payload must retain fundamental alignment");

inline void *compactAllocate(size_t size) {
  if (size > SIZE_MAX - sizeof(AllocationHeader))
    return nullptr;
  auto *block = static_cast<AllocationHeader *>(malloc(sizeof(AllocationHeader) + size));
  if (!block)
    return nullptr;
  block->size = size;
  return block + 1;
}

inline void compactFree(void *pointer) {
  if (pointer)
    free(static_cast<AllocationHeader *>(pointer) - 1);
}

inline void *compactReallocate(void *pointer, size_t size) {
  if (!pointer)
    return compactAllocate(size);
  if (size > SIZE_MAX - sizeof(AllocationHeader))
    return nullptr;
  auto *block = static_cast<AllocationHeader *>(pointer) - 1;
  if (size < block->size) {
    // newlib-nano does not reclaim space when realloc shrinks a block.
    void *smaller = compactAllocate(size);
    if (!smaller)
      return pointer; // The original allocation remains usable.
    memcpy(smaller, pointer, size);
    compactFree(pointer);
    return smaller;
  }
  block = static_cast<AllocationHeader *>(realloc(block, sizeof(AllocationHeader) + size));
  if (!block)
    return nullptr;
  block->size = size;
  return block + 1;
}

} // namespace detail
} // namespace ed
