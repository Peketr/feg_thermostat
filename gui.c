
#include "gui.h"

#include "sl_spidrv_instances.h"

#include "zigbee_helpers.h"
#include "start_image.h"

void draw_display(glib_context_t* glib_context, int16_t target_temp, int16_t current_temp, uint8_t open_valves, bool heating_enabled){
  glib_clear(glib_context);
  if(heating_enabled){
    glib_draw_char(glib_context,10,20,48+target_temp/1000,0xFFFF,0x0000,4,4);
    glib_draw_char(glib_context,34,20,48+(target_temp/100%10),0xFFFF,0x0000,4,4);
    glib_draw_char(glib_context,58,20,'.',0xFFFF,0x0000,4,4);
    glib_draw_char(glib_context,82,20,48+(target_temp/10%10),0xFFFF,0x0000,4,4);
  }

  glib_draw_char(glib_context,10,60,48+current_temp/1000,0xFFFF,0x0000,4,4);
  glib_draw_char(glib_context,34,60,48+(current_temp/100%10),0xFFFF,0x0000,4,4);
  glib_draw_char(glib_context,58,60,'.',0xFFFF,0x0000,4,4);
  glib_draw_char(glib_context,82,60,48+(current_temp/10%10),0xFFFF,0x0000,4,4);

  if (!heating_enabled){
    glib_draw_string(glib_context, "Off", 0, 0);
  } else if (open_valves == 0) {
    glib_draw_string(glib_context, "Idle", 0, 0);
  }else if (open_valves == 1){
    glib_draw_string(glib_context, "Half", 0, 0);
  }else if (open_valves == 2){
    glib_draw_string(glib_context, "Full", 0, 0);
  }

  if (steering_in_progress()) {
    glib_draw_string(glib_context, "Connecting", 65, 0);
  } else if (on_network()){
    glib_draw_string(glib_context, "Connected", 65, 0);
  } else {
    glib_draw_string(glib_context, "Disconnected", 40, 0);
  }
  
  glib_update_display();
}

sl_status_t oled_init(glib_context_t* glib_context){
  //  OLED initialization.
  sl_status_t sc = mikroe_ssd1351_init(sl_spidrv_inst0_handle);
  if (sc == SL_STATUS_OK) {
    glib_init(glib_context);
    glib_set_bg_color(glib_context, 0x0000);
    glib_set_text_color(glib_context, 0xFFFF);
    glib_enable_display(true);
    
    mikroe_ssd1351_image(ppcat128x128, 0, 0);
    //glib_update_display();  
    
  }
  return sc;
}