//
// Created by kirill on 12/2/24.
//

#include "../include/uvent/system/Settings.h"

namespace usub::uvent::settings {
    int tw_levels = 4;
    uint64_t timeout_duration_ms = 20000;
    int max_read_retries = 100;
    int max_write_retries = 100;
    int max_pre_allocated_timer_wheel_operations_items = 256;
    int max_pre_allocated_tasks_items = 256;
    int max_pre_allocated_tmp_sockets_items = 256;
}