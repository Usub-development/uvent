//
// Created by kirill on 12/2/24.
//

#ifndef UVENT_SETTINGS_H
#define UVENT_SETTINGS_H

#include <cstdint>

namespace usub::uvent::settings {
    /**
     * \brief Timer wheel levels.
     * This variable defines the number of hierarchical levels in the timer wheel.
     * Each level represents a range of time buckets for scheduling timers efficiently.
     */
    extern int tw_levels;

    /**
     * \brief Connection timeout duration.
     * This variable specifies the maximum duration (in milliseconds) that a client can remain connected.
     * If no activity is detected from the client within this time frame, the connection will be automatically closed.
     * The default value is set to 20,000 milliseconds (20 seconds).
     */
    extern uint64_t timeout_duration_ms;
    /**
     * \brief Maximum number of read retries on EINTR.
     * Defines how many consecutive EINTR errors are allowed during a read operation
     * before giving up. Prevents infinite loops caused by repeated signal interruptions.
     */
    extern int max_read_retries;
    /**
     * \brief Maximum number of write retries on EINTR.
     * Defines how many consecutive EINTR errors are allowed during a write operation
     * before the operation is aborted. Prevents hangs due to persistent signal interruptions.
     */
    extern int max_write_retries;

    extern int max_pre_allocated_timer_wheel_operations_items;

    extern int max_pre_allocated_tasks_items;

    extern int max_pre_allocated_tmp_sockets_items;

    extern int max_pre_allocated_tmp_coroutines_items;
}

#endif //UVENT_SETTINGS_H
