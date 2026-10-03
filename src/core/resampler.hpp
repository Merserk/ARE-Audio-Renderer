#pragma once
#include "core/format.hpp"
#include <cmath>
#include <memory>

namespace are {
enum class SrcAlgorithm : std::uint32_t { r8brain, sinc };
[[nodiscard]] constexpr bool valid_src(SrcAlgorithm algorithm) noexcept {
    return algorithm==SrcAlgorithm::r8brain || algorithm==SrcAlgorithm::sinc;
}
[[nodiscard]] inline bool valid_playback_rate(double rate) noexcept {
    return std::isfinite(rate) && rate>=0.05 && rate<=128.0;
}
// Producer-owned converter. Filtering never runs in the ASIO callback.
// Output is normalized, interleaved Float64; initial filter latency is removed.
class SampleRateConverter {
public:
    static constexpr std::size_t input_block = 2048;
    static constexpr double transition_band = 0.5;
    static constexpr double stopband_db = 218.0;
    static constexpr double sinc_precision = 33.0;
    static constexpr double sinc_passband = 0.998;
    SampleRateConverter();
    ~SampleRateConverter();
    SampleRateConverter(const SampleRateConverter&) = delete;
    SampleRateConverter& operator=(const SampleRateConverter&) = delete;
    // Playback speed changes the effective input rate, including fractional
    // rates. Pitch follows speed, as in the default DirectSound renderer.
    void configure(const SourceFormat& source, std::uint32_t output_rate, SrcAlgorithm algorithm = SrcAlgorithm::r8brain, double playback_rate = 1.0);
    void clear() noexcept;
    [[nodiscard]] bool active() const noexcept;
    [[nodiscard]] std::size_t block_frames() const noexcept;
    // Input must contain at most block_frames() complete frames. Consume returned
    // data before calling again. drain() returns bounded chunks until done().
    std::span<const double> process(std::span<const std::byte> input);
    std::span<const double> drain();
    [[nodiscard]] bool done() const noexcept;
    [[nodiscard]] std::uint64_t target_frames() const noexcept;
private:
    struct State;
    std::unique_ptr<State> state_;
};
} // namespace are
