// UART decoder (src/decode/protocols/uart.cpp) on synthetic lines: 1 MS/s, 100 kBd, 10 points a bit.
#include "openosc/decode/protocols/uart.hpp"
#include <unity.h>
#include <vector>

using namespace openosc;
using namespace openosc::decoding;
using namespace openosc::decoding::protocols;

namespace {
constexpr uint32_t PointsPerBit = 10;
const Rational Fs{1000000, 1};
ResultSet result;

// A line idling at mark; each call appends bits (LSB first data) at PointsPerBit points each.
struct Line {
    std::vector<uint8_t> levels;
    void hold(int level, uint32_t bits) { levels.insert(levels.end(), bits * PointsPerBit, uint8_t(level)); }
    void character(uint16_t value, unsigned dataBits, int parityBit, int stopLevel) {
        hold(0, 1);
        for (unsigned i = 0; i < dataBits; ++i) hold((value >> i) & 1, 1);
        if (parityBit >= 0) hold(parityBit, 1);
        hold(stopLevel, 1);
    }
    std::vector<Transition> transitions() const {
        std::vector<Transition> out;
        for (uint32_t i = 1; i < levels.size(); ++i)
            if (levels[i] != levels[i - 1]) out.push_back({i, levels[i], 0});
        return out;
    }
};

void decode(const Line &line, const UartOptions &options) {
    UartDecoder decoder;
    TEST_ASSERT_TRUE(decoder.configure(options, Fs));
    result.reset(RecordKey{}, 0, Protocol::Uart);
    decoder.start(result, line.levels.front());
    const auto t = line.transitions();
    decoder.feed(t.data(), unsigned(t.size()), result);
    decoder.finish(uint32_t(line.levels.size()), result);
}

UartOptions options8N1() {
    UartOptions o;
    o.baud = 100000;
    return o;
}
} // namespace

void setUp() {}
void tearDown() {}

void test_byte_0x55_8n1() {
    Line line;
    line.hold(1, 30); // More than a whole frame of idle: the phase is known.
    line.character(0x55, 8, -1, 1);
    line.hold(1, 40);
    decode(line, options8N1());
    TEST_ASSERT_EQUAL_UINT(1, result.count());
    const auto &e = result.event(0);
    TEST_ASSERT_EQUAL_HEX8(UartData, e.kind);
    TEST_ASSERT_EQUAL_UINT8(1, e.count);
    TEST_ASSERT_EQUAL_HEX8(0x55, *result.pool(e.payload));
    TEST_ASSERT_EQUAL_UINT32(30 * PointsPerBit, e.first);
    TEST_ASSERT_EQUAL_HEX16(0, e.flags & (ErrorFlags | Ambiguous));
}

void test_close_characters_form_one_event() {
    Line line;
    line.hold(1, 30);
    line.character(0x41, 8, -1, 1);
    line.hold(1, 2);
    line.character(0xA3, 8, -1, 1);
    line.hold(1, 40);
    decode(line, options8N1());
    TEST_ASSERT_EQUAL_UINT(1, result.count());
    const auto &e = result.event(0);
    TEST_ASSERT_EQUAL_UINT8(2, e.count);
    TEST_ASSERT_EQUAL_HEX8(0x41, result.pool(e.payload)[0]);
    TEST_ASSERT_EQUAL_HEX8(0xA3, result.pool(e.payload)[1]);
}

void test_stop_bit_at_space_is_framing_error() {
    Line line;
    line.hold(1, 30);
    line.character(0x55, 8, -1, 0);
    line.hold(1, 40);
    decode(line, options8N1());
    TEST_ASSERT_EQUAL_UINT(1, result.count());
    TEST_ASSERT_EQUAL_HEX8(UartData, result.event(0).kind);
    TEST_ASSERT_TRUE(result.event(0).flags & Framing);
}

void test_wrong_even_parity_is_flagged() {
    UartOptions o = options8N1();
    o.format.parity = Parity::Even;
    Line good, bad;
    good.hold(1, 30);
    good.character(0x07, 8, 1, 1); // Three ones: even parity bit 1.
    good.hold(1, 40);
    bad.hold(1, 30);
    bad.character(0x07, 8, 0, 1);
    bad.hold(1, 40);
    decode(good, o);
    TEST_ASSERT_EQUAL_UINT(1, result.count());
    TEST_ASSERT_FALSE(result.event(0).flags & ParityError);
    decode(bad, o);
    TEST_ASSERT_EQUAL_UINT(1, result.count());
    TEST_ASSERT_TRUE(result.event(0).flags & ParityError);
}

void test_line_held_at_space_is_a_break() {
    Line line;
    line.hold(1, 30);
    line.hold(0, 25);
    line.hold(1, 40);
    decode(line, options8N1());
    TEST_ASSERT_EQUAL_UINT(1, result.count());
    TEST_ASSERT_EQUAL_HEX8(UartBreak, result.event(0).kind);
}

void test_invalid_configuration_is_refused() {
    UartDecoder decoder;
    UartOptions o = options8N1();
    o.baud = 0;
    TEST_ASSERT_FALSE(decoder.configure(o, Fs));
    o = options8N1();
    o.baud = 1000000; // One point a bit: below the two points a bit the clock needs.
    TEST_ASSERT_FALSE(decoder.configure(o, Fs));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_byte_0x55_8n1);
    RUN_TEST(test_close_characters_form_one_event);
    RUN_TEST(test_stop_bit_at_space_is_framing_error);
    RUN_TEST(test_wrong_even_parity_is_flagged);
    RUN_TEST(test_line_held_at_space_is_a_break);
    RUN_TEST(test_invalid_configuration_is_refused);
    return UNITY_END();
}
