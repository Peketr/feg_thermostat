#ifndef ZIGBEE_HELPERS_H
#define ZIGBEE_HELPERS_H

#include "app/framework/include/af.h"
#include "sl_zigbee_system_common.h"
#include "sl_status.h"
#include "sl_zigbee_debug_print.h"
#include "sl_zigbee_system_common.h"
#include "zap-id.h"
#include "zap-type.h"
#include "zigbee_app_framework_event.h"
#include "zigbee_common_callback_dispatcher.h"
#include "network-steering.h"

#define THERMOSTAT_ENDPOINT 1

sl_zigbee_af_status_t get_target(int16_t *target_temp,uint8_t *system_mode);
void update_measurement(uint32_t rh_data, int32_t temp_data);
sl_zigbee_af_status_t set_target_temp(int16_t target_temp);

sl_zigbee_af_status_t set_system_mode(bool enable_heating);
sl_zigbee_af_status_t update_running_state(uint8_t open_valves);

bool on_network();

#endif //ZIGBEE_HELPERS_H