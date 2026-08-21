//
// Created by kirill on 8/20/26.
//

#ifndef UVENT_HAZARD_POINTERS_H
#define UVENT_HAZARD_POINTERS_H

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <vector>

#include "uvent/utils/datastructures/DataStructuresMetadata.h"
#include "uvent/utils/intrinsics/optimizations.h"

namespace usub::utils::sync
{
    class HazardDomain
    {
    public:
        static constexpr std::size_t kSlots = data_structures::metadata::CACHELINE_SIZE / sizeof(void*);

        struct Retired
        {
            void* p;
            void (*deleter)(void*);
        };

        struct alignas(data_structures::metadata::CACHELINE_SIZE) Record
        {
            std::atomic<void*> hp[kSlots];
            alignas(data_structures::metadata::CACHELINE_SIZE) std::atomic<bool> active{false};
            Record* next{nullptr};
            std::vector<Retired> retired;
            std::vector<void*> scratch;
            unsigned victim{0};
        };

        static HazardDomain& instance() noexcept;

        Record* local_record();

        template <class T>
        T* protect(Record* rec, const std::atomic<T*>& src) noexcept
        {
            T* p = src.load(std::memory_order_acquire);
            for (;;)
            {
                if (!p)
                    return nullptr;
                void* raw = static_cast<void*>(p);
                for (std::size_t i = 0; i < kSlots; ++i)
                    if (rec->hp[i].load(std::memory_order_relaxed) == raw)
                        return p;

                std::atomic<void*>& slot = rec->hp[rec->victim];
                rec->victim = (rec->victim + 1) & (kSlots - 1);
                hazard_publish(slot, raw);

                T* again = src.load(std::memory_order_seq_cst);
                if (again == p)
                    return p;
                p = again;
            }
        }

        void retire(Record* rec, void* p, void (*deleter)(void*));

        void scan(Record* rec);

        std::size_t records_count() const noexcept { return this->n_records_.load(std::memory_order_acquire); }

    private:
        HazardDomain() = default;

        Record* acquire_record();
        void release_record(Record* rec);
        void collect_hazards(std::vector<void*>& out);
        void reclaim(Record* rec, std::vector<Retired>& list);

        std::atomic<Record*> head_{nullptr};
        std::atomic<std::size_t> n_records_{0};
        std::mutex orphan_mu_;
        std::vector<Retired> orphans_;

        struct LocalGuard
        {
            Record* rec{nullptr};
            ~LocalGuard();
        };
        static thread_local LocalGuard tls_;
    };
} // namespace usub::utils::sync

#endif // UVENT_HAZARD_POINTERS_H
