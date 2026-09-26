#include <Arduino.h>
#include <driver/uart.h>
#include <esp_app_format.h>
#include <esp_ota_ops.h>
#include <esp_partition.h>
#include <esp_system.h>
#include <mbedtls/sha256.h>
#include <string.h>

#include "build_version.h"
#include "h2_ota_receiver.h"
#include "plantlink_ota.h"

namespace {

constexpr uart_port_t kUart=UART_NUM_1;
constexpr uint32_t kMaxFirmware=0xE0000u;
constexpr uint32_t kOtaAuthorizationWindowMs=5000u;
constexpr char kExpectedDistributionMarker[]=ESP_PLANTS_H2_RELEASE_MARKER;

// Keep a single contiguous identity record in the loadable image.  The release
// packager scans firmware.bin itself (not the ELF symbol table), so both exact
// ASCII tokens must survive compiler/linker garbage collection.  Make the
// alpha.23 compatibility bridge literal explicit here as a belt-and-suspenders
// guard: the bridge environment must report alpha.23 even while the normal H2
// release target remains independently versioned at alpha.25.
#if defined(ESP_PLANTS_DISTRIBUTION_BUILD)
  #if defined(ESP_PLANTS_H2_COMPAT_BRIDGE_ALPHA23)
    #define ESP_PLANTS_H2_BINARY_BUILD_ID "ESPPLANTS-H2-0.2.0-alpha.23"
  #else
    #define ESP_PLANTS_H2_BINARY_BUILD_ID ESP_PLANTS_H2_BUILD_ID
  #endif
  #define ESP_PLANTS_H2_BINARY_MARKER "ESP-PLANTS-H2-DISTRIBUTION-BUILD"
#else
  #define ESP_PLANTS_H2_BINARY_BUILD_ID ESP_PLANTS_H2_BUILD_ID
  #define ESP_PLANTS_H2_BINARY_MARKER "ESP-PLANTS-H2-DEVELOPMENT-BUILD"
#endif

__attribute__((used, retain)) static const char kFirmwareIdentity[] =
    ESP_PLANTS_H2_BINARY_MARKER "\0" ESP_PLANTS_H2_BINARY_BUILD_ID;

#if defined(ESP_PLANTS_H2_COMPAT_BRIDGE_ALPHA23)
static_assert(sizeof(ESP_PLANTS_H2_BINARY_BUILD_ID) ==
                  sizeof("ESPPLANTS-H2-0.2.0-alpha.23"),
              "alpha.23 compatibility bridge build ID changed unexpectedly");
#endif

uint16_t txSeq=0x8000;
uint32_t otaAuthorizedUntilMs=0;
uint16_t authorizedBeginSequence=0;
bool otaAuthorizationArmed=false;
struct OtaState{bool active=false;esp_ota_handle_t handle=0;const esp_partition_t*part=nullptr;uint32_t size=0,next=0;uint8_t expectedSha[32]{};char build[plantlink_ota::kBuildIdBytes]{};mbedtls_sha256_context sha;bool shaActive=false;bool buildSeen=false,markerSeen=false;uint8_t tail[160]{};size_t tailLen=0;} ota;

void status(plantlink_ota::Status s,plantlink_ota::Error e,uint32_t next,uint16_t responseSeq=0){uint8_t p[6]{};p[0]=uint8_t(s);p[1]=uint8_t(e);plantlink::putU32LE(p+2,next);uint8_t encoded[plantlink::kMaxEncodedBytes]{};size_t n=plantlink::encodeFrame(plantlink::MessageType::H2OtaStatus,e==plantlink_ota::Error::None?plantlink::FlagResponse:uint8_t(plantlink::FlagResponse|plantlink::FlagError),responseSeq?responseSeq:txSeq++,p,sizeof(p),encoded,sizeof(encoded));if(n)uart_write_bytes(kUart,encoded,n);}
void resetOta(){if(ota.active&&ota.handle)esp_ota_abort(ota.handle);if(ota.shaActive)mbedtls_sha256_free(&ota.sha);ota=OtaState{};}
bool containsBytes(const uint8_t*data,size_t length,const char*needle){if(!data||!needle)return false;const size_t needleLength=strlen(needle);if(!needleLength||needleLength>length)return false;for(size_t offset=0;offset+needleLength<=length;++offset){if(memcmp(data+offset,needle,needleLength)==0)return true;}return false;}
void observeIdentity(const uint8_t*d,size_t n){uint8_t combined[sizeof(ota.tail)+plantlink_ota::kChunkDataBytes]{};const size_t keep=ota.tailLen;if(keep)memcpy(combined,ota.tail,keep);if(n)memcpy(combined+keep,d,n);const size_t total=keep+n;if(!ota.buildSeen&&containsBytes(combined,total,ota.build))ota.buildSeen=true;if(!ota.markerSeen&&containsBytes(combined,total,kExpectedDistributionMarker))ota.markerSeen=true;ota.tailLen=total>sizeof(ota.tail)?sizeof(ota.tail):total;if(ota.tailLen)memcpy(ota.tail,combined+total-ota.tailLen,ota.tailLen);}
bool consumeOtaAuthorization(const plantlink::Frame&f){if(!otaAuthorizationArmed)return false;const uint32_t now=millis();const bool fresh=static_cast<int32_t>(otaAuthorizedUntilMs-now)>0;const bool sequenceMatches=f.sequence==authorizedBeginSequence;otaAuthorizationArmed=false;otaAuthorizedUntilMs=0;authorizedBeginSequence=0;return fresh&&sequenceMatches;}
void beginOta(const plantlink::Frame&f){if(!consumeOtaAuthorization(f)){status(plantlink_ota::Status::Error,plantlink_ota::Error::Unauthorized,0,f.sequence);return;}if(f.payloadLength!=plantlink_ota::kBeginBytes){status(plantlink_ota::Status::Error,plantlink_ota::Error::Metadata,0,f.sequence);return;}if(ota.active){status(plantlink_ota::Status::Error,plantlink_ota::Error::Busy,ota.next,f.sequence);return;}uint8_t protocol=f.payload[0];uint32_t size=plantlink::getU32LE(f.payload+1);if(protocol!=plantlink::kProtocolVersion||size<65536||size>kMaxFirmware||f.payload[37+95]!=0){status(plantlink_ota::Status::Error,plantlink_ota::Error::Metadata,0,f.sequence);return;}const esp_partition_t*part=esp_ota_get_next_update_partition(nullptr);const esp_partition_t*running=esp_ota_get_running_partition();if(!part||part==running||size>part->size){status(plantlink_ota::Status::Error,plantlink_ota::Error::Partition,0,f.sequence);return;}ota=OtaState{};ota.part=part;ota.size=size;memcpy(ota.expectedSha,f.payload+5,32);memcpy(ota.build,f.payload+37,96);esp_err_t err=esp_ota_begin(part,size,&ota.handle);if(err!=ESP_OK){ota=OtaState{};status(plantlink_ota::Status::Error,plantlink_ota::Error::Partition,0,f.sequence);return;}mbedtls_sha256_init(&ota.sha);if(mbedtls_sha256_starts(&ota.sha,0)!=0){esp_ota_abort(ota.handle);ota=OtaState{};status(plantlink_ota::Status::Error,plantlink_ota::Error::Digest,0,f.sequence);return;}ota.shaActive=true;ota.active=true;status(plantlink_ota::Status::Ready,plantlink_ota::Error::None,0,f.sequence);}
void chunkOta(const plantlink::Frame&f){if(!ota.active||f.payloadLength<5){status(plantlink_ota::Status::Error,plantlink_ota::Error::Sequence,ota.next,f.sequence);return;}uint32_t off=plantlink::getU32LE(f.payload);size_t n=f.payloadLength-4;if(off<ota.next){status(plantlink_ota::Status::Receiving,plantlink_ota::Error::None,ota.next,f.sequence);return;}if(off!=ota.next||n>plantlink_ota::kChunkDataBytes||n>ota.size-ota.next){status(plantlink_ota::Status::Error,plantlink_ota::Error::Sequence,ota.next,f.sequence);return;}if(ota.next==0){if(n<sizeof(esp_image_header_t)){status(plantlink_ota::Status::Error,plantlink_ota::Error::Image,0,f.sequence);resetOta();return;}esp_image_header_t incoming{},running{};memcpy(&incoming,f.payload+4,sizeof(incoming));const esp_partition_t*rp=esp_ota_get_running_partition();if(!rp||esp_partition_read(rp,0,&running,sizeof(running))!=ESP_OK||incoming.magic!=ESP_IMAGE_HEADER_MAGIC||incoming.chip_id!=running.chip_id){status(plantlink_ota::Status::Error,plantlink_ota::Error::Image,0,f.sequence);resetOta();return;}}
if(mbedtls_sha256_update(&ota.sha,f.payload+4,n)!=0||esp_ota_write(ota.handle,f.payload+4,n)!=ESP_OK){status(plantlink_ota::Status::Error,plantlink_ota::Error::Write,ota.next,f.sequence);resetOta();return;}observeIdentity(f.payload+4,n);ota.next+=n;status(plantlink_ota::Status::Receiving,plantlink_ota::Error::None,ota.next,f.sequence);}
void endOta(const plantlink::Frame&f){if(!ota.active||ota.next!=ota.size){status(plantlink_ota::Status::Error,plantlink_ota::Error::End,ota.next,f.sequence);return;}uint8_t digest[32]{};if(mbedtls_sha256_finish(&ota.sha,digest)!=0){status(plantlink_ota::Status::Error,plantlink_ota::Error::Digest,ota.next,f.sequence);resetOta();return;}mbedtls_sha256_free(&ota.sha);ota.shaActive=false;if(memcmp(digest,ota.expectedSha,32)!=0){status(plantlink_ota::Status::Error,plantlink_ota::Error::Digest,ota.next,f.sequence);resetOta();return;}if(!ota.buildSeen||!ota.markerSeen){status(plantlink_ota::Status::Error,plantlink_ota::Error::Build,ota.next,f.sequence);resetOta();return;}esp_ota_handle_t h=ota.handle;const esp_partition_t*p=ota.part;ota.handle=0;ota.active=false;if(esp_ota_end(h)!=ESP_OK){status(plantlink_ota::Status::Error,plantlink_ota::Error::Image,ota.next,f.sequence);ota=OtaState{};return;}if(esp_ota_set_boot_partition(p)!=ESP_OK){status(plantlink_ota::Status::Error,plantlink_ota::Error::End,ota.next,f.sequence);ota=OtaState{};return;}status(plantlink_ota::Status::Verified,plantlink_ota::Error::None,ota.next,f.sequence);delay(75);status(plantlink_ota::Status::Rebooting,plantlink_ota::Error::None,ota.next);uart_wait_tx_done(kUart,pdMS_TO_TICKS(500));delay(250);esp_restart();}

}  // namespace

namespace espplants_h2_ota {

void noteControllerHello(const plantlink::Frame&f){
  if(ota.active)return;
  const bool explicitIntent=
      f.payloadLength==8 &&
      plantlink::getU32LE(f.payload+4)==plantlink_ota::kAuthorizeIntentMagic;
  if(!explicitIntent){
    otaAuthorizationArmed=false;
    otaAuthorizedUntilMs=0;
    authorizedBeginSequence=0;
    return;
  }
  authorizedBeginSequence=static_cast<uint16_t>(f.sequence+1u);
  otaAuthorizedUntilMs=millis()+kOtaAuthorizationWindowMs;
  otaAuthorizationArmed=true;
}

bool handlePlantLinkFrame(const plantlink::Frame&f){
  switch(f.type){
    case plantlink::MessageType::H2OtaBegin:
      beginOta(f);
      return true;
    case plantlink::MessageType::H2OtaChunk:
      chunkOta(f);
      return true;
    case plantlink::MessageType::H2OtaEnd:
      endOta(f);
      return true;
    case plantlink::MessageType::H2OtaAbort:
      if(ota.active){
        resetOta();
        status(plantlink_ota::Status::Aborted,plantlink_ota::Error::None,0,f.sequence);
      }else{
        status(plantlink_ota::Status::Error,plantlink_ota::Error::Unauthorized,0,f.sequence);
      }
      return true;
    default:
      return false;
  }
}

}  // namespace espplants_h2_ota
