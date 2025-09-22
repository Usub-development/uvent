//
// Created by kirill on 11/11/24.
//

#include "uvent/poll/PollerBase.h"

namespace usub::uvent::core {
    PollerBase::PollerBase() {}

    int PollerBase::get_poll_fd()
    {
        return this->poll_fd;
    }
}
