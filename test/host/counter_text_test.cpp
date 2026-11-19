// g++ -std=gnu++17 -Iinclude -Ilib/lvgl test/host/counter_text_test.cpp -o <scratchpad>/counter_text_test
#include "openosc/ui/scope/counter_text.hpp"
#include <cstring>

using namespace openosc;
static int failures = 0;
static void expect(const char *got, const char *want) {
    if (std::strcmp(got, want)) { std::printf("got \"%s\", want \"%s\"\n", got, want); ++failures; }
}
// The text after `n` settled readings of `hertz` (Clean unless told).
static void reading(double hertz, unsigned average, const char *want, int ppm = 0,
                    scope::CounterQuality quality = scope::CounterQuality::Clean) {
    plot::CounterText text;
    plot::CounterOptions o; o.source = 1; o.average = uint8_t(average); o.ppm = int16_t(ppm);
    scope::CounterSnapshot s;
    s.channels[0].valid = true; s.channels[0].hertz = hertz; s.channels[0].quality = quality;
    char out[32];
    for (unsigned i = 0; i < average; ++i) { s.sequence = i + 1; text.update(s, o, out, sizeof out); }
    expect(out, want);
}
int main() {
    // The tester's worked checks (batch 4 item 6), N = 1 then N = 10.
    reading(1e3, 1, "1.00kHz");       reading(1e3, 10, "1.000kHz");
    reading(1e4, 1, "10.00kHz");      reading(1e4, 10, "10.000kHz");
    reading(1e5, 1, "100.00kHz");     reading(1e5, 10, "100.000kHz");
    reading(1e6, 1, "1.00000MHz");    reading(1e6, 10, "1.000000MHz");
    reading(2e6, 1, "2.00000MHz");    reading(2e6, 10, "2.000000MHz");
    reading(1e7, 1, "10.0000MHz");    reading(1e7, 10, "10.00000MHz");
    reading(2e7, 1, "20.0000MHz");    reading(2e7, 10, "20.00000MHz");
    reading(1999618, 1, "1.99962MHz");
    reading(1e6, 1, "999.81kHz", -190);  // 1 MHz at -190 ppm: 999810 Hz, step 10 Hz.
    reading(150, 10, "150Hz"); reading(15.5, 1, "20Hz"); reading(1.2, 10, "1Hz");
    reading(999996, 1, "1.00000MHz"); // Rounds up into the next unit.
    reading(1013804, 3, "1.01380MHz"); // s = 1.15 Hz: step 10 Hz.
    reading(1e6, 1, "1.00MHz?", 0, scope::CounterQuality::Unverified);
    reading(1234.5, 1, "1.23kHz?", 0, scope::CounterQuality::Changing);
    reading(0, 1, "no signal");
    reading(4, 1, "4Hz"); reading(0.4, 10, "0.4Hz");     // Settled low counts: one digit at least.
    reading(999.6, 1, "1.00kHz?", 0, scope::CounterQuality::Changing); // Three digits after the carry.
    reading(9999999.7, 10, "10.00000MHz");                // Seven.
    {   // Hysteresis: s = 1.4 ppm * f crosses 10 Hz at 7.14 MHz; 7.3 MHz keeps the 10 Hz step it came with.
        plot::CounterText text; plot::CounterOptions o; o.source = 1; scope::CounterSnapshot s; char out[32];
        s.channels[0].valid = true;
        s.channels[0].hertz = 7e6; s.sequence = 1; text.update(s, o, out, sizeof out); expect(out, "7.00000MHz");
        s.channels[0].hertz = 7.3e6; s.sequence = 2; text.update(s, o, out, sizeof out); expect(out, "7.30000MHz");
        s.channels[0].hertz = 8e6; s.sequence = 3; text.update(s, o, out, sizeof out); expect(out, "8.0000MHz");
        s.channels[0].hertz = 6.9e6; s.sequence = 4; text.update(s, o, out, sizeof out); expect(out, "6.9000MHz");
        s.channels[0].hertz = 6.3e6; s.sequence = 5; text.update(s, o, out, sizeof out); expect(out, "6.30000MHz");
    }
    std::printf("counter text: %s\n", failures ? "FAILED" : "ok");
    return failures != 0;
}
