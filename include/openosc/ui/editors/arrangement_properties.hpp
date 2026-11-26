#pragma once
#include "openosc/settings/screen_arrangement.hpp"
#include "openosc/ui/editors/property_list.hpp"

namespace openosc::ui {
// The rows every layout list shares: "Position N" (1-based).
struct ArrangementRows {
    static void name(unsigned slot, char *output, size_t capacity);
    static void blockName(const char *name, char *output, size_t capacity);
};
// LAYOUT (Scope, DMM digital, DMM analog): one Choice row per position, top to bottom under the
// status bar, each offering every block. Taking block B at position N moves B there and shifts the
// blocks between (BlockOrder::place), so the other rows show the new order at once and no block
// is doubled. MENU commits a list choice. When browsing, Right steps forward and Left in Step
// mode steps back. The owner applies the change immediately. F4 cancels an open list.
template<class Block> class ArrangementProperties final : public PropertyList {
public:
    using Order = BlockOrder<Block>;
    explicit ArrangementProperties(const char *title = "LAYOUT", const Order &order = Order()) : title_(title), order_(order) {}
    const Order &order() const { return order_; }

    const char *title() const override { return title_; }
    unsigned count() const override { return Order::Size; }
    PropertyKind kind(unsigned) const override { return PropertyKind::Choice; }
    void name(unsigned row, char *output, size_t capacity) const override { ArrangementRows::name(row, output, capacity); }
    unsigned options(unsigned row) const override { return row < Order::Size ? Order::Size : 0; }
    unsigned option(unsigned row) const override { return row < Order::Size ? unsigned(order_.at(row)) : 0; }
    void optionName(unsigned, unsigned index, char *output, size_t capacity) const override {
        ArrangementRows::blockName(index < Order::Size ? Order::name(Block(index)) : "", output, capacity);
    }
    bool choose(unsigned row, unsigned index) override { return index < Order::Size && order_.place(row, Block(index)); }

private:
    const char *title_;
    Order order_;
};
} // namespace openosc::ui
