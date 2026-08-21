//
// Created by kirill on 8/20/26.
//

#include "uvent/utils/sync/HazardPointers.h"

#include <algorithm>

namespace usub::utils::sync
{
    thread_local HazardDomain::LocalGuard HazardDomain::tls_{};

    HazardDomain& HazardDomain::instance() noexcept
    {
        static HazardDomain d;
        return d;
    }

    HazardDomain::Record* HazardDomain::local_record()
    {
        if (!tls_.rec)
            tls_.rec = acquire_record();
        return tls_.rec;
    }

    HazardDomain::Record* HazardDomain::acquire_record()
    {
        for (Record* r = this->head_.load(std::memory_order_acquire); r; r = r->next)
        {
            bool expected = false;
            if (!r->active.load(std::memory_order_relaxed) &&
                r->active.compare_exchange_strong(expected, true, std::memory_order_acq_rel, std::memory_order_relaxed))
                return r;
        }

        auto* r = new Record{};
        for (auto& h : r->hp)
            h.store(nullptr, std::memory_order_relaxed);
        r->active.store(true, std::memory_order_relaxed);
        r->retired.reserve(64);

        Record* old = this->head_.load(std::memory_order_relaxed);
        do
        {
            r->next = old;
        } while (!this->head_.compare_exchange_weak(old, r, std::memory_order_release, std::memory_order_relaxed));
        this->n_records_.fetch_add(1, std::memory_order_acq_rel);
        return r;
    }

    void HazardDomain::release_record(Record* rec)
    {
        scan(rec);
        if (!rec->retired.empty())
        {
            std::lock_guard lk(this->orphan_mu_);
            this->orphans_.insert(this->orphans_.end(), rec->retired.begin(), rec->retired.end());
            rec->retired.clear();
        }
        for (auto& h : rec->hp)
            h.store(nullptr, std::memory_order_relaxed);
        rec->active.store(false, std::memory_order_release);
    }

    HazardDomain::LocalGuard::~LocalGuard()
    {
        if (this->rec)
        {
            instance().release_record(this->rec);
            this->rec = nullptr;
        }
    }

    void HazardDomain::retire(Record* rec, void* p, void (*deleter)(void*))
    {
        rec->retired.push_back({p, deleter});
        const std::size_t threshold =
            std::max<std::size_t>(64, 2 * kSlots * this->n_records_.load(std::memory_order_relaxed));
        if (rec->retired.size() >= threshold)
            scan(rec);
    }

    void HazardDomain::collect_hazards(std::vector<void*>& out)
    {
        out.clear();
        for (Record* r = this->head_.load(std::memory_order_acquire); r; r = r->next)
        {
            if (!r->active.load(std::memory_order_acquire))
                continue;
            for (auto& h : r->hp)
            {
                void* p = h.load(std::memory_order_acquire);
                if (p)
                    out.push_back(p);
            }
        }
        std::sort(out.begin(), out.end());
    }

    void HazardDomain::reclaim(Record* rec, std::vector<Retired>& list)
    {
        std::vector<void*>& hazards = rec->scratch;
        full_fence();
        collect_hazards(hazards);

        std::size_t w = 0;
        for (std::size_t i = 0; i < list.size(); ++i)
        {
            Retired& r = list[i];
            if (std::binary_search(hazards.begin(), hazards.end(), r.p))
                list[w++] = r;
            else
                r.deleter(r.p);
        }
        list.resize(w);
    }

    void HazardDomain::scan(Record* rec)
    {
        reclaim(rec, rec->retired);

        if (this->orphan_mu_.try_lock())
        {
            std::lock_guard lk(this->orphan_mu_, std::adopt_lock);
            if (!this->orphans_.empty())
                reclaim(rec, this->orphans_);
        }
    }
} // namespace usub::utils::sync
