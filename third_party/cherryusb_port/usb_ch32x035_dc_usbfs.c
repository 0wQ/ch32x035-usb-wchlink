/*
 * CherryUSB device-controller port for the CH32X035 USBFS peripheral.
 *
 * This implementation follows the CH32X035 register and DMA layout directly.
 * In particular, EP4 shares UEP0_DMA, while EP1-3 and EP5-7 each use one DMA
 * base with OUT at offset 0 and IN at offset 64 when both directions are open.
 */
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <ch32x035.h>
#include <ch32x035_usb.h>
#include <usbd_core.h>

#include "usb_ch32x035_dc_usbfs.h"

#ifdef CONFIG_USB_HS
#error "CH32X035 only provides a USB full-speed device controller"
#endif

#ifndef CONFIG_CH32X035_USBFS_ZERO_COPY
#define CONFIG_CH32X035_USBFS_ZERO_COPY 1
#endif

#define X035_ALWAYS_INLINE       static inline __attribute__((always_inline))
#define X035_USBFS_IRQ_ATTR      __attribute__((interrupt("WCH-Interrupt-fast")))
#define X035_USBFS_HIGHCODE_ATTR __attribute__((section(".highcode")))

#define X035_USB_EP_COUNT          8u
#define X035_USB_EP_MPS            64u
#define X035_USB_EP0_4_BUFFER_SIZE (X035_USB_EP_MPS * 3u)
#define X035_USB_EP_BUFFER_SIZE    (X035_USB_EP_MPS * 2u)
#define X035_USB_SRAM_START        ((uintptr_t)0x20000000u)
#define X035_USB_SRAM_END          ((uintptr_t)0x20005000u)

#define X035_USB_TX_CTRL_MASK (USBFS_UEP_T_AUTO_TOG | USBFS_UEP_T_TOG | USBFS_UEP_T_RES_MASK)
#define X035_USB_RX_CTRL_MASK (USBFS_UEP_R_AUTO_TOG | USBFS_UEP_R_TOG | USBFS_UEP_R_RES_MASK)

typedef struct {
    uint16_t mps;
    uint8_t type;
    bool enabled;
    bool stalled;
    bool active;
    bool dma_direct;
    bool accept_toggle_mismatch;
    uint8_t *buffer;
    uint32_t remaining;
    uint32_t transferred;
    uint16_t packet_len;
} x035_usb_ep_state_t;

typedef struct {
    struct usb_setup_packet setup;
    uint8_t pending_address;
    bool address_pending;
    bool ep0_rx_toggle;
    bool ep0_tx_toggle;
    x035_usb_ep_state_t in_ep[X035_USB_EP_COUNT];
    x035_usb_ep_state_t out_ep[X035_USB_EP_COUNT];
    __attribute__((aligned(4))) uint8_t ep0_4_buffer[X035_USB_EP0_4_BUFFER_SIZE];
    __attribute__((aligned(4))) uint8_t ep_buffer[6u][X035_USB_EP_BUFFER_SIZE];
} x035_usb_dcd_t;

static x035_usb_dcd_t s_dcd;

static bool x035_usb_bus_valid(uint8_t busid) {
    return busid == 0u;
}

static bool x035_usb_ep_valid(uint8_t ep_idx) {
    return ep_idx < X035_USB_EP_COUNT;
}

static uint8_t x035_usb_ep_slot(uint8_t ep_idx) {
    switch (ep_idx) {
        case 1u: return 0u;
        case 2u: return 1u;
        case 3u: return 2u;
        case 5u: return 3u;
        case 6u: return 4u;
        default: return 5u;
    }
}

static volatile uint32_t *x035_usb_dma_reg(uint8_t ep_idx) {
    switch (ep_idx) {
        case 0u:
        case 4u: return &USBFSD->UEP0_DMA;
        case 1u: return &USBFSD->UEP1_DMA;
        case 2u: return &USBFSD->UEP2_DMA;
        case 3u: return &USBFSD->UEP3_DMA;
        case 5u: return &USBFSD->UEP5_DMA;
        case 6u: return &USBFSD->UEP6_DMA;
        default: return &USBFSD->UEP7_DMA;
    }
}

static volatile uint16_t *x035_usb_tx_len_reg(uint8_t ep_idx) {
    switch (ep_idx) {
        case 0u: return &USBFSD->UEP0_TX_LEN;
        case 1u: return &USBFSD->UEP1_TX_LEN;
        case 2u: return &USBFSD->UEP2_TX_LEN;
        case 3u: return &USBFSD->UEP3_TX_LEN;
        case 4u: return &USBFSD->UEP4_TX_LEN;
        case 5u: return &USBFSD->UEP5_TX_LEN;
        case 6u: return &USBFSD->UEP6_TX_LEN;
        default: return &USBFSD->UEP7_TX_LEN;
    }
}

static volatile uint16_t *x035_usb_ctrl_reg(uint8_t ep_idx) {
    switch (ep_idx) {
        case 0u: return &USBFSD->UEP0_CTRL_H;
        case 1u: return &USBFSD->UEP1_CTRL_H;
        case 2u: return &USBFSD->UEP2_CTRL_H;
        case 3u: return &USBFSD->UEP3_CTRL_H;
        case 4u: return &USBFSD->UEP4_CTRL_H;
        case 5u: return &USBFSD->UEP5_CTRL_H;
        case 6u: return &USBFSD->UEP6_CTRL_H;
        default: return &USBFSD->UEP7_CTRL_H;
    }
}

static volatile uint8_t *x035_usb_mode_reg(uint8_t ep_idx) {
    if (ep_idx == 1u || ep_idx == 4u) {
        return &USBFSD->UEP4_1_MOD;
    }
    if (ep_idx == 2u || ep_idx == 3u) {
        return &USBFSD->UEP2_3_MOD;
    }
    return &USBFSD->UEP567_MOD;
}

static uint8_t x035_usb_rx_mode_mask(uint8_t ep_idx) {
    static const uint8_t masks[X035_USB_EP_COUNT] = {
        0u, USBFS_UEP1_RX_EN, USBFS_UEP2_RX_EN, USBFS_UEP3_RX_EN,
        USBFS_UEP4_RX_EN, USBFS_UEP5_RX_EN, USBFS_UEP6_RX_EN, USBFS_UEP7_RX_EN};

    return masks[ep_idx];
}

static uint8_t x035_usb_tx_mode_mask(uint8_t ep_idx) {
    static const uint8_t masks[X035_USB_EP_COUNT] = {
        0u, USBFS_UEP1_TX_EN, USBFS_UEP2_TX_EN, USBFS_UEP3_TX_EN,
        USBFS_UEP4_TX_EN, USBFS_UEP5_TX_EN, USBFS_UEP6_TX_EN, USBFS_UEP7_TX_EN};

    return masks[ep_idx];
}

static uint8_t x035_usb_buffer_mode_mask(uint8_t ep_idx) {
    switch (ep_idx) {
        case 1u: return USBFS_UEP1_BUF_MOD;
        case 2u: return USBFS_UEP2_BUF_MOD;
        case 3u: return USBFS_UEP3_BUF_MOD;
        default: return 0u;
    }
}

X035_ALWAYS_INLINE uint8_t x035_usb_get_tx_ctrl(uint8_t ep_idx) {
    return (uint8_t)(*x035_usb_ctrl_reg(ep_idx) & X035_USB_TX_CTRL_MASK);
}

X035_ALWAYS_INLINE uint8_t x035_usb_get_rx_ctrl(uint8_t ep_idx) {
    return (uint8_t)(*x035_usb_ctrl_reg(ep_idx) & X035_USB_RX_CTRL_MASK);
}

X035_ALWAYS_INLINE uint32_t x035_usb_irq_save(void) {
    uint32_t state;
    uint32_t mask = 0x88u;

    __asm__ volatile("csrrc %0, 0x800, %1" : "=r"(state) : "r"(mask) : "memory");
    __asm__ volatile("fence.i" ::: "memory");
    return state & mask;
}

X035_ALWAYS_INLINE void x035_usb_irq_restore(uint32_t state) {
    __asm__ volatile("csrs 0x800, %0" : : "r"(state) : "memory");
}

X035_ALWAYS_INLINE void x035_usb_set_tx_ctrl(uint8_t ep_idx, uint8_t value) {
    volatile uint16_t *ctrl = x035_usb_ctrl_reg(ep_idx);
    uint32_t irq_state = x035_usb_irq_save();
    uint16_t current = *ctrl;

    // IN 和 OUT 控制位共享同一寄存器，原子更新避免主循环与 USB 中断互相覆盖 toggle
    *ctrl = (current & (uint16_t)~X035_USB_TX_CTRL_MASK) | (value & X035_USB_TX_CTRL_MASK);
    x035_usb_irq_restore(irq_state);
}

X035_ALWAYS_INLINE void x035_usb_set_rx_ctrl(uint8_t ep_idx, uint8_t value) {
    volatile uint16_t *ctrl = x035_usb_ctrl_reg(ep_idx);
    uint32_t irq_state = x035_usb_irq_save();
    uint16_t current = *ctrl;

    *ctrl = (current & (uint16_t)~X035_USB_RX_CTRL_MASK) | (value & X035_USB_RX_CTRL_MASK);
    x035_usb_irq_restore(irq_state);
}

X035_ALWAYS_INLINE void x035_usb_dma_fence(void) {
#ifdef X035_USBFS_PORT_TEST
    __asm__ volatile("" ::: "memory");
#else
    __asm__ volatile("fence iorw, iorw" ::: "memory");
#endif
}

X035_ALWAYS_INLINE uint32_t x035_usb_min(uint32_t left, uint32_t right) {
    return left < right ? left : right;
}

X035_ALWAYS_INLINE void x035_usb_set_rx_response(uint8_t ep_idx, uint8_t response) {
    x035_usb_set_rx_ctrl(ep_idx, (x035_usb_get_rx_ctrl(ep_idx) & (uint8_t)~USBFS_UEP_R_RES_MASK) | response);
}

X035_ALWAYS_INLINE void x035_usb_set_tx_response(uint8_t ep_idx, uint8_t response) {
    x035_usb_set_tx_ctrl(ep_idx, (x035_usb_get_tx_ctrl(ep_idx) & (uint8_t)~USBFS_UEP_T_RES_MASK) | response);
}

static bool x035_usb_mode_enabled(uint8_t ep_idx, bool in) {
    uint8_t mask = in ? x035_usb_tx_mode_mask(ep_idx) : x035_usb_rx_mode_mask(ep_idx);
    return (*x035_usb_mode_reg(ep_idx) & mask) != 0u;
}

static void x035_usb_set_mode(uint8_t ep_idx, bool in, bool enable) {
    volatile uint8_t *reg = x035_usb_mode_reg(ep_idx);
    uint8_t mask = in ? x035_usb_tx_mode_mask(ep_idx) : x035_usb_rx_mode_mask(ep_idx);
    uint8_t value = *reg & (uint8_t)~x035_usb_buffer_mode_mask(ep_idx);

    *reg = enable ? (value | mask) : (value & (uint8_t)~mask);
}

static uint8_t *x035_usb_owned_buffer(uint8_t ep_idx, bool in) {
    if (ep_idx == 0u) {
        return s_dcd.ep0_4_buffer;
    }
    if (ep_idx == 4u) {
        uint32_t offset = X035_USB_EP_MPS;
        if (in && x035_usb_mode_enabled(4u, false)) {
            offset += X035_USB_EP_MPS;
        }
        return s_dcd.ep0_4_buffer + offset;
    }

    return s_dcd.ep_buffer[x035_usb_ep_slot(ep_idx)] +
           ((in && x035_usb_mode_enabled(ep_idx, false)) ? X035_USB_EP_MPS : 0u);
}

static void x035_usb_restore_dma(uint8_t ep_idx) {
    uint8_t *buffer = ep_idx == 0u || ep_idx == 4u
                          ? s_dcd.ep0_4_buffer
                          : s_dcd.ep_buffer[x035_usb_ep_slot(ep_idx)];

    *x035_usb_dma_reg(ep_idx) = (uint32_t)(uintptr_t)buffer;
    x035_usb_dma_fence();
}

static void x035_usb_restore_setup_dma(void) {
    *x035_usb_dma_reg(0u) = (uint32_t)(uintptr_t)&s_dcd.setup;
    x035_usb_dma_fence();
}

static bool x035_usb_sram_range(const void *buffer, uint32_t length) {
#if CONFIG_CH32X035_USBFS_ZERO_COPY && !defined(X035_USBFS_PORT_TEST)
    uintptr_t address = (uintptr_t)buffer;

    return address >= X035_USB_SRAM_START && address <= X035_USB_SRAM_END &&
           length <= (uint32_t)(X035_USB_SRAM_END - address);
#else
    (void)buffer;
    (void)length;
    return false;
#endif
}

static uint8_t x035_usb_ready_response(const x035_usb_ep_state_t *state, bool in) {
    if (state->type == USB_ENDPOINT_TYPE_ISOCHRONOUS) {
        return in ? USBFS_UEP_T_RES_NONE : USBFS_UEP_R_RES_NONE;
    }
    return in ? USBFS_UEP_T_RES_ACK : USBFS_UEP_R_RES_ACK;
}

static bool x035_usb_transaction_ok(const x035_usb_ep_state_t *state, uint8_t intst) {
    return state->type == USB_ENDPOINT_TYPE_ISOCHRONOUS || (intst & USBFS_UIS_TOG_OK) != 0u;
}

static void x035_usb_reset_transfer(x035_usb_ep_state_t *state) {
    state->active = false;
    state->dma_direct = false;
    state->buffer = NULL;
    state->remaining = 0u;
    state->transferred = 0u;
    state->packet_len = 0u;
}

static void x035_usb_compact_mode(uint8_t ep_idx) {
    bool changed = false;

    if (!s_dcd.in_ep[ep_idx].enabled && !s_dcd.out_ep[ep_idx].active &&
        x035_usb_mode_enabled(ep_idx, true)) {
        x035_usb_set_mode(ep_idx, true, false);
        changed = true;
    }
    if (!s_dcd.out_ep[ep_idx].enabled && !s_dcd.in_ep[ep_idx].active &&
        x035_usb_mode_enabled(ep_idx, false)) {
        x035_usb_set_mode(ep_idx, false, false);
        changed = true;
    }
    if (changed) {
        x035_usb_restore_dma(ep_idx);
    }
}

static void x035_usb_reset_endpoint_registers(void) {
    USBFSD->UEP4_1_MOD = 0u;
    USBFSD->UEP2_3_MOD = 0u;
    USBFSD->UEP567_MOD = 0u;

    for (uint8_t ep_idx = 0u; ep_idx < X035_USB_EP_COUNT; ++ep_idx) {
        *x035_usb_tx_len_reg(ep_idx) = 0u;
        x035_usb_set_tx_ctrl(ep_idx, USBFS_UEP_T_RES_NAK);
        x035_usb_set_rx_ctrl(ep_idx, USBFS_UEP_R_RES_NAK);
    }
}

static void x035_usb_setup_dma_buffers(void) {
    static const uint8_t dma_endpoints[] = {0u, 1u, 2u, 3u, 5u, 6u, 7u};

    for (uint8_t i = 0u; i < sizeof(dma_endpoints); ++i) {
        if (dma_endpoints[i] == 0u) {
            x035_usb_restore_setup_dma();
        } else {
            x035_usb_restore_dma(dma_endpoints[i]);
        }
    }
}

static void x035_usb_arm_setup(void) {
    x035_usb_reset_transfer(&s_dcd.in_ep[0]);
    x035_usb_reset_transfer(&s_dcd.out_ep[0]);
    x035_usb_restore_setup_dma();
    *x035_usb_tx_len_reg(0u) = 0u;
    x035_usb_set_tx_ctrl(0u, USBFS_UEP_T_RES_NAK);
    x035_usb_set_rx_ctrl(0u, USBFS_UEP_R_RES_ACK);
    s_dcd.ep0_rx_toggle = true;
    s_dcd.ep0_tx_toggle = true;
}

static void x035_usb_load_in_packet(uint8_t ep_idx) X035_USBFS_HIGHCODE_ATTR __attribute__((noinline));
static void x035_usb_load_in_packet(uint8_t ep_idx) {
    x035_usb_ep_state_t *state = &s_dcd.in_ep[ep_idx];
    uint32_t count = x035_usb_min(state->remaining, state->mps);
    bool direct = ep_idx != 4u && count != 0u && !x035_usb_mode_enabled(ep_idx, false) &&
                  (((uintptr_t)state->buffer & 3u) == 0u) && x035_usb_sram_range(state->buffer, count);

    state->packet_len = (uint16_t)count;
    state->dma_direct = direct;
    if (direct) {
        *x035_usb_dma_reg(ep_idx) = (uint32_t)(uintptr_t)state->buffer;
    } else {
        uint8_t *target = x035_usb_owned_buffer(ep_idx, true);
        x035_usb_restore_dma(ep_idx);
        if (count != 0u) {
            memcpy(target, state->buffer, count);
        }
    }
    *x035_usb_tx_len_reg(ep_idx) = (uint16_t)count;
    x035_usb_dma_fence();
    x035_usb_set_tx_response(ep_idx, x035_usb_ready_response(state, true));
}

static void x035_usb_arm_out_packet(uint8_t ep_idx) X035_USBFS_HIGHCODE_ATTR __attribute__((noinline));
static void x035_usb_arm_out_packet(uint8_t ep_idx) {
    x035_usb_ep_state_t *state = &s_dcd.out_ep[ep_idx];
    bool direct = ep_idx != 4u && state->remaining >= state->mps && !x035_usb_mode_enabled(ep_idx, true) &&
                  (((uintptr_t)state->buffer & 3u) == 0u) && x035_usb_sram_range(state->buffer, state->mps);

    state->dma_direct = direct;
    if (direct) {
        *x035_usb_dma_reg(ep_idx) = (uint32_t)(uintptr_t)state->buffer;
        x035_usb_dma_fence();
    } else {
        x035_usb_restore_dma(ep_idx);
    }
    x035_usb_set_rx_response(ep_idx, x035_usb_ready_response(state, false));
}

static void x035_usb_complete_noncontrol_out(uint8_t busid, uint8_t ep_idx, uint8_t intst) {
    x035_usb_ep_state_t *state = &s_dcd.out_ep[ep_idx];
    uint16_t packet_len = USBFSD->RX_LEN;
    uint32_t copy_len;
    bool complete;
    bool toggle_mismatch;

    if (!state->enabled || !state->active) {
        return;
    }
    toggle_mismatch = !x035_usb_transaction_ok(state, intst) &&
                      state->accept_toggle_mismatch;
    if (!x035_usb_transaction_ok(state, intst) && !toggle_mismatch) {
        return;
    }

    x035_usb_set_rx_response(ep_idx, USBFS_UEP_R_RES_NAK);
    if (state->type != USB_ENDPOINT_TYPE_ISOCHRONOUS && !toggle_mismatch) {
        x035_usb_set_rx_ctrl(ep_idx, x035_usb_get_rx_ctrl(ep_idx) ^ USBFS_UEP_R_TOG);
    }
    state->accept_toggle_mismatch = false;

    if (packet_len > state->mps) {
        packet_len = state->mps;
    }
    copy_len = x035_usb_min(packet_len, state->remaining);
    if (!state->dma_direct && copy_len != 0u) {
        memcpy(state->buffer, x035_usb_owned_buffer(ep_idx, false), copy_len);
    }
    state->buffer += copy_len;
    state->remaining -= copy_len;
    state->transferred += copy_len;
    state->dma_direct = false;

    complete = packet_len < state->mps || state->remaining == 0u || copy_len != packet_len;
    if (complete) {
        uint32_t transferred = state->transferred;

        state->active = false;
        state->buffer = NULL;
        state->remaining = 0u;
        state->packet_len = 0u;
        x035_usb_compact_mode(ep_idx);
        usbd_event_ep_out_complete_handler(busid, ep_idx, transferred);
        return;
    }

    x035_usb_arm_out_packet(ep_idx);
}

static void x035_usb_complete_noncontrol_in(uint8_t busid, uint8_t ep_idx, uint8_t intst) {
    x035_usb_ep_state_t *state = &s_dcd.in_ep[ep_idx];

    /* X035 IN 完成中断不保证报告 TOG_OK */
    if (!state->enabled || !state->active || state->packet_len > state->remaining) {
        return;
    }

    x035_usb_set_tx_response(ep_idx, USBFS_UEP_T_RES_NAK);
    state->buffer += state->packet_len;
    state->remaining -= state->packet_len;
    state->transferred += state->packet_len;
    state->packet_len = 0u;
    state->dma_direct = false;
    if (state->type != USB_ENDPOINT_TYPE_ISOCHRONOUS) {
        x035_usb_set_tx_ctrl(ep_idx, x035_usb_get_tx_ctrl(ep_idx) ^ USBFS_UEP_T_TOG);
    }
    if (state->remaining != 0u) {
        x035_usb_load_in_packet(ep_idx);
        return;
    }

    {
        uint32_t transferred = state->transferred;

        state->active = false;
        state->buffer = NULL;
        x035_usb_compact_mode(ep_idx);
        usbd_event_ep_in_complete_handler(busid, ep_idx | 0x80u, transferred);
    }
}

static void x035_usb_complete_ep0_in(uint8_t busid, uint8_t intst) {
    x035_usb_ep_state_t *state = &s_dcd.in_ep[0];
    uint32_t completed;

    (void)intst;

    /* X035 EP0 IN completion is valid even when TOG_OK is not reported. */
    if (!state->active || state->packet_len > state->remaining) {
        return;
    }

    x035_usb_set_tx_response(0u, USBFS_UEP_T_RES_NAK);
    completed = state->packet_len;
    state->active = false;
    state->buffer = NULL;
    state->remaining = 0u;
    state->packet_len = 0u;
    s_dcd.ep0_tx_toggle = completed == state->mps ? !s_dcd.ep0_tx_toggle : true;

    if (s_dcd.address_pending) {
        USBFSD->DEV_ADDR = (USBFSD->DEV_ADDR & USBFS_UDA_GP_BIT) | s_dcd.pending_address;
        s_dcd.address_pending = false;
    }

    usbd_event_ep_in_complete_handler(busid, 0x80u, completed);
    if (!s_dcd.in_ep[0].active && !s_dcd.out_ep[0].active) {
        x035_usb_arm_setup();
    }
}

static void x035_usb_complete_ep0_out(uint8_t busid, uint8_t intst) {
    x035_usb_ep_state_t *state = &s_dcd.out_ep[0];
    uint16_t packet_len = USBFSD->RX_LEN;
    uint32_t copy_len;

    (void)intst;
    /* X035 EP0 OUT 完成中断不保证报告 TOG_OK */
    if (!state->active) {
        return;
    }

    x035_usb_set_rx_response(0u, USBFS_UEP_R_RES_NAK);
    if (packet_len > X035_USB_EP_MPS) {
        packet_len = X035_USB_EP_MPS;
    }
    copy_len = x035_usb_min(packet_len, state->remaining);
    if (copy_len != 0u) {
        memcpy(state->buffer, s_dcd.ep0_4_buffer, copy_len);
    }

    state->active = false;
    state->buffer = NULL;
    state->remaining = 0u;
    state->packet_len = 0u;
    s_dcd.ep0_rx_toggle = packet_len == state->mps ? !s_dcd.ep0_rx_toggle : true;
    usbd_event_ep_out_complete_handler(busid, 0u, copy_len);
    if (!s_dcd.in_ep[0].active && !s_dcd.out_ep[0].active) {
        x035_usb_arm_setup();
    }
}

static void x035_usb_handle_setup(uint8_t busid) {
    x035_usb_reset_transfer(&s_dcd.in_ep[0]);
    x035_usb_reset_transfer(&s_dcd.out_ep[0]);
    s_dcd.in_ep[0].stalled = false;
    s_dcd.out_ep[0].stalled = false;
    s_dcd.address_pending = false;
    x035_usb_set_tx_ctrl(0u, USBFS_UEP_T_TOG | USBFS_UEP_T_RES_NAK);
    x035_usb_set_rx_ctrl(0u, USBFS_UEP_R_TOG | USBFS_UEP_R_RES_NAK);
    s_dcd.ep0_rx_toggle = true;
    s_dcd.ep0_tx_toggle = true;
    usbd_event_ep0_setup_complete_handler(busid, (uint8_t *)&s_dcd.setup);
}

static void x035_usb_handle_bus_reset(uint8_t busid) {
    memset(&s_dcd, 0, sizeof(s_dcd));
    USBFSD->DEV_ADDR = 0u;
    x035_usb_reset_endpoint_registers();
    x035_usb_setup_dma_buffers();
    usbd_event_reset_handler(busid);
    x035_usb_arm_setup();
}

__WEAK void usb_dc_low_level_init(void) {
}

__WEAK void usb_dc_low_level_deinit(void) {
}

__WEAK void usb_dc_delay_us(uint32_t us) {
    volatile uint32_t count = us * 100u;

    while (count-- != 0u) {
        __asm__ volatile("nop");
    }
}

int usb_dc_init(uint8_t busid) {
    uint8_t int_en = USBFS_UIE_SUSPEND | USBFS_UIE_BUS_RST | USBFS_UIE_TRANSFER;

    if (!x035_usb_bus_valid(busid)) {
        return -USB_ERR_INVAL;
    }

    usb_dc_low_level_init();
    USBFSD->INT_EN = 0u;
    USBFSD->UDEV_CTRL = 0u;
    memset(&s_dcd, 0, sizeof(s_dcd));

    USBFSD->BASE_CTRL = USBFS_UC_RESET_SIE | USBFS_UC_CLR_ALL;
    usb_dc_delay_us(10u);
    USBFSD->BASE_CTRL = 0u;
    x035_usb_reset_endpoint_registers();
    x035_usb_setup_dma_buffers();
    x035_usb_arm_setup();

#ifdef CONFIG_USBDEV_SOF_ENABLE
    int_en |= USBFS_UIE_DEV_SOF;
#endif
    USBFSD->DEV_ADDR = 0u;
    USBFSD->INT_FG = 0xffu;
    USBFSD->INT_EN = int_en;
    USBFSD->BASE_CTRL = USBFS_UC_DEV_PU_EN | USBFS_UC_INT_BUSY | USBFS_UC_DMA_EN;
    USBFSD->UDEV_CTRL = USBFS_UD_PD_DIS | USBFS_UD_PORT_EN;
    return 0;
}

int usb_dc_deinit(uint8_t busid) {
    if (!x035_usb_bus_valid(busid)) {
        return -USB_ERR_INVAL;
    }

    USBFSD->INT_EN = 0u;
    USBFSD->UDEV_CTRL = 0u;
    USBFSD->BASE_CTRL = USBFS_UC_RESET_SIE | USBFS_UC_CLR_ALL;
    usb_dc_delay_us(10u);
    USBFSD->BASE_CTRL = 0u;
    USBFSD->UEP4_1_MOD = 0u;
    USBFSD->UEP2_3_MOD = 0u;
    USBFSD->UEP567_MOD = 0u;
    memset(&s_dcd, 0, sizeof(s_dcd));
    usb_dc_low_level_deinit();
    return 0;
}

int usbd_set_address(uint8_t busid, const uint8_t address) {
    if (!x035_usb_bus_valid(busid) || address > USBFS_USB_ADDR_MASK) {
        return -USB_ERR_INVAL;
    }

    if (address == 0u) {
        USBFSD->DEV_ADDR &= USBFS_UDA_GP_BIT;
        s_dcd.address_pending = false;
    } else {
        s_dcd.pending_address = address;
        s_dcd.address_pending = true;
    }
    return 0;
}

int usbd_set_remote_wakeup(uint8_t busid) {
    if (!x035_usb_bus_valid(busid)) {
        return -USB_ERR_INVAL;
    }

    USBFSD->UDEV_CTRL |= USBFS_UD_LOW_SPEED;
    usb_dc_delay_us(8000u);
    USBFSD->UDEV_CTRL &= (uint8_t)~USBFS_UD_LOW_SPEED;
    usb_dc_delay_us(1000u);
    return 0;
}

uint8_t usbd_get_port_speed(uint8_t busid) {
    return x035_usb_bus_valid(busid) ? USB_SPEED_FULL : USB_SPEED_UNKNOWN;
}

int usbd_ep_open(uint8_t busid, const struct usb_endpoint_descriptor *ep) {
    uint8_t ep_idx;
    uint8_t ep_type;
    uint16_t mps;
    bool in;
    x035_usb_ep_state_t *state;
    x035_usb_ep_state_t *opposite;

    if (!x035_usb_bus_valid(busid) || ep == NULL || (ep->bEndpointAddress & 0x70u) != 0u) {
        return -USB_ERR_INVAL;
    }

    ep_idx = USB_EP_GET_IDX(ep->bEndpointAddress);
    ep_type = USB_GET_ENDPOINT_TYPE(ep->bmAttributes);
    mps = USB_GET_MAXPACKETSIZE(ep->wMaxPacketSize);
    in = USB_EP_DIR_IS_IN(ep->bEndpointAddress);
    if (!x035_usb_ep_valid(ep_idx) || mps == 0u || mps > X035_USB_EP_MPS ||
        (ep_idx == 0u && ep_type != USB_ENDPOINT_TYPE_CONTROL) ||
        (ep_idx != 0u && ep_type == USB_ENDPOINT_TYPE_CONTROL)) {
        return -USB_ERR_INVAL;
    }

    state = in ? &s_dcd.in_ep[ep_idx] : &s_dcd.out_ep[ep_idx];
    opposite = in ? &s_dcd.out_ep[ep_idx] : &s_dcd.in_ep[ep_idx];
    if (state->active || (ep_idx != 0u && opposite->active && !x035_usb_mode_enabled(ep_idx, in))) {
        return -USB_ERR_BUSY;
    }

    x035_usb_reset_transfer(state);
    state->mps = mps;
    state->type = ep_type;
    state->enabled = true;
    state->stalled = false;

    if (ep_idx == 0u) {
        if (in) {
            x035_usb_set_tx_ctrl(0u, USBFS_UEP_T_RES_NAK);
        } else {
            x035_usb_set_rx_ctrl(0u, USBFS_UEP_R_RES_NAK);
        }
        return 0;
    }

    x035_usb_restore_dma(ep_idx);
    x035_usb_set_mode(ep_idx, in, true);
    if (in) {
        *x035_usb_tx_len_reg(ep_idx) = 0u;
        x035_usb_set_tx_ctrl(ep_idx, USBFS_UEP_T_RES_NAK);
    } else {
        x035_usb_set_rx_ctrl(ep_idx, USBFS_UEP_R_RES_NAK);
    }
    return 0;
}

int usbd_ep_close(uint8_t busid, const uint8_t ep) {
    uint8_t ep_idx = USB_EP_GET_IDX(ep);
    bool in = USB_EP_DIR_IS_IN(ep);
    x035_usb_ep_state_t *state;
    x035_usb_ep_state_t *opposite;

    if (!x035_usb_bus_valid(busid) || (ep & 0x70u) != 0u || !x035_usb_ep_valid(ep_idx)) {
        return -USB_ERR_INVAL;
    }
    if (ep_idx == 0u) {
        return 0;
    }

    state = in ? &s_dcd.in_ep[ep_idx] : &s_dcd.out_ep[ep_idx];
    opposite = in ? &s_dcd.out_ep[ep_idx] : &s_dcd.in_ep[ep_idx];
    if (in) {
        *x035_usb_tx_len_reg(ep_idx) = 0u;
        x035_usb_set_tx_ctrl(ep_idx, USBFS_UEP_T_RES_NAK);
    } else {
        x035_usb_set_rx_ctrl(ep_idx, USBFS_UEP_R_RES_NAK);
    }
    memset(state, 0, sizeof(*state));

    if (!opposite->active) {
        x035_usb_set_mode(ep_idx, in, false);
        x035_usb_restore_dma(ep_idx);
    }
    return 0;
}

int ch32x035_usbd_ep_abort_in(uint8_t busid, uint8_t ep) {
    uint8_t ep_idx = USB_EP_GET_IDX(ep);
    x035_usb_ep_state_t *state;

    if (!x035_usb_bus_valid(busid) || !USB_EP_DIR_IS_IN(ep) || (ep & 0x70u) != 0u ||
        ep_idx == 0u || !x035_usb_ep_valid(ep_idx)) {
        return -USB_ERR_INVAL;
    }

    state = &s_dcd.in_ep[ep_idx];
    if (!state->enabled || state->stalled) {
        return -USB_ERR_NOTCONN;
    }

    // 新命令到达时废弃旧回复，保持 toggle 让下一包沿用当前 Bulk 会话
    x035_usb_reset_transfer(state);
    *x035_usb_tx_len_reg(ep_idx) = 0u;
    x035_usb_set_tx_response(ep_idx, USBFS_UEP_T_RES_NAK);
    return 0;
}

void ch32x035_usbd_set_ep_in_toggle(uint8_t busid, uint8_t ep, bool toggle) {
    uint8_t ep_idx = USB_EP_GET_IDX(ep);

    if (!x035_usb_bus_valid(busid) || !USB_EP_DIR_IS_IN(ep) || (ep & 0x70u) != 0u ||
        ep_idx == 0u || !x035_usb_ep_valid(ep_idx)) {
        return;
    }
    x035_usb_set_tx_ctrl(ep_idx,
                         (x035_usb_get_tx_ctrl(ep_idx) & (uint8_t)~USBFS_UEP_T_TOG) |
                             (toggle ? USBFS_UEP_T_TOG : 0u));
}

void ch32x035_usbd_toggle_ep_in_toggle(uint8_t busid, uint8_t ep) {
    uint8_t ep_idx = USB_EP_GET_IDX(ep);

    if (!x035_usb_bus_valid(busid) || !USB_EP_DIR_IS_IN(ep) || (ep & 0x70u) != 0u ||
        ep_idx == 0u || !x035_usb_ep_valid(ep_idx)) {
        return;
    }
    x035_usb_set_tx_ctrl(ep_idx, x035_usb_get_tx_ctrl(ep_idx) ^ USBFS_UEP_T_TOG);
}

void ch32x035_usbd_set_ep_out_toggle(uint8_t busid, uint8_t ep, bool toggle) {
    uint8_t ep_idx = USB_EP_GET_IDX(ep);

    if (!x035_usb_bus_valid(busid) || !USB_EP_DIR_IS_OUT(ep) || (ep & 0x70u) != 0u ||
        ep_idx == 0u || !x035_usb_ep_valid(ep_idx)) {
        return;
    }
    x035_usb_set_rx_ctrl(ep_idx,
                         (x035_usb_get_rx_ctrl(ep_idx) & (uint8_t)~USBFS_UEP_R_TOG) |
                             (toggle ? USBFS_UEP_R_TOG : 0u));
}

void ch32x035_usbd_set_ep_out_toggle_resync(uint8_t busid, uint8_t ep, bool enable) {
    uint8_t ep_idx = USB_EP_GET_IDX(ep);

    if (!x035_usb_bus_valid(busid) || !USB_EP_DIR_IS_OUT(ep) || (ep & 0x70u) != 0u ||
        ep_idx == 0u || !x035_usb_ep_valid(ep_idx)) {
        return;
    }
    s_dcd.out_ep[ep_idx].accept_toggle_mismatch = enable;
}

int usbd_ep_set_stall(uint8_t busid, const uint8_t ep) {
    uint8_t ep_idx = USB_EP_GET_IDX(ep);
    bool in = USB_EP_DIR_IS_IN(ep);
    x035_usb_ep_state_t *state;

    if (!x035_usb_bus_valid(busid) || (ep & 0x70u) != 0u || !x035_usb_ep_valid(ep_idx)) {
        return -USB_ERR_INVAL;
    }

    state = in ? &s_dcd.in_ep[ep_idx] : &s_dcd.out_ep[ep_idx];
    state->stalled = true;
    x035_usb_reset_transfer(state);
    if (in) {
        *x035_usb_tx_len_reg(ep_idx) = 0u;
        x035_usb_set_tx_ctrl(ep_idx, USBFS_UEP_T_TOG | USBFS_UEP_T_RES_STALL);
    } else {
        x035_usb_set_rx_ctrl(ep_idx, USBFS_UEP_R_TOG | USBFS_UEP_R_RES_STALL);
    }

    if (ep_idx == 0u) {
        x035_usb_restore_dma(0u);
        if (in) {
            x035_usb_set_rx_ctrl(0u, USBFS_UEP_R_RES_ACK);
        }
    }
    return 0;
}

int usbd_ep_clear_stall(uint8_t busid, const uint8_t ep) {
    uint8_t ep_idx = USB_EP_GET_IDX(ep);
    bool in = USB_EP_DIR_IS_IN(ep);
    x035_usb_ep_state_t *state;

    if (!x035_usb_bus_valid(busid) || (ep & 0x70u) != 0u || !x035_usb_ep_valid(ep_idx)) {
        return -USB_ERR_INVAL;
    }

    state = in ? &s_dcd.in_ep[ep_idx] : &s_dcd.out_ep[ep_idx];
    state->stalled = false;
    x035_usb_reset_transfer(state);
    if (ep_idx == 0u) {
        x035_usb_arm_setup();
    } else if (in) {
        *x035_usb_tx_len_reg(ep_idx) = 0u;
        // 清除 halt 后从 DATA0 开始新的端点会话
        x035_usb_set_tx_ctrl(ep_idx,
                             (x035_usb_get_tx_ctrl(ep_idx) &
                              (uint8_t)~USBFS_UEP_T_TOG) |
                                 USBFS_UEP_T_RES_NAK);
    } else {
        x035_usb_set_rx_ctrl(ep_idx,
                             (x035_usb_get_rx_ctrl(ep_idx) &
                              (uint8_t)~USBFS_UEP_R_TOG) |
                                 USBFS_UEP_R_RES_NAK);
    }
    return 0;
}

int usbd_ep_is_stalled(uint8_t busid, const uint8_t ep, uint8_t *stalled) {
    uint8_t ep_idx = USB_EP_GET_IDX(ep);

    if (!x035_usb_bus_valid(busid) || stalled == NULL || (ep & 0x70u) != 0u ||
        !x035_usb_ep_valid(ep_idx)) {
        return -USB_ERR_INVAL;
    }

    *stalled = USB_EP_DIR_IS_IN(ep) ? s_dcd.in_ep[ep_idx].stalled : s_dcd.out_ep[ep_idx].stalled;
    return 0;
}

int usbd_ep_start_write(uint8_t busid, const uint8_t ep, const uint8_t *data, uint32_t data_len) X035_USBFS_HIGHCODE_ATTR __attribute__((noinline));
int usbd_ep_start_write(uint8_t busid, const uint8_t ep, const uint8_t *data, uint32_t data_len) {
    uint8_t ep_idx = USB_EP_GET_IDX(ep);
    x035_usb_ep_state_t *state;

    if (!x035_usb_bus_valid(busid) || !USB_EP_DIR_IS_IN(ep) || (ep & 0x70u) != 0u ||
        !x035_usb_ep_valid(ep_idx) || (data == NULL && data_len != 0u)) {
        return -USB_ERR_INVAL;
    }

    state = &s_dcd.in_ep[ep_idx];
    if (!state->enabled || state->stalled) {
        return -USB_ERR_NOTCONN;
    }
    if (state->active) {
        return -USB_ERR_BUSY;
    }

    state->active = true;
    state->buffer = (uint8_t *)data;
    state->remaining = data_len;
    state->transferred = 0u;
    state->packet_len = 0u;
    state->dma_direct = false;

    if (ep_idx == 0u) {
        uint32_t count = x035_usb_min(data_len, state->mps);

        state->packet_len = (uint16_t)count;
        if (count != 0u) {
            x035_usb_restore_dma(0u);
            memcpy(s_dcd.ep0_4_buffer, data, count);
        }
        *x035_usb_tx_len_reg(0u) = (uint16_t)count;
        x035_usb_dma_fence();
        x035_usb_set_tx_ctrl(0u, (s_dcd.ep0_tx_toggle ? USBFS_UEP_T_TOG : 0u) | USBFS_UEP_T_RES_ACK);
        return 0;
    }

    x035_usb_load_in_packet(ep_idx);
    return 0;
}

int usbd_ep_start_read(uint8_t busid, const uint8_t ep, uint8_t *data, uint32_t data_len) X035_USBFS_HIGHCODE_ATTR __attribute__((noinline));
int usbd_ep_start_read(uint8_t busid, const uint8_t ep, uint8_t *data, uint32_t data_len) {
    uint8_t ep_idx = USB_EP_GET_IDX(ep);
    x035_usb_ep_state_t *state;

    if (!x035_usb_bus_valid(busid) || !USB_EP_DIR_IS_OUT(ep) || (ep & 0x70u) != 0u ||
        !x035_usb_ep_valid(ep_idx) || (data == NULL && data_len != 0u)) {
        return -USB_ERR_INVAL;
    }

    state = &s_dcd.out_ep[ep_idx];
    if (!state->enabled || state->stalled) {
        return -USB_ERR_NOTCONN;
    }
    if (state->active) {
        return -USB_ERR_BUSY;
    }
    if (ep_idx != 0u && data_len == 0u) {
        return -USB_ERR_INVAL;
    }

    state->active = true;
    state->buffer = data;
    state->remaining = data_len;
    state->transferred = 0u;
    state->packet_len = 0u;
    state->dma_direct = false;

    if (ep_idx == 0u) {
        x035_usb_restore_dma(0u);
        x035_usb_set_rx_ctrl(0u, (s_dcd.ep0_rx_toggle ? USBFS_UEP_R_TOG : 0u) | USBFS_UEP_R_RES_ACK);
        return 0;
    }

    x035_usb_arm_out_packet(ep_idx);
    return 0;
}

void USBD_IRQHandler(uint8_t busid) {
    uint8_t intflag;

    if (!x035_usb_bus_valid(busid)) {
        return;
    }

    intflag = USBFSD->INT_FG;
    if ((intflag & USBFS_UIF_BUS_RST) != 0u) {
        USBFSD->INT_FG = USBFS_UIF_BUS_RST | (intflag & USBFS_UIF_TRANSFER);
        x035_usb_handle_bus_reset(busid);
        return;
    }

    if ((intflag & USBFS_UIF_TRANSFER) != 0u) {
        uint8_t intst = USBFSD->INT_ST;
        uint8_t ep_idx = intst & USBFS_UIS_ENDP_MASK;

        switch (intst & USBFS_UIS_TOKEN_MASK) {
            case USBFS_UIS_TOKEN_SETUP:
                /* SETUP is always handled by EP0; X035 endpoint bits may be stale here. */
                x035_usb_handle_setup(busid);
                break;

            case USBFS_UIS_TOKEN_IN:
                if (ep_idx == 0u) {
                    x035_usb_complete_ep0_in(busid, intst);
                } else if (x035_usb_ep_valid(ep_idx)) {
                    x035_usb_complete_noncontrol_in(busid, ep_idx, intst);
                }
                break;

            case USBFS_UIS_TOKEN_OUT:
                if (ep_idx == 0u) {
                    x035_usb_complete_ep0_out(busid, intst);
                } else if (x035_usb_ep_valid(ep_idx)) {
                    x035_usb_complete_noncontrol_out(busid, ep_idx, intst);
                }
                break;

            case USBFS_UIS_TOKEN_SOF:
#ifdef CONFIG_USBDEV_SOF_ENABLE
                usbd_event_sof_handler(busid);
#endif
                break;

            default:
                break;
        }
        USBFSD->INT_FG = USBFS_UIF_TRANSFER;
        return;
    }

    if ((intflag & USBFS_UIF_SUSPEND) != 0u) {
        USBFSD->INT_FG = USBFS_UIF_SUSPEND;
        if ((USBFSD->MIS_ST & USBFS_UMS_SUSPEND) != 0u) {
            usbd_event_suspend_handler(busid);
        } else {
            usbd_event_resume_handler(busid);
        }
        return;
    }

    USBFSD->INT_FG = intflag;
}

void USBFS_IRQHandler(void) X035_USBFS_IRQ_ATTR X035_USBFS_HIGHCODE_ATTR;
void USBFS_IRQHandler(void) {
    USBD_IRQHandler(0u);
}
