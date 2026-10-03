#pragma once
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

namespace are {
enum class SampleKind : std::uint8_t { integer, float32, float64 };
struct SourceFormat {
    SampleKind kind{SampleKind::integer};
    std::uint32_t rate{};
    std::uint16_t channels{};
    std::uint16_t container_bits{};
    std::uint16_t valid_bits{};
    std::uint32_t channel_mask{};
    [[nodiscard]] std::size_t sample_bytes() const noexcept { return container_bits / 8; }
    [[nodiscard]] std::size_t frame_bytes() const noexcept { return sample_bytes() * channels; }
    [[nodiscard]] bool valid() const noexcept;
    bool operator==(const SourceFormat&) const = default;
};
struct OutputFormat {
    SampleKind kind{};
    std::uint16_t container_bits{};
    std::uint16_t valid_bits{};
    bool big_endian{};
    // ASIO Int32LSB/MSB16/18/20/24 stores sign-extended, right-justified data.
    bool right_justified{};
    [[nodiscard]] std::size_t sample_bytes() const noexcept { return container_bits / 8; }
};
[[nodiscard]] std::optional<OutputFormat> asio_format(long sample_type) noexcept;
[[nodiscard]] bool is_lossless(const SourceFormat& source, const OutputFormat& target) noexcept;
[[nodiscard]] std::string_view kind_name(SampleKind kind) noexcept;
// Normalized double for DSP; validated source and a complete sample required.
[[nodiscard]] double normalized_sample(const SourceFormat& source, const std::byte* sample) noexcept;

// Called only with a validated, lossless pair. No allocation, locks, gain, DSP,
// dithering or resampling. The destination contains one ASIO channel.
void convert_channel(const SourceFormat& source, const OutputFormat& target,
                     std::span<const std::byte> interleaved, std::uint16_t channel,
                     std::span<std::byte> planar) noexcept;
} // namespace are
