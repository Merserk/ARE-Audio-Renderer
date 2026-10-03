#pragma once
#include "core/format.hpp"
#include <array>

namespace are {
struct PlanarBuffer {
    OutputFormat format;
    std::span<std::byte> samples;
    std::span<const double> normalized{}; // Optional unquantized DSP input.
};

// Callback-owned state. Configure before starting ASIO; process allocates nothing.
// Unity gain outside a transition leaves the encoded samples untouched.
class SignalProcessor {
public:
    void configure(std::uint32_t rate) noexcept;
    void restart() noexcept { was_audio_ = false; origin_ = last_; edge_frame_ = 0; }
    void process(std::span<PlanarBuffer> channels, std::size_t frames,
                 std::size_t audio_begin, std::size_t audio_end,
                 long volume_db100, bool smooth, std::uint16_t precision = 0,
                 bool force_quantization = false) noexcept;
    [[nodiscard]] bool transitioning() const noexcept;
    [[nodiscard]] bool silent() const noexcept { return silent_; }
private:
    std::array<double, 32> last_{};
    std::array<double, 32> origin_{};
    std::array<std::uint64_t, 32> random_{};
    std::uint32_t random(std::size_t channel) noexcept;
    std::uint32_t ramp_frames_{1};
    std::uint32_t edge_frame_{};
    std::uint32_t gain_frame_{};
    long volume_{1}; // Force initial target calculation.
    double gain_{1}, gain_start_{1}, gain_target_{1};
    bool was_audio_{};
    bool silent_{true};
    bool smooth_{};
};
} // namespace are
