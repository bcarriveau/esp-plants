#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "plants_ota_installer.h"
#include "update_policy.h"

namespace espplants_h2_ota {

enum class Result : uint8_t { OK = 0, FAILED, BOOTSTRAP_REQUIRED };
enum class TargetState : uint8_t {
  UNKNOWN = 0,
  MATCH,
  OLDER_THAN_RELEASE,
  NEWER_THAN_RELEASE,
};

inline TargetState targetStateForIdentity(const char *currentBuildId,
                                          const char *releaseVersion,
                                          const char *releaseBuildId) {
  constexpr char prefix[] = "ESPPLANTS-H2-";
  if (!currentBuildId || !releaseVersion || !releaseBuildId ||
      strncmp(currentBuildId, prefix, sizeof(prefix) - 1U) != 0) {
    return TargetState::UNKNOWN;
  }

  const char *currentVersion = currentBuildId + sizeof(prefix) - 1U;
  int comparison = 0;
  if (!espplants_update_policy::compareSemanticVersions(
          currentVersion, releaseVersion, comparison)) {
    return TargetState::UNKNOWN;
  }
  if (comparison < 0) return TargetState::OLDER_THAN_RELEASE;
  if (comparison > 0) return TargetState::NEWER_THAN_RELEASE;

  return strcmp(currentBuildId, releaseBuildId) == 0 ? TargetState::MATCH
                                                     : TargetState::UNKNOWN;
}

TargetState targetStateForRelease(const espplants_ota_installer::Release &release);
Result updateForRelease(const espplants_ota_installer::Release &release,
                        espplants_ota_installer::ProgressCallback progress,
                        char *message, size_t messageCapacity);

}  // namespace espplants_h2_ota
