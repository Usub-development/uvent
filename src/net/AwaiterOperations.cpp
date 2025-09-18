//
// Created by root on 9/13/25.
//

#include "uvent/net/AwaiterOperations.h"
#include "uvent/system/SystemContext.h"

namespace usub::uvent::net::detail
{
    AwaiterRead::AwaiterRead(SocketHeader* header) : header_(header)
    {
    }

    bool AwaiterRead::await_ready()
    {
        return false;
    }

    void AwaiterRead::await_suspend(std::coroutine_handle<> h)
    {
        if (!this->header_->is_reading_now())
        {
            system::this_thread::detail::pl->updateEvent(this->header_, core::READ);
            this->header_->try_mark_reading();
        }
        auto c = std::coroutine_handle<uvent::detail::AwaitableFrameBase>::from_address(h.address());
        c.promise().set_awaited();
        this->header_->first = c;
        this->header_->clear_busy();
    }

    void AwaiterRead::await_resume()
    {
    }

    AwaiterWrite::AwaiterWrite(SocketHeader* header) : header_(header)
    {
    }

    bool AwaiterWrite::await_ready()
    {
        return false;
    }

    void AwaiterWrite::await_suspend(std::coroutine_handle<> h)
    {
        if (!this->header_->is_writing_now())
        {
            system::this_thread::detail::pl->updateEvent(this->header_, core::WRITE);
            this->header_->try_mark_writing();
        }
        auto c = std::coroutine_handle<uvent::detail::AwaitableFrameBase>::from_address(h.address());
        c.promise().set_awaited();
        this->header_->second = c;
        this->header_->clear_busy();
    }

    void AwaiterWrite::await_resume()
    {
    }

    AwaiterAccept::AwaiterAccept(SocketHeader* header) : header_(header)
    {
    }

    bool AwaiterAccept::await_ready()
    {
        return false;
    }

    void AwaiterAccept::await_suspend(std::coroutine_handle<> h)
    {
        auto c = std::coroutine_handle<uvent::detail::AwaitableFrameBase>::from_address(h.address());
        this->header_->first = c;
        c.promise().set_awaited();
        this->header_->clear_busy();
    }

    void AwaiterAccept::await_resume()
    {
    }
}
