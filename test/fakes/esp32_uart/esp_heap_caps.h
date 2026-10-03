// SPDX-License-Identifier: MIT
#pragma once
#include <cassert>
#include <cstdlib>
constexpr unsigned MALLOC_CAP_INTERNAL = 1, MALLOC_CAP_8BIT = 2, MALLOC_CAP_SPIRAM = 4;
inline void* heap_caps_malloc(std::size_t size, unsigned caps) {
    assert(caps == (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    return std::malloc(size);
}
inline unsigned heap_caps_get_free_size(unsigned) { return 100000; }
inline unsigned heap_caps_get_minimum_free_size(unsigned) { return 90000; }
inline unsigned heap_caps_get_largest_free_block(unsigned) { return 80000; }
