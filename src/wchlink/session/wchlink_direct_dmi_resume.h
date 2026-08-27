#pragma once

#include <stdbool.h>
#include <stdint.h>

struct wchlink_direct_dmi_resume {
    uint32_t dmstatus;
    bool status_pending;
};

void wchlink_direct_dmi_resume_reset(
    struct wchlink_direct_dmi_resume *resume);

bool wchlink_direct_dmi_resume_is_request(uint8_t address, uint32_t value);

void wchlink_direct_dmi_resume_store_status(
    struct wchlink_direct_dmi_resume *resume, uint32_t dmstatus);

bool wchlink_direct_dmi_resume_take_status(
    struct wchlink_direct_dmi_resume *resume, uint8_t address,
    uint32_t *dmstatus);
