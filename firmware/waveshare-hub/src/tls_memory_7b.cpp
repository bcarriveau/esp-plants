// ESP32-S3 7B TLS memory headroom shim.
//
// Arduino-ESP32 3.0.7 / ESP-IDF 5.1.x already builds mbedTLS with dynamic
// TX/RX buffers, but its prebuilt mbedTLS allocator defaults to internal DRAM.
// The 1024x600 7B display needs more internal DMA memory than the original 7",
// leaving GitHub HTTPS handshakes on the edge of MBEDTLS_ERR_SSL_ALLOC_FAILED.
//
// Keep small TLS allocations in internal RAM, but move large allocations to
// PSRAM on the 7B. The stock esp_mbedtls_mem_free() uses heap_caps_free(), so
// it can release either internal-RAM or PSRAM allocations safely.
//
// This does not alter certificate verification, HTTPS policy, OTA validation,
// RGB timing, framebuffer mode, or bounce-buffer size.

#include <esp_attr.h>
#include <esp_heap_caps.h>

#include <stddef.h>
#include <stdint.h>

#include "tls_memory_7b_policy.h"

#if defined(ESP_PLANTS_WAVESHARE_7B)

extern "C" void *__real_esp_mbedtls_mem_calloc(size_t n, size_t size);

extern "C" IRAM_ATTR void *__wrap_esp_mbedtls_mem_calloc(size_t n,
                                                          size_t size) {
  if (n == 0U || size == 0U) {
    return __real_esp_mbedtls_mem_calloc(n, size);
  }
  if (size > SIZE_MAX / n) return nullptr;

  const size_t bytes = n * size;
  if (bytes >= espplants_tls_memory::kPsramThresholdBytes) {
    void *external = heap_caps_calloc(
        n, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (external) return external;
  }

  // Preserve the framework's normal internal-DRAM allocation behavior for
  // smaller objects and as a bounded fallback if PSRAM is unavailable.
  return __real_esp_mbedtls_mem_calloc(n, size);
}

#endif  // ESP_PLANTS_WAVESHARE_7B
