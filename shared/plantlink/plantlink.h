#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string.h>
namespace plantlink {
constexpr uint8_t kProtocolVersion=1;
constexpr size_t kMaxPayloadBytes=256;
constexpr size_t kHeaderBytes=7;
constexpr size_t kCrcBytes=4;
constexpr size_t kMaxDecodedBytes=kHeaderBytes+kMaxPayloadBytes+kCrcBytes;
constexpr size_t kMaxEncodedBytes=kMaxDecodedBytes+(kMaxDecodedBytes/254)+2;
enum class MessageType:uint8_t{Hello=0x01,HelloAck=0x02,Heartbeat=0x03,NetworkStatus=0x10,PermitJoin=0x11,DeviceJoined=0x12,DeviceLeft=0x13,RemoveDevice=0x14,InfrastructureReport=0x15,SensorReport=0x20,SetSensorOption=0x21,CommandResult=0x22,RawZigbeeEvent=0x30,FactoryResetNetwork=0x40,H2OtaBegin=0x50,H2OtaChunk=0x51,H2OtaEnd=0x52,H2OtaStatus=0x53,H2OtaAbort=0x54};
enum FrameFlags:uint8_t{FlagNone=0,FlagResponse=1,FlagError=2};
enum CapabilityFlags:uint32_t{CapabilityNone=0,CapabilityZigbeeCoordinator=1u<<0,CapabilityZg303zDecoder=1u<<1,CapabilityRawZigbeeLog=1u<<2,CapabilityInfrastructureRegistry=1u<<3,CapabilityH2Ota=1u<<4};
enum SensorFieldFlags:uint16_t{SensorHasTemperature=1u<<0,SensorHasHumidity=1u<<1,SensorHasSoilMoisture=1u<<2,SensorHasBattery=1u<<3,SensorHasWaterWarning=1u<<4};
enum InfrastructureFlags:uint8_t{InfrastructureOnline=1u<<0,InfrastructureDirectNeighbor=1u<<1};
struct Frame{MessageType type=MessageType::Hello;uint8_t flags=0;uint16_t sequence=0;uint16_t payloadLength=0;uint8_t payload[kMaxPayloadBytes]{};};
using FrameObserver=void(*)(const Frame&);
inline FrameObserver &frameObserver(){static FrameObserver observer=nullptr;return observer;}
inline void setFrameObserver(FrameObserver observer){frameObserver()=observer;}
constexpr size_t kSensorReportPayloadBytes=21; constexpr int8_t kRssiUnavailableDbm=static_cast<int8_t>(-128);
struct SensorReportData{uint8_t ieee[8]{};uint16_t shortAddress=0xffff;uint16_t fieldFlags=0;int16_t temperatureCentiC=0;uint16_t humidityCentiPct=0;uint8_t soilMoisturePct=0;uint8_t batteryPct=0;uint8_t waterWarning=0;uint8_t lqi=0;int8_t rssiDbm=kRssiUnavailableDbm;};
constexpr size_t kInfrastructureReportPayloadBytes=14;
struct InfrastructureReportData{uint8_t ieee[8]{};uint16_t shortAddress=0xffff;uint8_t flags=0;uint8_t deviceType=0;uint8_t lqi=0;int8_t rssiDbm=kRssiUnavailableDbm;};
inline void putU16LE(uint8_t*p,uint16_t v){p[0]=v&0xff;p[1]=(v>>8)&0xff;} inline uint16_t getU16LE(const uint8_t*p){return uint16_t(p[0])|(uint16_t(p[1])<<8);} inline void putU32LE(uint8_t*p,uint32_t v){p[0]=v&0xff;p[1]=(v>>8)&0xff;p[2]=(v>>16)&0xff;p[3]=(v>>24)&0xff;} inline uint32_t getU32LE(const uint8_t*p){return uint32_t(p[0])|(uint32_t(p[1])<<8)|(uint32_t(p[2])<<16)|(uint32_t(p[3])<<24);}
inline uint32_t crc32(const uint8_t*d,size_t n){uint32_t c=0xffffffffu;for(size_t i=0;i<n;++i){c^=d[i];for(uint8_t b=0;b<8;++b){uint32_t m=uint32_t(-(int32_t(c&1u)));c=(c>>1)^(0xedb88320u&m);}}return ~c;}
inline size_t cobsEncode(const uint8_t*in,size_t n,uint8_t*out,size_t cap){if(!cap)return 0;size_t r=0,w=1,ci=0;uint8_t code=1;while(r<n){if(in[r]==0){if(ci>=cap)return 0;out[ci]=code;code=1;ci=w++;if(w>cap)return 0;++r;}else{if(w>=cap)return 0;out[w++]=in[r++];++code;if(code==0xff){if(ci>=cap)return 0;out[ci]=code;code=1;ci=w++;if(w>cap)return 0;}}}if(ci>=cap)return 0;out[ci]=code;return w;}
inline size_t cobsDecode(const uint8_t*in,size_t n,uint8_t*out,size_t cap){if(!n)return 0;size_t r=0,w=0;while(r<n){uint8_t code=in[r];if(!code)return 0;++r;for(uint8_t i=1;i<code;++i){if(r>=n||w>=cap)return 0;out[w++]=in[r++];}if(code!=0xff&&r<n){if(w>=cap)return 0;out[w++]=0;}}return w;}
inline size_t encodeFrame(MessageType t,uint8_t f,uint16_t s,const uint8_t*p,uint16_t pn,uint8_t*out,size_t cap){if(pn>kMaxPayloadBytes||cap<2)return 0;uint8_t d[kMaxDecodedBytes]{};d[0]=kProtocolVersion;d[1]=uint8_t(t);d[2]=f;putU16LE(d+3,s);putU16LE(d+5,pn);if(pn&&p)memcpy(d+kHeaderBytes,p,pn);size_t co=kHeaderBytes+pn;putU32LE(d+co,crc32(d,co));size_t dl=co+kCrcBytes;size_t en=cobsEncode(d,dl,out,cap-1);if(!en||en>=cap)return 0;out[en]=0;return en+1;}
inline bool decodeFrame(const uint8_t*e,size_t en,Frame&o){uint8_t d[kMaxDecodedBytes]{};size_t n=cobsDecode(e,en,d,sizeof(d));if(n<kHeaderBytes+kCrcBytes||d[0]!=kProtocolVersion)return false;uint16_t pn=getU16LE(d+5);if(pn>kMaxPayloadBytes||n!=kHeaderBytes+pn+kCrcBytes)return false;size_t co=kHeaderBytes+pn;if(getU32LE(d+co)!=crc32(d,co))return false;o.type=MessageType(d[1]);o.flags=d[2];o.sequence=getU16LE(d+3);o.payloadLength=pn;if(pn)memcpy(o.payload,d+kHeaderBytes,pn);return true;}
class Decoder{public:bool feed(uint8_t b,Frame&o){if(b==0){if(length_==0)return false;bool ok=decodeFrame(encoded_,length_,o);length_=0;overflowed_=false;if(ok&&frameObserver())frameObserver()(o);return ok;}if(overflowed_)return false;if(length_>=sizeof(encoded_)){overflowed_=true;return false;}encoded_[length_++]=b;return false;}void reset(){length_=0;overflowed_=false;}private:uint8_t encoded_[kMaxEncodedBytes]{};size_t length_=0;bool overflowed_=false;};
inline size_t serializeSensorReport(const SensorReportData&i,uint8_t*out,size_t cap){if(cap<kSensorReportPayloadBytes)return 0;memcpy(out,i.ieee,8);putU16LE(out+8,i.shortAddress);putU16LE(out+10,i.fieldFlags);putU16LE(out+12,uint16_t(i.temperatureCentiC));putU16LE(out+14,i.humidityCentiPct);out[16]=i.soilMoisturePct;out[17]=i.batteryPct;out[18]=i.waterWarning;out[19]=i.lqi;out[20]=uint8_t(i.rssiDbm);return kSensorReportPayloadBytes;}
inline bool parseSensorReport(const uint8_t*p,size_t n,SensorReportData&o){if(!p||n!=kSensorReportPayloadBytes)return false;memcpy(o.ieee,p,8);o.shortAddress=getU16LE(p+8);o.fieldFlags=getU16LE(p+10);o.temperatureCentiC=int16_t(getU16LE(p+12));o.humidityCentiPct=getU16LE(p+14);o.soilMoisturePct=p[16];o.batteryPct=p[17];o.waterWarning=p[18];o.lqi=p[19];o.rssiDbm=int8_t(p[20]);return true;}
inline size_t serializeInfrastructureReport(const InfrastructureReportData&i,uint8_t*out,size_t cap){if(cap<kInfrastructureReportPayloadBytes)return 0;memcpy(out,i.ieee,8);putU16LE(out+8,i.shortAddress);out[10]=i.flags;out[11]=i.deviceType;out[12]=i.lqi;out[13]=uint8_t(i.rssiDbm);return kInfrastructureReportPayloadBytes;}
inline bool parseInfrastructureReport(const uint8_t*p,size_t n,InfrastructureReportData&o){if(!p||n!=kInfrastructureReportPayloadBytes)return false;memcpy(o.ieee,p,8);o.shortAddress=getU16LE(p+8);o.flags=p[10];o.deviceType=p[11];o.lqi=p[12];o.rssiDbm=int8_t(p[13]);return true;}
inline void formatIeee(const uint8_t ieee[8],char*out,size_t outSize){static const char hex[]="0123456789ABCDEF";if(!out||outSize<24)return;size_t p=0;for(int i=7;i>=0;--i){out[p++]=hex[(ieee[i]>>4)&0xf];out[p++]=hex[ieee[i]&0xf];if(i)out[p++]=':';}out[p]=0;}
} // namespace plantlink
