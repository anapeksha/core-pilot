#pragma once
#include "FreeRTOS.h"
#include <chrono>
#include <mutex>
#include <new>
struct StaticSemaphore_t { alignas(std::timed_mutex) unsigned char storage[sizeof(std::timed_mutex)]; };
using SemaphoreHandle_t = std::timed_mutex*;
inline SemaphoreHandle_t xSemaphoreCreateMutexStatic(StaticSemaphore_t* buffer) {
  return new (buffer->storage) std::timed_mutex;
}
inline int xSemaphoreTake(SemaphoreHandle_t mutex, TickType_t ticks) {
  return mutex->try_lock_for(std::chrono::milliseconds(ticks)) ? pdTRUE : 0;
}
inline void xSemaphoreGive(SemaphoreHandle_t mutex) { mutex->unlock(); }
inline void vSemaphoreDelete(SemaphoreHandle_t mutex) { mutex->~timed_mutex(); }
