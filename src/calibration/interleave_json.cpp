#include "openosc/calibration/interleave_file.hpp"
#include "openosc/settings/json_input.hpp"
#include <ArduinoJson.h>

namespace openosc::calibration {
namespace {
constexpr size_t DocumentBytes = JSON_OBJECT_SIZE(5) + JSON_ARRAY_SIZE(InterleaveProfile::Clocks) +
                                 InterleaveProfile::Clocks * JSON_OBJECT_SIZE(3) + 64;
}
bool InterleaveJson::parse(const char *text,size_t bytes,const CalibrationData &factory,InterleaveProfile &output) {
    if (bytes>MaxBytes || !JsonInput::completeObject(text,bytes,3)) return false;
    StaticJsonDocument<DocumentBytes> document;
    if (deserializeJson(document,text,bytes,DeserializationOption::NestingLimit(3))) return false;
    const auto root=document.as<JsonObjectConst>();
    const unsigned schema=root["schema"].is<unsigned>() ? root["schema"].as<unsigned>() : 0;
    if ((schema!=1 && schema!=2) || !root["enabled"].is<bool>() ||
        !root["factory_crc16"].is<uint16_t>() || root["factory_crc16"].as<uint16_t>()!=factory.crc() ||
        !root["offset_codes"].is<float>()) return false;
    InterleaveProfile candidate; candidate.factoryCrc=factory.crc();
    candidate.enabled=root["enabled"]; candidate.offset=root["offset_codes"].as<float>();
    if (schema==2) {
        if (!root["clocks"].is<JsonArrayConst>()) return false;
        const auto clocks=root["clocks"].as<JsonArrayConst>();
        if (clocks.size()>InterleaveProfile::Clocks) return false;
        for (JsonObjectConst entry : clocks) {
            if (!entry["core_mhz"].is<uint16_t>() || !entry["adc_divider"].is<uint8_t>() || !entry["offset_codes"].is<float>()) return false;
            const uint16_t mhz=entry["core_mhz"].as<uint16_t>();
            const uint8_t divider=entry["adc_divider"].as<uint8_t>();
            if (!InterleaveProfile::timing(uint32_t(mhz)*1000000U,divider) || candidate.at(uint32_t(mhz)*1000000U,divider)) return false;
            candidate.clocked[candidate.clockedCount++]={mhz,divider,entry["offset_codes"].as<float>()};
        }
    }
    if (!candidate.valid(factory)) return false;
    output=candidate;
    return true;
}
size_t InterleaveJson::encode(const InterleaveProfile &values,const CalibrationData &factory,char *output,size_t capacity) {
    if (!values.valid(factory) || !output || !capacity) return 0;
    StaticJsonDocument<DocumentBytes> document;
    document["schema"]=values.clockedCount ? 2 : 1; document["enabled"]=values.enabled;
    document["factory_crc16"]=values.factoryCrc; document["offset_codes"]=values.offset;
    if (values.clockedCount) {
        auto clocks=document.createNestedArray("clocks");
        for (unsigned i=0;i<values.clockedCount;++i) {
            auto entry=clocks.createNestedObject();
            entry["core_mhz"]=values.clocked[i].coreMhz; entry["adc_divider"]=values.clocked[i].adcDivider;
            entry["offset_codes"]=values.clocked[i].offset;
        }
    }
    if (document.overflowed() || measureJson(document)>=capacity) return 0;
    return serializeJson(document,output,capacity);
}
} // namespace openosc::calibration
