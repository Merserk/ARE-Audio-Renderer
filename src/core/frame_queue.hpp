#pragma once
#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <vector>

namespace are {
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable: 4324) // Deliberate cache-line separation of SPSC counters.
#endif
// One DirectShow producer, one ASIO consumer. Reconfigure/reset only with both
// stopped. Publish complete frames; never split a PCM sample across callbacks.
class FrameQueue {
public:
    void configure(std::size_t capacity, std::size_t frame_bytes) {
        data_.assign(capacity * frame_bytes, std::byte{});
        capacity_ = capacity;
        frame_bytes_ = frame_bytes;
        reset();
    }
    void reset() noexcept { read_.store(0); write_.store(0); }
    [[nodiscard]] std::size_t capacity() const noexcept { return capacity_; }
    [[nodiscard]] std::size_t size() const noexcept {
        // Snapshot write first: read can advance only as far as published write.
        const auto w = write_.load(std::memory_order_acquire);
        const auto r = read_.load(std::memory_order_acquire);
        return w >= r ? std::size_t(w - r) : 0;
    }
    std::size_t push(std::span<const std::byte> source) noexcept {
        const auto w = write_.load(std::memory_order_relaxed);
        const auto r = read_.load(std::memory_order_acquire);
        const auto count = std::min(source.size() / frame_bytes_, capacity_ - std::size_t(w - r));
        const auto offset = std::size_t(w % capacity_);
        const auto first = std::min(count, capacity_ - offset);
        std::memcpy(data_.data() + offset * frame_bytes_, source.data(), first * frame_bytes_);
        if (count > first) std::memcpy(data_.data(), source.data() + first * frame_bytes_, (count - first) * frame_bytes_);
        write_.store(w + count, std::memory_order_release);
        return count;
    }
    [[nodiscard]] std::span<const std::byte> front(std::size_t max_frames) const noexcept {
        const auto r = read_.load(std::memory_order_relaxed);
        const auto w = write_.load(std::memory_order_acquire);
        const auto offset = std::size_t(r % capacity_);
        const auto count = std::min({max_frames, std::size_t(w - r), capacity_ - offset});
        return {data_.data() + offset * frame_bytes_, count * frame_bytes_};
    }
    void consume(std::size_t frames) noexcept { read_.fetch_add(frames, std::memory_order_release); }
private:
    std::vector<std::byte> data_;
    std::size_t capacity_{};
    std::size_t frame_bytes_{};
    alignas(64) std::atomic<std::uint64_t> read_{};
    alignas(64) std::atomic<std::uint64_t> write_{};
};
} // namespace are
#ifdef _MSC_VER
#pragma warning(pop)
#endif
