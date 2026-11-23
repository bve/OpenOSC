#pragma once
#include "openosc/board/current.hpp"
#include <cstdint>

namespace openosc::layout {
constexpr uint32_t FactoryBase = 0x08000000;
constexpr uint32_t FactoryBytes = 0x20000;
constexpr uint32_t LoaderBase = 0x08020000;
// Stable 128 KiB reservation, including space for future Recovery features.
constexpr uint32_t LoaderBytes = 0x20000;
constexpr uint32_t ApplicationBase = LoaderBase + LoaderBytes;
constexpr uint32_t HeaderBytes = 1024;
constexpr uint32_t VectorBase = ApplicationBase + HeaderBytes;
constexpr uint32_t FlashEnd = board::Current::FlashEnd;
constexpr uint32_t MaxPayload = FlashEnd - VectorBase;
constexpr uint32_t SectorBytes = 8192;
constexpr uint32_t ProgramBytes = 16;
constexpr uint32_t BoardId = board::Current::ImageBoardId;
constexpr uint32_t CalibrationBase = 0x0801c000;
// The one factory-bootloader sector Recovery may rewrite (its power-on hold time, PowerHold).
constexpr uint32_t FactoryBootSector = 0x0800e000;
static_assert((VectorBase & 1023U) == 0);
static_assert(ApplicationBase == 0x08040000);
static_assert(FactoryBootSector % SectorBytes == 0 && FactoryBootSector + SectorBytes <= CalibrationBase);
static_assert(LoaderBase % SectorBytes == 0 && LoaderBytes % SectorBytes == 0);
}

