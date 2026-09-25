#pragma once
#include <stddef.h>
#include <stdint.h>
#include "plantlink.h"

namespace plantlink_ota {

// Message IDs and capability flags are authoritative in plantlink.h.
// This header only defines the H2 OTA payload contract and OTA status values.
constexpr size_t kChunkDataBytes=248;
constexpr size_t kBeginBytes=133;
constexpr size_t kStatusBytes=6;
constexpr size_t kBuildIdBytes=96;
enum class Status:uint8_t{Ready=1,Receiving=2,Verified=3,Rebooting=4,Aborted=5,Error=0x80};
enum class Error:uint8_t{None=0,Busy,Metadata,Partition,Sequence,Write,Image,Digest,Build,End};

}  // namespace plantlink_ota
