#pragma once

#include "plantlink.h"

namespace espplants_h2_ota {

// Called by the H2's single PlantLink dispatcher after a frame has been decoded.
// Returns true when the frame is an H2 OTA command and was consumed here.
bool handlePlantLinkFrame(const plantlink::Frame &frame);

}  // namespace espplants_h2_ota
