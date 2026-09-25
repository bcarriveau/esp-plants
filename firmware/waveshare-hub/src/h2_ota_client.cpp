#include "h2_ota_client.h"
#include <Arduino.h>
#include <WiFi.h>
#include <esp_crt_bundle.h>
#include <esp_heap_caps.h>
#include <esp_http_client.h>
#include <mbedtls/sha256.h>
#include <algorithm>
#include <cstring>
#include <strings.h>
#include "plantlink.h"
#include "plantlink_ota.h"
#include "update_policy.h"
#include "../../m5-h2-zigbee/include/build_version.h"

namespace espplants_h2_ota { namespace {
constexpr char kReleasePrefix[]="https://github.com/bcarriveau/esp-plants/releases/download/";
constexpr char kH2Prefix[]="ESPPLANTS-H2-";
constexpr char kExpectedH2Build[]=ESP_PLANTS_H2_BUILD_ID;
constexpr char kExpectedH2Version[]=ESP_PLANTS_H2_VERSION;
constexpr size_t kMaxH2Bytes=0xE0000u;
constexpr uint32_t kAckTimeoutMs=2500, kRebootTimeoutMs=15000;
volatile uint8_t lastStatus=0,lastError=0;volatile uint32_t nextOffset=0;volatile uint32_t eventCounter=0;char lastHello[96]{};portMUX_TYPE mux=portMUX_INITIALIZER_UNLOCKED;uint16_t sequence=0x5000;
void copyText(char*d,size_t c,const char*s){if(d&&c)snprintf(d,c,"%s",s?s:"");}
void observer(const plantlink::Frame&f){portENTER_CRITICAL(&mux);if(f.type==plantlink::MessageType::H2OtaStatus&&f.payloadLength==6){lastStatus=f.payload[0];lastError=f.payload[1];nextOffset=plantlink::getU32LE(f.payload+2);++eventCounter;}else if(f.type==plantlink::MessageType::HelloAck){size_t n=std::min<size_t>(f.payloadLength,sizeof(lastHello)-1);memcpy(lastHello,f.payload,n);lastHello[n]=0;++eventCounter;}portEXIT_CRITICAL(&mux);}
void sendFrame(plantlink::MessageType t,const uint8_t*p,uint16_t n){uint8_t e[plantlink::kMaxEncodedBytes]{};size_t len=plantlink::encodeFrame(t,plantlink::FlagNone,sequence++,p,n,e,sizeof(e));if(len)Serial0.write(e,len);}
bool waitStatus(uint32_t before,uint32_t timeout,uint8_t &s,uint8_t&e,uint32_t&next){uint32_t start=millis();while(millis()-start<timeout){portENTER_CRITICAL(&mux);uint32_t now=eventCounter;s=lastStatus;e=lastError;next=nextOffset;portEXIT_CRITICAL(&mux);if(now!=before&&s)return true;delay(2);}return false;}
uint32_t counter(){portENTER_CRITICAL(&mux);uint32_t c=eventCounter;portEXIT_CRITICAL(&mux);return c;}
bool allowedUrl(const char*u){char host[96]{};return espplants_update_policy::parseAllowedHttpsUrl(u,host,sizeof(host));}

struct AssetHeaderState{
  size_t totalBytes=0;
  bool invalid=false;
  char location[espplants_update_policy::kMaxRedirectUrlLength+1]{};
};

esp_err_t assetHeaderEvent(esp_http_client_event_t *event){
  if(!event||!event->user_data)return ESP_OK;
  AssetHeaderState &state=*static_cast<AssetHeaderState*>(event->user_data);
  if(event->event_id!=HTTP_EVENT_ON_HEADER||!event->header_key||!event->header_value)
    return state.invalid?ESP_FAIL:ESP_OK;
  size_t updated=state.totalBytes;
  if(!espplants_update_policy::accumulateHeaderBytes(
         state.totalBytes,strlen(event->header_key),strlen(event->header_value),updated)){
    state.invalid=true;
    return ESP_FAIL;
  }
  state.totalBytes=updated;
  if(strcasecmp(event->header_key,"Location")==0){
    const char *value=event->header_value;
    while(*value==' '||*value=='\t')++value;
    if(!espplants_update_policy::redirectUrlLengthValid(value)){
      state.invalid=true;
      return ESP_FAIL;
    }
    copyText(state.location,sizeof(state.location),value);
  }
  return ESP_OK;
}

bool getAsset(const char*url,uint8_t*dst,size_t cap,size_t&out,char*msg,size_t mc){
  char current[espplants_update_policy::kMaxRedirectUrlLength+1]{};
  copyText(current,sizeof(current),url);
  for(int redirects=0;redirects<=3;++redirects){
    if(!allowedUrl(current)){copyText(msg,mc,"H2 release URL was rejected");return false;}

    AssetHeaderState headers{};
    esp_http_client_config_t cfg{};
    cfg.url=current;
    cfg.method=HTTP_METHOD_GET;
    cfg.timeout_ms=12000;
    cfg.disable_auto_redirect=true;
    cfg.max_redirection_count=0;
    cfg.transport_type=HTTP_TRANSPORT_OVER_SSL;
    cfg.crt_bundle_attach=esp_crt_bundle_attach;
    cfg.skip_cert_common_name_check=false;
    cfg.buffer_size=2048;
    cfg.buffer_size_tx=2048;
    cfg.keep_alive_enable=false;
    cfg.event_handler=assetHeaderEvent;
    cfg.user_data=&headers;

    esp_http_client_handle_t c=esp_http_client_init(&cfg);
    if(!c){copyText(msg,mc,"H2 HTTPS client allocation failed");return false;}
    esp_http_client_set_header(c,"Accept-Encoding","identity");
    esp_http_client_set_header(c,"Connection","close");
    if(esp_http_client_open(c,0)!=ESP_OK){
      esp_http_client_cleanup(c);
      copyText(msg,mc,"H2 HTTPS open failed");
      return false;
    }

    const int64_t headerLength=esp_http_client_fetch_headers(c);
    int code=esp_http_client_get_status_code(c);
    if(headerLength<0||headers.invalid){
      esp_http_client_close(c);
      esp_http_client_cleanup(c);
      copyText(msg,mc,"H2 release headers were invalid");
      return false;
    }

    if(code==301||code==302||code==303||code==307||code==308){
      const bool rejected=
          redirects==3||
          !headers.location[0]||
          !allowedUrl(headers.location);
      if(!rejected)copyText(current,sizeof(current),headers.location);
      esp_http_client_close(c);
      esp_http_client_cleanup(c);
      if(rejected){
        copyText(msg,mc,"H2 release redirect was rejected");
        return false;
      }
      continue;
    }

    if(code!=200){
      esp_http_client_close(c);
      esp_http_client_cleanup(c);
      snprintf(msg,mc,"H2 release asset returned HTTP %d",code);
      return false;
    }

    out=0;
    uint32_t idle=millis();
    while(true){
      if(out==cap){
        esp_http_client_close(c);
        esp_http_client_cleanup(c);
        copyText(msg,mc,"H2 release asset exceeded size limit");
        return false;
      }
      int n=esp_http_client_read(
          c,reinterpret_cast<char*>(dst+out),std::min<size_t>(4096,cap-out));
      if(n>0){out+=n;idle=millis();continue;}
      if(n==0&&esp_http_client_is_complete_data_received(c))break;
      if(millis()-idle>15000){
        esp_http_client_close(c);
        esp_http_client_cleanup(c);
        copyText(msg,mc,"H2 release download stalled");
        return false;
      }
      delay(1);
    }
    esp_http_client_close(c);
    esp_http_client_cleanup(c);
    return true;
  }
  copyText(msg,mc,"H2 redirect limit exceeded");
  return false;
}

bool hexDigest(const uint8_t*d,size_t n,uint8_t out[32]){if(n<64)return false;auto h=[](uint8_t c)->int{if(c>='0'&&c<='9')return c-'0';if(c>='a'&&c<='f')return c-'a'+10;if(c>='A'&&c<='F')return c-'A'+10;return -1;};for(int i=0;i<32;++i){int a=h(d[i*2]),b=h(d[i*2+1]);if(a<0||b<0)return false;out[i]=uint8_t((a<<4)|b);}return true;}
}
void observePlantLinkFrame(const void *frame){if(frame)observer(*static_cast<const plantlink::Frame*>(frame));}
Result updateForRelease(const espplants_ota_installer::Release &release,espplants_ota_installer::ProgressCallback progress,char *message,size_t mc){char expected[96]{},asset[128]{},shaAsset[140]{},url[512]{};snprintf(expected,sizeof(expected),"%s",kExpectedH2Build);snprintf(asset,sizeof(asset),"esp-plants-h2-%s.bin",kExpectedH2Version);snprintf(shaAsset,sizeof(shaAsset),"%s.sha256",asset);
plantlink::FrameObserver previous=plantlink::frameObserver();plantlink::setFrameObserver(observer);
uint8_t helloPayload[8]{};plantlink::putU32LE(helloPayload,millis());uint32_t before=counter();sendFrame(plantlink::MessageType::Hello,helloPayload,sizeof(helloPayload));uint32_t start=millis();bool already=false,phase2=false;while(millis()-start<1200){portENTER_CRITICAL(&mux);char h[96]{};memcpy(h,lastHello,sizeof(h));portEXIT_CRITICAL(&mux);if(strcmp(h,expected)==0){already=true;phase2=true;break;}if(strncmp(h,kH2Prefix,strlen(kH2Prefix))==0)phase2=true;delay(5);}if(already){plantlink::setFrameObserver(previous);copyText(message,mc,"H2 already matches release");return Result::OK;}
uint8_t *shaText=static_cast<uint8_t*>(heap_caps_malloc(128,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));uint8_t *image=static_cast<uint8_t*>(heap_caps_malloc(kMaxH2Bytes,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));if(!shaText||!image){if(shaText)heap_caps_free(shaText);if(image)heap_caps_free(image);plantlink::setFrameObserver(previous);copyText(message,mc,"H2 OTA PSRAM allocation failed");return Result::FAILED;}size_t shaLen=0,imageLen=0;snprintf(url,sizeof(url),"%s%s/%s",kReleasePrefix,release.tag,shaAsset);if(!getAsset(url,shaText,128,shaLen,message,mc)){heap_caps_free(shaText);heap_caps_free(image);plantlink::setFrameObserver(previous);return Result::FAILED;}uint8_t expectedSha[32]{};if(!hexDigest(shaText,shaLen,expectedSha)){heap_caps_free(shaText);heap_caps_free(image);plantlink::setFrameObserver(previous);copyText(message,mc,"H2 SHA-256 sidecar was invalid");return Result::FAILED;}heap_caps_free(shaText);snprintf(url,sizeof(url),"%s%s/%s",kReleasePrefix,release.tag,asset);if(!getAsset(url,image,kMaxH2Bytes,imageLen,message,mc)){heap_caps_free(image);plantlink::setFrameObserver(previous);return Result::FAILED;}if(imageLen<65536){heap_caps_free(image);plantlink::setFrameObserver(previous);copyText(message,mc,"H2 firmware asset was too small");return Result::FAILED;}uint8_t actual[32]{};mbedtls_sha256(image,imageLen,actual,0);if(memcmp(actual,expectedSha,32)!=0){heap_caps_free(image);plantlink::setFrameObserver(previous);copyText(message,mc,"H2 firmware SHA-256 mismatch");return Result::FAILED;}
// Explicitly authorize exactly the next H2OtaBegin frame immediately before
// the privileged operation. Normal startup Hello frames carry zero here and
// never arm OTA. This is a physical-link/session confirmation, not crypto.
before=counter();plantlink::putU32LE(helloPayload,millis());plantlink::putU32LE(helloPayload+4,plantlink_ota::kAuthorizeIntentMagic);sendFrame(plantlink::MessageType::Hello,helloPayload,sizeof(helloPayload));start=millis();while(millis()-start<1200){if(counter()!=before)break;delay(2);}
uint8_t begin[plantlink_ota::kBeginBytes]{};begin[0]=plantlink::kProtocolVersion;plantlink::putU32LE(begin+1,imageLen);memcpy(begin+5,expectedSha,32);snprintf(reinterpret_cast<char*>(begin+37),96,"%s",expected);before=counter();sendFrame(plantlink::MessageType::H2OtaBegin,begin,sizeof(begin));uint8_t s=0,e=0;uint32_t next=0;if(!waitStatus(before,kAckTimeoutMs,s,e,next)){heap_caps_free(image);plantlink::setFrameObserver(previous);copyText(message,mc,phase2?"H2 OTA did not answer":"H2 needs one USB bootstrap to alpha.11");return phase2?Result::FAILED:Result::BOOTSTRAP_REQUIRED;}if(s==uint8_t(plantlink_ota::Status::Error)){heap_caps_free(image);plantlink::setFrameObserver(previous);snprintf(message,mc,"H2 OTA begin failed (%u)",e);return Result::FAILED;}
for(uint32_t off=0;off<imageLen;){size_t n=std::min<size_t>(plantlink_ota::kChunkDataBytes,imageLen-off);uint8_t chunk[4+plantlink_ota::kChunkDataBytes]{};plantlink::putU32LE(chunk,off);memcpy(chunk+4,image+off,n);bool acked=false;for(int retry=0;retry<3&&!acked;++retry){before=counter();sendFrame(plantlink::MessageType::H2OtaChunk,chunk,4+n);if(waitStatus(before,kAckTimeoutMs,s,e,next)&&s!=uint8_t(plantlink_ota::Status::Error)&&next>=off+n)acked=true;}if(!acked){sendFrame(plantlink::MessageType::H2OtaAbort,nullptr,0);heap_caps_free(image);plantlink::setFrameObserver(previous);snprintf(message,mc,"H2 OTA transfer failed at %lu",(unsigned long)off);return Result::FAILED;}off=next;if(progress)progress(off,imageLen);}
heap_caps_free(image);before=counter();sendFrame(plantlink::MessageType::H2OtaEnd,nullptr,0);if(!waitStatus(before,kAckTimeoutMs*2,s,e,next)||s==uint8_t(plantlink_ota::Status::Error)){plantlink::setFrameObserver(previous);copyText(message,mc,"H2 final validation failed");return Result::FAILED;}
plantlink::putU32LE(helloPayload+4,0);start=millis();while(millis()-start<kRebootTimeoutMs){sendFrame(plantlink::MessageType::Hello,helloPayload,sizeof(helloPayload));delay(250);portENTER_CRITICAL(&mux);char h[96]{};memcpy(h,lastHello,sizeof(h));portEXIT_CRITICAL(&mux);if(strcmp(h,expected)==0){plantlink::setFrameObserver(previous);copyText(message,mc,"H2 updated and reboot-confirmed");return Result::OK;}}plantlink::setFrameObserver(previous);copyText(message,mc,"H2 updated but reboot confirmation timed out");return Result::FAILED;}
}
