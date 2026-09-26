#pragma once
#include <stddef.h>
#include <stdint.h>
#include "plants_ota_installer.h"

namespace espplants_h2_ota {

enum class Result : uint8_t { OK = 0, FAILED, BOOTSTRAP_REQUIRED };
enum class TargetState : uint8_t { UNKNOWN = 0, MATCH, DIFFERENT };

TargetState targetStateForRelease(const espplants_ota_installer::Release &release);
Result updateForRelease(const espplants_ota_installer::Release &release,
                        espplants_ota_installer::ProgressCallback progress,
                        char *message, size_t messageCapacity);

}  // namespace espplants_h2_ota
