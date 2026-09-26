#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string.h>

namespace espplants_update_policy {

constexpr uint32_t kManifestSchema = 1U;
constexpr uint32_t kUpdaterVersion = 1U;
constexpr size_t kMaxManifestBytes = 2048U;
constexpr size_t kMaxTagLength = 63U;
constexpr size_t kMaxVersionLength = 31U;
constexpr size_t kMaxBuildIdLength = 95U;
constexpr size_t kMaxAssetNameLength = 127U;
constexpr size_t kMaxNotesLength = 191U;
constexpr size_t kMaxHttpHeaderBytes = 16U * 1024U;
constexpr size_t kMaxRedirectUrlLength = 4095U;
constexpr size_t kMinHttpTxBufferBytes = 1024U;
constexpr size_t kHttpTxHeadroomBytes = 512U;
constexpr uint32_t kPackageHeaderBytes = 512U;
constexpr uint32_t kMinimumFirmwareBytes = 64U * 1024U;
constexpr uint32_t kMaximumPackageBytes = 7U * 1024U * 1024U;

inline bool boundedPrintableAscii(const char *text, size_t maximumLength,
                                  bool allowEmpty = false) {
  if (!text) return false;
  const size_t length = strnlen(text, maximumLength + 1U);
  if (length > maximumLength || (!allowEmpty && length == 0U)) return false;
  for (size_t index = 0; index < length; ++index) {
    const unsigned char value = static_cast<unsigned char>(text[index]);
    if (value < 0x20 || value > 0x7e) return false;
  }
  return true;
}

inline bool lowerHexDigest(const char *digest) {
  if (!digest || strlen(digest) != 64U) return false;
  for (size_t index = 0; index < 64U; ++index) {
    const char value = digest[index];
    if (!((value >= '0' && value <= '9') ||
          (value >= 'a' && value <= 'f'))) return false;
  }
  return true;
}

inline bool packageLayoutValid(uint32_t packageBytes, uint32_t firmwareBytes) {
  return firmwareBytes >= kMinimumFirmwareBytes &&
         packageBytes == firmwareBytes + kPackageHeaderBytes &&
         packageBytes <= kMaximumPackageBytes;
}

inline bool assetNameValid(const char *asset) {
  if (!boundedPrintableAscii(asset, kMaxAssetNameLength)) return false;
  constexpr char suffix[] = ".plantsota";
  const size_t length = strlen(asset);
  return length >= sizeof(suffix) - 1U &&
         strcmp(asset + length - (sizeof(suffix) - 1U), suffix) == 0;
}

inline bool h2AssetNameValid(const char *asset) {
  if (!boundedPrintableAscii(asset, kMaxAssetNameLength)) return false;
  constexpr char prefix[] = "esp-plants-h2-";
  constexpr char suffix[] = ".bin";
  const size_t length = strlen(asset);
  const size_t prefixLength = sizeof(prefix) - 1U;
  const size_t suffixLength = sizeof(suffix) - 1U;
  return length > prefixLength + suffixLength &&
         strncmp(asset, prefix, prefixLength) == 0 &&
         strcmp(asset + length - suffixLength, suffix) == 0;
}

inline bool tagValid(const char *tag) {
  if (!boundedPrintableAscii(tag, kMaxTagLength)) return false;
  for (const char *p = tag; *p; ++p) {
    const char c = *p;
    const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                    (c >= '0' && c <= '9') || c == '.' || c == '-' ||
                    c == '_' || c == '+';
    if (!ok) return false;
  }
  return true;
}

inline bool accumulateHeaderBytes(size_t currentTotal, size_t keyLength,
                                  size_t valueLength, size_t &updatedTotal) {
  constexpr size_t framingBytes = 4U;
  if (currentTotal > kMaxHttpHeaderBytes ||
      keyLength > kMaxHttpHeaderBytes - currentTotal) return false;
  const size_t afterKey = currentTotal + keyLength;
  if (valueLength > kMaxHttpHeaderBytes - afterKey) return false;
  const size_t afterValue = afterKey + valueLength;
  if (framingBytes > kMaxHttpHeaderBytes - afterValue) return false;
  updatedTotal = afterValue + framingBytes;
  return true;
}

inline bool redirectUrlLengthValid(const char *url) {
  return url && strnlen(url, kMaxRedirectUrlLength + 1U) <= kMaxRedirectUrlLength;
}

inline size_t httpTransmitBufferBytes(size_t urlLength) {
  if (urlLength > kMaxRedirectUrlLength) return 0U;
  const size_t required = urlLength + kHttpTxHeadroomBytes;
  return required < kMinHttpTxBufferBytes ? kMinHttpTxBufferBytes : required;
}

inline char asciiLower(char value) {
  return value >= 'A' && value <= 'Z'
      ? static_cast<char>(value + ('a' - 'A'))
      : value;
}

inline bool allowedReleaseHost(const char *host) {
  return host &&
         (strcmp(host, "github.com") == 0 ||
          strcmp(host, "objects.githubusercontent.com") == 0 ||
          strcmp(host, "release-assets.githubusercontent.com") == 0);
}

inline bool parseAllowedHttpsUrl(const char *url, char *host,
                                 size_t hostCapacity) {
  constexpr char prefix[] = "https://";
  if (!url || !host || hostCapacity == 0 ||
      strncmp(url, prefix, sizeof(prefix) - 1U) != 0) return false;
  const char *authority = url + sizeof(prefix) - 1U;
  const char *slash = strchr(authority, '/');
  if (!slash || slash == authority) return false;
  const size_t hostLength = static_cast<size_t>(slash - authority);
  if (hostLength >= hostCapacity || memchr(authority, '@', hostLength) ||
      memchr(authority, ':', hostLength)) return false;
  memcpy(host, authority, hostLength);
  host[hostLength] = 0;
  for (size_t index = 0; index < hostLength; ++index) host[index] = asciiLower(host[index]);
  return allowedReleaseHost(host);
}

inline bool framingIsUnambiguous(bool contentLengthSeen,
                                 bool transferEncodingSeen,
                                 bool transferEncodingChunkedOnly,
                                 bool clientReportsChunked) {
  return !(contentLengthSeen && transferEncodingSeen) &&
         (!transferEncodingSeen || transferEncodingChunkedOnly) &&
         (contentLengthSeen || clientReportsChunked);
}

}  // namespace espplants_update_policy
