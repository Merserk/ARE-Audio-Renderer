#include "core/channel_mapper.hpp"
#include <algorithm>
#include <bit>
#include <stdexcept>

namespace are {
namespace {
constexpr double surround_gain = 0.7071067811865475244;
// Conventional WAVE channel order for legacy formats without a speaker mask.
constexpr std::array<std::uint32_t, 9> default_masks{0, 0x4, 0x3, 0x7, 0x33, 0x37, 0x3f, 0x70f, 0x63f};
std::array<double, 2> stereo_gains(std::uint32_t speaker, std::size_t channel) noexcept {
    switch (speaker) {
    case 0x1: return {1, 0}; // Front left/right.
    case 0x2: return {0, 1};
    case 0x8: return {0.5, 0.5}; // Include LFE at -6 dB before headroom.
    case 0x10: case 0x40: case 0x200: case 0x1000: case 0x8000:
        return {surround_gain, 0};
    case 0x20: case 0x80: case 0x400: case 0x4000: case 0x20000:
        return {0, surround_gain};
    case 0x4: case 0x100: case 0x800: case 0x2000: case 0x10000:
        return {surround_gain, surround_gain};
    default:
        // Unknown/discrete channels are still audible, alternating left/right.
        return channel % 2 ? std::array<double, 2>{0, surround_gain}
                           : std::array<double, 2>{surround_gain, 0};
    }
}
}
void ChannelMapper::configure(const SourceFormat& source, std::uint16_t available_outputs) {
    if (!source.valid() || !available_outputs ||
        (source.channel_mask && std::popcount(source.channel_mask) != source.channels))
        throw std::invalid_argument("Invalid source layout or no available ASIO outputs");
    source_ = source;
    outputs_ = source.channels == 1 && available_outputs >= 2 ? 2
        : source.channels <= available_outputs ? source.channels
        : available_outputs >= 2 ? 2 : 1;
    matrix_ = {};
    mixed_.clear();
    if (!downmix()) return;
    auto mask = source.channel_mask;
    if (!mask && source.channels < default_masks.size()) mask = default_masks[source.channels];
    for (std::size_t c = 0; c < source.channels; ++c) {
        const auto speaker = mask & (~mask + 1u);
        mask &= ~speaker;
        const auto gain = stereo_gains(speaker, c);
        if (outputs_ == 1) matrix_[0][c] = (gain[0] + gain[1]) * 0.5;
        else { matrix_[0][c] = gain[0]; matrix_[1][c] = gain[1]; }
    }
    // One common attenuation preserves stereo balance and prevents coherent
    // full-scale channels from clipping; no limiter or automatic volume boost.
    double maximum = 1;
    for (std::size_t out = 0; out < outputs_; ++out) {
        double sum = 0;
        for (std::size_t c = 0; c < source.channels; ++c) sum += matrix_[out][c];
        maximum = std::max(maximum, sum);
    }
    for (auto& row : matrix_) for (auto& gain : row) gain /= maximum;
    mixed_.resize(block_frames * outputs_);
}
std::uint32_t ChannelMapper::output_mask() const noexcept {
    return downmix() || duplicate_mono() ? outputs_ == 1 ? 0x4u : 0x3u : source_.channel_mask;
}
SourceFormat ChannelMapper::processing_format() const noexcept {
    return downmix() ? SourceFormat{SampleKind::float64, source_.rate, outputs_, 64, 64, output_mask()} : source_;
}
std::span<const double> ChannelMapper::mix(std::span<const std::byte> input) {
    if (!downmix() || input.size() % source_.frame_bytes() || input.size() / source_.frame_bytes() > block_frames)
        throw std::invalid_argument("Invalid channel mix block");
    const auto frames = input.size() / source_.frame_bytes();
    for (std::size_t i = 0; i < frames; ++i) {
        std::array<double, 2> value{};
        for (std::size_t c = 0; c < source_.channels; ++c) {
            const auto sample = normalized_sample(source_, input.data() + i * source_.frame_bytes() + c * source_.sample_bytes());
            for (std::size_t out = 0; out < outputs_; ++out) value[out] += sample * matrix_[out][c];
        }
        for (std::size_t out = 0; out < outputs_; ++out) mixed_[i * outputs_ + out] = value[out];
    }
    return {mixed_.data(), frames * outputs_};
}
} // namespace are
