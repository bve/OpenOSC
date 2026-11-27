// I2C decoder (src/decode/protocols/i2c.cpp) on synthetic SDA (CH1) and SCL (CH2) lines: 20 points a
// bit, SDA changing 5 points into SCL low, never near an SCL edge.
#include "openosc/decode/protocols/i2c.hpp"
#include <unity.h>
#include <vector>

using namespace openosc::decoding;
using namespace openosc::decoding::protocols;

namespace {
ResultSet result;

struct Bus {
    std::vector<Transition> edges;
    uint32_t t = 100;
    int sda = 1, scl = 1;
    void set(uint8_t channel, int level, uint32_t at) {
        int &line = channel == 0 ? sda : scl;
        if (line == level) return;
        line = level;
        edges.push_back({at, uint8_t(level), channel});
    }
    void start() { set(0, 0, t); set(1, 0, t + 10); t += 10; }
    void bit(int b) { set(0, b, t + 5); set(1, 1, t + 10); set(1, 0, t + 20); t += 20; }
    void byte(uint8_t value, int ack) {
        for (int i = 7; i >= 0; --i) bit((value >> i) & 1);
        bit(ack);
    }
    void stop() { set(0, 0, t + 5); set(1, 1, t + 10); set(0, 1, t + 20); t += 40; }
};

void decode(const Bus &bus) {
    I2cDecoder decoder;
    TEST_ASSERT_TRUE(decoder.configure(I2cOptions{}));
    result.reset(RecordKey{}, 0, Protocol::I2c);
    decoder.start(result, 1);
    decoder.initialLevel(1, 1);
    decoder.feed(bus.edges.data(), unsigned(bus.edges.size()), result);
    decoder.finish(bus.t + 100, result);
}
} // namespace

void setUp() {}
void tearDown() {}

void test_write_address_and_data_acked() {
    Bus bus;
    bus.start();
    bus.byte(0x50 << 1, 0); // Address 0x50, write, ACK.
    bus.byte(0x3C, 0);
    bus.stop();
    decode(bus);
    TEST_ASSERT_EQUAL_UINT(1, result.count());
    const auto &e = result.event(0);
    TEST_ASSERT_EQUAL_HEX8(I2cTransaction, e.kind);
    TEST_ASSERT_EQUAL_HEX32(0x50, e.field);
    TEST_ASSERT_EQUAL_UINT32(0, e.aux & 1u);        // Write.
    TEST_ASSERT_EQUAL_UINT32(1u << 5, e.aux & (1u << 5)); // Ended with STOP.
    TEST_ASSERT_EQUAL_UINT8(1, e.count);
    TEST_ASSERT_EQUAL_HEX8(0x3C, *result.pool(e.payload));
    TEST_ASSERT_EQUAL_HEX16(0, e.flags & (ErrorFlags | Ambiguous));
}

void test_read_bit_is_reported() {
    Bus bus;
    bus.start();
    bus.byte((0x68 << 1) | 1, 0);
    bus.byte(0x9A, 1); // The master NACKs the last byte it reads.
    bus.stop();
    decode(bus);
    TEST_ASSERT_EQUAL_UINT(1, result.count());
    const auto &e = result.event(0);
    TEST_ASSERT_EQUAL_HEX32(0x68, e.field);
    TEST_ASSERT_EQUAL_UINT32(1, e.aux & 1u);
    TEST_ASSERT_EQUAL_HEX8(0x9A, *result.pool(e.payload));
    TEST_ASSERT_FALSE(e.flags & NoAck); // A NACK ending a read is normal.
}

void test_address_nack_is_flagged() {
    Bus bus;
    bus.start();
    bus.byte(0x50 << 1, 1); // No device answers.
    bus.stop();
    decode(bus);
    TEST_ASSERT_EQUAL_UINT(1, result.count());
    const auto &e = result.event(0);
    TEST_ASSERT_TRUE(e.flags & NoAck);
    TEST_ASSERT_EQUAL_UINT32(1u << 2, e.aux & (1u << 2));
}

void test_transaction_without_stop_is_cut() {
    Bus bus;
    bus.start();
    bus.byte(0x50 << 1, 0);
    bus.bit(1);
    bus.bit(0); // The record ends inside the data byte.
    decode(bus);
    TEST_ASSERT_EQUAL_UINT(1, result.count());
    TEST_ASSERT_TRUE(result.event(0).flags & DecodeEvent::Cut);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_write_address_and_data_acked);
    RUN_TEST(test_read_bit_is_reported);
    RUN_TEST(test_address_nack_is_flagged);
    RUN_TEST(test_transaction_without_stop_is_cut);
    return UNITY_END();
}
