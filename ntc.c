#include "ntc.h"

#include "em_device.h"
#include "sl_clock_manager.h"
#include "sl_device_clock.h"
#include "sl_gpio.h"
#include "sl_hal_iadc.h"
#include "sl_sleeptimer.h"

#include <math.h>

#define NTC_ADC_FULL_SCALE  4096.0f
#define NTC_OVERSAMPLE      8

static const sl_gpio_t ntc_vdd_pin = { SL_GPIO_PORT_D, 5 }; // top of R4
static const sl_gpio_t ntc_gnd_pin = { SL_GPIO_PORT_C, 8 }; // bottom of R3
static const sl_gpio_t ntc_adc_pin = { SL_GPIO_PORT_C, 9 };

void ntc_init(void)
{
  sl_clock_manager_enable_bus_clock(SL_BUS_CLOCK_GPIO);
  sl_clock_manager_enable_bus_clock(SL_BUS_CLOCK_IADC0);

  // Divider is unpowered at idle; only PD05 is toggled when sampling.
  sl_gpio_set_pin_mode(&ntc_gnd_pin, SL_GPIO_MODE_PUSH_PULL, false);
  sl_gpio_set_pin_mode(&ntc_vdd_pin, SL_GPIO_MODE_PUSH_PULL, false);
  sl_gpio_set_pin_mode(&ntc_adc_pin, SL_GPIO_MODE_DISABLED, false);

  sl_hal_iadc_init_t init = SL_HAL_IADC_INIT_DEFAULT;
  sl_hal_iadc_init_single_t init_single = SL_HAL_IADC_INITSINGLE_DEFAULT;
  sl_hal_iadc_single_input_t input = SL_HAL_IADC_SINGLEINPUT_DEFAULT;

  // Ratiometric: the same VDDX supplies the divider and the reference, so
  // supply drift cancels out of the counts/full-scale ratio.
  init.configs[0].reference = SL_HAL_IADC_VREF_VDDX;
  init.configs[0].vref = 3300;
  init.configs[0].analog_gain = SL_HAL_IADC_ANALOG_GAIN_1;
  init.configs[0].osr_high_speed = SL_HAL_IADC_OSR_HIGH_SPEED_32X;

  input.positive_port = SL_HAL_IADC_POS_PORT_INPUT_PORT_C;
  input.positive_pin = ntc_adc_pin.pin;
  input.negative_port = SL_HAL_IADC_NEG_PORT_INPUT_GND;
  input.negative_pin = 0;

  sl_hal_iadc_reset(IADC0);

  uint32_t iadc_freq;
  sl_clock_manager_get_clock_branch_frequency(SL_CLOCK_BRANCH_IADCCLK, &iadc_freq);
  sl_hal_iadc_init(IADC0, &init, iadc_freq);
  sl_hal_iadc_init_single(IADC0, &init_single, &input);

  // sl_hal_iadc does not touch BUSALLOC; PC09 is an odd pin on the C/D bus.
  GPIO->CDBUSALLOC |= GPIO_CDBUSALLOC_CDODD0_ADC0;

  sl_hal_iadc_enable(IADC0);
}

uint16_t ntc_read_counts(void)
{
  sl_gpio_clear_pin(&ntc_gnd_pin);
  sl_gpio_set_pin(&ntc_vdd_pin);
  sl_sleeptimer_delay_millisecond(1);

  uint32_t sum = 0;
  for (uint8_t i = 0; i < NTC_OVERSAMPLE; i++) {
    sl_hal_iadc_clear_interrupts(IADC0, IADC_IF_SINGLEDONE);
    sl_hal_iadc_set_command(IADC0, SL_HAL_IADC_CMD_START_SINGLE);
    while (!(sl_hal_iadc_get_pending_interrupts(IADC0) & IADC_IF_SINGLEDONE)) {
    }
    sum += sl_hal_iadc_read_single_data(IADC0) & 0xFFFU;
  }

  //sl_gpio_clear_pin(&ntc_vdd_pin);

  return (uint16_t)(sum / NTC_OVERSAMPLE);
}

int32_t ntc_read_temp_c_x100(void)
{
  uint16_t counts = ntc_read_counts();

  if (counts == 0 || counts >= (uint16_t)NTC_ADC_FULL_SCALE - 1) {
    return NTC_TEMP_INVALID;
  }

  float r_ntc = NTC_R_SERIES_OHM * ((float)counts / (NTC_ADC_FULL_SCALE - (float)counts));
  float inv_t = (1.0f / 298.15f) + (logf(r_ntc / NTC_R25_OHM) / NTC_BETA);

  return (int32_t)lroundf(((1.0f / inv_t) - 273.15f) * 100.0f);
}
