// DMM controller packets (src/measurement/dmm/packet.cpp, decoder.cpp): checksum, framing and decoding.
#include "openosc/measurement/dmm/packet.hpp"
#include "openosc/measurement/dmm/ranges.hpp"
#include "openosc/measurement/dmm/result.hpp"
#include <cstring>
#include <unity.h>

using namespace openosc;
using namespace openosc::dmm;

namespace {
// A ZT703S overload packet in the 25.000 MOhm range, captured on the instrument (src/measurement/dmm/ranges.cpp).
constexpr uint8_t Overload25MOhm[Packet::Bytes] = {0xff, 0x00, 0x10, 0x61, 0xeb, 0x00, 0x00, 0x00, 0x00,
                                                   0x00, 0x00, 0x00, 0xfe, 0xff, 0xff, 0x0e, 0x50, 0xba};

// A synthetic packet: payload p[0..15] after the 0xff header, checksum last. Digits are 7-segment
// patterns, p[4] the leftmost; bit 4 of a digit is the point before it (of p[4]: the sign).
struct Synthetic {
    uint8_t bytes[Packet::Bytes]{0xff};
    uint8_t *p = bytes + 1;
    void seal() { bytes[17] = Packet::checksum(bytes); }
};
// "1.2345" V DC, auto range: 1 2 3 4 5 = 0x0a 0xad 0x8f 0x4e 0xc7, the point on the "2".
Synthetic volts(bool negative) {
    Synthetic s;
    s.p[4] = uint8_t(0x0a | (negative ? 0x10 : 0));
    s.p[3] = 0xad | 0x10;
    s.p[2] = 0x8f;
    s.p[1] = 0x4e;
    s.p[0] = 0xc7;
    s.p[10] = 0x10 | 0x04; // DC, AUTO.
    s.p[15] = 0x02;        // V.
    s.seal();
    return s;
}
} // namespace

void setUp() {}
void tearDown() {}

void test_checksum_of_captured_packet() {
    TEST_ASSERT_EQUAL_HEX8(0xba, Packet::checksum(Overload25MOhm));
    TEST_ASSERT_TRUE(Packet::valid(Overload25MOhm, sizeof Overload25MOhm));
}

void test_corrupt_or_short_packet_is_invalid() {
    uint8_t copy[Packet::Bytes];
    std::memcpy(copy, Overload25MOhm, sizeof copy);
    copy[5] ^= 0x01;
    TEST_ASSERT_FALSE(Packet::valid(copy, sizeof copy));
    TEST_ASSERT_FALSE(Packet::valid(Overload25MOhm, Packet::Bytes - 1));
    Result r;
    TEST_ASSERT_FALSE(Decoder::decode(copy, sizeof copy, r));
}

void test_framer_resynchronises_after_garbage() {
    PacketFramer framer;
    uint8_t out[Packet::Bytes]{};
    const uint8_t garbage[] = {0x12, 0x34, 0xff, 0x00, 0x01};
    unsigned frames = 0;
    for (uint8_t b : garbage) frames += framer.push(b, out);
    for (uint8_t b : Overload25MOhm) frames += framer.push(b, out);
    TEST_ASSERT_EQUAL_UINT(1, frames);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(Overload25MOhm, out, Packet::Bytes);
    TEST_ASSERT_TRUE(framer.rejected() >= 1);
}

void test_decode_volts_dc_auto() {
    const auto s = volts(false);
    Result r;
    TEST_ASSERT_TRUE(Decoder::decode(s.bytes, Packet::Bytes, r));
    TEST_ASSERT_EQUAL_STRING("1.2345", r.mainText);
    TEST_ASSERT_EQUAL_STRING("V", r.unit);
    TEST_ASSERT_EQUAL(Kind::Dc, r.kind);
    TEST_ASSERT_EQUAL(RangeMode::Auto, r.range);
    TEST_ASSERT_EQUAL_UINT8(4, r.decimals);
    TEST_ASSERT_EQUAL_UINT32(12345, r.rawMagnitude);
    TEST_ASSERT_TRUE(r.primary.valid());
    TEST_ASSERT_EQUAL(Quantity::Voltage, r.primary.quantity);
    TEST_ASSERT_DOUBLE_WITHIN(1e-9, 1.2345, r.primary.si);
    TEST_ASSERT_EQUAL(Function::VoltageDc, Functions::identify(r));
    unsigned index = 99;
    TEST_ASSERT_NOT_NULL(Ranges::current(r, &index));
    TEST_ASSERT_EQUAL_UINT(0, index); // 2.5 V range on the ZT703S.
}

void test_decode_negative_sign() {
    const auto s = volts(true);
    Result r;
    TEST_ASSERT_TRUE(Decoder::decode(s.bytes, Packet::Bytes, r));
    TEST_ASSERT_EQUAL_STRING("-1.2345", r.mainText);
    TEST_ASSERT_DOUBLE_WITHIN(1e-9, -1.2345, r.primary.si);
}

void test_decode_captured_overload() {
    Result r;
    TEST_ASSERT_TRUE(Decoder::decode(Overload25MOhm, sizeof Overload25MOhm, r));
    TEST_ASSERT_EQUAL(Kind::Resistance, r.kind);
    TEST_ASSERT_EQUAL_STRING("MOhm", r.unit);
    TEST_ASSERT_EQUAL(DataStatus::Overload, r.primary.status);
    TEST_ASSERT_FALSE(r.primary.valid());
    TEST_ASSERT_EQUAL_UINT32(66000, r.rawMagnitude);
    unsigned index = 99;
    TEST_ASSERT_NOT_NULL(Ranges::current(r, &index));
    TEST_ASSERT_EQUAL_UINT(5, index); // 25 MOhm, by the overload alias.
}

void test_segment_patterns() {
    TEST_ASSERT_EQUAL_CHAR('0', Decoder::character(0xeb));
    TEST_ASSERT_EQUAL_CHAR('8', Decoder::character(0xef));
    TEST_ASSERT_EQUAL_CHAR(' ', Decoder::character(0x00));
    TEST_ASSERT_EQUAL_CHAR('L', Decoder::character(0x61));
    TEST_ASSERT_EQUAL_CHAR('-', Decoder::character(0x01)); // Unknown pattern.
    TEST_ASSERT_EQUAL(Decoder::Digits::Number, Decoder::digits("-1.234"));
    TEST_ASSERT_EQUAL(Decoder::Digits::Blank, Decoder::digits("--."));
    TEST_ASSERT_EQUAL(Decoder::Digits::Other, Decoder::digits("1-23"));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_checksum_of_captured_packet);
    RUN_TEST(test_corrupt_or_short_packet_is_invalid);
    RUN_TEST(test_framer_resynchronises_after_garbage);
    RUN_TEST(test_decode_volts_dc_auto);
    RUN_TEST(test_decode_negative_sign);
    RUN_TEST(test_decode_captured_overload);
    RUN_TEST(test_segment_patterns);
    return UNITY_END();
}
