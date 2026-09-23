#include <Arduino.h>
#include <driver/uart.h>
#include <esp_app_format.h>
#include <esp_ota_ops.h>
#include <esp_partition.h>
#include <esp_system.h>
#include <mbedtls/sha256.h>
#include <string.h>
#include "build_version.h"
#include "plantlink.h"
#include "plantlink_ota.h"

namespace {
constexpr uart_port_t kUart=UART_NUM_1;
constexpr uint32_t kMaxFirmware=0xEC000u;

struct FirmwareIdentityBlock {
  char marker[sizeof(ESP_PLANTS_H2_DISTRIBUTION_MARKER)];
  char build[sizeof(ESP_PLANTS_H2_BUILD_ID)];
};
__attribute__((used)) static const FirmwareIdentityBlock kFirmwareIdentity = {
  ESP_PLANTS_H2_DISTRIBUTION_MARKER,
  ESP_PLANTS_H2_BUILD_ID
};

uint16_t txSeq=0x8000;
struct OtaState{bool active=false;esp_ota_handle_t handle=0;const esp_partition_t*part=nullptr;uint32_t size=0,next=0;uint8_t expectedSha[32]{};char build[plantlink_ota::kBuildIdBytes]{};mbedtls_sha256_context sha;bool shaActive=false;bool buildSeen=false,markerSeen=false;char tail[160]{};size_t tailLen=0;} ota;

void rawSend(plantlink::MessageType type,const uint8_t*payload,uint16_t len,uint8_t flags=plantlink::FlagResponse){uint8_t encoded[plantlink::kMaxEncodedBytes]{};size_t n=plantlink::encodeFrame(type,flags,txSeq++,payload,len,encoded,sizeof(encoded));if(n)uart_write_bytes(kUart,encoded,n);}
void status(plantlink_ota::Status s,plantlink_ota::Error e,uint32_t next,uint16_t responseSeq=0){uint8_t p[6]{};p[0]=uint8_t(s);p[1]=uint8_t(e);plantlink::putU32LE(p+2,next);uint8_t encoded[plantlink::kMaxEncodedBytes]{};size_t n=plantlink::encodeFrame(plantlink::MessageType::H2OtaStatus,e==plantlink_ota::Error::None?plantlink::FlagResponse:uint8_t(plantlink::FlagResponse|plantlink::FlagError),responseSeq?responseSeq:txSeq++,p,sizeof(p),encoded,sizeof(encoded));if(n)uart_write_bytes(kUart,encoded,n);}
void resetOta(){if(ota.active&&ota.handle)esp_ota_abort(ota.handle);if(ota.shaActive)mbedtls_sha256_free(&ota.sha);ota=OtaState{};}
void observeIdentity(const uint8_t*d,size_t n){char combined[420]{};size_t keep=ota.tailLen;memcpy(combined,ota.tail,keep);size_t take=n; if(keep+take>sizeof(combined)-1)take=sizeof(combined)-1-keep;memcpy(combined+keep,d,take);combined[keep+take]=0;if(strstr(combined,ota.build))ota.buildSeen=true;if(strstr(combined,kFirmwareIdentity.marker))ota.markerSeen=true;size_t total=keep+take;ota.tailLen=total>sizeof(ota.tail)-1?sizeof(ota.tail)-1:total;memcpy(ota.tail,combined+total-ota.tailLen,ota.tailLen);ota.tail[ota.tailLen]=0;}
void sendPhase2Hello(uint16_t seq){rawSend(plantlink::MessageType::HelloAck,reinterpret_cast<const uint8_t*>(kFirmwareIdentity.build),strlen(kFirmwareIdentity.build),plantlink::FlagResponse);uint8_t hb[8]{};plantlink::putU32LE(hb,millis());plantlink::putU32LE(hb+4,plantlink::CapabilityZigbeeCoordinator|plantlink::CapabilityZg303zDecoder|plantlink::CapabilityRawZigbeeLog|plantlink::CapabilityInfrastructureRegistry|plantlink::CapabilityH2Ota);rawSend(plantlink::MessageType::Heartbeat,hb,sizeof(hb),plantlink::FlagResponse);(void)seq;}
void beginOta(const plantlink::Frame&f){if(f.payloadLength!=plantlink_ota::kBeginBytes){status(plantlink_ota::Status::Error,plantlink_ota::Error::Metadata,0,f.sequence);return;}if(ota.active){status(plantlink_ota::Status::Error,plantlink_ota::Error::Busy,ota.next,f.sequence);return;}uint8_t protocol=f.payload[0];uint32_t size=plantlink::getU32LE(f.payload+1);if(protocol!=plantlink::kProtocolVersion||size<65536||size>kMaxFirmware||f.payload[37+95]!=0){status(plantlink_ota::Status::Error,plantlink_ota::Error::Metadata,0,f.sequence);return;}const esp_partition_t*part=esp_ota_get_next_update_partition(nullptr);const esp_partition_t*running=esp_ota_get_running_partition();if(!part||part==running||size>part->size){status(plantlink_ota::Status::Error,plantlink_ota::Error::Partition,0,f.sequence);return;}ota=OtaState{};ota.part=part;ota.size=size;memcpy(ota.expectedSha,f.payload+5,32);memcpy(ota.build,f.payload+37,96);esp_err_t err=esp_ota_begin(part,size,&ota.handle);if(err!=ESP_OK){ota=OtaState{};status(plantlink_ota::Status::Error,plantlink_ota::Error::Partition,0,f.sequence);return;}mbedtls_sha256_init(&ota.sha);if(mbedtls_sha256_starts(&ota.sha,0)!=0){esp_ota_abort(ota.handle);ota=OtaState{};status(plantlink_ota::Status::Error,plantlink_ota::Error::Digest,0,f.sequence);return;}ota.shaActive=true;ota.active=true;status(plantlink_ota::Status::Ready,plantlink_ota::Error::None,0,f.sequence);}
void chunkOta(const plantlink::Frame&f){if(!ota.active||f.payloadLength<5){status(plantlink_ota::Status::Error,plantlink_ota::Error::Sequence,ota.next,f.sequence);return;}uint32_t off=plantlink::getU32LE(f.payload);size_t n=f.payloadLength-4;if(off<ota.next){status(plantlink_ota::Status::Receiving,plantlink_ota::Error::None,ota.next,f.sequence);return;}if(off!=ota.next||n>plantlink_ota::kChunkDataBytes||n>ota.size-ota.next){status(plantlink_ota::Status::Error,plantlink_ota::Error::Sequence,ota.next,f.sequence);return;}if(ota.next==0){if(n<sizeof(esp_image_header_t)){status(plantlink_ota::Status::Error,plantlink_ota::Error::Image,0,f.sequence);resetOta();return;}esp_image_header_t incoming{},running{};memcpy(&incoming,f.payload+4,sizeof(incoming));const esp_partition_t*rp=esp_ota_get_running_partition();if(!rp||esp_partition_read(rp,0,&running,sizeof(running))!=ESP_OK||incoming.magic!=ESP_IMAGE_HEADER_MAGIC||incoming.chip_id!=running.chip_id){status(plantlink_ota::Status::Error,plantlink_ota::Error::Image,0,f.sequence);resetOta();return;}}
if(mbedtls_sha256_update(&ota.sha,f.payload+4,n)!=0||esp_ota_write(ota.handle,f.payload+4,n)!=ESP_OK){status(plantlink_ota::Status::Error,plantlink_ota::Error::Write,ota.next,f.sequence);resetOta();return;}observeIdentity(f.payload+4,n);ota.next+=n;status(plantlink_ota::Status::Receiving,plantlink_ota::Error::None,ota.next,f.sequence);}
void endOta(const plantlink::Frame&f){if(!ota.active||ota.next!=ota.size){status(plantlink_ota::Status::Error,plantlink_ota::Error::End,ota.next,f.sequence);return;}uint8_t digest[32]{};if(mbedtls_sha256_finish(&ota.sha,digest)!=0){status(plantlink_ota::Status::Error,plantlink_ota::Error::Digest,ota.next,f.sequence);resetOta();return;}mbedtls_sha256_free(&ota.sha);ota.shaActive=false;if(memcmp(digest,ota.expectedSha,32)!=0){status(plantlink_ota::Status::Error,plantlink_ota::Error::Digest,ota.next,f.sequence);resetOta();return;}if(!ota.buildSeen||!ota.markerSeen){status(plantlink_ota::Status::Error,plantlink_ota::Error::Build,ota.next,f.sequence);resetOta();return;}esp_ota_handle_t h=ota.handle;const esp_partition_t*p=ota.part;ota.handle=0;ota.active=false;if(esp_ota_end(h)!=ESP_OK){status(plantlink_ota::Status::Error,plantlink_ota::Error::Image,ota.next,f.sequence);ota=OtaState{};return;}if(esp_ota_set_boot_partition(p)!=ESP_OK){status(plantlink_ota::Status::Error,plantlink_ota::Error::End,ota.next,f.sequence);ota=OtaState{};return;}status(plantlink_ota::Status::Verified,plantlink_ota::Error::None,ota.next,f.sequence);delay(75);status(plantlink_ota::Status::Rebooting,plantlink_ota::Error::None,ota.next);uart_wait_tx_done(kUart,pdMS_TO_TICKS(500));delay(250);esp_restart();}
void observer(const plantlink::Frame&f){switch(f.type){case plantlink::MessageType::Hello:sendPhase2Hello(f.sequence);break;case plantlink::MessageType::H2OtaBegin:beginOta(f);break;case plantlink::MessageType::H2OtaChunk:chunkOta(f);break;case plantlink::MessageType::H2OtaEnd:endOta(f);break;case plantlink::MessageType::H2OtaAbort:resetOta();status(plantlink_ota::Status::Aborted,plantlink_ota::Error::None,0,f.sequence);break;default:break;}}
struct Hook{Hook(){plantlink::setFrameObserver(observer);}} hook;
}
