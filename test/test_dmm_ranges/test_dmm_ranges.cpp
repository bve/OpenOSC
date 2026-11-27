// DMM range tables (src/measurement/dmm/ranges.cpp) of both models.
#include "openosc/measurement/dmm/ranges.hpp"
#include <cmath>
#include <unity.h>

using namespace openosc;
using namespace openosc::dmm;

void setUp() {}
void tearDown() {}

void test_tables_of_both_models() {
    unsigned count = 0;
    const auto *v703 = Ranges::list(Function::VoltageDc, board::DmmCounts::Counts25000, count);
    TEST_ASSERT_EQUAL_UINT(4, count);
    TEST_ASSERT_DOUBLE_WITHIN(1e-12, 2.5, v703[0].maximumSi);
    TEST_ASSERT_DOUBLE_WITHIN(1e-12, 1000, v703[3].maximumSi);
    const auto *v706 = Ranges::list(Function::VoltageDc, board::DmmCounts::Counts66000, count);
    TEST_ASSERT_EQUAL_UINT(4, count);
    TEST_ASSERT_DOUBLE_WITHIN(1e-12, 6.6, v706[0].maximumSi);
    Ranges::list(Function::Resistance, board::DmmCounts::Counts25000, count);
    TEST_ASSERT_EQUAL_UINT(7, count);
    Ranges::list(Function::Resistance, board::DmmCounts::Counts66000, count);
    TEST_ASSERT_EQUAL_UINT(6, count);
    TEST_ASSERT_NULL(Ranges::list(Function::Diode, board::DmmCounts::Counts25000, count));
    TEST_ASSERT_EQUAL_UINT(0, count);
}

void test_choose_smallest_range_that_fits() {
    unsigned index = 99;
    TEST_ASSERT_TRUE(Ranges::choose(Function::VoltageDc, 12.0, index));
    TEST_ASSERT_EQUAL_UINT(1, index); // 25 V on the ZT703S.
    TEST_ASSERT_TRUE(Ranges::choose(Function::VoltageDc, 2.5, index));
    TEST_ASSERT_EQUAL_UINT(0, index); // The full scale itself still fits.
    TEST_ASSERT_FALSE(Ranges::choose(Function::VoltageDc, 2000.0, index));
    TEST_ASSERT_FALSE(Ranges::choose(Function::VoltageDc, 0.0, index));
    TEST_ASSERT_FALSE(Ranges::choose(Function::VoltageDc, NAN, index));
}

void test_selectable_functions() {
    TEST_ASSERT_TRUE(Ranges::selectable(Function::Capacitance));
    TEST_ASSERT_FALSE(Ranges::selectable(Function::Diode));
    TEST_ASSERT_FALSE(Ranges::selectable(Function::Continuity));
    TEST_ASSERT_FALSE(Ranges::selectable(Function::Unknown));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_tables_of_both_models);
    RUN_TEST(test_choose_smallest_range_that_fits);
    RUN_TEST(test_selectable_functions);
    return UNITY_END();
}
