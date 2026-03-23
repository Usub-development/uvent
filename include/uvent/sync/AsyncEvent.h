#ifndef UVENT_SYNC_ASYNCEVENT_H
#define UVENT_SYNC_ASYNCEVENT_H

#include <atomic>
#include <coroutine>
#include <cstdint>

#include "uvent/sync/SyncCommon.h"
#include "uvent/utils/sync/TaggedPtr.h"
#include "uvent/system/SystemContext.h"

namespace usub::uvent::sync {

    enum class Reset { Auto, Manual };

    class AsyncEvent {
        enum class NodeState : uint8_t {
            Waiting   = 0,
            Cancelled = 1,
            Claimed   = 2
        };

        struct WaitNode {
            std::coroutine_handle<>  h{};
            WaitNode*                next{};
            int                      thread_id{-1};
            std::atomic<NodeState>   st{NodeState::Waiting};
        };

        const Reset              reset_;
        std::atomic<bool>        set_{false};
        TaggedPtr<WaitNode>      head_;

        void push_waiter(WaitNode* n) noexcept {
            auto snap = head_.load(std::memory_order_relaxed);
            do {
                n->next = snap.ptr;
            } while (!head_.compare_exchange_weak(
                snap, n,
                std::memory_order_release,
                std::memory_order_relaxed));
        }

        WaitNode* pop_one() noexcept {
            for (;;) {
                auto snap = head_.load(std::memory_order_acquire);
                if (!snap.ptr) return nullptr;
                WaitNode* next = snap.ptr->next;
                if (head_.compare_exchange_weak(snap, next))
                    return snap.ptr;
            }
        }

        WaitNode* exchange_all() noexcept {
            auto snap = head_.load(std::memory_order_acquire);
            while (snap.ptr) {
                if (head_.compare_exchange_weak(snap, nullptr))
                    return snap.ptr;
            }
            return nullptr;
        }

        static bool try_claim(WaitNode* n) noexcept {
            NodeState exp = NodeState::Waiting;
            return n->st.compare_exchange_strong(
                exp, NodeState::Claimed,
                std::memory_order_acq_rel,
                std::memory_order_relaxed);
        }

    public:
        explicit AsyncEvent(Reset r = Reset::Auto,
                            bool initially_set = false) noexcept
            : reset_(r), set_(initially_set), head_(nullptr) {}

        ~AsyncEvent() {
            WaitNode* list = exchange_all();
            while (list) {
                WaitNode* next = list->next;
                delete list;
                list = next;
            }
        }

        AsyncEvent(const AsyncEvent&)            = delete;
        AsyncEvent& operator=(const AsyncEvent&) = delete;
        AsyncEvent(AsyncEvent&&)                 = delete;
        AsyncEvent& operator=(AsyncEvent&&)      = delete;

        struct WaitAwaiter {
            AsyncEvent* self;
            WaitNode*   node{};

            bool await_ready() noexcept {
                if (self->reset_ == Reset::Auto) {
                    bool expected = true;
                    if (self->set_.compare_exchange_strong(
                            expected, false,
                            std::memory_order_acquire,
                            std::memory_order_relaxed))
                        return true;
                    return false;
                }
                return self->set_.load(std::memory_order_acquire);
            }

            bool await_suspend(std::coroutine_handle<> h) noexcept {
                node = new WaitNode{};
                node->h         = h;
                node->thread_id = detail::current_thread_id();
                node->st.store(NodeState::Waiting, std::memory_order_relaxed);

                self->push_waiter(node);

                if (self->set_.load(std::memory_order_acquire)) {
                    NodeState exp = NodeState::Waiting;
                    if (node->st.compare_exchange_strong(
                            exp, NodeState::Cancelled,
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

        WaitAwaiter wait() noexcept { return WaitAwaiter{this}; }

        void set() noexcept {
            set_.store(true, std::memory_order_release);

            if (reset_ == Reset::Auto) {
                for (;;) {
                    WaitNode* n = pop_one();
                    if (!n) break;

                    if (try_claim(n)) {
                        set_.store(false, std::memory_order_release);
                        detail::resume_on(n->h, n->thread_id);
                        delete n;
                        return;
                    }
                    delete n;
                }
            } else {
                WaitNode* list = exchange_all();
                while (list) {
                    WaitNode* next = list->next;
                    if (try_claim(list))
                        detail::resume_on(list->h, list->thread_id);
                    delete list;
                    list = next;
                }
            }
        }

        void reset() noexcept {
            if (reset_ == Reset::Manual)
                set_.store(false, std::memory_order_release);
        }
    };

} // namespace usub::uvent::sync

#endif // UVENT_SYNC_ASYNCEVENT_H
