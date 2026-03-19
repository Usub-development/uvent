#ifndef UVENT_SYNC_CANCELLATION_H
#define UVENT_SYNC_CANCELLATION_H

#include <atomic>
#include <coroutine>

#include "uvent/sync/SyncCommon.h"
#include "uvent/system/SystemContext.h"
#include "uvent/utils/sync/TaggedPtr.h"

namespace usub::uvent::sync
{

    struct CancelState
    {
        enum class NodeState : uint8_t
        {
            Waiting = 0,
            Cancelled = 1,
            Claimed = 2
        };

        struct WaitNode
        {
            std::coroutine_handle<> h{};
            WaitNode* next{};
            int thread_id{-1};
            std::atomic<NodeState> st{NodeState::Waiting};
        };

        std::atomic<bool> requested{false};
        TaggedPtr<WaitNode> head{nullptr};

        ~CancelState()
        {
            auto snap = head.load(std::memory_order_acquire);
            while (snap.ptr)
            {
                if (head.compare_exchange_weak(snap, nullptr))
                {
                    WaitNode* list = snap.ptr;
                    while (list)
                    {
                        WaitNode* next = list->next;
                        delete list;
                        list = next;
                    }
                    break;
                }
            }
        }

        void push_waiter(WaitNode* n) noexcept
        {
            auto snap = head.load(std::memory_order_relaxed);
            do
            {
                n->next = snap.ptr;
            }
            while (!head.compare_exchange_weak(snap, n, std::memory_order_release, std::memory_order_relaxed));
        }

        WaitNode* exchange_all() noexcept
        {
            auto snap = head.load(std::memory_order_acquire);
            while (snap.ptr)
            {
                if (head.compare_exchange_weak(snap, nullptr))
                    return snap.ptr;
            }
            return nullptr;
        }
    };

    class CancellationToken
    {
        CancelState* s_{};

    public:
        explicit CancellationToken(CancelState* s = nullptr) noexcept : s_(s) {}

        bool stop_requested() const noexcept { return s_ && s_->requested.load(std::memory_order_acquire); }

        struct Awaiter
        {
            CancelState* s;
            CancelState::WaitNode* node{};

            bool await_ready() noexcept { return s->requested.load(std::memory_order_acquire); }

            bool await_suspend(std::coroutine_handle<> h) noexcept
            {
                using NodeState = CancelState::NodeState;

                node = new CancelState::WaitNode{};
                node->h = h;
                node->thread_id = detail::current_thread_id();
                node->st.store(NodeState::Waiting, std::memory_order_relaxed);

                s->push_waiter(node);

                if (s->requested.load(std::memory_order_acquire))
                {
                    NodeState exp = NodeState::Waiting;
                    if (node->st.compare_exchange_strong(exp, NodeState::Cancelled, std::memory_order_acq_rel,
                                                         std::memory_order_relaxed))
                    {
                        return false;
                    }
                    return true;
                }
                return true;
            }

            void await_resume() noexcept {}
        };

        Awaiter on_cancel() const noexcept { return Awaiter{s_}; }
    };

    class CancellationSource
    {
        CancelState state_;

    public:
        CancellationToken token() noexcept { return CancellationToken(&state_); }

        void request_cancel() noexcept
        {
            using NodeState = CancelState::NodeState;

            bool was = state_.requested.exchange(true, std::memory_order_acq_rel);
            if (was)
                return;

            CancelState::WaitNode* list = state_.exchange_all();
            while (list)
            {
                auto* n = list;
                list = list->next;

                NodeState exp = NodeState::Waiting;
                if (n->st.compare_exchange_strong(exp, NodeState::Claimed, std::memory_order_acq_rel,
                                                  std::memory_order_relaxed))
                {
                    detail::resume_on(n->h, n->thread_id);
                }
                delete n;
            }
        }
    };

} // namespace usub::uvent::sync

#endif // UVENT_SYNC_CANCELLATION_H
