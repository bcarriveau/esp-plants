#pragma once

#include <stddef.h>

namespace espplants_tls_memory {

// Keep this single source of truth shared by the 7B mbedTLS allocator and the
// OTA preflight. Allocations at or above this size are routed to PSRAM on 7B.
constexpr size_t kPsramThresholdBytes = 4U * 1024U;

}  // namespace espplants_tls_memory
