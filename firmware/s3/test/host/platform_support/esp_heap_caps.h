#pragma once
#include <cstdlib>
inline constexpr unsigned MALLOC_CAP_SPIRAM = 1, MALLOC_CAP_8BIT = 2, MALLOC_CAP_INTERNAL = 4;
inline void* heap_caps_malloc(std::size_t size, unsigned) { return std::malloc(size); }
inline void* heap_caps_calloc(std::size_t count, std::size_t size, unsigned) { return std::calloc(count, size); }
inline void heap_caps_free(void* ptr) { std::free(ptr); }
