from __future__ import annotations
import argparse,hashlib,json,re,shutil,struct
from pathlib import Path
from typing import NamedTuple
PACKAGE_MAGIC=b"ESP-PLANTS-OTA"; PACKAGE_HARDWARE_ID=b"WAVESHARE-ESP32-S3-LCD-7"; PACKAGE_PRODUCT_ID=b"ESP-PLANTS-WAVESHARE"; FORMAT_VERSION=1; HEADER_SIZE=512; ESP_IMAGE_MAGIC=0xE9; ESP32_S3_CHIP_ID=9
HEADER_STRUCT=struct.Struct("<16sHH32s32s96sI32s296s"); MANIFEST_ASSET_NAME="esp-plants-waveshare.manifest.json"; MAX_MANIFEST_BYTES=2048; DISTRIBUTION_FIRMWARE_MARKER=b"ESP-PLANTS-DISTRIBUTION-BUILD"; H2_DISTRIBUTION_MARKER=b"ESP-PLANTS-H2-DISTRIBUTION-BUILD"; DISTRIBUTION_BUILD_FLAG="ESP_PLANTS_DISTRIBUTION_BUILD"; MIN_FIRMWARE_BYTES=64*1024; MAX_PACKAGE_BYTES=7*1024*1024; H2_MAX_BYTES=0xC0000
class BuildIdentity(NamedTuple): version:str; hardware:str; product:str; channel:str; build_id:str; updater_version:int; release_notes:str
class PackageMetadata(NamedTuple): package_size:int; package_sha256:str; firmware_size:int; firmware_sha256:str; build_id:str
def _fixed(v:bytes,n:int,f:str)->bytes:
    if len(v)>=n: raise ValueError(f"{f} must be shorter than {n} bytes")
    return v+bytes(n-len(v))
def _str(t,n):
    m=re.search(rf'#define\s+{re.escape(n)}\s+"([^"]*)"',t) or re.search(rf'#define\s+{re.escape(n)}\s+\\?\s*\n?\s*"([^"]*)"',t)
    if not m: raise ValueError(f"{n} was not found")
    return m.group(1)
def _int(t,n):
    m=re.search(rf'#define\s+{re.escape(n)}\s+(\d+)',t)
    if not m: raise ValueError(f"{n} was not found")
    return int(m.group(1))
def read_build_identity(p:Path)->BuildIdentity:
    t=p.read_text();v=_str(t,"ESP_PLANTS_WAVESHARE_VERSION");return BuildIdentity(v,_str(t,"ESP_PLANTS_WAVESHARE_HARDWARE_ID"),_str(t,"ESP_PLANTS_WAVESHARE_PRODUCT_ID"),_str(t,"ESP_PLANTS_WAVESHARE_RELEASE_CHANNEL"),f"ESPPLANTS-WAVESHARE-{v}",_int(t,"ESP_PLANTS_WAVESHARE_UPDATER_VERSION"),_str(t,"ESP_PLANTS_WAVESHARE_RELEASE_NOTES"))
def validate_firmware(f:bytes):
    if len(f)<MIN_FIRMWARE_BYTES or f[0]!=ESP_IMAGE_MAGIC: raise ValueError("invalid ESP application image")
    if struct.unpack_from("<H",f,12)[0]!=ESP32_S3_CHIP_ID: raise ValueError("firmware is not ESP32-S3")
def create_package(f:bytes,i:BuildIdentity)->bytes:
    validate_firmware(f)
    if DISTRIBUTION_FIRMWARE_MARKER not in f or i.build_id.encode() not in f: raise ValueError("Waveshare firmware lacks distribution/build identity")
    d=hashlib.sha256(f).digest();h=HEADER_STRUCT.pack(_fixed(PACKAGE_MAGIC,16,"magic"),1,512,_fixed(PACKAGE_HARDWARE_ID,32,"hardware"),_fixed(PACKAGE_PRODUCT_ID,32,"product"),_fixed(i.build_id.encode(),96,"build"),len(f),d,bytes(296));p=h+f
    if len(p)>MAX_PACKAGE_BYTES: raise ValueError("package too large")
    return p
def metadata(p:bytes,i:BuildIdentity)->PackageMetadata:
    f=p[512:];return PackageMetadata(len(p),hashlib.sha256(p).hexdigest(),len(f),hashlib.sha256(f).hexdigest(),i.build_id)
def h2_asset(repo:Path,i:BuildIdentity,release:Path):
    src=repo/"firmware"/"m5-h2-zigbee"/".pio"/"build"/"m5_gateway_h2_release"/"firmware.bin"
    if not src.is_file(): raise ValueError(f"H2 release firmware missing: {src}")
    data=src.read_bytes(); build=f"ESPPLANTS-H2-{i.version}".encode()
    if len(data)<MIN_FIRMWARE_BYTES or len(data)>H2_MAX_BYTES or data[0]!=ESP_IMAGE_MAGIC: raise ValueError("H2 firmware size/image invalid")
    if H2_DISTRIBUTION_MARKER not in data or build not in data: raise ValueError("H2 firmware lacks distribution/build identity")
    name=f"esp-plants-h2-{i.version}.bin"; out=release/name; out.write_bytes(data); digest=hashlib.sha256(data).hexdigest(); (release/(name+".sha256")).write_text(digest+"  "+name+"\n",encoding="ascii")
    return {"product":"esp-plants-h2","hardware":"m5stack-unit-gateway-h2","version":i.version,"build_id":build.decode(),"protocol":1,"asset":name,"firmware_size":len(data),"firmware_sha256":digest}
def create_manifest(i,m,a,h2):
    d={"schema":1,"tag":f"v{i.version}","product":i.product,"hardware":i.hardware,"channel":i.channel,"version":i.version,"build_id":m.build_id,"asset":a,"package_size":m.package_size,"package_sha256":m.package_sha256,"firmware_size":m.firmware_size,"firmware_sha256":m.firmware_sha256,"min_updater":i.updater_version,"notes":i.release_notes,"h2":h2};b=(json.dumps(d,separators=(",",":"),sort_keys=True)+"\n").encode("ascii")
    if len(b)>MAX_MANIFEST_BYTES: raise ValueError("manifest exceeds firmware limit")
    return b
def write_release_assets(fw:Path,hdr:Path,release:Path):
    i=read_build_identity(hdr);p=create_package(fw.read_bytes(),i);m=metadata(p,i);release.mkdir(parents=True,exist_ok=True);a=f"esp-plants-waveshare-{i.version}.plantsota";(release/a).write_bytes(p);h2=h2_asset(hdr.parents[3],i,release);(release/MANIFEST_ASSET_NAME).write_bytes(create_manifest(i,m,a,h2));return release/a,release/MANIFEST_ASSET_NAME,m
def _enabled(env): return "ESP_PLANTS_DISTRIBUTION_BUILD" in str(env.get("BUILD_FLAGS",[]))
def _post(source,target,env):
    if not _enabled(env): raise RuntimeError("distribution build required")
    project=Path(env.subst("$PROJECT_DIR")); asset,manifest,m=write_release_assets(Path(target[0].get_abspath()),project/"include"/"build_version.h",project.parents[1]/"release");print(asset);print(manifest);print(m)
def main():
    ap=argparse.ArgumentParser();ap.add_argument("firmware",type=Path);ap.add_argument("build_header",type=Path);ap.add_argument("release_dir",type=Path);a=ap.parse_args();print(write_release_assets(a.firmware,a.build_header,a.release_dir));return 0
try: Import("env")
except NameError: env=None
if env is not None and not env.IsIntegrationDump(): env.AddPostAction("$BUILD_DIR/${PROGNAME}.bin",_post)
if __name__=="__main__": raise SystemExit(main())
