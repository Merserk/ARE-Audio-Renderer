#include "core/format.hpp"
#include "core/frame_queue.hpp"
#include <array>
#include <bit>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <thread>

using namespace are;
void require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
std::span<const std::byte> bytes(const auto& value) { return std::as_bytes(std::span(value)); }
std::span<std::byte> writable(auto& value) { return std::as_writable_bytes(std::span(value)); }
int main() {
    try {
        SourceFormat source{SampleKind::integer, 48000, 2, 16, 16, 0};
        require(source.valid(), "valid PCM rejected");
        require(!asio_format(32) && !asio_format(40) && !asio_format(-1), "DSD/unknown format accepted");
        // All 65,536 signed 16-bit sample values, in a stereo interleaved stream.
        std::vector<std::int16_t> samples(65536 * 2);
        for (int n = -32768; n <= 32767; ++n) {
            const auto i = std::size_t(n + 32768) * 2;
            samples[i] = std::int16_t(n); samples[i + 1] = std::int16_t(-1 - n);
        }
        for (const long type : {0, 1, 2, 8, 9, 10, 11, 16, 17, 18, 24, 25, 26, 27}) {
            const auto output = *asio_format(type);
            require(is_lossless(source, output), "16-bit widening rejected");
            std::vector<std::byte> result(65536 * output.sample_bytes());
            convert_channel(source, output, bytes(samples), 1, result);
            for (int n = -32768; n <= 32767; ++n) {
                const auto offset = std::size_t(n + 32768) * output.sample_bytes();
                std::uint32_t raw = 0;
                for (std::size_t b = 0; b < output.sample_bytes(); ++b) raw |= std::uint32_t(std::to_integer<unsigned>(result[offset + (output.big_endian ? output.sample_bytes() - 1 - b : b)])) << (8 * b);
                const auto value = std::int64_t(-1 - n) * (std::int64_t{1} << ((output.right_justified ? output.valid_bits : output.container_bits) - 16));
                const auto mask = output.container_bits == 32 ? 0xffffffffu : (1u << output.container_bits) - 1;
                require(raw == (std::uint32_t(value) & mask), "PCM widening/endian/channel mismatch");
            }
        }
        // 24-bit integer -> float preserves every bit of the normalized value.
        source = {SampleKind::integer, 44100, 1, 24, 24, 0};
        std::vector<std::byte> pcm24(65536 * 3);
        std::vector<float> floats(65536);
        for (std::size_t i = 0; i < floats.size(); ++i) {
            const auto value = std::int32_t(i * 256) - 8388608;
            pcm24[3 * i] = std::byte(value & 255); pcm24[3 * i + 1] = std::byte((value >> 8) & 255); pcm24[3 * i + 2] = std::byte((value >> 16) & 255);
        }
        convert_channel(source, *asio_format(19), pcm24, 0, writable(floats));
        for (std::size_t i = 0; i < floats.size(); ++i) require(double(floats[i]) * 8388608.0 == double(std::int32_t(i * 256) - 8388608), "24-bit float conversion lost precision");
        require(!is_lossless(source, *asio_format(16)), "24-to-16 truncation allowed");
        source = {SampleKind::integer, 48000, 1, 32, 32, 0};
        require(!is_lossless(source, *asio_format(19)), "32-bit PCM converted lossily to Float32");
        std::array<std::int32_t, 5> boundary{INT32_MIN, -1, 0, 1, INT32_MAX};
        std::array<double, 5> doubles{};
        convert_channel(source, *asio_format(20), bytes(boundary), 0, writable(doubles));
        for (std::size_t i = 0; i < boundary.size(); ++i) require(doubles[i] * 2147483648.0 == boundary[i], "32-bit PCM -> Float64 not exact");
        source = {SampleKind::integer, 48000, 1, 32, 20, 0};
        std::array<std::int32_t, 4> padded{INT32_MIN, -4096, 0, 0x7ffff000};
        std::array<std::int32_t, 4> packed{};
        convert_channel(source, *asio_format(26), bytes(padded), 0, writable(packed));
        require(packed == std::array<std::int32_t,4>{-524288,-1,0,524287}, "left-aligned Wave PCM -> right-justified ASIO failed");
        source = {SampleKind::integer, 48000, 1, 8, 8, 0};
        std::array<unsigned char, 3> pcm8{0,128,255}; std::array<std::int16_t, 3> signed16{};
        convert_channel(source, *asio_format(16), bytes(pcm8), 0, writable(signed16));
        require(signed16 == std::array<std::int16_t,3>{-32768,0,32512}, "unsigned PCM8 handling failed");
        // Direct floating transport must preserve NaN payloads and signed zero.
        source = {SampleKind::float32, 48000, 1, 32, 32, 0};
        std::array<std::uint32_t, 6> payloads{0,0x80000000,0x7fc12345,0xff800000,0x3f7fffff,0x00000001};
        std::array<std::uint32_t, 6> copy{};
        convert_channel(source, *asio_format(19), bytes(payloads), 0, writable(copy)); require(copy == payloads, "Float32 payload changed");
        convert_channel(source, *asio_format(3), bytes(payloads), 0, writable(copy));
        for (std::size_t i = 0; i < copy.size(); ++i) require(copy[i] == std::byteswap(payloads[i]), "Float32 endian conversion changed payload");
        source.kind = SampleKind::float64; source.container_bits = source.valid_bits = 64;
        require(!is_lossless(source, *asio_format(19)), "Float64 narrowing accepted");
        // Concurrent producer/consumer, including wraps at a non-power-of-two
        // capacity. Check every frame's sequence and payload, with real threads.
        FrameQueue queue; queue.configure(127, sizeof(std::uint32_t));
        constexpr std::uint32_t count = 1000000;
        std::thread producer([&] {
            std::uint32_t n = 0;
            while (n < count) { std::array<std::uint32_t, 37> chunk{}; const auto size = std::min<std::uint32_t>(37, count - n); for (std::uint32_t i = 0; i < size; ++i) chunk[i] = n + i; n += std::uint32_t(queue.push(bytes(chunk).first(size * 4))); std::this_thread::yield(); }
        });
        bool ordered = true; std::uint32_t next = 0;
        while (next < count) {
            const auto chunk = queue.front(43);
            if (chunk.empty()) { std::this_thread::yield(); continue; }
            for (std::size_t i = 0; i < chunk.size() / 4; ++i) { std::uint32_t value{}; std::memcpy(&value, chunk.data() + 4 * i, 4); ordered = ordered && value == next++; }
            queue.consume(chunk.size() / 4);
        }
        producer.join(); require(ordered && queue.size() == 0, "Concurrent frame queue reordered or lost samples");
        std::cout << "Lossless PCM/float conversion and 1,000,000-frame concurrent queue passed.\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
