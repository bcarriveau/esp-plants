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

struct SemanticVersionView {
  uint32_t major = 0;
  uint32_t minor = 0;
  uint32_t patch = 0;
  const char *prerelease = nullptr;
  size_t prereleaseLength = 0;
};

inline bool semanticVersionNumber(const char *begin, const char *end,
                                  uint32_t &value) {
  if (!begin || !end || begin >= end) return false;
  uint32_t parsed = 0;
  for (const char *p = begin; p < end; ++p) {
    if (*p < '0' || *p > '9') return false;
    const uint32_t digit = static_cast<uint32_t>(*p - '0');
    if (parsed > (UINT32_MAX - digit) / 10U) return false;
    parsed = parsed * 10U + digit;
  }
  value = parsed;
  return true;
}

inline bool semanticIdentifierListValid(const char *begin, const char *end) {
  if (!begin || !end || begin >= end) return false;
  bool haveCharacter = false;
  for (const char *p = begin; p < end; ++p) {
    const char c = *p;
    if (c == '.') {
      if (!haveCharacter) return false;
      haveCharacter = false;
      continue;
    }
    const bool valid = (c >= 'a' && c <= 'z') ||
                       (c >= 'A' && c <= 'Z') ||
                       (c >= '0' && c <= '9') || c == '-';
    if (!valid) return false;
    haveCharacter = true;
  }
  return haveCharacter;
}

inline bool parseSemanticVersion(const char *text, SemanticVersionView &out) {
  if (!text) return false;
  const size_t rawLength = strnlen(text, kMaxVersionLength + 2U);
  if (rawLength == 0U || rawLength > kMaxVersionLength + 1U) return false;

  const char *begin = text;
  if (*begin == 'v' || *begin == 'V') ++begin;
  if (!*begin || static_cast<size_t>((text + rawLength) - begin) > kMaxVersionLength) {
    return false;
  }
  const char *end = text + rawLength;

  const char *plus = static_cast<const char *>(memchr(begin, '+', end - begin));
  const char *versionEnd = plus ? plus : end;
  if (plus && !semanticIdentifierListValid(plus + 1, end)) return false;

  const char *dash = static_cast<const char *>(memchr(begin, '-', versionEnd - begin));
  const char *coreEnd = dash ? dash : versionEnd;
  if (dash && !semanticIdentifierListValid(dash + 1, versionEnd)) return false;

  const char *dot1 = static_cast<const char *>(memchr(begin, '.', coreEnd - begin));
  if (!dot1) return false;
  const char *dot2 = static_cast<const char *>(memchr(dot1 + 1, '.', coreEnd - (dot1 + 1)));
  if (!dot2 || memchr(dot2 + 1, '.', coreEnd - (dot2 + 1))) return false;

  SemanticVersionView parsed;
  if (!semanticVersionNumber(begin, dot1, parsed.major) ||
      !semanticVersionNumber(dot1 + 1, dot2, parsed.minor) ||
      !semanticVersionNumber(dot2 + 1, coreEnd, parsed.patch)) {
    return false;
  }
  if (dash) {
    parsed.prerelease = dash + 1;
    parsed.prereleaseLength = static_cast<size_t>(versionEnd - (dash + 1));
  }
  out = parsed;
  return true;
}

inline bool semanticIdentifierNumeric(const char *begin, const char *end) {
  if (!begin || !end || begin >= end) return false;
  for (const char *p = begin; p < end; ++p) {
    if (*p < '0' || *p > '9') return false;
  }
  return true;
}

inline int compareSemanticIdentifier(const char *aBegin, const char *aEnd,
                                     const char *bBegin, const char *bEnd) {
  const bool aNumeric = semanticIdentifierNumeric(aBegin, aEnd);
  const bool bNumeric = semanticIdentifierNumeric(bBegin, bEnd);
  if (aNumeric != bNumeric) return aNumeric ? -1 : 1;

  if (aNumeric) {
    while (aBegin < aEnd - 1 && *aBegin == '0') ++aBegin;
    while (bBegin < bEnd - 1 && *bBegin == '0') ++bBegin;
    const size_t aLength = static_cast<size_t>(aEnd - aBegin);
    const size_t bLength = static_cast<size_t>(bEnd - bBegin);
    if (aLength != bLength) return aLength < bLength ? -1 : 1;
  }

  const size_t aLength = static_cast<size_t>(aEnd - aBegin);
  const size_t bLength = static_cast<size_t>(bEnd - bBegin);
  const size_t common = aLength < bLength ? aLength : bLength;
  const int lexical = memcmp(aBegin, bBegin, common);
  if (lexical < 0) return -1;
  if (lexical > 0) return 1;
  if (aLength == bLength) return 0;
  return aLength < bLength ? -1 : 1;
}

inline int compareSemanticPrerelease(const SemanticVersionView &a,
                                     const SemanticVersionView &b) {
  if (a.prereleaseLength == 0U && b.prereleaseLength == 0U) return 0;
  if (a.prereleaseLength == 0U) return 1;
  if (b.prereleaseLength == 0U) return -1;

  const char *aPos = a.prerelease;
  const char *bPos = b.prerelease;
  const char *aLimit = a.prerelease + a.prereleaseLength;
  const char *bLimit = b.prerelease + b.prereleaseLength;
  while (aPos < aLimit && bPos < bLimit) {
    const char *aDot = static_cast<const char *>(memchr(aPos, '.', aLimit - aPos));
    const char *bDot = static_cast<const char *>(memchr(bPos, '.', bLimit - bPos));
    const char *aEnd = aDot ? aDot : aLimit;
    const char *bEnd = bDot ? bDot : bLimit;
    const int comparison = compareSemanticIdentifier(aPos, aEnd, bPos, bEnd);
    if (comparison != 0) return comparison;
    aPos = aDot ? aDot + 1 : aLimit;
    bPos = bDot ? bDot + 1 : bLimit;
  }
  if (aPos == aLimit && bPos == bLimit) return 0;
  return aPos == aLimit ? -1 : 1;
}

inline bool compareSemanticVersions(const char *aText, const char *bText,
                                    int &comparison) {
  SemanticVersionView a;
  SemanticVersionView b;
  if (!parseSemanticVersion(aText, a) || !parseSemanticVersion(bText, b)) {
    comparison = 0;
    return false;
  }
  if (a.major != b.major) comparison = a.major < b.major ? -1 : 1;
  else if (a.minor != b.minor) comparison = a.minor < b.minor ? -1 : 1;
  else if (a.patch != b.patch) comparison = a.patch < b.patch ? -1 : 1;
  else comparison = compareSemanticPrerelease(a, b);
  return true;
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
