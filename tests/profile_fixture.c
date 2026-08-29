#include "wchlink/protocol/wchlink_family.h"
#include "wchlink/target/rvswd_target_profile.h"

#include <assert.h>
#include <stddef.h>

static void assert_profile(uint32_t chip_id, uint8_t family,
                           bool ch5xx_protocol,
                           enum rvswd_target_loader loader,
                           enum rvswd_memory_write_mode memory_write_mode) {
    const struct rvswd_target_profile *from_chip =
        rvswd_target_profile_from_chip_id(chip_id);
    const struct rvswd_target_profile *from_family =
        rvswd_target_profile_from_family(family);

    assert(from_chip != NULL);
    assert(from_chip == from_family);
    assert(from_chip->wchlink_family == family);
    assert(from_chip->ch5xx_protocol == ch5xx_protocol);
    assert(from_chip->loader != NULL);
    assert(from_chip->loader->kind == loader);
    assert(from_chip->memory_write_mode == memory_write_mode);
}

static void assert_loader(const struct rvswd_target_profile *profile,
                          uint32_t code_address, uint32_t data_address,
                          uint32_t stack_top, uint32_t checksum_address,
                          uint32_t download_limit,
                          uint32_t download_packet_size,
                          uint32_t data_page_size, bool variable_length) {
    const struct rvswd_target_loader_profile *loader = profile->loader;

    assert(loader != NULL);
    assert(loader->code_address == code_address);
    assert(loader->data_address == data_address);
    assert(loader->stack_top == stack_top);
    assert(loader->checksum_address == checksum_address);
    assert(loader->download_limit == download_limit);
    assert(loader->download_packet_size == download_packet_size);
    assert(loader->data_page_size == data_page_size);
    assert(loader->variable_length == variable_length);
}

int main(void) {
    const struct rvswd_target_profile *x035_profile;
    const struct rvswd_target_profile *l103_profile;
    const struct rvswd_target_profile *v30x_profile;
    const struct rvswd_target_profile *ch58x_profile;

    assert_profile(0x03510611u, WCHLINK_TARGET_FAMILY_X03X, false,
                   RVSWD_TARGET_LOADER_DEFAULT,
                   RVSWD_MEMORY_WRITE_STREAMING);
    assert_profile(0x10300500u, WCHLINK_TARGET_FAMILY_CH32L10X, false,
                   RVSWD_TARGET_LOADER_L103,
                   RVSWD_MEMORY_WRITE_STREAMING);
    assert_profile(0x30300000u, WCHLINK_TARGET_FAMILY_CH32V30X, false,
                   RVSWD_TARGET_LOADER_DEFAULT,
                   RVSWD_MEMORY_WRITE_STREAMING);
    assert_profile(0x30500000u, WCHLINK_TARGET_FAMILY_CH32V30X, false,
                   RVSWD_TARGET_LOADER_DEFAULT,
                   RVSWD_MEMORY_WRITE_STREAMING);
    assert_profile(0x30700500u, WCHLINK_TARGET_FAMILY_CH32V30X, false,
                   RVSWD_TARGET_LOADER_DEFAULT,
                   RVSWD_MEMORY_WRITE_STREAMING);
    assert_profile(0x82000000u, WCHLINK_TARGET_FAMILY_CH58X, true,
                   RVSWD_TARGET_LOADER_CH5XX,
                   RVSWD_MEMORY_WRITE_WORD);
    assert_profile(0x83000000u, WCHLINK_TARGET_FAMILY_CH58X, true,
                   RVSWD_TARGET_LOADER_CH5XX,
                   RVSWD_MEMORY_WRITE_WORD);
    assert_profile(0x91000000u, WCHLINK_TARGET_FAMILY_CH59X, true,
                   RVSWD_TARGET_LOADER_CH5XX,
                   RVSWD_MEMORY_WRITE_WORD);
    assert_profile(0x92000000u, WCHLINK_TARGET_FAMILY_CH59X, true,
                   RVSWD_TARGET_LOADER_CH5XX,
                   RVSWD_MEMORY_WRITE_WORD);

    assert(rvswd_target_profile_from_chip_id(0u) == NULL);
    assert(rvswd_target_profile_from_chip_id(0x12345678u) == NULL);
    assert(rvswd_target_profile_from_family(0u) == NULL);

    x035_profile = rvswd_target_profile_from_family(WCHLINK_TARGET_FAMILY_X03X);
    l103_profile = rvswd_target_profile_from_family(WCHLINK_TARGET_FAMILY_CH32L10X);
    v30x_profile = rvswd_target_profile_from_family(WCHLINK_TARGET_FAMILY_CH32V30X);
    ch58x_profile = rvswd_target_profile_from_family(WCHLINK_TARGET_FAMILY_CH58X);
    assert_loader(x035_profile, 0x20000000u, 0x20001000u, 0x20005000u,
                  0x20002010u,
                  512u, 256u, 1u, false);
    assert_loader(l103_profile, 0x20000000u, 0x20001000u, 0x20005000u,
                  0x20002010u, 512u, 256u, 1u, false);
    assert_loader(v30x_profile, 0x20000000u, 0x20001000u, 0x20005000u,
                  0x20002010u,
                  512u, 256u, 1u, false);
    assert_loader(ch58x_profile, 0x20004000u, 0x20005000u, 0x20007000u,
                  0x20006010u, 2048u, 256u, 256u, true);
    assert(rvswd_target_profile_resolve(0x30700500u,
                                        WCHLINK_TARGET_FAMILY_CH58X, true) ==
           v30x_profile);
    assert(rvswd_target_profile_resolve(0u, WCHLINK_TARGET_FAMILY_CH58X,
                                        false) == NULL);
    assert(rvswd_target_profile_resolve(0u, WCHLINK_TARGET_FAMILY_CH58X,
                                        true) == ch58x_profile);
    assert(rvswd_target_profile_resolve(0x12345678u,
                                        WCHLINK_TARGET_FAMILY_CH58X, true) ==
           ch58x_profile);
    return 0;
}
