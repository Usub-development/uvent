#include "uvent/utils/sync/QSBR.h"

namespace usub::utils::sync
{
    thread_local QSBR::ThreadState* QSBR::tls_ = nullptr;
    thread_local std::vector<QSBR::Retired> QSBR::retired_tls_;

    QSBR::~QSBR()
    {
        ThreadState* s = this->head_.load(std::memory_order_acquire);
        while (s)
        {
            ThreadState* next = s->next;
            delete s;
            s = next;
        }
        for (auto& r : this->orphans_)
            r.deleter(r.p);
    }

    void QSBR::attach_current_thread()
    {
        if (this->tls_)
            return;
        for (ThreadState* s = this->head_.load(std::memory_order_acquire); s; s = s->next)
        {
            bool expected = false;
            if (s->in_use.compare_exchange_strong(expected, true, std::memory_order_acq_rel, std::memory_order_relaxed))
            {
                s->active.store(false, std::memory_order_relaxed);
                this->tls_ = s;
                return;
            }
        }
        auto* s = new ThreadState();
        s->in_use.store(true, std::memory_order_relaxed);
        ThreadState* h = this->head_.load(std::memory_order_relaxed);
        do
        {
            s->next = h;
        }
        while (!this->head_.compare_exchange_weak(h, s, std::memory_order_release, std::memory_order_relaxed));
        this->tls_ = s;
    }

    void QSBR::detach_current_thread()
    {
        if (!this->tls_)
            return;
        this->tls_->active.store(false, std::memory_order_release);
        if (!this->retired_tls_.empty())
        {
            std::lock_guard<std::mutex> lk(this->orphan_mu_);
            this->orphans_.insert(this->orphans_.end(), this->retired_tls_.begin(), this->retired_tls_.end());
            this->orphan_count_.store(this->orphans_.size(), std::memory_order_release);
            this->retired_tls_.clear();
        }
        this->tls_->in_use.store(false, std::memory_order_release);
        this->tls_ = nullptr;
    }

    void QSBR::enter() noexcept
    {
        this->tls_->epoch.store(this->global_epoch_.load(std::memory_order_relaxed), std::memory_order_relaxed);
        this->tls_->active.store(true, std::memory_order_release);
    }

    void QSBR::leave() noexcept { this->tls_->active.store(false, std::memory_order_release); }

    void QSBR::retire(void* p, void (*deleter)(void*)) noexcept
    {
        this->retired_tls_.push_back({deleter, p, this->global_epoch_.load(std::memory_order_relaxed)});
    }

    uint64_t QSBR::min_active_epoch() const noexcept
    {
        uint64_t m = std::numeric_limits<uint64_t>::max();
        for (ThreadState* s = this->head_.load(std::memory_order_acquire); s; s = s->next)
        {
            if (!s->in_use.load(std::memory_order_acquire))
                continue;
            if (s->active.load(std::memory_order_acquire))
            {
                const uint64_t e = s->epoch.load(std::memory_order_acquire);
                if (e < m)
                    m = e;
            }
        }
        if (m == std::numeric_limits<uint64_t>::max())
            return this->global_epoch_.load(std::memory_order_relaxed);
        return m;
    }

    void QSBR::adopt_orphans_into(std::vector<Retired>& out, uint64_t safe)
    {
        std::lock_guard<std::mutex> lk(this->orphan_mu_);
        size_t w = 0;
        for (auto& r : this->orphans_)
        {
            if (r.epoch < safe)
                out.push_back(r);
            else
                this->orphans_[w++] = r;
        }
        this->orphans_.resize(w);
        this->orphan_count_.store(w, std::memory_order_release);
    }

    void QSBR::quiesce_tick()
    {
        const uint64_t min_epoch = min_active_epoch();
        if (min_epoch == this->global_epoch_.load(std::memory_order_relaxed))
            this->global_epoch_.fetch_add(1, std::memory_order_acq_rel);

        const uint64_t safe = min_active_epoch();

        size_t w = 0;
        for (auto& i : this->retired_tls_)
        {
            if (i.epoch < safe)
                i.deleter(i.p);
            else
                this->retired_tls_[w++] = i;
        }
        this->retired_tls_.resize(w);

        if (this->orphan_count_.load(std::memory_order_acquire) != 0)
        {
            std::vector<Retired> adopt;
            adopt_orphans_into(adopt, safe);
            for (auto& r : adopt)
                r.deleter(r.p);
        }
    }
} // namespace usub::utils::sync
