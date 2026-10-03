#include "core/resampler.hpp"
#include <CDSPResampler.h>
#include <soxr.h>
#include <algorithm>
#include <array>
#include <stdexcept>
#include <string>
#include <vector>

namespace are {
struct SampleRateConverter::State {
    SourceFormat source;
    std::uint32_t rate{};
    double effective_rate{}, playback_rate{};
    std::vector<std::unique_ptr<r8b::CDSPResampler>> channels;
    soxr_t sinc{};
    std::vector<double> input, output;
    std::uint64_t received{}, emitted{};
    std::size_t block{}, startup{}, drain_blocks{}, drain_limit{};
    bool finishing{};
    ~State() { soxr_delete(sinc); }
    std::uint64_t target() const noexcept {
        if (playback_rate==1.0)
            return received / source.rate * rate + (received % source.rate * rate + source.rate / 2) / source.rate;
        return std::uint64_t(std::floor(static_cast<long double>(received)*rate/effective_rate+0.5L));
    }
    std::span<const double> convert(std::size_t frames, bool zeros) {
        if (sinc) {
            std::size_t consumed{}, count{};
            const auto error=soxr_process(sinc,zeros ? nullptr : input.data(),zeros ? 0 : frames,&consumed,
                output.data(),output.size()/source.channels,&count);
            if (error) throw std::runtime_error(error);
            if (!zeros && consumed!=frames) throw std::runtime_error("Sinc failed to consume an input block");
            const auto available=std::size_t(std::min<std::uint64_t>(count,target()-emitted));
            if (zeros && !available && emitted!=target()) throw std::runtime_error("Sinc ended before the source duration");
            emitted+=available;
            return {output.data(),available*source.channels};
        }
        std::array<double*,32> planar{};
        int count = -1;
        for (std::size_t c=0; c<channels.size(); ++c) {
            auto* data=input.data()+c*block;
            if (zeros) std::fill_n(data, frames, 0.0);
            const auto produced=channels[c]->process(data,int(frames),planar[c]);
            if (count>=0 && produced!=count) throw std::runtime_error("Resampler channel lengths differ");
            count=produced;
        }
        if (count<0) throw std::runtime_error("Invalid resampler output");
        const auto available=std::size_t(std::min<std::uint64_t>(std::uint64_t(count),target()-emitted));
        if (available*channels.size()>output.size()) throw std::runtime_error("Resampler output exceeded its bound");
        for (std::size_t i=0; i<available; ++i)
            for (std::size_t c=0; c<channels.size(); ++c) output[i*channels.size()+c]=planar[c][i];
        emitted+=available;
        return {output.data(),available*channels.size()};
    }
};
SampleRateConverter::SampleRateConverter() = default;
SampleRateConverter::~SampleRateConverter() = default;
void SampleRateConverter::configure(const SourceFormat& source, std::uint32_t output_rate, SrcAlgorithm algorithm, double playback_rate) {
    if (!source.valid() || output_rate<8000 || output_rate>768000) throw std::invalid_argument("Unsupported resampling rate or format");
    if (!valid_src(algorithm)) throw std::invalid_argument("Unsupported SRC algorithm");
    if (!valid_playback_rate(playback_rate)) throw std::invalid_argument("Unsupported playback speed");
    state_.reset();
    const auto effective_rate=source.rate*playback_rate;
    if (effective_rate==output_rate) return;
    auto state=std::make_unique<State>();
    state->source=source; state->rate=output_rate;
    state->effective_rate=effective_rate; state->playback_rate=playback_rate;
    // Limit a producer output block at large upsampling/slow-motion ratios.
    // Otherwise 8 kHz -> 768 kHz at 0.05x could allocate gigabytes for 32 ch.
    const auto ratio=output_rate/effective_rate;
    state->block=std::size_t(std::clamp(std::floor(32768.0/ratio),1.0,double(input_block)));
    state->input.resize(state->block*source.channels);
    if (algorithm==SrcAlgorithm::sinc) {
        auto quality=soxr_quality_spec(SOXR_32_BITQ|SOXR_LINEAR_PHASE,SOXR_DOUBLE_PRECISION|SOXR_HI_PREC_CLOCK|SOXR_ROLLOFF_NONE);
        quality.precision=sinc_precision;
        quality.passband_end=sinc_passband;
        quality.stopband_begin=1.0;
        const auto io=soxr_io_spec(SOXR_FLOAT64_I,SOXR_FLOAT64_I);
        const auto runtime=soxr_runtime_spec(1); // Producer thread; no worker pool in the audio callback.
        soxr_error_t error{};
        state->sinc=soxr_create(effective_rate,output_rate,source.channels,&error,&io,&quality,&runtime);
        if (error || !state->sinc) throw std::runtime_error(error ? error : "Cannot create Sinc converter");
        // A fixed output bound prevents FFT bursts from allocating in process().
        const auto capacity=std::size_t(std::ceil(state->block*ratio))+16;
        state->output.resize(capacity*source.channels);
    } else {
        for (std::size_t c=0; c<source.channels; ++c)
            state->channels.push_back(std::make_unique<r8b::CDSPResampler>(effective_rate,output_rate,int(state->block),transition_band,stopband_db,r8b::fprLinearPhase));
        state->output.resize(std::size_t(state->channels[0]->getMaxOutLen(int(state->block)))*source.channels);
        state->startup=std::size_t(state->channels[0]->getInputRequiredForOutput(1));
    }
    state->drain_limit=(state->startup+state->block-1)/state->block+32;
    state_=std::move(state);
}
void SampleRateConverter::clear() noexcept {
    if (!state_) return;
    for (auto& channel:state_->channels) channel->clear();
    if (state_->sinc) soxr_clear(state_->sinc); // A failure is retained by soxr and reported by process().
    state_->received=state_->emitted=0; state_->drain_blocks=0; state_->finishing=false;
}
bool SampleRateConverter::active() const noexcept { return state_!=nullptr; }
std::size_t SampleRateConverter::block_frames() const noexcept { return state_?state_->block:input_block; }
std::uint64_t SampleRateConverter::target_frames() const noexcept { return state_?state_->target():0; }
bool SampleRateConverter::done() const noexcept { return !state_ || (state_->finishing && state_->emitted==state_->target()); }
std::span<const double> SampleRateConverter::process(std::span<const std::byte> data) {
    if (!state_ || state_->finishing) throw std::logic_error("Inactive or finished resampler");
    const auto& source=state_->source;
    if (data.size()%source.frame_bytes() || data.size()/source.frame_bytes()>state_->block) throw std::invalid_argument("Invalid resampler input block");
    const auto frames=data.size()/source.frame_bytes();
    if (!frames) return {};
    for (std::size_t c=0; c<source.channels; ++c)
        for (std::size_t i=0; i<frames; ++i)
            state_->input[state_->sinc ? i*source.channels+c : c*state_->block+i]=normalized_sample(source,data.data()+i*source.frame_bytes()+c*source.sample_bytes());
    state_->received+=frames;
    return state_->convert(frames,false);
}
std::span<const double> SampleRateConverter::drain() {
    if (!state_) return {};
    if (state_->sinc && !state_->finishing) {
        // SoX buffers FFT output internally. Its explicit EOS drains all of
        // it, in bounded chunks, without guessed filter/startup limits.
        const auto capacity=state_->output.size()/state_->source.channels;
        const auto pending=state_->target()-state_->emitted;
        state_->drain_limit=std::size_t((pending+capacity-1)/capacity)+2;
    }
    state_->finishing=true;
    if (done()) return {};
    if (++state_->drain_blocks>state_->drain_limit) throw std::runtime_error("Resampler failed to drain");
    // Both backends remove initial filter delay. r8brain receives zero padding;
    // Sinc receives its explicit end-of-input signal. Trim to track duration.
    return state_->convert(state_->block,true);
}
} // namespace are
