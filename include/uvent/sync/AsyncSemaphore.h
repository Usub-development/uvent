#ifndef UVENT_SYNC_ASYNCSEMAPHORE_H
#define UVENT_SYNC_ASYNCSEMAPHORE_H

#include <atomic>
#include <coroutine>
#include <cstdint>

#include "uvent/sync/SyncCommon.h"
#include "uvent/utils/sync/TaggedPtr.h"
#include "uvent/system/SystemContext.h"

namespace usub::uvent::sync {

    class AsyncSemaphore {
        enum class NodeState : uint8_t {
            Waiting   = 0,
            Cancelled = 1,
            Claimed   = 2
        };

        struct WaitNode {
            std::coroutine_handle<>    h{};
            WaitNode*                  next{};
            int                        thread_id{-1};
            std::atomic<NodeState>     st{NodeState::Waiting};
        };

        std::atomic<int32_t>   count_;
        TaggedPtr<WaitNode>    head_;

        void push_waiter(WaitNode* n) noexcept {
            auto snap = head_.load(std::memory_order_relaxed);
            do {
                n->next = snap.ptr;
            } while (!head_.compare_exchange_weak(
                snap, n,
                std::memory_order_release,
                std::memory_order_relaxed));
        }

        WaitNode* pop_waiter() noexcept {
            for (;;) {
                auto snap = head_.load(std::memory_order_acquire);
                if (!snap.ptr) return nullptr;
                WaitNode* next = snap.ptr->next;
                if (head_.compare_exchange_weak(snap, next))
                    return snap.ptr;
            }
        }

        bool try_take_token() noexcept {
            int32_t c = count_.load(std::memory_order_relaxed);
            while (c > 0) {
                if (count_.compare_exchange_weak(
                        c, c - 1,
                        std::memory_order_acquire,
                        std::memory_order_relaxed))
                    return true;
            }
            return false;
        }

    public:
        explicit AsyncSemaphore(int32_t initial) noexcept
            : count_(initial), head_(nullptr) {}

        ~AsyncSemaphore() {
            while (WaitNode* n = pop_waiter())
                delete n;
        }

        AsyncSemaphore(const AsyncSemaphore&)            = delete;
        AsyncSemaphore& operator=(const AsyncSemaphore&) = delete;

        struct AcquireAwaiter {
            AsyncSemaphore* self{};
            WaitNode*       node{};

            bool await_ready() noexcept {
                return self->try_take_token();
            }

            bool await_suspend(std::coroutine_handle<> h) noexcept {
                node = new WaitNode{};
                node->h         = h;
                node->thread_id = detail::current_thread_id();
                node->st.store(NodeState::Waiting, std::memory_order_relaxed);

                self->push_waiter(node);

                if (self->try_take_token()) {
                    NodeState expected = NodeState::Waiting;
                    if (node->st.compare_exchange_strong(
                            expected, NodeState::Cancelled,
                            std::memory_order_acq_rel,
                            std::memory_order_relaxed)) {
                        return false;
                    }
                    return true;
                }
                return true;
            }

            void await_resume() noexcept {}
        };

        AcquireAwaiter acquire() noexcept { return AcquireAwaiter{this}; }

        bool try_acquire() noexcept { return try_take_token(); }

        void release(int32_t k = 1) noexcept {
            for (int32_t i = 0; i < k; ++i) {
                for (;;) {
                    WaitNode* n = pop_waiter();
                    if (!n) {
                        count_.fetch_add(1, std::memory_order_release);
                        break;
                    }

                    NodeState expected = NodeState::Waiting;
                    if (!n->st.compare_exchange_strong(
                            expected, NodeState::Claimed,
                            std::memory_order_acq_rel,
                            std::memory_order_relaxed)) {
                        delete n;
                        continue;
                    }

                    detail::resume_on(n->h, n->thread_id);
                    delete n;
                    break;
                }
            }
        }
    };

} // namespace usub::uvent::sync

#endif // UVENT_SYNC_ASYNCSEMAPHORE_H
