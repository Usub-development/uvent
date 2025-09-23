//
// Created by root on 9/11/25.
//

#include "uvent/net/Socket.h"

namespace usub::uvent::net::detail
{
    void processSocketTimeout(void* ptr)
    {
        auto header = static_cast<SocketHeader*>(ptr);
        auto socket = Socket<Proto::TCP, Role::ACTIVE>::from_existing(header);

#if UVENT_DEBUG
        spdlog::warn("Socket timeout: {}, counter: {}", header->fd, header->get_counter());
#endif
        const uint64_t expected = header->timeout_epoch_load();

        if (!header->try_mark_busy())
        {
            socket.release();
            return;
        }

        if (header->timeout_epoch_changed(expected))
        {
            header->clear_busy();
            socket.release();
            return;
        }
        header->mark_disconnected();

        auto r = std::exchange(header->first, nullptr);
        auto w = std::exchange(header->second, nullptr);
        header->clear_reading();
        header->clear_writing();
        header->clear_busy();

        epoll_ctl(system::this_thread::detail::pl->get_poll_fd(), EPOLL_CTL_DEL, header->fd, nullptr);
        ::close(header->fd);
#if UVENT_DEBUG
        spdlog::warn("Socket counter in timeout: {}", header->get_counter());
#endif
        if (!header->is_done_client_coroutine_with_timeout() && r) system::this_thread::detail::q->enqueue(r);
        if (!header->is_done_client_coroutine_with_timeout() && r) system::this_thread::detail::q->enqueue(w);

        header->state.fetch_sub(1, std::memory_order_acq_rel);
    }
}