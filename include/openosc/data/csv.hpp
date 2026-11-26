#pragma once
#include "openosc/acquisition/frame.hpp"
#include "openosc/acquisition/trend.hpp"
#include "openosc/measurement/dmm/result.hpp"

namespace openosc {
class Csv {
public:
    static constexpr const char *DmmHeader =
        "timestamp_us,sequence,status,quantity,value_si,unit,display,packet_hold,user_hold,peer_relative\r\n";
    static constexpr const char *ScopeHeader = "index,time_seconds,ch1_code,ch2_code,ch1_volts,ch2_volts\r\n";
    // With the Scope's DMM trace shown: the reading in force at each row's time, in SI (scopeMeter's line).
    static constexpr const char *ScopeMeterHeader = "index,time_seconds,ch1_code,ch2_code,ch1_volts,ch2_volts,dmm\r\n";
    // A trend's rows (Csv::trend): the min, mean and max of each channel per stored point, in volts, and of
    // the DMM in SI with the Scope's DMM trace shown.
    static constexpr const char *ScopeTrendHeader = "index,time_seconds,ch1_min,ch1_mean,ch1_max,ch2_min,ch2_mean,ch2_max\r\n";
    static constexpr const char *ScopeTrendMeterHeader =
        "index,time_seconds,ch1_min,ch1_mean,ch1_max,ch2_min,ch2_mean,ch2_max,dmm_min,dmm_mean,dmm_max\r\n";
    static size_t dmm(const dmm::Result &result, bool userHold, char *output, size_t capacity);
    static const char *quantity(Quantity value); // The quantity column's word: "voltage", ... "unknown".
    // meter: the row's dmm field (ScopeMeterHeader), NaN left empty; nullptr: no such column.
    static size_t scope(const scope::FrameView &frame, size_t index, char *output, size_t capacity,
                        const float *meter = nullptr);
    // The longest row scope() gives for any point of a record of these metadata, CRLF included, 0 for
    // metadata it cannot write: the widest index and time of the record (time's digits from the rate's
    // factors 2 and 5, else Decimal's 12), each recorded channel's widest code (4) and volts over its 256 codes,
    // and with `meter` the dmm field's widest reading.
    // The bound the waveform CSV's free-space check reserves a row (GitHub #164).
    static size_t scopeRowLimit(const scope::FrameMetadata &metadata, bool meter = false);
    // Row `index` of a trend copy, its time the middle of its ADC points at the copy's own rate (a last point
    // STOP cut short: to the microsecond); `metadata`: the channels' scales. `meter`: with the dmm columns; a
    // value without a number (a channel that is off, a point without a reading) is left empty.
    static size_t trend(const scope::FrameMetadata &metadata, const scope::TrendView &view, size_t index, bool meter,
                        char *output, size_t capacity);
    // The longest row trend() gives for `metadata`'s samples at its rate, CRLF included, 0 for metadata it
    // cannot write: scopeRowLimit's index and time (its fraction six digits at least), and a float at seven
    // digits for each value.
    static size_t trendRowLimit(const scope::FrameMetadata &metadata, bool meter);
    // "# trend: samples_per_point=N,last_point_samples=M": the ADC points each row covers, and the last row's
    // (fewer than N when STOP ended the record within that point).
    static size_t scopeTrend(uint32_t samplesPerPoint, uint32_t lastPointSamples, char *output, size_t capacity);
    static size_t scopeMetadata(const scope::FrameMetadata &metadata, char *output, size_t capacity);
    // "# dmm: function=...,quantity=...,unit=..." of the dmm column; function is MeterFunctions' key.
    static size_t scopeMeter(const char *function, Quantity quantity, char *output, size_t capacity);
};
} // namespace openosc
