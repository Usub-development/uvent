//
// Created by root on 9/11/25.
//

#include "uvent/net/Socket.h"

namespace usub::uvent::net::detail
{
    void processSocketTimeout(std::any arg)
    {
        auto header = std::any_cast<SocketHeader*>(arg);
        auto socket = Socket<Proto::TCP, Role::ACTIVE>::from_existing(header);

#if UVENT_DEBUG
        spdlog::warn("Socket timeout: {}, counter: {}", header->fd, header->get_counter());
#endif
#ifndef UVENT_ENABLE_REUSEADDR
        const uint64_t expected = header->timeout_epoch_load();
        if (!header->try_mark_busy()) {
            socket.release();
            return;
        }

        if (header->timeout_epoch_changed(expected)) {
            header->clear_busy();
            socket.release();
            return;
        }
        header->mark_disconnected();
#endif
        auto r = std::exchange(header->first, nullptr);
        auto w = std::exchange(header->second, nullptr);
#ifndef UVENT_ENABLE_REUSEADDR
        header->clear_busy();
#endif
        system::this_thread::detail::pl.removeEvent(header);
#if UVENT_DEBUG
        spdlog::warn("Socket counter in timeout: {}", header->get_counter());
#endif
        header->socket_info |= static_cast<uint8_t>(AdditionalState::TIMEOUT);
        if (!header->is_done_client_coroutine_with_timeout() && r) system::this_thread::detail::q->enqueue(r);
        if (!header->is_done_client_coroutine_with_timeout() && r) system::this_thread::detail::q->enqueue(w);

#ifndef UVENT_ENABLE_REUSEADDR
        header->state.fetch_sub(1, std::memory_order_acq_rel);
#endif
    }
}
