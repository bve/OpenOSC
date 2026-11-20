#include "openosc/calibration/interleave_match_file.hpp"
#include "openosc/acquisition/ranges.hpp"
#include "openosc/calibration/interleave_profile.hpp"
#include "openosc/settings/run_mode.hpp"
#include "openosc/settings/json_input.hpp"
#include "openosc/settings/json_workspace.hpp"
#include "openosc/storage/json_files.hpp"
#include "openosc/storage/layout.hpp"

namespace openosc::calibration {
bool InterleaveMatchProfiles::load(uint16_t factoryCrc, const uint32_t uid[3]) {
    if (!empty()) return false;
    JsonWorkspace::Text text;
    size_t bytes = 0;
    return text && JsonFiles::read(storage::Layout::InterleaveMatchFile, text.data(), MaxBytes + 1, bytes) ==
                       JsonFiles::ReadResult::Complete && parse(text.data(), bytes, factoryCrc, uid);
}

bool InterleaveMatchProfiles::parse(const char *text, size_t bytes, uint16_t factoryCrc, const uint32_t uid[3]) {
    if (!empty() || bytes > MaxBytes || !JsonInput::completeObject(text, bytes, 5)) return false;
    constexpr size_t Capacity = JSON_OBJECT_SIZE(4) + JSON_ARRAY_SIZE(MaxProfiles) +
        MaxProfiles * (JSON_OBJECT_SIZE(7) + JSON_ARRAY_SIZE(scope::InterleaveMatch::Taps)) + 128;
    JsonWorkspace::Document<Capacity> document;
    if (!document || deserializeJson(*document, text, bytes, DeserializationOption::NestingLimit(5))) return false;
    const auto root = document->as<JsonObjectConst>();
    if (!root["schema"].is<unsigned>() || root["schema"].as<unsigned>() != 1 ||
        !root["factory_crc16"].is<uint16_t>() || root["factory_crc16"].as<uint16_t>() != factoryCrc)
        return false;
    const auto serial = root["serial"].as<JsonString>();
    constexpr char Hex[] = "0123456789ABCDEF";
    if (serial.size() != 24) return false;
    for (unsigned i = 0; i < serial.size(); ++i)
        if (serial.c_str()[i] != Hex[(uid[i / 8] >> (28 - 4 * (i % 8))) & 15]) return false;
    const auto profiles = root["profiles"].as<JsonArrayConst>();
    if (!profiles.size() || profiles.size() > MaxProfiles) return false;
    InterleaveMatchProfiles candidate;
    for (JsonObjectConst item : profiles) {
        const auto taps = item["q15"].as<JsonArrayConst>();
        if (!item["core_mhz"].is<uint16_t>() || !item["rate_num"].is<uint32_t>() ||
            !item["rate_den"].is<uint32_t>() || !item["range_mv"].is<uint16_t>() ||
            !item["coupling"].is<uint8_t>() || !item["position"].is<int16_t>() ||
            taps.size() != scope::InterleaveMatch::Taps)
            return false;
        auto &entry = candidate.entries_[candidate.count_];
        entry.clockMhz = item["core_mhz"].as<uint16_t>();
        entry.rate = {item["rate_num"].as<uint32_t>(), item["rate_den"].as<uint32_t>()};
        entry.rangeMv = item["range_mv"].as<uint16_t>();
        entry.position = item["position"].as<int16_t>();
        const unsigned coupling = item["coupling"].as<uint8_t>();
        const uint32_t clock = uint32_t(entry.clockMhz) * 1000000U;
        if (!InterleaveProfile::timing(clock, adcDividerAt(clock)) || !entry.rate.valid() ||
            entry.rate.value() > double(clock) * 2 / adcDividerAt(clock) ||
            !scope::VoltageRanges::contains(entry.rangeMv) || entry.position < 0 || entry.position > 160 ||
            coupling > 1)
            return false;
        entry.coupling = static_cast<scope::Coupling>(coupling);
        for (unsigned i = 0; i < scope::InterleaveMatch::Taps; ++i) {
            if (!taps[i].is<int16_t>()) return false;
            entry.filter.q15[i] = taps[i].as<int16_t>();
        }
        scope::ChannelMetadata channel;
        channel.millivoltsPerDivision = entry.rangeMv;
        channel.coupling = entry.coupling;
        channel.analogPosition = entry.position;
        if (!entry.filter.valid() || candidate.find(clock, entry.rate, channel)) return false;
        ++candidate.count_;
    }
    *this = candidate;
    return true;
}
} // namespace openosc::calibration
