#pragma once
#include "openosc/ui/editors/property_list.hpp"
#include "openosc/ui/scope/controls.hpp"
#include <cstdio>

namespace openosc::ui {
// DISPLAY: the section list, then a section's rows, drafts until committed. fine: a press steps
// one, a held key the coarse step.
class DisplayProperties final : public PropertyList {
public:
    explicit DisplayProperties(plot::Controls &controls, bool fine = true) : controls_(controls), fine_(fine) {}
    const char *title() const override { return controls_.listing ? "DISPLAY" : controls_.section(); }
    bool listing() const { return controls_.listing; }
    // The control row (plot::Controls::Row) of an index; Section in the list.
    unsigned row(unsigned index) const { return controls_.listing ? unsigned(plot::Controls::Section) : controls_.row(index); }
    void value(unsigned index, char *output, size_t capacity) const override {
        if (!controls_.listing && index < count()) controls_.value(row(index), output, capacity);
        else PropertyList::value(index, output, capacity);
    }
    unsigned options(unsigned index) const override { return !controls_.listing && index < count() ? controls_.choices(row(index)) : 0; }
    unsigned option(unsigned index) const override { return !controls_.listing && index < count() ? controls_.choice(row(index)) : 0; }
    void optionName(unsigned index, unsigned option, char *output, size_t capacity) const override {
        controls_.choiceName(row(index), option, output, capacity);
    }
    void adjust(unsigned index, int direction) override { if (!controls_.listing && index < count()) controls_.adjust(row(index), direction, fine_); }
    int32_t number(unsigned index) const override { return !controls_.listing && index < count() ? controls_.number(row(index)) : 0; }
    void setNumber(unsigned index, int32_t value) override { if (!controls_.listing && index < count()) controls_.setNumber(row(index), value); }
    unsigned count() const override { return controls_.listing ? unsigned(plot::Controls::Group::Count) : controls_.count(); }
    PropertyKind kind(unsigned index) const override { return controls_.listing ? PropertyKind::Page : controls_.kind(row(index)); }
    void name(unsigned index, char *output, size_t capacity) const override {
        if (controls_.listing) std::snprintf(output, capacity, "%s", plot::Controls::groupName(index));
        else controls_.name(row(index), output, capacity);
    }
private:
    plot::Controls &controls_;
    bool fine_;
};
} // namespace openosc::ui
