#include "core/signal_processor.hpp"
#include <algorithm>
#include <bit>
#include <cmath>

namespace are {
namespace {
std::uint64_t load(const std::byte* data, const OutputFormat& format) noexcept {
    std::uint64_t value = 0;
    const auto bytes = format.sample_bytes();
    for (std::size_t b = 0; b < bytes; ++b)
        value |= std::uint64_t(std::to_integer<unsigned>(data[format.big_endian ? bytes - 1 - b : b])) << (8 * b);
    return value;
}
void store(std::byte* data, const OutputFormat& format, std::uint64_t value) noexcept {
    const auto bytes = format.sample_bytes();
    for (std::size_t b = 0; b < bytes; ++b)
        data[format.big_endian ? bytes - 1 - b : b] = std::byte((value >> (8 * b)) & 0xff);
}
double read(const std::byte* data, const OutputFormat& format) noexcept {
    const auto bits = load(data, format);
    if (format.kind == SampleKind::float32) return std::bit_cast<float>(std::uint32_t(bits));
    if (format.kind == SampleKind::float64) return std::bit_cast<double>(bits);
    const auto sign = std::uint64_t{1} << (format.container_bits - 1);
    const auto integer = std::bit_cast<std::int64_t>((bits ^ sign) - sign);
    const auto depth = format.right_justified ? format.valid_bits : format.container_bits;
    return double(integer) / double(std::uint64_t{1} << (depth - 1));
}
double write(std::byte* data, const OutputFormat& format, double sample, std::uint16_t precision, double noise) noexcept {
    if (!std::isfinite(sample)) sample = 0; // Processed output must not create NaN/overflow.
    const auto depth = precision ? precision : format.kind == SampleKind::integer ? format.valid_bits : 0;
    std::int64_t quantized{};
    if (depth) {
        const auto scale = double(std::uint64_t{1} << (depth - 1));
        quantized = std::int64_t(std::clamp(std::round(sample * scale + (sample != 0 ? noise : 0)), -scale, scale - 1));
        sample = double(quantized) / scale;
    }
    if (format.kind == SampleKind::float32) {
        store(data, format, std::bit_cast<std::uint32_t>(float(sample)));
    } else if (format.kind == SampleKind::float64) {
        store(data, format, std::bit_cast<std::uint64_t>(sample));
    } else {
        const auto packing_depth = format.right_justified ? format.valid_bits : format.container_bits;
        store(data, format, std::bit_cast<std::uint64_t>(quantized) << (packing_depth - depth));
    }
    return sample;
}
double curve(std::uint32_t frame, std::uint32_t length) noexcept {
    if (frame >= length) return 1;
    const auto x = double(frame) / length;
    return x * x * (3 - 2 * x);
}
}
void SignalProcessor::configure(std::uint32_t rate) noexcept {
    *this = {};
    ramp_frames_ = std::max(1u, rate / 200); // Five milliseconds.
    edge_frame_ = gain_frame_ = ramp_frames_;
    for (std::size_t c=0; c<random_.size(); ++c) random_[c]=0x853c49e6748fea9bull ^ ((c+1)*0x9e3779b97f4a7c15ull);
}
std::uint32_t SignalProcessor::random(std::size_t channel) noexcept {
    // Independent PCG32 streams. TPDF uses the difference of two uniforms.
    const auto previous=random_[channel];
    random_[channel]=previous*6364136223846793005ull+(channel*2+1);
    const auto bits=std::uint32_t(((previous>>18)^previous)>>27);
    return std::rotr(bits,int(previous>>59));
}
bool SignalProcessor::transitioning() const noexcept {
    return smooth_ && (edge_frame_ < ramp_frames_ || gain_frame_ < ramp_frames_);
}
void SignalProcessor::process(std::span<PlanarBuffer> channels, std::size_t frames,
                              std::size_t audio_begin, std::size_t audio_end,
                              long volume_db100, bool smooth, std::uint16_t precision,
                              bool force_quantization) noexcept {
    smooth_ = smooth;
    if (volume_ != volume_db100) {
        gain_start_ = gain_;
        gain_target_ = volume_db100 <= -10000 ? 0 : std::pow(10.0, double(volume_db100) / 2000);
        gain_frame_ = 0;
        // Initial silence has no previous signal to preserve.
        if (volume_ == 1 || !smooth) { gain_ = gain_target_; gain_frame_ = ramp_frames_; }
        volume_ = volume_db100;
    }
    if (!smooth) { gain_ = gain_target_; gain_frame_ = edge_frame_ = ramp_frames_; }
    silent_ = true;
    const auto count = std::min(channels.size(), last_.size());
    for (std::size_t frame = 0; frame < frames; ++frame) {
        const bool audio = frame >= audio_begin && frame < audio_end;
        if (audio != was_audio_) {
            origin_ = last_;
            edge_frame_ = smooth ? 0 : ramp_frames_;
            was_audio_ = audio;
        }
        if (gain_frame_ < ramp_frames_) {
            ++gain_frame_;
            gain_ = gain_start_ + (gain_target_ - gain_start_) * curve(gain_frame_, ramp_frames_);
        }
        const auto mix = curve(edge_frame_, ramp_frames_);
        for (std::size_t c = 0; c < count; ++c) {
            auto& channel = channels[c];
            auto* destination = channel.samples.data() + frame * channel.format.sample_bytes();
            const bool exact = audio && mix == 1 && gain_ == 1 && channel.normalized.empty() && !force_quantization;
            const auto sample = audio ? (channel.normalized.empty() ? read(destination, channel.format) : channel.normalized[frame]) : 0;
            const auto output = audio ? origin_[c] * (1 - mix) + sample * (mix * gain_)
                                      : origin_[c] * (1 - mix);
            double actual=output;
            if (!exact) {
                const bool dither=(precision || force_quantization || !channel.normalized.empty()) && output != 0;
                const double noise=dither ? (double(random(c))-double(random(c)))/4294967296.0 : 0;
                actual=write(destination, channel.format, output, precision, noise);
            }
            last_[c] = std::isfinite(actual) ? actual : 0;
            if (last_[c] != 0) silent_ = false;
        }
        if (edge_frame_ < ramp_frames_) ++edge_frame_;
    }
}
} // namespace are
