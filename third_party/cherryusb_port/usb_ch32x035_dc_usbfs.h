#ifndef USB_CH32X035_DC_USBFS_H
#define USB_CH32X035_DC_USBFS_H

#include <stdbool.h>
#include <stdint.h>

// 取消尚未被主机读取的 IN 传输，保留当前端点的切换位
int ch32x035_usbd_ep_abort_in(uint8_t busid, uint8_t ep);

// 设置非控制 IN 端点下一包使用的 DATA PID
void ch32x035_usbd_set_ep_in_toggle(uint8_t busid, uint8_t ep, bool toggle);

// 翻转非控制 IN 端点下一包使用的 DATA PID
void ch32x035_usbd_toggle_ep_in_toggle(uint8_t busid, uint8_t ep);

// 设置非控制 OUT 端点下一包使用的 DATA PID
void ch32x035_usbd_set_ep_out_toggle(uint8_t busid, uint8_t ep, bool toggle);

// 允许 OUT 端点在下一包接受一次相反 DATA PID，用于跨主机会话重同步
void ch32x035_usbd_set_ep_out_toggle_resync(uint8_t busid, uint8_t ep, bool enable);

#endif
