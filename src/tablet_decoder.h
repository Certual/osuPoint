#pragma once
#include "tablet_spec.h"

DecodeReason DecodeSample(const uint8_t* report, int reportSize, const TabletSpec& spec, PenSample& out) noexcept;
