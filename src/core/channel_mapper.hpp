#pragma once
#include "core/format.hpp"
#include <array>
#include <vector>

namespace are {
// Configured before opening ASIO. Mixing runs on the producer, before SRC,
// gain, fades and final quantization. Direct/mono-duplicate paths retain bytes.
class ChannelMapper {
public:
    static constexpr std::size_t block_frames = 4096;
    void configure(const SourceFormat& source, std::uint16_t available_outputs);
    [[nodiscard]] std::uint16_t output_channels() const noexcept { return outputs_; }
    [[nodiscard]] bool downmix() const noexcept { return outputs_ < source_.channels; }
    [[nodiscard]] bool duplicate_mono() const noexcept { return source_.channels == 1 && outputs_ == 2; }
    [[nodiscard]] std::uint16_t source_channel(std::uint16_t output) const noexcept { return duplicate_mono() ? 0 : output; }
    [[nodiscard]] std::uint32_t output_mask() const noexcept;
    [[nodiscard]] SourceFormat processing_format() const noexcept;
    // Complete source frames, at most block_frames. Valid only for a downmix.
    [[nodiscard]] std::span<const double> mix(std::span<const std::byte> input);
private:
    SourceFormat source_{};
    std::uint16_t outputs_{};
    std::array<std::array<double, 32>, 2> matrix_{};
    std::vector<double> mixed_;
};
} // namespace are
