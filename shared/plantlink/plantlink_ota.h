#pragma once
#include <stddef.h>
#include <stdint.h>
#include "plantlink.h"
namespace plantlink_ota {
constexpr uint8_t kBegin=0x50, kChunk=0x51, kEnd=0x52, kStatus=0x53, kAbort=0x54;
constexpr uint32_t kCapabilityH2Ota=1u<<4;
constexpr size_t kChunkDataBytes=248;
constexpr size_t kBeginBytes=133;
constexpr size_t kStatusBytes=6;
constexpr size_t kBuildIdBytes=96;
enum class Status:uint8_t{Ready=1,Receiving=2,Verified=3,Rebooting=4,Aborted=5,Error=0x80};
enum class Error:uint8_t{None=0,Busy,Metadata,Partition,Sequence,Write,Image,Digest,Build,End};
inline plantlink::MessageType type(uint8_t v){return static_cast<plantlink::MessageType>(v);}
}
