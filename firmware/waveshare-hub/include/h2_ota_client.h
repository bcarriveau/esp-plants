#pragma once
#include <stddef.h>
#include <stdint.h>
#include "plants_ota_installer.h"
namespace espplants_h2_ota {
enum class Result:uint8_t{OK=0,FAILED,BOOTSTRAP_REQUIRED};
Result updateForRelease(const espplants_ota_installer::Release &release,
                        espplants_ota_installer::ProgressCallback progress,
                        char *message,size_t messageCapacity);
}
