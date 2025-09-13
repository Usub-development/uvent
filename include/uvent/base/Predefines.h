//
// Created by root on 9/10/25.
//

#ifndef PREDEFINES_H
#define PREDEFINES_H

namespace usub
{
    class Uvent;

    namespace uvent
    {
        class ThreadPool;
        namespace core
        {
            class PollerBase;
            class EPoller;
        }

        namespace system
        {
            class Thread;
        }

        namespace net
        {
            namespace detail
            {
                class AwaitableAccept;
            }
        }

        namespace detail
        {
            class AwaitableRead;
            class AwaitableWrite;
            class AwaitableAccept;
            template <typename>
            class AwaitableFrame;
            template <typename>
            class AwaitableIOFrame;
        }
    }
}

#endif //PREDEFINES_H
