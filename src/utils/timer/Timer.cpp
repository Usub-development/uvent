//
// Created by root on 9/9/25.
//

#include "uvent/utils/timer/Timer.h"

namespace usub::uvent::utils
{
    Timer::Timer(timer_duration_t duration, int fd, TimerType type) : duration_ms(duration), fd(fd), type(type),
                                                                      expiryTime(0),
                                                                      active(true), id(0) {}

    Timer::Timer(timer_duration_t duration, TimerType type) : duration_ms(duration), fd(-1), type(type),
                                                              expiryTime(0),
                                                              active(true), id(0) {}
}