#include "openosc/calibration/interleave_match_file.hpp"
#include "openosc/acquisition/ranges.hpp"
#include "openosc/calibration/interleave_profile.hpp"
#include "openosc/settings/run_mode.hpp"
#include "openosc/settings/json_input.hpp"
#include "openosc/settings/json_workspace.hpp"
#include "openosc/storage/directories.hpp"
#include "openosc/storage/json_files.hpp"
#include "openosc/storage/layout.hpp"

namespace openosc::calibration {
namespace {
template <unsigned Entries>
constexpr size_t capacity() {
    return JSON_OBJECT_SIZE(4) + JSON_ARRAY_SIZE(Entries) +
           Entries * (JSON_OBJECT_SIZE(7) + JSON_ARRAY_SIZE(scope::InterleaveMatch::Taps)) + 128;
}
void serialText(const uint32_t uid[3], char (&text)[25]) {
    constexpr char Hex[] = "0123456789ABCDEF";
    for (unsigned i = 0; i < 24; ++i) text[i] = Hex[(uid[i / 8] >> (28 - 4 * (i % 8))) & 15];
    text[24] = 0;
}
JsonFiles::ReadResult readProfile(char *text, size_t capacity, size_t &bytes) {
    const auto result = JsonFiles::read(storage::Layout::InterleaveMatchFile, text, capacity, bytes);
    if (result != JsonFiles::ReadResult::Missing) return result;
    return JsonFiles::read(storage::Layout::InterleaveMatchPrevious, text, capacity, bytes);
}
}
bool InterleaveMatchProfiles::load(uint16_t factoryCrc, const uint32_t uid[3]) {
    if (!empty()) return false;
    JsonWorkspace::Text text;
    size_t bytes = 0;
    return text && readProfile(text.data(), MaxBytes + 1, bytes) ==
                       JsonFiles::ReadResult::Complete && parse(text.data(), bytes, factoryCrc, uid);
}

bool InterleaveMatchProfiles::parse(const char *text, size_t bytes, uint16_t factoryCrc, const uint32_t uid[3]) {
    if (!empty() || bytes > MaxBytes || !JsonInput::completeObject(text, bytes, 5)) return false;
    JsonWorkspace::Document<capacity<MaxProfiles>()> document;
    if (!document || deserializeJson(*document, text, bytes, DeserializationOption::NestingLimit(5))) return false;
    const auto root = document->as<JsonObjectConst>();
    if (!root["schema"].is<unsigned>() || root["schema"].as<unsigned>() != 1 ||
        !root["factory_crc16"].is<uint16_t>() || root["factory_crc16"].as<uint16_t>() != factoryCrc)
        return false;
    const auto serial = root["serial"].as<JsonString>();
    char expected[25];
    serialText(uid, expected);
    if (serial.size() != 24 || serial != JsonString(expected)) return false;
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

DataStatus InterleaveMatchProfiles::save(uint32_t clockHz, const Rational &rate, const scope::ChannelMetadata &channel,
                                         const scope::InterleaveMatch &filter, const uint32_t uid[3]) {
    const auto divider = adcDividerAt(clockHz);
    if (!InterleaveProfile::timing(clockHz, divider) || !rate.valid() || rate.value() > double(clockHz) * 2 / divider ||
        !channel.calibrated || !scope::VoltageRanges::contains(channel.millivoltsPerDivision) ||
        channel.analogPosition < 0 || channel.analogPosition > 160 || unsigned(channel.coupling) > 1 || !filter.valid())
        return DataStatus::InvalidArgument;
    InterleaveMatchProfiles candidate;
    JsonWorkspace::Text text;
    if (!text) return DataStatus::Busy;
    const auto path = storage::Layout::InterleaveMatchFile;
    size_t bytes = 0;
    const auto read = readProfile(text.data(), MaxBytes + 1, bytes);
    // Read disk each time: another profile may have been saved since this boot.
    if (read == JsonFiles::ReadResult::Complete) {
        if (!candidate.parse(text.data(), bytes, channel.calibrationCrc, uid)) return DataStatus::InvalidPacket;
    } else if (read != JsonFiles::ReadResult::Missing) return DataStatus::IoError;
    const auto *existing = candidate.find(clockHz, rate, channel);
    if (!existing && candidate.count_ == MaxProfiles) return DataStatus::NoSpace;
    unsigned index = 0;
    if (existing) {
        while (&candidate.entries_[index].filter != existing) ++index;
    } else index = candidate.count_++;
    candidate.entries_[index] = {rate, uint16_t(clockHz / 1000000), uint16_t(channel.millivoltsPerDivision),
                                 channel.analogPosition, channel.coupling, filter};
    {
        JsonWorkspace::Document<capacity<MaxProfiles>()> document;
        if (!document) return DataStatus::Busy;
        char serial[25];
        serialText(uid, serial);
        (*document)["schema"] = 1;
        (*document)["serial"] = serial;
        (*document)["factory_crc16"] = channel.calibrationCrc;
        auto profiles = document->createNestedArray("profiles");
        for (unsigned i = 0; i < candidate.count_; ++i) {
            const auto &entry = candidate.entries_[i];
            auto item = profiles.createNestedObject();
            item["core_mhz"] = entry.clockMhz; item["rate_num"] = entry.rate.numerator;
            item["rate_den"] = entry.rate.denominator; item["range_mv"] = entry.rangeMv;
            item["coupling"] = unsigned(entry.coupling); item["position"] = entry.position;
            auto taps = item.createNestedArray("q15");
            for (const auto tap : entry.filter.q15) taps.add(tap);
        }
        if (document->overflowed() || measureJson(*document) > MaxBytes) return DataStatus::IoError;
        bytes = serializeJson(*document, text.data(), MaxBytes + 1);
    }
    if (!bytes || storage::ensureDirectory(storage::Layout::Calibration) != FR_OK) return DataStatus::IoError;
    return JsonFiles::write(path, text.data(), bytes) ? DataStatus::Ok : DataStatus::IoError;
}
} // namespace openosc::calibration
