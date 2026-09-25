#pragma once

#include "plantlink.h"

namespace espplants_h2_ota {

// Only a Hello carrying the explicit H2 OTA intent marker arms the next
// H2OtaBegin frame. Ordinary startup/link Hello frames do not authorize OTA.
void noteControllerHello(const plantlink::Frame &frame);
bool handlePlantLinkFrame(const plantlink::Frame &frame);

}  // namespace espplants_h2_ota
