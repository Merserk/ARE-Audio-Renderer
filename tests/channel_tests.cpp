#include "core/channel_mapper.hpp"
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

using namespace are;
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
int main() {
    try {
        ChannelMapper mapper;
        SourceFormat source{SampleKind::float64, 48000, 2, 64, 64, 3};
        mapper.configure(source, 2);
        require(!mapper.downmix() && !mapper.duplicate_mono() && mapper.processing_format() == source, "Stereo bypass changed format");
        mapper.configure(source, 1);
        const std::vector<double> stereo{0.8, -0.4, 0.0, 0.0};
        auto mixed = mapper.mix(std::as_bytes(std::span(stereo)));
        require(std::abs(mixed[0] - 0.2) < 1e-15 && mixed[1] == 0 && mapper.output_mask() == 4, "Stereo to mono is not an average");
        source.channels = 1; source.channel_mask = 4;
        mapper.configure(source, 8);
        require(mapper.duplicate_mono() && mapper.output_channels() == 2 && mapper.source_channel(0) == 0 && mapper.source_channel(1) == 0 && mapper.processing_format() == source, "Mono duplication lost the exact sample path");
        mapper.configure(source, 1);
        require(!mapper.duplicate_mono() && mapper.output_channels() == 1, "Single output mono rejected");
        constexpr double center = 0.7071067811865475244;
        for (const auto mask : {0x3fu, 0x60fu}) {
            source.channels = 6; source.channel_mask = mask;
            mapper.configure(source, 2);
            const double scale = 1 / (1 + center + 0.5 + center);
            for (std::size_t c = 0; c < 6; ++c) {
                std::vector<double> input(6); input[c] = 1;
                mixed = mapper.mix(std::as_bytes(std::span(input)));
                const double expected_left[] {scale, 0, center * scale, 0.5 * scale, center * scale, 0};
                const double expected_right[] {0, scale, center * scale, 0.5 * scale, 0, center * scale};
                require(std::abs(mixed[0] - expected_left[c]) < 1e-15 && std::abs(mixed[1] - expected_right[c]) < 1e-15, "5.1 dialogue, LFE or surround routed incorrectly");
            }
        }
        for (const auto layout : {std::pair{3, 0x7u}, {4, 0x33u}, {5, 0x37u}, {6, 0x60fu}, {7, 0x70fu}, {8, 0x63fu}, {8, 0xffu}, {12, 0x2d63fu}, {32, 0u}}) {
            source.channels = std::uint16_t(layout.first); source.channel_mask = layout.second;
            for (const auto available : {1, 2, 4, 32}) {
                mapper.configure(source, std::uint16_t(available));
                if (!mapper.downmix()) {
                    require(mapper.output_channels() == source.channels && mapper.processing_format() == source, "Native surround was changed");
                    continue;
                }
                std::vector<double> input(source.channels * 2, 1);
                for (std::size_t c = source.channels; c < input.size(); ++c) input[c] = -1;
                mixed = mapper.mix(std::as_bytes(std::span(input)));
                for (double sample : mixed) require(std::abs(sample) <= 1.000000000000001, "Coherent full-scale downmix clips");
                for (std::size_t c = 0; c < source.channels; ++c) {
                    std::fill(input.begin(), input.end(), 0); input[c] = 1;
                    mixed = mapper.mix(std::as_bytes(std::span(input)));
                    double energy = 0;
                    for (double sample : mixed) energy += sample * sample;
                    require(energy > 0, "Downmix dropped an input channel");
                }
            }
        }
        source = {SampleKind::float64, 48000, 6, 64, 64, 0x60f}; mapper.configure(source, 2);
        std::vector<double> silent(6 * 7); silent[0] = std::numeric_limits<double>::quiet_NaN(); silent[1] = std::numeric_limits<double>::infinity();
        for (double sample : mapper.mix(std::as_bytes(std::span(silent)))) require(sample == 0, "Nonfinite input contaminated silence");
        bool rejected = false;
        try { mapper.configure(source, 0); } catch (const std::invalid_argument&) { rejected = true; }
        require(rejected, "Missing device outputs accepted");
        std::cout << "Mono duplication, stereo averaging, native surround, speaker masks, dialogue/LFE retention, headroom and silence passed.\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
