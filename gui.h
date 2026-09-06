#ifndef GUI_H
#define GUI_H

// Display
#include "mikroe_ssd1351.h"
#include "glib.h"

void draw_display(glib_context_t* glib_context, int16_t target_temp, int16_t current_temp, uint8_t open_valves, bool heating_enabled);

sl_status_t oled_init(glib_context_t* glib_context);

#endif