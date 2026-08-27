#include "wchlink/session/wchlink_direct_dmi_resume.h"

#include "wchlink/transport/rvswd_transport.h"

#include <stddef.h>

void wchlink_direct_dmi_resume_reset(
    struct wchlink_direct_dmi_resume *resume) {
    if (resume == NULL) {
        return;
    }
    resume->dmstatus = 0u;
    resume->status_pending = false;
}

bool wchlink_direct_dmi_resume_is_request(uint8_t address, uint32_t value) {
    const uint32_t required =
        RVSWD_DMCONTROL_DMACTIVE | RVSWD_DMCONTROL_RESUMEREQ;

    return address == RVSWD_DMI_CONTROL && (value & required) == required &&
           (value & ~RVSWD_DMCONTROL_RESUME_ALLOWED) == 0u;
}

void wchlink_direct_dmi_resume_store_status(
    struct wchlink_direct_dmi_resume *resume, uint32_t dmstatus) {
    if (resume != NULL) {
        resume->dmstatus = dmstatus;
        resume->status_pending = true;
    }
}

bool wchlink_direct_dmi_resume_take_status(
    struct wchlink_direct_dmi_resume *resume, uint8_t address,
    uint32_t *dmstatus) {
    if (resume == NULL || dmstatus == NULL) {
        return false;
    }
    if (address != RVSWD_DMI_STATUS || !resume->status_pending) {
        wchlink_direct_dmi_resume_reset(resume);
        return false;
    }

    *dmstatus = resume->dmstatus;
    wchlink_direct_dmi_resume_reset(resume);
    return true;
}
