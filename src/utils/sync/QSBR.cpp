//
// Created by root on 9/11/25.
//

#include "uvent/utils/sync/QSBR.h"

namespace usub::utils::sync
{
    thread_local QSBR::ThreadState* QSBR::tls_ = nullptr;
    thread_local std::vector<QSBR::Retired> QSBR::retired_tls_;

    void QSBR::attach_current_thread() {
        if (this->tls_) return;
        thread_local ThreadState state;
        {
            std::lock_guard<std::mutex> lk(this->reg_mu_);
            this->threads_.push_back(&state);
        }
        this->tls_ = &state;
    }

    void QSBR::detach_current_thread() {
        if (!this->tls_) return;
        this->tls_->active.store(false, std::memory_order_relaxed);
        this->tls_ = nullptr;
    }

    void QSBR::enter() noexcept {
        this->tls_->active.store(true, std::memory_order_release);
        this->tls_->epoch.store(this->global_epoch_.load(std::memory_order_relaxed),
                          std::memory_order_release);
    }

    void QSBR::leave() noexcept {
        this->tls_->active.store(false, std::memory_order_release);
    }

    void QSBR::retire(void* p, void(*deleter)(void*)) noexcept {
        this->retired_tls_.push_back({deleter, p, this->global_epoch_.load(std::memory_order_relaxed)});
    }

    uint64_t QSBR::min_active_epoch() const noexcept {
        uint64_t m = std::numeric_limits<uint64_t>::max();
        for (auto* s : this->threads_) {
            if (!s) continue;
            if (s->active.load(std::memory_order_acquire)) {
                uint64_t e = s->epoch.load(std::memory_order_acquire);
                if (e < m) m = e;
            }
        }
        if (m == std::numeric_limits<uint64_t>::max()) {
            return this->global_epoch_.load(std::memory_order_relaxed);
        }
        return m;
    }

    void QSBR::quiesce_tick() {
        uint64_t min_epoch = min_active_epoch();
        if (min_epoch == this->global_epoch_.load(std::memory_order_relaxed)) {
            this->global_epoch_.fetch_add(1, std::memory_order_acq_rel);
        }
        uint64_t safe = min_active_epoch();

        size_t w = 0;
        for (auto & i : this->retired_tls_) {
            if (i.epoch < safe) {
                i.deleter(i.p);
            } else {
                this->retired_tls_[w++] = i;
            }
        }
        this->retired_tls_.resize(w);
    }

}
