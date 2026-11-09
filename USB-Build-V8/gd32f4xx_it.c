#include "drv_usbd_int.h"
#include "custom_hid_core.h"
extern usb_core_driver custom_hid;
extern void usb_timer_irq(void);
void USBHS_IRQHandler(void){usbd_isr(&custom_hid);}
void TIMER2_IRQHandler(void){usb_timer_irq();}
