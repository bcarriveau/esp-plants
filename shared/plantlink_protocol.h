#pragma once

// Deprecated compatibility include.
//
// PlantLink has exactly one authoritative protocol definition:
//   shared/plantlink/plantlink.h
//
// Keep this legacy path as a forwarding shim so an old include cannot silently
// select a second set of message IDs or payload limits.
#include "plantlink/plantlink.h"
