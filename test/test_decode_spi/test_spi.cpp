// SPI decoder (src/decode/protocols/spi.cpp) on synthetic MOSI (CH1) and SCLK (CH2) lines at 10 MS/s:
// 20 points a bit, data set 5 points before the sampling edge; the default 10 us gap is 100 points.
#include "openosc/decode/protocols/spi.hpp"
#include <unity.h>
#include <vector>

using namespace openosc;
using namespace openosc::decoding;
using namespace openosc::decoding::protocols;

namespace {
const Rational Fs{10000000, 1};
ResultSet result;

struct Bus {
    std::vector<Transition> edges;
    uint32_t t = 1000; // Idle longer than the gap before the first edge: the word boundary is known.
    int data = 0, clock = 0;
    void set(uint8_t channel, int level, uint32_t at) {
        int &line = channel == 0 ? data : clock;
        if (line == level) return;
        line = level;
        edges.push_back({at, uint8_t(level), channel});
    }
    // Mode 0: data changes while SCLK is low, sampled on the rising edge.
    void bits(uint16_t value, unsigned count) {
        for (int i = int(count) - 1; i >= 0; --i) {
            set(0, (value >> i) & 1, t);
            set(1, 1, t + 5);
            set(1, 0, t + 15);
            t += 20;
        }
    }
};

void decode(const Bus &bus, const SpiOptions &options, int initialClock = 0) {
    SpiDecoder decoder;
    TEST_ASSERT_TRUE(decoder.configure(options, Fs));
    result.reset(RecordKey{}, 0, Protocol::Spi);
    decoder.start(result, 0);
    decoder.initialLevel(1, initialClock);
    decoder.feed(bus.edges.data(), unsigned(bus.edges.size()), result);
    decoder.finish(bus.t + 1000, result);
}
} // namespace

void setUp() {}
void tearDown() {}

void test_mode0_byte_msb_first() {
    Bus bus;
    bus.bits(0xA5, 8);
    decode(bus, SpiOptions{});
    TEST_ASSERT_EQUAL_UINT(1, result.count());
    const auto &e = result.event(0);
    TEST_ASSERT_EQUAL_HEX8(SpiFrame, e.kind);
    TEST_ASSERT_EQUAL_UINT8(1, e.count);
    TEST_ASSERT_EQUAL_HEX8(0xA5, *result.pool(e.payload));
    TEST_ASSERT_EQUAL_HEX16(0, e.flags & (ErrorFlags | Ambiguous | DecodeEvent::Cut));
}

void test_two_words_in_one_burst() {
    Bus bus;
    bus.bits(0x3C, 8);
    bus.bits(0x81, 8);
    decode(bus, SpiOptions{});
    TEST_ASSERT_EQUAL_UINT(1, result.count());
    const auto &e = result.event(0);
    TEST_ASSERT_EQUAL_UINT8(2, e.count);
    TEST_ASSERT_EQUAL_HEX8(0x3C, result.pool(e.payload)[0]);
    TEST_ASSERT_EQUAL_HEX8(0x81, result.pool(e.payload)[1]);
}

void test_partial_last_word_is_cut() {
    Bus bus;
    bus.bits(0xA5, 8);
    bus.bits(0x2, 2);
    decode(bus, SpiOptions{});
    TEST_ASSERT_EQUAL_UINT(1, result.count());
    const auto &e = result.event(0);
    TEST_ASSERT_EQUAL_UINT8(1, e.count); // The partial word is not stored.
    TEST_ASSERT_TRUE(e.flags & DecodeEvent::Cut);
    TEST_ASSERT_EQUAL_UINT32(2, e.aux & 0x1Fu);
}

void test_clock_not_at_cpol_idle_is_framing() {
    SpiOptions options;
    options.cpol = 1; // Mode 2 configured, but the clock idles low.
    Bus bus;
    bus.bits(0xA5, 8);
    decode(bus, options);
    TEST_ASSERT_TRUE(result.count() >= 1);
    TEST_ASSERT_TRUE(result.event(0).flags & Framing);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_mode0_byte_msb_first);
    RUN_TEST(test_two_words_in_one_burst);
    RUN_TEST(test_partial_last_word_is_cut);
    RUN_TEST(test_clock_not_at_cpol_idle_is_framing);
    return UNITY_END();
}
