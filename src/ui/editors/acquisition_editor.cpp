#include "openosc/ui/editors/scope_editor.hpp"
#include "openosc/acquisition/timebases.hpp"
#include "openosc/acquisition/record_depths.hpp"
#include "openosc/ui/scope/format.hpp"
#include <cstdio>

namespace openosc::ui {
namespace {
constexpr unsigned Timebases = sizeof scope::Timebases::Values / sizeof *scope::Timebases::Values;
}
PropertyKind AcquisitionEditor::kind(unsigned row) const {
    switch (row) {
    case Timebase: case Memory: case DeepMemory: return PropertyKind::Choice;
    case Rolling: return PropertyKind::Toggle;
    default: return PropertyKind::Info;
    }
}
void AcquisitionEditor::name(unsigned row, char *output, size_t capacity) const {
    static constexpr const char *names[] = {"Time / div", "Record memory", "Captured", "Sample rate", "Rolling", "Deep memory"};
    std::snprintf(output, capacity, "%s", row < Fields ? names[row] : "");
}
unsigned AcquisitionEditor::options(unsigned row) const {
    return row == Timebase ? Timebases + 1 : row == Memory ? scope::RecordBudget::StepCount : row == Rolling || row == DeepMemory ? 2 : 0;
}
unsigned AcquisitionEditor::option(unsigned row) const {
    if (row == Timebase) {
        if (request_.trend) return Timebases;
        for (unsigned i = 0; i < Timebases; ++i) if (scope::Timebases::Values[i] == request_.timebaseNs) return i;
        return Timebases + 1;
    }
    if (row == Memory) {
        for (unsigned i = 0; i < scope::RecordBudget::StepCount; ++i) if (scope::RecordBudget::Steps[i] == request_.recordPoints) return i;
        return scope::RecordBudget::StepCount;
    }
    return row == Rolling ? request_.rollAuto : row == DeepMemory ? !request_.longerRecord : 0;
}
void AcquisitionEditor::optionName(unsigned row, unsigned index, char *output, size_t capacity) const {
    if (row == Timebase && index == Timebases) std::snprintf(output, capacity, "Trend");
    else if (row == Timebase && index < Timebases) plot::Format::quantity(scope::Timebases::Values[index] / 1000000000.0, "s", output, capacity);
    else if (row == Memory && index < scope::RecordBudget::StepCount) {
        char total[16]; plot::Format::points(scope::RecordBudget::Steps[index], total, sizeof total);
        std::snprintf(output, capacity, "%s total points", total);
    } else if (row == Rolling) std::snprintf(output, capacity, "%s", index ? "Auto" : "Off");
    else if (row == DeepMemory) std::snprintf(output, capacity, "%s", index ? "Higher sample rate" : "Longer record");
    else if (capacity) output[0] = 0;
}
void AcquisitionEditor::value(unsigned row, char *output, size_t capacity) const {
    if (row == Timebase && request_.trend) optionName(row, Timebases, output, capacity);
    else if (row == Timebase) plot::Format::quantity(request_.timebaseNs / 1000000000.0, "s", output, capacity);
    else if (row == Memory) {
        char total[16]; plot::Format::points(request_.recordPoints, total, sizeof total);
        std::snprintf(output, capacity, "%s total points", total);
    } else if (row == Captured) {
        // The real per-channel length of the last record, which may differ from the budget split.
        const unsigned channels = scope::RecordBudget::channels(metadata_.channels[0].enabled, metadata_.channels[1].enabled);
        if (metadata_.samples) std::snprintf(output, capacity, "%lu x %u ch", (unsigned long)metadata_.samples, channels);
        else std::snprintf(output, capacity, "No data");
    } else if (row == Rate) {
        char rate[32] = "No data";
        if (metadata_.samplesPerSecond.valid()) plot::Format::quantity(metadata_.samplesPerSecond.value(), "S/s", rate, sizeof rate);
        std::snprintf(output, capacity, "%s%s", metadata_.samplesPerSecond.valid() && !metadata_.timingVerified ? "~" : "", rate);
    } else PropertyList::value(row, output, capacity);
}
bool AcquisitionEditor::choose(unsigned row, unsigned index) {
    if (index >= options(row)) return false;
    if (row == Timebase) {
        request_.trend = index == Timebases;
        if (!request_.trend) request_.timebaseNs = scope::Timebases::Values[index];
    }
    else if (row == Memory) request_.recordPoints = scope::RecordBudget::Steps[index];
    else if (row == Rolling) request_.rollAuto = index;
    else if (row == DeepMemory) request_.longerRecord = index == 0;
    else return false;
    return true;
}
} // namespace openosc::ui
