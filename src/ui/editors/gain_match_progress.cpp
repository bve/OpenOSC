#include "openosc/ui/editors/gain_match_progress.hpp"
#include "openosc/ui/scope/format.hpp"
#include "openosc/data/decimal.hpp"
#include <cstdio>

namespace openosc::ui {
namespace {
using Step = calibration::ChannelMatch::Step;
using Issue = calibration::ChannelMatch::Issue;
using Row = GainMatchProgress::Row;
constexpr Row Ready[] = {Row::Target, Row::Range, Row::Probes, Row::Measure, Row::Cancel};
constexpr Row Measuring[] = {Row::Target, Row::Range, Row::Progress, Row::Measuring, Row::Cancel};
constexpr Row Swap[] = {Row::Target, Row::Range, Row::Ratio, Row::Swapped, Row::Cancel};
constexpr Row Review[] = {Row::Spans, Row::Compensation, Row::Ratio, Row::Correction, Row::Resolution, Row::Save, Row::Swap, Row::Cancel};
constexpr Row ReviewMixedProbes[] = {Row::Spans, Row::Compensation, Row::Ratio, Row::Correction, Row::Resolution, Row::Save, Row::Cancel};
constexpr Row Swapped[] = {Row::Spans, Row::Compensation, Row::Probe, Row::Correction, Row::Resolution, Row::Save, Row::Cancel};
struct Rows { const Row *rows; unsigned count; };
template <unsigned N> constexpr Rows of(const Row (&rows)[N]) { return {rows, N}; }
Rows rows(const calibration::ChannelMatch &match) {
    switch (match.step()) {
    case Step::Measuring: return of(Measuring);
    case Step::Swap: return of(Swap);
    case Step::Review:
        if (match.passes() >= 2) return of(Swapped);
        return match.canSwap() ? of(Review) : of(ReviewMixedProbes);
    default: return of(Ready); // Ready and Failed (Measure again).
    }
}
void percent(double fraction, char *output, size_t capacity) { // "+2.1%"
    char number[16];
    Decimal::fixed(fraction * 100, 1, number, sizeof number);
    std::snprintf(output, capacity, "%s%s%%", fraction >= 0 ? "+" : "", number);
}
void factor(double value, char *output, size_t capacity) {
    char number[16];
    Decimal::fixed(value, 4, number, sizeof number);
    std::snprintf(output, capacity, "x%s", number);
}
} // namespace
const char *GainMatchProgress::notice() const {
    switch (match_.issue()) {
    case Issue::NoFrame: return "No fresh capture: RUN";
    case Issue::Setup: return "Settings changed: measure again";
    case Issue::Square: return "Need a square with full plateaus on screen";
    case Issue::Saturated: return "Input at the ADC limit: raise V/div";
    case Issue::Span: return "Under 60 codes: lower V/div";
    case Issue::Flat: return "Plateaus not flat: compensate the probes";
    case Issue::Implausible: return "Correction outside 0.8..1.25: check probes";
    case Issue::None: break;
    }
    switch (match_.step()) {
    case Step::Measuring: return "Keep both tips on the source";
    case Step::Swap: return "Swap the probes, both on the source";
    case Step::Review:
        if (match_.passes() >= 2) return "Probe ratio separated";
        return match_.canSwap() ? "Save, or swap probes to cancel them" : "Swap needs equal probe ratios";
    default: return "Both tips on one stable square";
    }
}
unsigned GainMatchProgress::count() const { return rows(match_).count; }
Row GainMatchProgress::at(unsigned row) const {
    const auto list = rows(match_);
    return row < list.count ? list.rows[row] : Row::Cancel;
}
unsigned GainMatchProgress::defaultRow() const {
    const auto list = rows(match_);
    for (unsigned i = 0; i < list.count; ++i)
        if (list.rows[i] == Row::Measure || list.rows[i] == Row::Swapped || list.rows[i] == Row::Save) return i;
    return list.count - 1;
}
PropertyKind GainMatchProgress::kind(unsigned row) const {
    switch (at(row)) {
    case Row::Measure: case Row::Swapped: case Row::Save: case Row::Swap: case Row::Cancel: return PropertyKind::Action;
    default: return PropertyKind::Info;
    }
}
void GainMatchProgress::name(unsigned row, char *output, size_t capacity) const {
    static constexpr const char *Names[] = {"Correct", "V/div", "Probes CH1/CH2", "Progress", "First pass", "Span CH1/CH2",
                                            "Edge delta CH1/CH2", "Probes differ", "Correction", "Resolution", "Measure", "Measuring...",
                                            "Probes swapped", "Save", "Swap probes", "Cancel"};
    const auto r = at(row);
    std::snprintf(output, capacity, "%s", r == Row::Measure && match_.step() == Step::Failed ? "Measure again" : Names[unsigned(r)]);
}
void GainMatchProgress::value(unsigned row, char *output, size_t capacity) const {
    if (capacity) *output = 0;
    char a[20], b[20];
    switch (at(row)) {
    case Row::Target: std::snprintf(output, capacity, "CH%u to CH%u", match_.target() + 1, match_.reference() + 1); break;
    case Row::Range: {
        const auto &channel = match_.setup(match_.target());
        const double volts = channel.millivoltsPerDivision / 1000.0 * channel.probeNumerator / channel.probeDenominator;
        plot::Format::quantity(volts, "V/div", a, sizeof a);
        std::snprintf(output, capacity, "%s, DC", a);
        break;
    }
    case Row::Probes:
        std::snprintf(output, capacity, "%lu:%lu / %lu:%lu", (unsigned long)match_.setup(0).probeNumerator,
                      (unsigned long)match_.setup(0).probeDenominator, (unsigned long)match_.setup(1).probeNumerator,
                      (unsigned long)match_.setup(1).probeDenominator);
        break;
    case Row::Progress: std::snprintf(output, capacity, "Pass %u %u%%", match_.passes() + 1, match_.percent()); break;
    case Row::Ratio: case Row::Correction:
        factor(at(row) == Row::Ratio ? match_.ratio() : match_.proposed(), a, sizeof a);
        if (at(row) == Row::Ratio) { std::snprintf(output, capacity, "%s", a); break; }
        factor(match_.inForce(), b, sizeof b);
        std::snprintf(output, capacity, "%s (now %s)", a, b);
        break;
    case Row::Spans:
        Decimal::fixed(match_.span(0), 2, a, sizeof a); Decimal::fixed(match_.span(1), 2, b, sizeof b);
        std::snprintf(output, capacity, "%s / %s", a, b);
        break;
    case Row::Compensation:
        percent(match_.compensation(0), a, sizeof a); percent(match_.compensation(1), b, sizeof b);
        std::snprintf(output, capacity, "%s / %s", a, b);
        break;
    case Row::Probe: percent(match_.probeDifference(), output, capacity); break;
    case Row::Resolution: // The ADC's noise resolves the levels, or their quantization limits the ratio.
        if (match_.resolution() < .001) { std::snprintf(output, capacity, "<0.1%%"); break; }
        Decimal::fixed(match_.resolution() * 100, 1, a, sizeof a);
        std::snprintf(output, capacity, "+-%s%%", a);
        break;
    default: break;
    }
}
} // namespace openosc::ui
