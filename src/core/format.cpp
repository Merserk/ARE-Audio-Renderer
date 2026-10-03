#include "core/format.hpp"
#include <bit>
#include <cstring>
#include <cmath>

namespace are {
bool SourceFormat::valid() const noexcept {
    if (rate < 8000 || rate > 768000 || channels == 0 || channels > 32) return false;
    if (kind == SampleKind::float32) return container_bits == 32 && valid_bits == 32;
    if (kind == SampleKind::float64) return container_bits == 64 && valid_bits == 64;
    return (container_bits == 8 || container_bits == 16 || container_bits == 24 || container_bits == 32)
        && valid_bits > 0 && valid_bits <= container_bits;
}
std::optional<OutputFormat> asio_format(long type) noexcept {
    const bool big = type < 16;
    const long base = big ? type : type - 16;
    switch (base) {
    case 0: return OutputFormat{SampleKind::integer, 16, 16, big, false};
    case 1: return OutputFormat{SampleKind::integer, 24, 24, big, false};
    case 2: return OutputFormat{SampleKind::integer, 32, 32, big, false};
    case 3: return OutputFormat{SampleKind::float32, 32, 32, big, false};
    case 4: return OutputFormat{SampleKind::float64, 64, 64, big, false};
    case 8: return OutputFormat{SampleKind::integer, 32, 16, big, true};
    case 9: return OutputFormat{SampleKind::integer, 32, 18, big, true};
    case 10: return OutputFormat{SampleKind::integer, 32, 20, big, true};
    case 11: return OutputFormat{SampleKind::integer, 32, 24, big, true};
    default: return std::nullopt;
    }
}
bool is_lossless(const SourceFormat& s, const OutputFormat& t) noexcept {
    if (!s.valid()) return false;
    if (s.kind == SampleKind::integer) {
        if (t.kind == SampleKind::integer) return t.valid_bits >= s.valid_bits;
        return t.kind == SampleKind::float64 || s.valid_bits <= 24;
    }
    if (s.kind == SampleKind::float32) return t.kind == SampleKind::float32 || t.kind == SampleKind::float64;
    return t.kind == SampleKind::float64;
}
std::string_view kind_name(SampleKind kind) noexcept {
    switch (kind) {
    case SampleKind::integer: return "PCM";
    case SampleKind::float32: return "Float32";
    case SampleKind::float64: return "Float64";
    }
    return "Unknown";
}
namespace {
std::uint64_t load_le(const std::byte* p, std::size_t bytes) noexcept {
    std::uint64_t value = 0;
    for (std::size_t i = 0; i < bytes; ++i) value |= std::uint64_t(std::to_integer<unsigned char>(p[i])) << (8 * i);
    return value;
}
void store(std::byte* p, std::uint64_t value, std::size_t bytes, bool big) noexcept {
    for (std::size_t i = 0; i < bytes; ++i) p[big ? bytes - 1 - i : i] = std::byte((value >> (8 * i)) & 0xff);
}
std::int64_t read_integer(const SourceFormat& s, const std::byte* p) noexcept {
    if (s.container_bits == 8) return (std::int64_t(std::to_integer<unsigned char>(*p)) - 128) >> (8 - s.valid_bits);
    const auto raw = load_le(p, s.sample_bytes());
    const auto sign = std::uint64_t{1} << (s.container_bits - 1);
    const auto signed_value = std::bit_cast<std::int64_t>((raw ^ sign) - sign);
    return signed_value >> (s.container_bits - s.valid_bits);
}
} // namespace
double normalized_sample(const SourceFormat& s, const std::byte* p) noexcept {
    if (s.kind == SampleKind::integer)
        return double(read_integer(s, p)) / double(std::uint64_t{1} << (s.valid_bits - 1));
    const auto raw = load_le(p, s.sample_bytes());
    const double value = s.kind == SampleKind::float32 ? double(std::bit_cast<float>(std::uint32_t(raw))) : std::bit_cast<double>(raw);
    return std::isfinite(value) ? value : 0;
}
void convert_channel(const SourceFormat& s, const OutputFormat& t,
                     std::span<const std::byte> input, std::uint16_t channel,
                     std::span<std::byte> output) noexcept {
    const auto frames = input.size() / s.frame_bytes();
    if (channel >= s.channels || output.size() < frames * t.sample_bytes()) return;
    const auto* p = input.data() + std::size_t(channel) * s.sample_bytes();
    auto* q = output.data();
    for (std::size_t i = 0; i < frames; ++i, p += s.frame_bytes(), q += t.sample_bytes()) {
        if (s.kind == SampleKind::integer) {
            const auto n = read_integer(s, p);
            if (t.kind == SampleKind::integer) {
                // Shift an unsigned representation to avoid signed-shift UB.
                const auto shift = (t.right_justified ? t.valid_bits : t.container_bits) - s.valid_bits;
                store(q, std::bit_cast<std::uint64_t>(n) << shift, t.sample_bytes(), t.big_endian);
            } else {
                const double normalized = double(n) / double(std::uint64_t{1} << (s.valid_bits - 1));
                if (t.kind == SampleKind::float32) store(q, std::bit_cast<std::uint32_t>(float(normalized)), 4, t.big_endian);
                else store(q, std::bit_cast<std::uint64_t>(normalized), 8, t.big_endian);
            }
        } else if (s.kind == t.kind) {
            // Preserve all bits, including signed zero and NaN payloads.
            store(q, load_le(p, s.sample_bytes()), t.sample_bytes(), t.big_endian);
        } else {
            const float f = std::bit_cast<float>(std::uint32_t(load_le(p, 4)));
            store(q, std::bit_cast<std::uint64_t>(double(f)), 8, t.big_endian);
        }
    }
}
} // namespace are
