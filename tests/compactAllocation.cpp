// Copyright 2026 Stixels
// SPDX-License-Identifier: Apache-2.0

#include <assert.h>
#include <stdint.h>
#include <initializer_list>
#include "CompactAllocation.h"

int main() {
  using namespace ed::detail;
  for (size_t size : {size_t(1), size_t(8), size_t(127), size_t(1024)}) {
    void *p = compactAllocate(size);
    assert(p && uintptr_t(p) % alignof(max_align_t) == 0);
    memset(p, 0x5a, size);
    p = compactReallocate(p, size * 2);
    assert(p && uintptr_t(p) % alignof(max_align_t) == 0);
    for (size_t i = 0; i < size; ++i)
      assert(static_cast<unsigned char *>(p)[i] == 0x5a);
    void *original = p;
    p = compactReallocate(p, size);
    assert(p && p != original && uintptr_t(p) % alignof(max_align_t) == 0);
    for (size_t i = 0; i < size; ++i)
      assert(static_cast<unsigned char *>(p)[i] == 0x5a);
    // An impossible growth fails without discarding the original allocation.
    assert(!compactReallocate(p, SIZE_MAX));
    assert(static_cast<unsigned char *>(p)[0] == 0x5a);
    compactFree(p);
  }
  assert(!compactAllocate(SIZE_MAX));
  compactFree(nullptr);
  void *p = compactReallocate(nullptr, sizeof(double));
  assert(p);
  *static_cast<double *>(p) = 123.5;
  assert(*static_cast<double *>(p) == 123.5);
  compactFree(p);
}
