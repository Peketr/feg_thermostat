#ifndef NTC_H
#define NTC_H

#include <stdint.h>

// Divider: PD05 (drive high) - R4 - PC09 (IADC) - NTC - PC08 - R3(0R) - PD04 (drive low)
#define NTC_R_SERIES_OHM  9940.0f   // R4
#define NTC_R25_OHM       10000.0f
#define NTC_BETA          3050.0f    // B25/85

// Returned by ntc_read_temp_c_x100() when the divider reads open or shorted.
#define NTC_TEMP_INVALID  INT32_MIN

void ntc_init(void);

// Raw 12-bit IADC counts on PC09, averaged. 0..4095.
uint16_t ntc_read_counts(void);

// Temperature in hundredths of a degree Celsius, or NTC_TEMP_INVALID.
int32_t ntc_read_temp_c_x100(void);

#endif // NTC_H
