#pragma once

#include <stddef.h>
#include <stdint.h>

namespace espplants_ota_installer {

struct Release {
  char tag[64]{};
  char asset[128]{};
  char buildId[96]{};
  uint32_t packageSize = 0;
  uint32_t firmwareSize = 0;
  uint8_t packageSha256[32]{};
  uint8_t firmwareSha256[32]{};

  // H2 metadata comes from the same verified release manifest. Keeping it in
  // the transient Release object lets newer Waveshare firmware target the H2
  // version actually published with that release instead of a version baked
  // into an older updater binary.
  char h2Version[32]{};
  char h2Asset[128]{};
  char h2BuildId[96]{};
  uint32_t h2FirmwareSize = 0;
  uint8_t h2FirmwareSha256[32]{};
};

enum class Result : uint8_t {
  FAILED = 0,
  RESTART_PENDING,
};

enum class RestartState : uint8_t {
  IDLE = 0,
  PENDING,
  FAILED,
};

using ProgressCallback = void (*)(uint32_t receivedBytes, uint32_t packageBytes);

Result install(const Release &release, ProgressCallback progress,
               char *message, size_t messageCapacity);
void serviceRestart();
RestartState restartState();
void copyRestartMessage(char *destination, size_t capacity);

}  // namespace espplants_ota_installer
