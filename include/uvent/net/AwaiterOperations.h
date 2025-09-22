//
// Created by root on 9/13/25.
//

#ifndef AWAITEROPERATIONS_H
#define AWAITEROPERATIONS_H

#include "SocketMetadata.h"
#include "uvent/tasks/AwaitableFrame.h"

namespace usub::uvent::net::detail
{
    struct AwaiterRead
    {
        explicit AwaiterRead(SocketHeader* header);

        bool await_ready();

        void await_suspend(std::coroutine_handle<> h);

        void await_resume();

    private:
        SocketHeader* header_;
    };

    struct AwaiterWrite
    {
        explicit AwaiterWrite(SocketHeader* header);

        bool await_ready();

        void await_suspend(std::coroutine_handle<> h);

        void await_resume();

    private:
        SocketHeader* header_;
    };

    struct AwaiterAccept
    {
        explicit AwaiterAccept(SocketHeader* header);

        bool await_ready();

        void await_suspend(std::coroutine_handle<> h);

        void await_resume();

    private:
        SocketHeader* header_;
    };
}

#endif //AWAITEROPERATIONS_H
