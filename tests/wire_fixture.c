#include "wchlink/protocol/wchlink_wire.h"

#include <assert.h>
#include <string.h>

static void expect_bytes(size_t length, size_t expected_length,
                         const uint8_t *actual, const uint8_t *expected) {
    assert(length == expected_length);
    assert(memcmp(actual, expected, expected_length) == 0);
}

int main(void) {
    uint8_t response[32];
    size_t length;

    memset(response, 0xa5, sizeof(response));
    {
        const uint8_t expected[] = {0x82u, 0x0du, 0x01u, 0x00u};
        length = wchlink_wire_ack(response, sizeof(response), 0x0du);
        expect_bytes(length, sizeof(expected), response, expected);
        assert(response[length] == 0xa5u);
    }
    memset(response, 0xa5, sizeof(response));
    assert(wchlink_wire_ack(response, 3u, 0x0du) == 0u);
    assert(response[0] == 0xa5u);

    {
        const uint8_t expected[] = {0x81u, 0x02u, 0x01u, 0x02u};
        length = wchlink_wire_unsupported(response, sizeof(response), 0x02u);
        expect_bytes(length, sizeof(expected), response, expected);
    }
    {
        const uint8_t expected[] = {0x81u, 0x02u, 0x01u, 0x12u};
        length = wchlink_wire_family_error(response, sizeof(response), 0x02u,
                                           0x12u);
        expect_bytes(length, sizeof(expected), response, expected);
    }
    {
        const uint8_t expected[] = {0x81u, 0x55u, 0x01u, 0x12u};
        length = wchlink_wire_target_error(response, sizeof(response), 0x12u);
        expect_bytes(length, sizeof(expected), response, expected);
    }
    {
        const uint8_t expected[] = {0x82u, 0x02u, 0x01u, 0x07u};
        length = wchlink_wire_command_reply(response, sizeof(response), 0x02u,
                                            0x07u);
        expect_bytes(length, sizeof(expected), response, expected);
    }
    {
        const uint8_t expected[] = {0x82u, 0x0du, 0x04u, 0x03u,
                                    0x03u, 0x12u, 0x00u};
        length = wchlink_wire_identity(response, sizeof(response));
        expect_bytes(length, sizeof(expected), response, expected);
    }
    {
        const uint8_t expected[] = {0x82u, 0x0du, 0x05u, 0x0eu,
                                    0x10u, 0x32u, 0x07u, 0x10u};
        length = wchlink_wire_connect_reply(response, sizeof(response), true,
                                            0u, 0x0eu, 0x10320710u);
        expect_bytes(length, sizeof(expected), response, expected);
    }
    {
        const uint8_t failed[] = {0x81u, 0x55u, 0x01u, 0x12u};
        const uint8_t unsupported[] = {0x81u, 0x55u, 0x01u, 0x30u};

        length = wchlink_wire_connect_reply(response, sizeof(response), false,
                                            0x12u, 0x0eu, 0u);
        expect_bytes(length, sizeof(failed), response, failed);
        length = wchlink_wire_connect_reply(response, sizeof(response), true,
                                            0u, 0u, 0u);
        expect_bytes(length, sizeof(unsupported), response, unsupported);
    }
    {
        const struct wchlink_wire_chip_info info = {
            .flash_size = 0x1234u,
            .uid_low = 0x01020304u,
            .uid_high = 0x11223344u,
            .uid_tail = 0xaabbccddu,
            .chip_id = 0x10320710u,
        };
        const uint8_t expected[] = {
            0xffu,
            0xffu,
            0x12u,
            0x34u,
            0x01u,
            0x02u,
            0x03u,
            0x04u,
            0x11u,
            0x22u,
            0x33u,
            0x44u,
            0xaau,
            0xbbu,
            0xccu,
            0xddu,
            0x10u,
            0x32u,
            0x07u,
            0x10u,
        };

        length = wchlink_wire_chip_info(response, sizeof(response), &info);
        expect_bytes(length, sizeof(expected), response, expected);
    }
    {
        const struct wchlink_wire_chip_info info = {
            .ch5xx = true,
            .chip_id = 0x92000000u,
        };
        const uint8_t expected[] = {
            0x82u,
            0x0du,
            0x01u,
            0xffu,
            0x92u,
            0u,
            0u,
            0u,
            0u,
            0u,
            0u,
            0u,
            0u,
            0u,
            0u,
            0u,
            0u,
            0u,
            0u,
            0u,
        };

        length = wchlink_wire_chip_info(response, sizeof(response), &info);
        expect_bytes(length, sizeof(expected), response, expected);
    }
    {
        const uint8_t success[] = {0x82u, 0x08u, 0x06u, 0x11u, 0x12u,
                                   0x34u, 0x56u, 0x78u, 0x00u};
        const uint8_t retry[] = {0x82u, 0x08u, 0x06u, 0x11u, 0x12u,
                                 0x34u, 0x56u, 0x78u, 0x03u};

        length = wchlink_wire_dmi_reply(response, sizeof(response), 0x11u,
                                        0x12345678u, true, false);
        expect_bytes(length, sizeof(success), response, success);
        length = wchlink_wire_dmi_reply(response, sizeof(response), 0x11u,
                                        0x12345678u, false, true);
        expect_bytes(length, sizeof(retry), response, retry);
    }
    {
        const uint8_t expected[] = {
            0x81u,
            0x02u,
            0x0au,
            0x07u,
            0x03u,
            0x20u,
            0x00u,
            0x00u,
            0x00u,
            0x00u,
            0x00u,
            0x07u,
            0x00u,
        };

        length = wchlink_wire_loader_error(
            response, sizeof(response), 0x02u, 0x07u, 0x03u, 0x20000000u,
            0x00000700u);
        expect_bytes(length, sizeof(expected), response, expected);
    }
    {
        const uint8_t expected[] = {0x41u, 0x01u, 0x01u, 0xffu};
        length = wchlink_wire_data_reply(response, sizeof(response), 0xffu);
        expect_bytes(length, sizeof(expected), response, expected);
    }

    return 0;
}
