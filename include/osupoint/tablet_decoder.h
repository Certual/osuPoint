#pragma once

#include "osupoint/tablet_spec.h"
#include <cstdint>
#include <cstddef>

DecodeReason DecodeTabletPacket(
    const uint8_t* report,
    size_t reportSize,
    const RuntimeTabletSpec& spec,
    PenSample& out) noexcept;
