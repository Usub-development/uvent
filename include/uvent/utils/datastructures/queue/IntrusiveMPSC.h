//
// Created by kirill on 8/20/26.
//

#ifndef UVENT_INTRUSIVE_MPSC_H
#define UVENT_INTRUSIVE_MPSC_H

#include <atomic>
#include <type_traits>
#include "uvent/utils/datastructures/DataStructuresMetadata.h"
#include "uvent/utils/intrinsics/optimizations.h"

namespace usub::queue::concurrent
{
    struct MPSCNode
    {
        std::atomic<MPSCNode*> mpsc_next_{nullptr};
    };

    template <class Node>
    class alignas(data_structures::metadata::CACHELINE_SIZE) IntrusiveMPSCQueue
    {
        static_assert(std::is_base_of_v<MPSCNode, Node>, "Node must derive from MPSCNode");

    public:
        IntrusiveMPSCQueue() noexcept : tail_(&this->stub_), head_(&this->stub_) {}

        IntrusiveMPSCQueue(const IntrusiveMPSCQueue&) = delete;
        IntrusiveMPSCQueue& operator=(const IntrusiveMPSCQueue&) = delete;

        void push(Node* n) noexcept
        {
            MPSCNode* node = static_cast<MPSCNode*>(n);
            node->mpsc_next_.store(nullptr, std::memory_order_relaxed);
            MPSCNode* prev = this->tail_.exchange(node, std::memory_order_acq_rel);
            prev->mpsc_next_.store(node, std::memory_order_release);
        }

        Node* pop() noexcept
        {
            MPSCNode* head = this->head_;
            MPSCNode* next = head->mpsc_next_.load(std::memory_order_acquire);

            if (head == &this->stub_)
            {
                if (!next)
                    return nullptr;
                this->head_ = next;
                head = next;
                next = next->mpsc_next_.load(std::memory_order_acquire);
            }

            if (next)
            {
                this->head_ = next;
                prefetch_for_read(next);
                return static_cast<Node*>(head);
            }

            if (this->tail_.load(std::memory_order_acquire) != head)
                return nullptr;

            this->stub_.mpsc_next_.store(nullptr, std::memory_order_relaxed);
            MPSCNode* prev = this->tail_.exchange(&this->stub_, std::memory_order_acq_rel);
            prev->mpsc_next_.store(&this->stub_, std::memory_order_release);

            next = head->mpsc_next_.load(std::memory_order_acquire);
            if (next)
            {
                this->head_ = next;
                return static_cast<Node*>(head);
            }
            return nullptr;
        }

        bool empty_relaxed() const noexcept
        {
            return this->tail_.load(std::memory_order_relaxed) == &this->stub_ &&
                this->head_ == &this->stub_;
        }

    private:
        alignas(data_structures::metadata::CACHELINE_SIZE) std::atomic<MPSCNode*> tail_;
        alignas(data_structures::metadata::CACHELINE_SIZE) MPSCNode* head_;
        alignas(data_structures::metadata::CACHELINE_SIZE) MPSCNode stub_;
    };
} // namespace usub::queue::concurrent

#endif // UVENT_INTRUSIVE_MPSC_H
