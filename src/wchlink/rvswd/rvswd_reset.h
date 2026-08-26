#pragma once

#include "rvswd_transport.h"

#include <stdbool.h>

bool rvswd_reset_and_halt(struct rvswd_transport *transport);
bool rvswd_soft_reset_and_run(struct rvswd_transport *transport);
bool rvswd_reset_and_run(struct rvswd_transport *transport);
