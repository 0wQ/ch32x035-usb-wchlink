#include "wchlink_target_ports.h"

#include "wchlink_family.h"

#include <stddef.h>

void wchlink_target_ports_init(struct wchlink_target_ports *ports) {
    if (ports == NULL) {
        return;
    }
    rvswd_target_session_init(&ports->session);
}

void wchlink_target_ports_disconnect(struct wchlink_target_ports *ports) {
    if (ports == NULL) {
        return;
    }
    rvswd_target_session_disconnect(&ports->session);
}

struct rvswd_target_result wchlink_target_ports_connect(
    struct wchlink_target_ports *ports) {
    if (ports == NULL) {
        return rvswd_target_result_failure(RVSWD_TARGET_RESULT_CONNECT, 0x01u,
                                           false);
    }
    return rvswd_target_session_connect(&ports->session);
}

void wchlink_target_ports_set_family_hint(
    struct wchlink_target_ports *ports, uint8_t family) {
    if (ports == NULL) {
        return;
    }
    rvswd_target_session_set_family_hint(&ports->session, family);
}

bool wchlink_target_ports_is_connected(
    const struct wchlink_target_ports *ports) {
    return ports != NULL && rvswd_target_session_is_connected(&ports->session);
}

const struct rvswd_target_info *wchlink_target_ports_info(
    const struct wchlink_target_ports *ports) {
    return ports == NULL ? NULL : rvswd_target_session_info(&ports->session);
}

uint8_t wchlink_target_ports_family(
    const struct wchlink_target_ports *ports) {
    const struct rvswd_target_info *info = wchlink_target_ports_info(ports);

    return info == NULL ? 0u : info->family;
}

uint32_t wchlink_target_ports_chip_id(
    const struct wchlink_target_ports *ports) {
    const struct rvswd_target_info *info = wchlink_target_ports_info(ports);

    return info == NULL ? 0u : info->chip_id;
}

bool wchlink_target_ports_uses_ch5xx_loader(
    const struct wchlink_target_ports *ports) {
    uint8_t family = wchlink_target_ports_family(ports);

    return family == WCHLINK_TARGET_FAMILY_CH58X ||
           family == WCHLINK_TARGET_FAMILY_CH59X;
}

bool wchlink_target_ports_uses_l103_loader(
    const struct wchlink_target_ports *ports) {
    return wchlink_target_ports_family(ports) == WCHLINK_TARGET_FAMILY_L103;
}

bool wchlink_target_ports_supports_memory_streaming(
    const struct wchlink_target_ports *ports) {
    return ports != NULL &&
           rvswd_target_session_supports_memory_streaming(&ports->session);
}

struct rvswd_target_result wchlink_target_ports_read_dmi(
    struct wchlink_target_ports *ports, uint8_t address) {
    return ports == NULL
               ? rvswd_target_result_failure(RVSWD_TARGET_RESULT_DMI, 0x01u,
                                             false)
               : rvswd_target_session_read_dmi(&ports->session, address);
}

struct rvswd_target_result wchlink_target_ports_write_dmi(
    struct wchlink_target_ports *ports, uint8_t address, uint32_t value) {
    return ports == NULL
               ? rvswd_target_result_failure(RVSWD_TARGET_RESULT_DMI, 0x01u,
                                             false)
               : rvswd_target_session_write_dmi(&ports->session, address, value);
}

struct rvswd_target_result wchlink_target_ports_read_memory32(
    struct wchlink_target_ports *ports, uint32_t address) {
    return ports == NULL
               ? rvswd_target_result_failure(RVSWD_TARGET_RESULT_MEMORY, 0x01u,
                                             false)
               : rvswd_target_session_read_memory32(&ports->session, address);
}

struct rvswd_target_result wchlink_target_ports_write_memory32(
    struct wchlink_target_ports *ports, uint32_t address, uint32_t value) {
    return ports == NULL
               ? rvswd_target_result_failure(RVSWD_TARGET_RESULT_MEMORY, 0x01u,
                                             false)
               : rvswd_target_session_write_memory32(&ports->session, address,
                                                     value);
}

struct rvswd_target_result wchlink_target_ports_write_memory(
    struct wchlink_target_ports *ports, uint32_t address,
    const uint8_t *data, uint32_t length) {
    return ports == NULL
               ? rvswd_target_result_failure(RVSWD_TARGET_RESULT_MEMORY, 0x01u,
                                             false)
               : rvswd_target_session_write_memory(&ports->session, address, data,
                                                   length);
}

struct rvswd_target_result wchlink_target_ports_execute(
    struct wchlink_target_ports *ports, uint32_t entry, uint32_t stack_top,
    uint32_t mode, uint32_t address, uint32_t length, uint32_t data_address) {
    return ports == NULL
               ? rvswd_target_result_failure(RVSWD_TARGET_RESULT_DEBUG, 0x01u,
                                             false)
               : rvswd_target_session_execute(&ports->session, entry, stack_top,
                                              mode, address, length, data_address);
}

struct rvswd_target_result wchlink_target_ports_reset_and_halt(
    struct wchlink_target_ports *ports) {
    return ports == NULL
               ? rvswd_target_result_failure(RVSWD_TARGET_RESULT_RESET, 0x01u,
                                             false)
               : rvswd_target_session_reset_and_halt(&ports->session);
}

struct rvswd_target_result wchlink_target_ports_soft_reset_and_run(
    struct wchlink_target_ports *ports) {
    return ports == NULL
               ? rvswd_target_result_failure(RVSWD_TARGET_RESULT_RESET, 0x01u,
                                             false)
               : rvswd_target_session_soft_reset_and_run(&ports->session);
}

struct rvswd_target_result wchlink_target_ports_reset_and_run(
    struct wchlink_target_ports *ports) {
    return ports == NULL
               ? rvswd_target_result_failure(RVSWD_TARGET_RESULT_RESET, 0x01u,
                                             false)
               : rvswd_target_session_reset_and_run(&ports->session);
}

struct rvswd_target_result wchlink_target_ports_flash_erase_all(
    struct wchlink_target_ports *ports) {
    return ports == NULL
               ? rvswd_target_result_failure(RVSWD_TARGET_RESULT_FLASH, 0x01u,
                                             false)
               : rvswd_target_session_flash_erase_all(&ports->session);
}

struct rvswd_target_result wchlink_target_ports_flash_rewrite_page(
    struct wchlink_target_ports *ports, uint32_t address,
    const uint8_t *data) {
    return ports == NULL
               ? rvswd_target_result_failure(RVSWD_TARGET_RESULT_FLASH, 0x01u,
                                             false)
               : rvswd_target_session_flash_rewrite_page(&ports->session, address,
                                                         data);
}

struct rvswd_target_result wchlink_target_ports_flash_read_protected(
    struct wchlink_target_ports *ports) {
    return ports == NULL
               ? rvswd_target_result_failure(RVSWD_TARGET_RESULT_FLASH, 0x01u,
                                             false)
               : rvswd_target_session_flash_read_protected(&ports->session);
}

struct rvswd_target_result wchlink_target_ports_flash_write_protected(
    struct wchlink_target_ports *ports) {
    return ports == NULL
               ? rvswd_target_result_failure(RVSWD_TARGET_RESULT_FLASH, 0x01u,
                                             false)
               : rvswd_target_session_flash_write_protected(&ports->session);
}

struct rvswd_target_result wchlink_target_ports_flash_set_read_protected(
    struct wchlink_target_ports *ports, bool protected) {
    return ports == NULL
               ? rvswd_target_result_failure(RVSWD_TARGET_RESULT_FLASH, 0x01u,
                                             false)
               : rvswd_target_session_flash_set_read_protected(&ports->session,
                                                               protected);
}
