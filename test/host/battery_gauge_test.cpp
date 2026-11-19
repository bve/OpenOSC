// g++ -std=gnu++17 -Iinclude test/host/battery_gauge_test.cpp -o /tmp/battery_gauge_test && /tmp/battery_gauge_test
#include "openosc/power/battery.hpp"
#include <cstdio>
#include <cstdlib>

using openosc::power::BatteryGauge;

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { std::printf("%s:%d: %s\n", __FILE__, __LINE__, #cond); ++failures; } } while (0)

static void window(BatteryGauge &g, int mv) {
    for (unsigned i = 0; i < BatteryGauge::Window; ++i) g.sample(uint16_t((mv * 4166 + 2500) / 5000));
}
static void settle(BatteryGauge &g, int mv) {
    for (int i = 0; i < 20; ++i) window(g, mv);
}
static BatteryGauge onBattery() {
    BatteryGauge g;
    for (int i = 0; i < 11; ++i) g.digital(false, false);
    return g;
}

static void levels() {
    const struct { int mv; unsigned bars; bool low; } cases[] = {
        {4150, 5, false}, {3960, 5, false}, {3900, 4, false}, {3820, 4, false}, {3800, 3, false},
        {3700, 2, false}, {3600, 1, false}, {3460, 1, false}, {3440, 1, true}, {3300, 1, true}};
    for (const auto &c : cases) {
        auto g = onBattery();
        window(g, c.mv);
        CHECK(g.state().valid && g.state().measured && !g.state().low); // No low verdict from one window.
        settle(g, c.mv);
        if (g.state().bars != c.bars || g.state().low != c.low) std::printf("at %d mV\n", c.mv);
        CHECK(g.state().bars == c.bars);
        CHECK(g.state().low == c.low);
    }
}

static void noiseAtThreshold() {
    auto g = onBattery();
    window(g, 3830);
    const unsigned bars = g.state().bars;
    const int shownMv = g.state().millivolts;
    for (int i = 0; i < 50; ++i) window(g, i & 1 ? 3800 : 3850); // +-25 mV around the 4-bar threshold.
    CHECK(g.state().bars == bars);
    CHECK(g.state().millivolts + BatteryGauge::ShownStepMv > shownMv && g.state().millivolts < shownMv + BatteryGauge::ShownStepMv);
    settle(g, 3770); // Clearly below 3820 - 40.
    CHECK(g.state().bars == 3);
    settle(g, 3840); // Not yet 3820 + 40.
    CHECK(g.state().bars == 3);
    settle(g, 3870);
    CHECK(g.state().bars == 4);
}

static void discharge() {
    auto g = onBattery();
    unsigned last = 5, lastPercent = 100;
    bool wasLow = false;
    for (int mv = 4150; mv >= 3300; mv -= 5) {
        window(g, mv + ((mv / 5) % 3 - 1) * 15); // A little ripple.
        CHECK(g.state().bars <= last);
        CHECK(g.state().percent <= lastPercent);
        CHECK(!wasLow || g.state().low);
        last = g.state().bars; lastPercent = g.state().percent; wasLow = g.state().low;
    }
    CHECK(g.state().low && g.state().bars == 1 && g.state().percent == 0);
}

static void lowHysteresis() {
    auto g = onBattery();
    settle(g, 3430);
    CHECK(g.state().low);
    settle(g, 3470);
    CHECK(g.state().low);
    settle(g, 3500);
    CHECK(!g.state().low);
    settle(g, 3440);
    CHECK(g.state().low);
}

static void externalPower() {
    auto g = onBattery();
    window(g, 3700);
    for (int i = 0; i < 10; ++i) g.digital(true, false);
    CHECK(!g.state().external); // Debounced: 11 equal polls.
    g.digital(true, false);
    CHECK(g.state().external && !g.state().full && !g.state().low);
    window(g, 3300); // Ignored on external power.
    CHECK(!g.state().low);
    for (int i = 0; i < 11; ++i) g.digital(false, false);
    CHECK(!g.state().external && !g.state().valid);
    window(g, 3900);
    CHECK(g.state().valid && g.state().bars == 4 && g.state().millivolts >= 3895);
}

int main() {
    levels();
    noiseAtThreshold();
    discharge();
    lowHysteresis();
    externalPower();
    std::printf(failures ? "%d failure(s)\n" : "battery gauge: ok\n", failures);
    return failures ? EXIT_FAILURE : EXIT_SUCCESS;
}
