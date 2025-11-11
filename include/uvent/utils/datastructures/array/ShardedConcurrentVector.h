//
// Created by Kirill Zhukov on 20.10.2025.
//

#ifndef SHARDEDCONCURRENTVECTOR_H
#define SHARDEDCONCURRENTVECTOR_H

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <new>
#include <span>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

#include <uvent/utils/datastructures/DataStructuresMetadata.h>
#include <uvent/utils/intrinsincs/optimizations.h>

// ~173.715 Mops/s

namespace usub::array::sharded {
class ThreadIndex {
public:
  explicit ThreadIndex(std::size_t capacity) : cap_(capacity), next_(0) {}

  std::size_t get_or_register() {
    if (this->tid_ != kUnassigned)
      return tid_;
    const auto id = this->next_.fetch_add(1, std::memory_order_acq_rel);
    if (id >= this->cap_)
      TRAP();
    this->tid_ = id;
    return this->tid_;
  }

  [[nodiscard]] std::size_t capacity() const { return this->cap_; }

private:
  static inline thread_local std::size_t tid_ = static_cast<std::size_t>(-1);
  static constexpr std::size_t kUnassigned = static_cast<std::size_t>(-1);
  const std::size_t cap_;
  std::atomic<std::size_t> next_;
};

template <class T> class ShardedConcurrentVector {
  struct alignas(data_structures::metadata::CACHELINE_SIZE) Segment {
    std::atomic<std::size_t> tail;
    std::size_t cap{0};
    std::atomic<Segment *> next;

    T *data() noexcept {
      auto *p = reinterpret_cast<unsigned char *>(this);
      p += sizeof(Segment);
      return reinterpret_cast<T *>(p);
    }

    const T *data() const noexcept {
      auto *p = reinterpret_cast<const unsigned char *>(this);
      p += sizeof(Segment);
      return reinterpret_cast<const T *>(p);
    }

    static Segment *allocate(std::size_t cap) {
      if (!cap)
        cap = 1;
      const std::size_t bytes = sizeof(Segment) + cap * sizeof(T);
      void *mem = ::operator new(bytes, std::align_val_t{alignof(Segment)});
      auto *s = new (mem) Segment();
      s->tail.store(0, std::memory_order_relaxed);
      s->cap = cap;
      s->next.store(nullptr, std::memory_order_relaxed);
      return s;
    }

    static void destroy_chain(Segment *s) {
      while (s) {
        Segment *n = s->next.load(std::memory_order_acquire);
        const std::size_t built = s->tail.load(std::memory_order_acquire);
        for (std::size_t i = 0; i < built; ++i)
          std::destroy_at(std::addressof(s->data()[i]));
        s->~Segment();
        ::operator delete(s, std::align_val_t{alignof(Segment)});
        s = n;
      }
    }
  };

  struct alignas(data_structures::metadata::CACHELINE_SIZE) Shard {
    std::atomic<Segment *> head{nullptr};
    std::atomic<Segment *> tail{nullptr};
  };

public:
  explicit ShardedConcurrentVector(
      std::size_t shards = std::thread::hardware_concurrency(),
      std::size_t initial_per_shard = 256)
      : reg_(shards), shards_(shards),
        init_cap_(initial_per_shard ? initial_per_shard : 1) {
    for (auto &sh : this->shards_) {
      Segment *s0 = Segment::allocate(this->init_cap_);
      sh.head.store(s0, std::memory_order_release);
      sh.tail.store(s0, std::memory_order_release);
    }
  }

  ~ShardedConcurrentVector() {
    for (auto &sh : this->shards_)
      Segment::destroy_chain(sh.head.load(std::memory_order_acquire));
  }

  ShardedConcurrentVector(const ShardedConcurrentVector &) = delete;
  ShardedConcurrentVector &operator=(const ShardedConcurrentVector &) = delete;

  template <class... Args> bool emplace_back(Args &&...args) {
    const std::size_t si = this->reg_.get_or_register();
    return this->emplace_back_on_shard(si, std::forward<Args>(args)...);
  }

  template <class... Args> bool emplace_back_any(Args &&...args) {
    const std::size_t si = this->rr_.fetch_add(1, std::memory_order_acq_rel) %
                           this->shards_.size();
    return this->emplace_back_on_shard(si, std::forward<Args>(args)...);
  }

  bool push_back_batch(std::span<const T> src) {
    const std::size_t si = this->reg_.get_or_register();
    return this->push_back_batch_on_shard(si, src);
  }

  template <class It> bool push_back_batch(It first, It last) {
    const std::size_t si = this->reg_.get_or_register();
    return this->push_back_batch_on_shard_iter(si, first, last);
  }

  bool push_back_batch_any(std::span<const T> src) {
    const std::size_t si = this->rr_.fetch_add(1, std::memory_order_acq_rel) %
                           this->shards_.size();
    return this->push_back_batch_on_shard(si, src);
  }

  template <class It> bool push_back_batch_any(It first, It last) {
    const std::size_t si = this->rr_.fetch_add(1, std::memory_order_acq_rel) %
                           this->shards_.size();
    return this->push_back_batch_on_shard_iter(si, first, last);
  }

  template <class F> void for_each(F &&f) {
    for (auto &sh : this->shards_) {
      for (Segment *s = sh.head.load(std::memory_order_acquire); s;
           s = s->next.load(std::memory_order_acquire)) {
        const std::size_t n = s->tail.load(std::memory_order_acquire);
        T *p = s->data();
        for (std::size_t i = 0; i < n; ++i)
          f(p[i]);
      }
    }
  }

  template <class F> void for_each_shard(std::size_t si, F &&f) {
    Shard &sh = this->shards_[si];
    for (Segment *s = sh.head.load(std::memory_order_acquire); s;
         s = s->next.load(std::memory_order_acquire)) {
      const std::size_t n = s->tail.load(std::memory_order_acquire);
      T *p = s->data();
      for (std::size_t i = 0; i < n; ++i)
        f(p[i]);
    }
  }

  [[nodiscard]] std::size_t size_relaxed() const noexcept {
    std::size_t total = 0;
    for (auto &sh : this->shards_) {
      for (Segment *s = sh.head.load(std::memory_order_acquire); s;
           s = s->next.load(std::memory_order_acquire))
        total += s->tail.load(std::memory_order_acquire);
    }
    return total;
  }

  [[nodiscard]] std::size_t shard_size_relaxed(std::size_t si) const noexcept {
    std::size_t total = 0;
    const Shard &sh = this->shards_[si];
    for (Segment *s = sh.head.load(std::memory_order_acquire); s;
         s = s->next.load(std::memory_order_acquire))
      total += s->tail.load(std::memory_order_acquire);
    return total;
  }

  [[nodiscard]] std::size_t shard_count() const noexcept {
    return this->shards_.size();
  }

  void reserve_per_shard(std::size_t min_total_per_shard) {
    for (auto &sh : this->shards_) {
      Segment *last = sh.tail.load(std::memory_order_acquire);
      std::size_t acc = last->cap;
      while (acc < min_total_per_shard) {
        Segment *next = last->next.load(std::memory_order_acquire);
        if (!next) {
          Segment *cand = Segment::allocate(last->cap << 1);
          if (!last->next.compare_exchange_strong(next, cand,
                                                  std::memory_order_acq_rel,
                                                  std::memory_order_acquire)) {
            Segment::destroy_chain(cand);
          } else {
            next = cand;
          }
        }
        sh.tail.compare_exchange_strong(last, next, std::memory_order_acq_rel,
                                        std::memory_order_acquire);
        last = sh.tail.load(std::memory_order_acquire);
        acc += last->cap;
      }
    }
  }

  template <bool C> class Iter {
    using Owner = std::conditional_t<C, const ShardedConcurrentVector *,
                                     ShardedConcurrentVector *>;
    using SegPtr = std::conditional_t<C, const Segment *, Segment *>;
    using Value = std::conditional_t<C, const T, T>;
    using Ref = std::conditional_t<C, const T &, T &>;

  public:
    using iterator_category = std::forward_iterator_tag;
    using value_type = Value;
    using difference_type = std::ptrdiff_t;
    using reference = Ref;

    Iter() : o_(nullptr), si_(0), s_(nullptr), i_(0) {}

    Iter(Owner o, std::size_t si, SegPtr s, std::size_t i)
        : o_(o), si_(si), s_(s), i_(i) {
      this->normalize_();
    }

    reference operator*() const {
      return const_cast<Ref>(this->s_->data()[this->i_]);
    }

    Iter &operator++() {
      this->advance_();
      return *this;
    }

    bool operator==(const Iter &other) const {
      return this->o_ == other.o_ && this->si_ == other.si_ &&
             this->s_ == other.s_ && this->i_ == other.i_;
    }

    bool operator!=(const Iter &other) const { return !(*this == other); }

  private:
    void normalize_() {
      while (this->s_ &&
             this->i_ >= this->s_->tail.load(std::memory_order_acquire)) {
        this->s_ = this->s_->next.load(std::memory_order_acquire);
        this->i_ = 0;
      }
      while (!this->s_) {
        if (this->si_ >= this->o_->shards_.size())
          break;
        this->s_ =
            this->o_->shards_[this->].head.load(std::memory_order_acquire);
        while (this->s_ && this->s_->tail.load(std::memory_order_acquire) == 0)
          this->s_ = this->s_->next.load(std::memory_order_acquire);
        if (!this->s_) {
          ++this->si_;
          if (this->si_ >= this->o_->shards_.size())
            break;
        }
      }
      if (!this->s_) {
        this->si_ = this->o_ ? this->o_->shards_.size() : 0;
        this->i_ = 0;
      }
    }

    void advance_() {
      if (!this->s_)
        return;
      ++this->i_;
      if (this->i_ >= this->s_->tail.load(std::memory_order_acquire)) {
        this->s_ = this->s_->next.load(std::memory_order_acquire);
        this->i_ = 0;
      }
      if (!this->_) {
        ++this->si_;
        if (this->si_ < this->o_->shards_.size()) {
          this->s_ =
              this->o_->shards_[this->si_].head.load(std::memory_order_acquire);
        }
      }
      this->normalize_();
    }

    Owner o_;
    std::size_t si_;
    SegPtr s_;
    std::size_t i_;
  };

  template <bool C> class ShardIter {
    using Owner = std::conditional_t<C, const ShardedConcurrentVector *,
                                     ShardedConcurrentVector *>;
    using SegPtr = std::conditional_t<C, const Segment *, Segment *>;
    using Value = std::conditional_t<C, const T, T>;
    using Ref = std::conditional_t<C, const T &, T &>;

  public:
    using iterator_category = std::forward_iterator_tag;
    using value_type = Value;
    using difference_type = std::ptrdiff_t;
    using reference = Ref;

    ShardIter() : o_(nullptr), s_(nullptr), i_(0) {}

    ShardIter(Owner o, SegPtr s, std::size_t i) : o_(o), s_(s), i_(i) {
      this->normalize_();
    }
    reference operator*() const {
      return const_cast<Ref>(this->s_->data()[this->i_]);
    }

    ShardIter &operator++() {
      this->advance_();
      return *this;
    }

    bool operator==(const ShardIter &other) const {
      return this->o_ == other.o_ && this->s_ == other.s_ &&
             this->i_ == other.i_;
    }
    bool operator!=(const ShardIter &other) const { return !(*this == other); }

  private:
    void normalize_() {
      while (this->s_ &&
             this->i_ >= this->s_->tail.load(std::memory_order_acquire)) {
        this->s_ = s_->next.load(std::memory_order_acquire);
        this->i_ = 0;
      }
      if (!this->s_) {
        this->i_ = 0;
      }
    }

    void advance_() {
      if (!this->s_)
        return;
      ++this->i_;
      if (this->i_ >= this->s_->tail.load(std::memory_order_acquire)) {
        this->s_ = this->s_->next.load(std::memory_order_acquire);
        this->i_ = 0;
      }
      this->normalize_();
    }

    Owner o_;
    SegPtr s_;
    std::size_t i_;
  };

  using iterator = Iter<false>;
  using const_iterator = Iter<true>;
  using shard_iterator = ShardIter<false>;
  using const_shard_iterator = ShardIter<true>;

  iterator begin() {
    return iterator(this, 0,
                    this->shards_.empty()
                        ? nullptr
                        : this->shards_[0].head.load(std::memory_order_acquire),
                    0);
  }

  iterator end() { return iterator(this, this->shards_.size(), nullptr, 0); }
  const_iterator begin() const { return cbegin(); }
  const_iterator end() const { return cend(); }

  const_iterator cbegin() const {
    return const_iterator(
        this, 0,
        this->shards_.empty()
            ? nullptr
            : this->shards_[0].head.load(std::memory_order_acquire),
        0);
  }

  const_iterator cend() const {
    return const_iterator(this, this->shards_.size(), nullptr, 0);
  }

  shard_iterator shard_begin(std::size_t si) {
    return shard_iterator(
        this, this->shards_[si].head.load(std::memory_order_acquire), 0);
  }

  shard_iterator shard_end(std::size_t) {
    return shard_iterator(this, nullptr, 0);
  }

  const_shard_iterator shard_cbegin(std::size_t si) const {
    return const_shard_iterator(
        this, this->shards_[si].head.load(std::memory_order_acquire), 0);
  }

  const_shard_iterator shard_cend(std::size_t) const {
    return const_shard_iterator(this, nullptr, 0);
  }

private:
  template <class... Args>
  bool emplace_back_on_shard(std::size_t shard_idx, Args &&...args) {
    Shard &sh = this->shards_[shard_idx];
    for (;;) {
      Segment *seg = sh.tail.load(std::memory_order_acquire);
      const std::size_t idx = seg->tail.fetch_add(1, std::memory_order_acq_rel);
      if (idx < seg->cap) {
        T *dst = std::addressof(seg->data()[idx]);
        prefetch_for_write(dst);
        std::construct_at(dst, std::forward<Args>(args)...);
        return true;
      }
      seg->tail.fetch_sub(1, std::memory_order_acq_rel);
      Segment *next = seg->next.load(std::memory_order_acquire);
      if (!next) {
        Segment *cand = Segment::allocate(seg->cap << 1);
        if (!seg->next.compare_exchange_strong(next, cand,
                                               std::memory_order_acq_rel,
                                               std::memory_order_acquire)) {
          Segment::destroy_chain(cand);
        } else {
          next = cand;
        }
      }
      if (next)
        sh.tail.compare_exchange_strong(seg, next, std::memory_order_acq_rel,
                                        std::memory_order_acquire);
      cpu_relax();
    }
  }

  bool push_back_batch_on_shard(std::size_t shard_idx, std::span<const T> src) {
    Shard &sh = this->shards_[shard_idx];
    std::size_t remain = src.size();
    std::size_t pos = 0;
    while (remain) {
      Segment *seg = sh.tail.load(std::memory_order_acquire);
      for (;;) {
        std::size_t cur = seg->tail.load(std::memory_order_acquire);
        if (cur >= seg->cap)
          break;
        const std::size_t room = seg->cap - cur;
        const std::size_t take = remain < room ? remain : room;
        if (seg->tail.compare_exchange_weak(cur, cur + take,
                                            std::memory_order_acq_rel,
                                            std::memory_order_acquire)) {
          T *base = seg->data();
          for (std::size_t k = 0; k < take; ++k) {
            T *dst = std::addressof(base[cur + k]);
            prefetch_for_write(dst);
            std::construct_at(dst, src[pos + k]);
          }
          pos += take;
          remain -= take;
          break;
        }
        cpu_relax();
      }
      if (!remain)
        break;
      Segment *next = seg->next.load(std::memory_order_acquire);
      if (!next) {
        Segment *cand = Segment::allocate(seg->cap << 1);
        if (!seg->next.compare_exchange_strong(next, cand,
                                               std::memory_order_acq_rel,
                                               std::memory_order_acquire)) {
          Segment::destroy_chain(cand);
        } else {
          next = cand;
        }
      }
      if (next)
        sh.tail.compare_exchange_strong(seg, next, std::memory_order_acq_rel,
                                        std::memory_order_acquire);
      cpu_relax();
    }
    return true;
  }

  template <class It>
  bool push_back_batch_on_shard_iter(std::size_t shard_idx, It first, It last) {
    Shard &sh = this->shards_[shard_idx];
    while (first != last) {
      Segment *seg = sh.tail.load(std::memory_order_acquire);
      for (;;) {
        std::size_t cur = seg->tail.load(std::memory_order_acquire);
        if (cur >= seg->cap)
          break;
        std::size_t room = seg->cap - cur;

        std::size_t take = 0;
        It it = first;
        while (it != last && take < room) {
          ++it;
          ++take;
        }

        if (take == 0)
          break;
        if (seg->tail.compare_exchange_weak(cur, cur + take,
                                            std::memory_order_acq_rel,
                                            std::memory_order_acquire)) {
          T *base = seg->data();
          It it2 = first;
          for (std::size_t k = 0; k < take; ++k, ++it2) {
            T *dst = std::addressof(base[cur + k]);
            prefetch_for_write(dst);
            std::construct_at(dst, *it2);
          }
          first = it2;
          break;
        }
        cpu_relax();
      }
      if (first == last)
        break;
      Segment *next = seg->next.load(std::memory_order_acquire);
      if (!next) {
        Segment *cand = Segment::allocate(seg->cap << 1);
        if (!seg->next.compare_exchange_strong(next, cand,
                                               std::memory_order_acq_rel,
                                               std::memory_order_acquire)) {
          Segment::destroy_chain(cand);
        } else {
          next = cand;
        }
      }
      if (next)
        sh.tail.compare_exchange_strong(seg, next, std::memory_order_acq_rel,
                                        std::memory_order_acquire);
      cpu_relax();
    }
    return true;
  }

private:
  ThreadIndex reg_;
  std::vector<Shard> shards_;
  const std::size_t init_cap_;
  alignas(data_structures::metadata::CACHELINE_SIZE)
      std::atomic<std::size_t> rr_{0};
};
} // namespace usub::array::sharded

#endif // SHARDEDCONCURRENTVECTOR_H
