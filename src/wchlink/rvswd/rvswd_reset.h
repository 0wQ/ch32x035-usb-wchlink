#pragma once

#include "wchlink/rvswd/rvswd_operation.h"

#include <stdbool.h>

bool rvswd_reset_and_halt(struct rvswd_operation *operation);
bool rvswd_soft_reset_and_run(struct rvswd_operation *operation);
bool rvswd_reset_and_run(struct rvswd_operation *operation);
