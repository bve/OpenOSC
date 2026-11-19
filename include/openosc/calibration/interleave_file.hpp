#pragma once
#include "openosc/calibration/interleave_profile.hpp"
#include "openosc/calibration/calibration_file.hpp"
#include <cstddef>

namespace openosc::calibration {
// scope_interleave.json: schema 1, the factory CRC, enabled and offset_codes (the ZT706S service
// offset). Schema 2 adds "clocks": [{"core_mhz": 200, "adc_divider": 2, "offset_codes": 9.4}, ...],
// the ZT703S's offset per timing (core clock and ADC divider). A profile without clocked offsets is written as schema 1 (the ZT706S's file stays
// the one its firmware wrote); both are read.
class InterleaveJson {
public:
    using Profile = InterleaveProfile;
    static constexpr const char *Path = storage::Layout::InterleaveFile, *Previous = storage::Layout::InterleavePrevious;
    static constexpr size_t MaxBytes = 512;
    static constexpr CalibrationFileText Text{"Invalid interleave offset", "Cannot read interleave JSON", "Cannot save interleave JSON",
                                              "Cannot preserve invalid interleave JSON", "Interleave loaded; file restore failed"};
    static InterleaveProfile empty(const CalibrationData &factory) { InterleaveProfile profile; profile.factoryCrc = factory.crc(); return profile; }
    static bool parse(const char *text, size_t bytes, const CalibrationData &factory, InterleaveProfile &output);
    static size_t encode(const InterleaveProfile &values, const CalibrationData &factory, char *output, size_t capacity);
};
class InterleaveFile final : public CalibrationFile<InterleaveJson> {
public:
    using CalibrationFile::CalibrationFile;
};
} // namespace openosc::calibration
