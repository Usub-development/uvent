#ifndef UVENT_SYNC_TAGGEDPTR_H
#define UVENT_SYNC_TAGGEDPTR_H

#include <atomic>
#include <cstdint>
#include <type_traits>

namespace usub::uvent::sync {

    /// TaggedPtr<T> — ABA-safe tagged pointer for lock-free data structures.
    ///
    /// Packs a 16-bit generation counter into the upper bits of a 64-bit word
    /// alongside a 48-bit pointer.  Every successful CAS bumps the tag by 1
    /// automatically, so a recycled address with a stale tag will never match.
    ///
    /// Layout (64-bit, 4-level paging):
    ///   [63 ─── 48] 16-bit tag   (wraps at 65535 → 0)
    ///   [47 ───  0] 48-bit pointer (sign-extended for canonical form)
    ///
    /// For 5-level paging (LA57) set la57 = true — tag shrinks to 7 bits.
    ///
    template<class T>
    class TaggedPtr {
        static_assert(sizeof(void*) == 8, "TaggedPtr requires a 64-bit platform");

        static constexpr bool     la57       = false;
        static constexpr unsigned kPtrBits   = la57 ? 57 : 48;
        static constexpr unsigned kTagBits   = 64 - kPtrBits;
        static constexpr uint64_t kPtrMask   = (1ULL << kPtrBits) - 1;
        static constexpr unsigned kTagShift  = kPtrBits;
        static constexpr uint64_t kTagVMask  = (1ULL << kTagBits) - 1;

        std::atomic<uint64_t> bits_{0};

        // ── Pack / unpack ──────────────────────────────────────────
        static constexpr uint64_t pack(T* ptr, uint16_t tag) noexcept {
            const auto raw = reinterpret_cast<uint64_t>(ptr) & kPtrMask;
            return raw | (static_cast<uint64_t>(tag & kTagVMask) << kTagShift);
        }

        static constexpr T* unpack_ptr(uint64_t v) noexcept {
            const uint64_t raw = v & kPtrMask;
            constexpr uint64_t sign_bit = 1ULL << (kPtrBits - 1);
            return reinterpret_cast<T*>((raw ^ sign_bit) - sign_bit);
        }

        static constexpr uint16_t unpack_tag(uint64_t v) noexcept {
            return static_cast<uint16_t>((v >> kTagShift) & kTagVMask);
        }

    public:
        /// Immutable snapshot of the current {pointer, tag} pair.
        struct Snapshot {
            T*       ptr{};
            uint16_t tag{};
        };

        // ── Constructors ───────────────────────────────────────────
        constexpr TaggedPtr() noexcept = default;

        explicit constexpr TaggedPtr(T* p, uint16_t tag = 0) noexcept
            : bits_(pack(p, tag)) {}

        TaggedPtr(const TaggedPtr&)            = delete;
        TaggedPtr& operator=(const TaggedPtr&) = delete;

        // ── Load / store ───────────────────────────────────────────
        [[nodiscard]]
        Snapshot load(std::memory_order mo = std::memory_order_acquire) const noexcept {
            const uint64_t v = bits_.load(mo);
            return {unpack_ptr(v), unpack_tag(v)};
        }

        void store(Snapshot s,
                   std::memory_order mo = std::memory_order_release) noexcept {
            bits_.store(pack(s.ptr, s.tag), mo);
        }

        void store(T* ptr, uint16_t tag,
                   std::memory_order mo = std::memory_order_release) noexcept {
            bits_.store(pack(ptr, tag), mo);
        }

        // ── CAS — tag auto-incremented on success ──────────────────
        //
        //  On failure `expected` is updated to the current snapshot
        //  (both pointer AND tag), so callers can retry without
        //  reloading.

        bool compare_exchange_weak(
            Snapshot& expected,
            T* desired,
            std::memory_order success = std::memory_order_acq_rel,
            std::memory_order failure = std::memory_order_acquire) noexcept
        {
            uint64_t exp = pack(expected.ptr, expected.tag);
            const uint16_t ntag = static_cast<uint16_t>((expected.tag + 1) & kTagVMask);
            const uint64_t des  = pack(desired, ntag);

            if (bits_.compare_exchange_weak(exp, des, success, failure))
                return true;
            expected = {unpack_ptr(exp), unpack_tag(exp)};
            return false;
        }

        bool compare_exchange_strong(
            Snapshot& expected,
            T* desired,
            std::memory_order success = std::memory_order_acq_rel,
            std::memory_order failure = std::memory_order_acquire) noexcept
        {
            uint64_t exp = pack(expected.ptr, expected.tag);
            const uint16_t ntag = static_cast<uint16_t>((expected.tag + 1) & kTagVMask);
            const uint64_t des  = pack(desired, ntag);

            if (bits_.compare_exchange_strong(exp, des, success, failure))
                return true;
            expected = {unpack_ptr(exp), unpack_tag(exp)};
            return false;
        }

        // ── CAS — explicit desired tag (no auto-increment) ────────

        bool compare_exchange_weak(
            Snapshot& expected,
            T* desired,
            uint16_t desired_tag,
            std::memory_order success = std::memory_order_acq_rel,
            std::memory_order failure = std::memory_order_acquire) noexcept
        {
            uint64_t exp = pack(expected.ptr, expected.tag);
            const uint64_t des = pack(desired, desired_tag);

            if (bits_.compare_exchange_weak(exp, des, success, failure))
                return true;
            expected = {unpack_ptr(exp), unpack_tag(exp)};
            return false;
        }

        bool compare_exchange_strong(
            Snapshot& expected,
            T* desired,
            uint16_t desired_tag,
            std::memory_order success = std::memory_order_acq_rel,
            std::memory_order failure = std::memory_order_acquire) noexcept
        {
            uint64_t exp = pack(expected.ptr, expected.tag);
            const uint64_t des = pack(desired, desired_tag);

            if (bits_.compare_exchange_strong(exp, des, success, failure))
                return true;
            expected = {unpack_ptr(exp), unpack_tag(exp)};
            return false;
        }
    };

} // namespace usub::uvent::sync

#endif // UVENT_SYNC_TAGGEDPTR_H
