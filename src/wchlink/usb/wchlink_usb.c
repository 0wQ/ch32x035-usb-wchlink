#include "wchlink_usb.h"

#include "bsp/bsp_delay.h"
#include "bsp/bsp_system.h"
#include "bsp/bsp_uid.h"
#include "wchlink_session.h"

#include <stdbool.h>
#include <string.h>

#include <ch32x035.h>
#include <usb_ch32x035_dc_usbfs.h>
#include <usbd_cdc_acm.h>
#include <usbd_core.h>

#define WCHLINK_MPS                       64u
#define WCHLINK_SERIAL_LEN                13u
#define WCHLINK_CONFIG_DESC_SIZE          120u
#define WCHLINK_VID                       0x1a86u
#define WCHLINK_PID                       0x8010u
#define WCHLINK_CONTROL_FAMILY            0x0du
#define WCHLINK_CONTROL_STOP              0xffu
#define WCHLINK_STOP_RESPONSE_LIFETIME_US 500000u
#define WCHLINK_RESPONSE_LIFETIME_US      100000u

static const uint8_t wchlink_device_descriptor[] = {
    USB_DEVICE_DESCRIPTOR_INIT(USB_1_1, 0xef, 0x02, 0x01,
                               WCHLINK_VID, WCHLINK_PID, 0x0219, 0x01),
};

static const uint8_t wchlink_config_descriptor[] = {
    USB_CONFIG_DESCRIPTOR_INIT(WCHLINK_CONFIG_DESC_SIZE, 3u, 0x01u,
                               USB_CONFIG_BUS_POWERED, 100u),
    // 接口 0 保持 WCH-Link RVSWD 主通道，接口 1 和 2 提供官方 CDC 结构
    0x08u,
    USB_DESCRIPTOR_TYPE_INTERFACE_ASSOCIATION,
    0u,
    1u,
    0xffu,
    0u,
    0u,
    2u,
    USB_INTERFACE_DESCRIPTOR_INIT(0u, 0u, 4u, 0xffu, 0x80u, 0x55u, 0u),
    USB_ENDPOINT_DESCRIPTOR_INIT(0x82u, USB_ENDPOINT_TYPE_BULK, WCHLINK_MPS, 0u),
    USB_ENDPOINT_DESCRIPTOR_INIT(0x02u, USB_ENDPOINT_TYPE_BULK, WCHLINK_MPS, 0u),
    USB_ENDPOINT_DESCRIPTOR_INIT(0x81u, USB_ENDPOINT_TYPE_BULK, WCHLINK_MPS, 0u),
    USB_ENDPOINT_DESCRIPTOR_INIT(0x01u, USB_ENDPOINT_TYPE_BULK, WCHLINK_MPS, 0u),
    0x08u,
    USB_DESCRIPTOR_TYPE_INTERFACE_ASSOCIATION,
    1u,
    2u,
    USB_DEVICE_CLASS_CDC,
    CDC_ABSTRACT_CONTROL_MODEL,
    CDC_COMMON_PROTOCOL_AT_COMMANDS,
    4u,
    0x09u,
    USB_DESCRIPTOR_TYPE_INTERFACE,
    1u,
    0u,
    1u,
    USB_DEVICE_CLASS_CDC,
    CDC_ABSTRACT_CONTROL_MODEL,
    CDC_COMMON_PROTOCOL_AT_COMMANDS,
    0u,
    0x05u,
    CDC_CS_INTERFACE,
    CDC_FUNC_DESC_HEADER,
    WBVAL(CDC_V1_10),
    0x05u,
    CDC_CS_INTERFACE,
    CDC_FUNC_DESC_CALL_MANAGEMENT,
    0u,
    1u,
    0x04u,
    CDC_CS_INTERFACE,
    CDC_FUNC_DESC_ABSTRACT_CONTROL_MANAGEMENT,
    0x02u,
    0x05u,
    CDC_CS_INTERFACE,
    CDC_FUNC_DESC_UNION,
    1u,
    2u,
    0x07u,
    USB_DESCRIPTOR_TYPE_ENDPOINT,
    0x84u,
    USB_ENDPOINT_TYPE_INTERRUPT,
    WBVAL(WCHLINK_MPS),
    1u,
    0x09u,
    USB_DESCRIPTOR_TYPE_INTERFACE,
    2u,
    0u,
    2u,
    CDC_DATA_INTERFACE_CLASS,
    0u,
    0u,
    0u,
    USB_ENDPOINT_DESCRIPTOR_INIT(0x83u, USB_ENDPOINT_TYPE_BULK, WCHLINK_MPS, 0u),
    USB_ENDPOINT_DESCRIPTOR_INIT(0x03u, USB_ENDPOINT_TYPE_BULK, WCHLINK_MPS, 0u),
};

static const char wchlink_langid[] = {0x09, 0x04};
static char wchlink_serial[WCHLINK_SERIAL_LEN];
static const char *const wchlink_strings[] = {
    wchlink_langid,
    "wch.cn",
    "WCH-Link",
    wchlink_serial,
    "WCH-Link",
};

static const uint8_t *wchlink_device_descriptor_callback(uint8_t speed) {
    (void)speed;
    return wchlink_device_descriptor;
}

static const uint8_t *wchlink_config_descriptor_callback(uint8_t speed) {
    (void)speed;
    return wchlink_config_descriptor;
}

static const char *wchlink_string_descriptor_callback(uint8_t speed, uint8_t index) {
    (void)speed;
    if (index >= (sizeof(wchlink_strings) / sizeof(wchlink_strings[0]))) {
        return NULL;
    }
    return wchlink_strings[index];
}

static const struct usb_descriptor wchlink_descriptor = {
    .device_descriptor_callback = wchlink_device_descriptor_callback,
    .config_descriptor_callback = wchlink_config_descriptor_callback,
    .device_quality_descriptor_callback = NULL,
    .other_speed_descriptor_callback = NULL,
    .string_descriptor_callback = wchlink_string_descriptor_callback,
};

static void wchlink_init_serial(void) {
    char uid_hex[BSP_UID_HEX16_STR_LEN];

    bsp_get_uid_hex16(uid_hex);
    memcpy(wchlink_serial, "035", 3u);
    memcpy(&wchlink_serial[3], uid_hex, 9u);
    wchlink_serial[WCHLINK_SERIAL_LEN - 1u] = '\0';
}

static struct usbd_interface wchlink_interface;
static struct usbd_endpoint wchlink_out_endpoint;
static struct usbd_endpoint wchlink_in_endpoint;
static struct usbd_endpoint wchlink_data_out_endpoint;
static struct usbd_endpoint wchlink_data_in_endpoint;
static struct usbd_endpoint wchlink_cdc_out_endpoint;
static struct usbd_endpoint wchlink_cdc_in_endpoint;
static struct usbd_interface wchlink_cdc_control_interface;
static struct usbd_interface wchlink_cdc_data_interface;

static uint8_t wchlink_request[WCHLINK_MPS] __attribute__((aligned(4)));
static uint8_t wchlink_response[WCHLINK_MPS] __attribute__((aligned(4)));
static uint8_t wchlink_data_packet[WCHLINK_MPS] __attribute__((aligned(4)));
static uint8_t wchlink_data_out_buffer[256u] __attribute__((aligned(4)));
static volatile uint16_t wchlink_request_length;
static volatile bool wchlink_request_pending;
static volatile bool wchlink_request_armed;
static volatile bool wchlink_response_pending;
static volatile bool wchlink_response_recovery_pending;
static volatile bool wchlink_response_expiring;
static volatile bool wchlink_response_is_stop;
static uint64_t wchlink_response_deadline_us;
static volatile bool wchlink_data_in_pending;
static volatile bool wchlink_data_out_active;
static volatile bool wchlink_data_out_pending;
static volatile uint16_t wchlink_data_out_length;
static volatile bool wchlink_configured;
static uint8_t wchlink_cdc_packet[WCHLINK_MPS] __attribute__((aligned(4)));
static struct cdc_line_coding wchlink_cdc_line_coding;
static volatile bool wchlink_cdc_out_active;
static volatile bool wchlink_cdc_out_pending;
static volatile uint16_t wchlink_cdc_out_length;
static volatile bool wchlink_cdc_in_pending;

static void wchlink_service_data_in(void);
static void wchlink_service_data_out(void);
static void wchlink_cdc_arm_read(void);
static void wchlink_cdc_service(void);

static void wchlink_arm_request(void) {
    if (wchlink_configured && !wchlink_request_armed && !wchlink_request_pending) {
        if (usbd_ep_start_read(0u, 0x01u, wchlink_request, sizeof(wchlink_request)) == 0) {
            wchlink_request_armed = true;
        }
    }
}

static void wchlink_out_callback(uint8_t busid, uint8_t ep, uint32_t nbytes) {
    (void)busid;
    (void)ep;
    wchlink_request_armed = false;
    if (nbytes > sizeof(wchlink_request)) {
        nbytes = sizeof(wchlink_request);
    }
    if (wchlink_response_pending) {
        // 主机进程退出不会产生 USB reset，交给主循环复位残留 IN 传输
        wchlink_response_recovery_pending = true;
    }
    wchlink_request_length = (uint16_t)nbytes;
    wchlink_request_pending = true;
}

static void wchlink_in_callback(uint8_t busid, uint8_t ep, uint32_t nbytes) {
    (void)busid;
    (void)ep;
    (void)nbytes;
    wchlink_response_pending = false;
    wchlink_response_recovery_pending = false;
    wchlink_response_expiring = false;
    wchlink_response_is_stop = false;
    wchlink_arm_request();
}

static void wchlink_data_in_callback(uint8_t busid, uint8_t ep, uint32_t nbytes) {
    (void)busid;
    (void)ep;
    (void)nbytes;
    wchlink_data_in_pending = false;
}

static void wchlink_data_out_callback(uint8_t busid, uint8_t ep, uint32_t nbytes) {
    (void)busid;
    (void)ep;
    if (nbytes > sizeof(wchlink_data_out_buffer)) {
        nbytes = sizeof(wchlink_data_out_buffer);
    }
    wchlink_data_out_length = (uint16_t)nbytes;
    wchlink_data_out_active = false;
    wchlink_data_out_pending = true;
}

static void wchlink_cdc_out_callback(uint8_t busid, uint8_t ep, uint32_t nbytes) {
    (void)busid;
    (void)ep;
    if (nbytes > sizeof(wchlink_cdc_packet)) {
        nbytes = sizeof(wchlink_cdc_packet);
    }
    wchlink_cdc_out_length = (uint16_t)nbytes;
    wchlink_cdc_out_active = false;
    wchlink_cdc_out_pending = true;
}

static void wchlink_cdc_in_callback(uint8_t busid, uint8_t ep, uint32_t nbytes) {
    (void)busid;
    (void)ep;
    (void)nbytes;
    wchlink_cdc_in_pending = false;
    wchlink_cdc_arm_read();
}

static void wchlink_event_handler(uint8_t busid, uint8_t event) {
    (void)busid;
    switch (event) {
        case USBD_EVENT_RESET:
        case USBD_EVENT_DISCONNECTED:
            wchlink_configured = false;
            wchlink_request_pending = false;
            wchlink_request_armed = false;
            wchlink_response_pending = false;
            wchlink_response_recovery_pending = false;
            wchlink_response_expiring = false;
            wchlink_response_is_stop = false;
            wchlink_data_in_pending = false;
            wchlink_data_out_active = false;
            wchlink_data_out_pending = false;
            wchlink_cdc_out_active = false;
            wchlink_cdc_out_pending = false;
            wchlink_cdc_in_pending = false;
            wchlink_session_reset();
            break;
        case USBD_EVENT_CONFIGURED:
            wchlink_configured = true;
            wchlink_request_pending = false;
            wchlink_request_armed = false;
            wchlink_response_pending = false;
            wchlink_response_recovery_pending = false;
            wchlink_response_expiring = false;
            wchlink_response_is_stop = false;
            wchlink_data_in_pending = false;
            wchlink_data_out_active = false;
            wchlink_data_out_pending = false;
            wchlink_cdc_out_active = false;
            wchlink_cdc_out_pending = false;
            wchlink_cdc_in_pending = false;
            wchlink_arm_request();
            wchlink_cdc_arm_read();
            break;
        case USBD_EVENT_SUSPEND:
        case USBD_EVENT_RESUME:
            break;
        default:
            break;
    }
}

void wchlink_usb_init(void) {
    wchlink_init_serial();
    wchlink_out_endpoint.ep_addr = 0x01u;
    wchlink_out_endpoint.ep_cb = wchlink_out_callback;
    wchlink_in_endpoint.ep_addr = 0x81u;
    wchlink_in_endpoint.ep_cb = wchlink_in_callback;
    wchlink_data_out_endpoint.ep_addr = 0x02u;
    wchlink_data_out_endpoint.ep_cb = wchlink_data_out_callback;
    wchlink_data_in_endpoint.ep_addr = 0x82u;
    wchlink_data_in_endpoint.ep_cb = wchlink_data_in_callback;
    wchlink_cdc_out_endpoint.ep_addr = 0x03u;
    wchlink_cdc_out_endpoint.ep_cb = wchlink_cdc_out_callback;
    wchlink_cdc_in_endpoint.ep_addr = 0x83u;
    wchlink_cdc_in_endpoint.ep_cb = wchlink_cdc_in_callback;
    wchlink_cdc_line_coding.dwDTERate = 115200u;
    wchlink_cdc_line_coding.bDataBits = 8u;
    wchlink_cdc_line_coding.bParityType = 0u;
    wchlink_cdc_line_coding.bCharFormat = 0u;

    usbd_desc_register(0u, &wchlink_descriptor);
    usbd_add_interface(0u, &wchlink_interface);
    usbd_add_endpoint(0u, &wchlink_out_endpoint);
    usbd_add_endpoint(0u, &wchlink_in_endpoint);
    usbd_add_endpoint(0u, &wchlink_data_out_endpoint);
    usbd_add_endpoint(0u, &wchlink_data_in_endpoint);
    usbd_add_interface(0u, usbd_cdc_acm_init_intf(0u, &wchlink_cdc_control_interface));
    usbd_add_interface(0u, usbd_cdc_acm_init_intf(0u, &wchlink_cdc_data_interface));
    usbd_add_endpoint(0u, &wchlink_cdc_out_endpoint);
    usbd_add_endpoint(0u, &wchlink_cdc_in_endpoint);
    usbd_initialize(0u, 0u, wchlink_event_handler);
}

static void wchlink_cdc_arm_read(void) {
    if (wchlink_configured && !wchlink_cdc_out_active && !wchlink_cdc_out_pending &&
        !wchlink_cdc_in_pending) {
        wchlink_cdc_out_active = true;
        if (usbd_ep_start_read(0u, 0x03u, wchlink_cdc_packet,
                               sizeof(wchlink_cdc_packet)) != 0) {
            wchlink_cdc_out_active = false;
        }
    }
}

static void wchlink_cdc_service(void) {
    uint16_t data_length;

    if (!wchlink_configured) {
        return;
    }
    // CDC 数据通道保持原始回环行为，不参与探针维护状态切换
    if (wchlink_cdc_out_pending && !wchlink_cdc_in_pending) {
        __disable_irq();
        data_length = wchlink_cdc_out_length;
        wchlink_cdc_out_pending = false;
        __enable_irq();

        wchlink_cdc_in_pending = true;
        if (usbd_ep_start_write(0u, 0x83u, wchlink_cdc_packet,
                                data_length) != 0) {
            wchlink_cdc_in_pending = false;
        }
    }
    wchlink_cdc_arm_read();
}

void usbd_cdc_acm_set_line_coding(uint8_t busid, uint8_t intf,
                                  struct cdc_line_coding *line_coding) {
    (void)busid;
    (void)intf;
    if (line_coding != NULL) {
        memcpy(&wchlink_cdc_line_coding, line_coding, sizeof(wchlink_cdc_line_coding));
    }
}

void usbd_cdc_acm_get_line_coding(uint8_t busid, uint8_t intf,
                                  struct cdc_line_coding *line_coding) {
    (void)busid;
    (void)intf;
    if (line_coding != NULL) {
        memcpy(line_coding, &wchlink_cdc_line_coding, sizeof(*line_coding));
    }
}

void usbd_cdc_acm_set_dtr(uint8_t busid, uint8_t intf, bool dtr) {
    (void)busid;
    (void)intf;
    (void)dtr;
}

void usbd_cdc_acm_set_rts(uint8_t busid, uint8_t intf, bool rts) {
    (void)busid;
    (void)intf;
    (void)rts;
}

static void wchlink_service_data_in(void) {
    size_t data_length;

    if (!wchlink_configured || wchlink_data_in_pending) {
        return;
    }

    if (wchlink_session_take_data_reply(wchlink_data_packet,
                                        sizeof(wchlink_data_packet))) {
        data_length = 4u;
    } else {
        if (!wchlink_session_data_read_active()) {
            return;
        }
        data_length = wchlink_session_read_data(wchlink_data_packet,
                                                sizeof(wchlink_data_packet));
    }
    if (data_length == 0u) {
        return;
    }

    wchlink_data_in_pending = true;
    if (usbd_ep_start_write(0u, 0x82u, wchlink_data_packet,
                            (uint32_t)data_length) != 0) {
        wchlink_data_in_pending = false;
    }
}

static void wchlink_service_data_out(void) {
    uint16_t data_length;

    if (!wchlink_configured) {
        return;
    }

    if (wchlink_response_recovery_pending) {
        __disable_irq();
        wchlink_response_recovery_pending = false;
        wchlink_response_pending = false;
        __enable_irq();
        // 主机超时后仍保留其当前 IN toggle，取消设备侧未完成传输即可
        (void)ch32x035_usbd_ep_abort_in(0u, 0x81u);
    }

    if (wchlink_data_out_pending) {
        __disable_irq();
        data_length = wchlink_data_out_length;
        wchlink_data_out_pending = false;
        __enable_irq();
        wchlink_session_write_data(wchlink_data_out_buffer, data_length);
    }

    if (wchlink_session_data_write_active() && !wchlink_data_out_active &&
        !wchlink_data_out_pending) {
        wchlink_data_out_active = true;
        if (usbd_ep_start_read(0u, 0x02u, wchlink_data_out_buffer,
                               sizeof(wchlink_data_out_buffer)) != 0) {
            wchlink_data_out_active = false;
        }
    }
}

static void wchlink_service_response_timeout(void) {
    if (!wchlink_response_pending || !wchlink_response_expiring ||
        bsp_time_us() < wchlink_response_deadline_us) {
        return;
    }

    // 主机异常退出时释放未读取的 IN 回复，避免下一会话永久阻塞
    __disable_irq();
    wchlink_response_pending = false;
    wchlink_response_expiring = false;
    wchlink_response_is_stop = false;
    __enable_irq();
    (void)ch32x035_usbd_ep_abort_in(0u, 0x81u);
    wchlink_arm_request();
}

void wchlink_usb_process(void) {
    uint8_t request[WCHLINK_MPS];
    uint16_t request_length;
    size_t response_length;

    if (!wchlink_configured) {
        return;
    }

    wchlink_service_response_timeout();
    wchlink_service_data_out();
    wchlink_service_data_in();
    wchlink_cdc_service();
    if (!wchlink_request_pending || wchlink_response_pending) {
        return;
    }

    __disable_irq();
    request_length = wchlink_request_length;
    memcpy(request, wchlink_request, request_length);
    wchlink_request_pending = false;
    __enable_irq();

    // 协议处理包含 RVSWD 长事务，先重新挂载 OUT 端点避免主机在处理期间超时
    wchlink_arm_request();

    response_length = wchlink_session_process(request, request_length,
                                              wchlink_response, sizeof(wchlink_response));
    if (wchlink_session_take_isp_request()) {
        bsp_system_enter_isp();
    }
    if (response_length == SIZE_MAX) {
        wchlink_arm_request();
        return;
    }
    if (response_length == 0u) {
        response_length = 4u;
        memset(wchlink_response, 0, response_length);
    }
    wchlink_response_pending = true;
    // 所有回复都设置有限生命周期，STOP 使用更短窗口并保留跨会话 toggle 恢复
    wchlink_response_expiring = true;
    wchlink_response_is_stop =
        request_length >= 4u && request[1] == WCHLINK_CONTROL_FAMILY &&
        request[3] == WCHLINK_CONTROL_STOP;
    wchlink_response_deadline_us =
        bsp_time_us() +
        (request_length >= 4u && request[1] == WCHLINK_CONTROL_FAMILY &&
                 request[3] == WCHLINK_CONTROL_STOP
             ? WCHLINK_STOP_RESPONSE_LIFETIME_US
             : WCHLINK_RESPONSE_LIFETIME_US);
    if (usbd_ep_start_write(0u, 0x81u, wchlink_response, (uint32_t)response_length) != 0) {
        wchlink_response_pending = false;
        wchlink_response_expiring = false;
        wchlink_response_is_stop = false;
        wchlink_arm_request();
    }
    wchlink_service_data_in();
    wchlink_service_data_out();
}
