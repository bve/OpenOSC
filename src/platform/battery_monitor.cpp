#ifdef OPENOSC_APPLICATION
#include "openosc/platform/battery_monitor.hpp"
#include "openosc/board/current.hpp"

namespace openosc {
namespace {
constexpr uint32_t ChargeMask = board::Current::ChargeStatus.mask();
constexpr uint32_t ExternalMask = GPIO_PIN_9; // PC9: the factory external-power status (not proven VBUS).
}
static_assert(board::Current::ChargeStatus.port == board::Port::C);
bool BatteryMonitor::initialize() {
    GPIO_InitTypeDef gpio{}; // GPIOC is clocked by Board::initialize.
    gpio.Pin=ExternalMask|ChargeMask; gpio.Mode=GPIO_MODE_INPUT; gpio.Pull=GPIO_NOPULL;
    HAL_GPIO_Init(GPIOC,&gpio); // Status lines always remain inputs.
    RCC_PeriphCLKInitTypeDef clock{};
    clock.PeriphClockSelection=RCC_PERIPHCLK_ADC|RCC_PERIPHCLK_CKPER;
    clock.CkperClockSelection=RCC_CLKPSOURCE_HSI;
    clock.AdcClockSelection=RCC_ADCCLKSOURCE_CLKP;
    if (HAL_RCCEx_PeriphCLKConfig(&clock)!=HAL_OK) return false;
    __HAL_RCC_ADC12_CLK_ENABLE();
    gpio.Pin=GPIO_PIN_4; gpio.Mode=GPIO_MODE_ANALOG;
    HAL_GPIO_Init(GPIOC,&gpio);
    adc_.Instance=ADC1;
    auto &init=adc_.Init;
    init.ClockPrescaler=ADC_CLOCK_ASYNC_DIV4;
    init.Resolution=ADC_RESOLUTION_12B;
    init.ScanConvMode=ADC_SCAN_DISABLE;
    init.EOCSelection=ADC_EOC_SINGLE_CONV;
    init.NbrOfConversion=1;
    init.ExternalTrigConv=ADC_SOFTWARE_START;
    init.ExternalTrigConvEdge=ADC_EXTERNALTRIGCONVEDGE_NONE;
    init.ConversionDataManagement=ADC_CONVERSIONDATA_DR;
    init.Overrun=ADC_OVR_DATA_OVERWRITTEN;
    init.LeftBitShift=ADC_LEFTBITSHIFT_NONE;
    if (HAL_ADC_Init(&adc_)!=HAL_OK ||
        HAL_ADCEx_Calibration_Start(&adc_,ADC_CALIB_OFFSET,ADC_SINGLE_ENDED)!=HAL_OK) return false;
    ADC_ChannelConfTypeDef channel{};
    channel.Channel=ADC_CHANNEL_4; channel.Rank=ADC_REGULAR_RANK_1;
    channel.SamplingTime=ADC_SAMPLETIME_387CYCLES_5;
    channel.SingleDiff=ADC_SINGLE_ENDED;
    channel.OffsetNumber=ADC_OFFSET_NONE;
    ready_=HAL_ADC_ConfigChannel(&adc_,&channel)==HAL_OK;
    return ready_;
}
// The status lines are read also without a working ADC; only the battery voltage needs it.
void BatteryMonitor::poll(uint32_t now) {
    if (pending_) {
        if (__HAL_ADC_GET_FLAG(&adc_,ADC_FLAG_EOC)) {
            gauge_.sample(uint16_t(HAL_ADC_GetValue(&adc_)));
            HAL_ADC_Stop(&adc_); pending_=false;
        } else if (uint32_t(now-started_)>=5) {
            HAL_ADC_Stop(&adc_); pending_=false;
        }
    }
    if (int32_t(now-next_)<0 || pending_) return;
    next_=now+100;
    const uint32_t pins=GPIOC->IDR;
    gauge_.digital(pins&ExternalMask,pins&ChargeMask);
    // PC9 can stay high after unplugging USB. Keep the PC4 measurement running in either state.
    if (ready_ && HAL_ADC_Start(&adc_)==HAL_OK) {
        pending_=true; started_=now;
    }
}
} // namespace openosc
#endif
