#include "config/default/driver/lan865x/drv_lan865x.h"  

// other includes and code...

void gm_op_cb() {
    // function implementation...
}

void gm_read_register() {
    DRV_LAN865X_ReadRegister(); 
    // other code...
}

void gm_write_register() {
    DRV_LAN865X_WriteRegister(); 
    // other code...
}

void gm_send_raw_eth_frame() {
    DRV_LAN865X_SendRawEthFrame(); 
    // other code...
}

void gm_get_and_clear_ts_capture() {
    DRV_LAN865X_GetAndClearTsCapture(); 
    // other code...
}