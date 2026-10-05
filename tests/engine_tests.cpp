#include "fake_asio.hpp"
#include "win/engine.hpp"
#include "win/media.hpp"
#include <cstring>
#include <future>
#include <iostream>
#include <stdexcept>

using namespace are;
using namespace are::win;
using namespace are::test;
void require(bool ok, const char* detail) { if (!ok) throw std::runtime_error(detail); }
struct Fixture {
    std::shared_ptr<ClockTimeline> clock = std::make_shared<ClockTimeline>();
    ComPtr<FakeASIO> driver;
    std::unique_ptr<Engine> engine;
    Settings settings{fake_clsid, 0, 0, FALSE};
    SourceFormat source{SampleKind::integer, 48000, 2, 16, 16, 0};
    explicit Fixture(FakeConfig config = {}) {
        driver.Attach(new FakeASIO(config));
        engine = std::make_unique<Engine>(clock, [this](REFCLSID, IASIO** out) { *out = driver.Get(); driver->AddRef(); return S_OK; });
        engine->set_smoothing(false); // Exact transport checks use the strict path.
    }
};
void channel_contracts() {
    for (const auto count : {1, 6, 8}) {
        Fixture native;
        native.source.channels = std::uint16_t(count);
        native.source.channel_mask = count == 1 ? 4u : count == 6 ? 0x60fu : 0x63fu;
        require(native.engine->open(native.source, native.settings) == S_OK, "Native channel layout rejected");
        const auto route = native.engine->channel_status();
        const auto outputs = count == 1 ? 2u : UINT(count);
        require(route.input_channels == UINT(count) && route.output_channels == outputs && !route.downmix, "Native channel status wrong");
        std::vector<std::int16_t> samples(std::size_t(count) * 128);
        for (std::size_t i = 0; i < 128; ++i) for (int c = 0; c < count; ++c) samples[i * count + c] = std::int16_t(1000 * (c + 1) - int(i));
        require(native.engine->submit(std::as_bytes(std::span(samples)), 0) == S_OK && native.engine->end_of_stream() == S_OK, "Native channel submit failed");
        require(native.engine->start(native.clock->now(), native.clock->now()) == S_OK, "Native channel start failed");
        for (std::size_t half = 0; half < 2; ++half) {
            native.driver->pump();
            for (std::size_t c = 0; c < outputs; ++c) for (std::size_t i = 0; i < 64; ++i) {
                std::int32_t sample{}; std::memcpy(&sample, native.driver->last_output(c).data() + i * 4, 4);
                const auto input_channel = count == 1 ? 0 : c;
                require(std::int64_t(sample) == std::int64_t(samples[(half * 64 + i) * count + input_channel]) * 65536, "Native surround or mono duplication changed samples");
            }
        }
        native.engine->close();
        require(native.engine->channel_status().output_channels == 0, "Closed device retained channel status");
    }
    for (const auto outputs : {1, 2}) {
        FakeConfig config; config.output_channels = outputs;
        Fixture mixed(config); mixed.source = {SampleKind::float64, 48000, 6, 64, 64, 0x60f};
        require(mixed.engine->open(mixed.source, mixed.settings) == S_OK, "Surround fallback rejected stereo/mono device");
        const auto route = mixed.engine->channel_status();
        require(route.downmix && route.input_channels == 6 && route.output_channels == UINT(outputs), "Downmix status wrong");
        std::vector<double> samples(128 * 6);
        for (std::size_t i = 0; i < 128; ++i) samples[i * 6 + 2] = 0.4; // Dialogue only.
        require(mixed.engine->submit(std::as_bytes(std::span(samples)), 0) == S_OK && mixed.engine->end_of_stream() == S_OK, "Downmix submit failed");
        require(mixed.engine->start(mixed.clock->now(), mixed.clock->now()) == S_OK, "Downmix start failed");
        const auto center = 0.7071067811865475244;
        const auto expected = 0.4 * center / (1 + 2 * center + 0.5);
        for (int half = 0; half < 2; ++half) {
            mixed.driver->pump();
            for (int c = 0; c < outputs; ++c) for (std::size_t i = 0; i < 64; ++i) {
                std::int32_t sample{}; std::memcpy(&sample, mixed.driver->last_output(std::size_t(c)).data() + i * 4, 4);
                require(std::abs(double(sample) / 2147483648.0 - expected) < 2e-9, "Downmix lost dialogue or changed gain");
            }
        }
        mixed.engine->close();
    }
    for (const auto algorithm : {SrcAlgorithm::r8brain, SrcAlgorithm::sinc}) {
        FakeConfig config; config.output_channels = 2;
        Fixture mixed(config); mixed.source = {SampleKind::integer, 44100, 8, 16, 16, 0x63f}; mixed.settings.keep_device_rate = TRUE;
        for (const auto speed : {1.0, 2.0}) {
            require(mixed.engine->open(mixed.source, mixed.settings, {}, {algorithm}, speed) == S_OK, "7.1 downmix with SRC/speed rejected");
            std::vector<std::int16_t> signal(4096 * 8, 6000);
            require(mixed.engine->submit(std::as_bytes(std::span(signal)), 0) == S_OK && mixed.engine->end_of_stream() == S_OK, "7.1 SRC submit/drain failed");
            require(mixed.engine->start(mixed.clock->now(), mixed.clock->now()) == S_OK, "7.1 SRC start failed");
            const auto frames = std::uint64_t(std::llround(4096.0 * 48000 / (44100 * speed)));
            double peak = 0;
            for (std::uint64_t i = 0; i < (frames + 63) / 64 + 4; ++i) {
                mixed.driver->pump();
                for (std::size_t c = 0; c < 2; ++c) for (std::size_t s = 0; s < 64; ++s) {
                    std::int32_t sample{}; std::memcpy(&sample, mixed.driver->last_output(c).data() + s * 4, 4);
                    peak = std::max(peak, std::abs(double(sample) / 2147483648.0));
                }
            }
            require(peak > 0.15 && peak < 0.3 && mixed.engine->status().delivered_frames == frames && mixed.engine->drained(), "7.1 SRC/speed lost level, duration or EOS");
            mixed.engine->abort(); mixed.engine->reset(); std::fill(signal.begin(), signal.end(), 0);
            require(mixed.engine->submit(std::as_bytes(std::span(signal)), 0) == S_OK && mixed.engine->end_of_stream() == S_OK, "Mixed seek silence failed");
            require(mixed.engine->start(mixed.clock->now(), mixed.clock->now()) == S_OK, "Mixed seek resume failed");
            for (std::uint64_t i = 0; i < (frames + 63) / 64 + 4; ++i) {
                mixed.driver->pump();
                for (std::size_t c = 0; c < 2; ++c) for (auto byte : mixed.driver->last_output(c)) require(byte == std::byte{}, "Mixed seek replayed previous audio");
            }
            require(mixed.driver->rate_changes == 0, "Downmix/SRC changed device clock");
            mixed.engine->close();
        }
    }
}
int main() {
    try {
        Fixture f;
        require(f.engine->open(f.source, f.settings) == S_OK, "engine open failed");
        std::vector<std::int16_t> input(128 * 2);
        for (int i = 0; i < 128; ++i) { input[2*i] = std::int16_t(i - 64); input[2*i+1] = std::int16_t(12000 - i); }
        require(f.engine->submit(std::as_bytes(std::span(input)), 0) == S_OK, "submit failed");
        f.engine->end_of_stream();
        require(f.engine->start(f.clock->now(), f.clock->now()) == S_OK, "ASIO start failed");
        for (int half = 0; half < 2; ++half) {
            f.driver->pump();
            for (int c = 0; c < 2; ++c) for (int i = 0; i < 64; ++i) {
                std::int32_t value{}; std::memcpy(&value, f.driver->last_output(std::size_t(c)).data() + i*4, 4);
                require(std::int64_t(value) == std::int64_t(input[2 * (half*64+i) + c]) * 65536, "callback output lost or changed sample");
            }
        }
        require(!f.engine->drained(), "EOS signalled before device latency drained");
        f.driver->pump(); f.driver->pump();
        require(f.engine->drained(), "EOS did not drain");
        auto status = f.engine->status(); require(status.delivered_frames == 128 && status.underruns == 0, "callback accounting failed");
        f.engine->pause();
        require(f.engine->start(f.clock->now(),f.clock->now()) == S_OK, "resume failed");
        require(f.driver->stale_start_buffers == 0, "resume replayed old device buffers");
        f.engine->pause(); f.engine->reset(); f.engine->set_mute(true);
        require(f.engine->submit(std::as_bytes(std::span(input)).first(64 * 4), 0) == S_OK, "mute submit failed");
        require(f.engine->start(f.clock->now(), f.clock->now()) == S_OK, "mute start failed");
        f.driver->pump();
        for (auto b : f.driver->last_output(0)) require(b == std::byte{}, "muted output was not silent");
        f.engine->abort(); f.engine->reset(); require(f.engine->set_rate(2.0)==S_OK,"manual speed setup failed");
        require(f.engine->submit(std::as_bytes(std::span(input)),0)==S_OK && f.engine->end_of_stream()==S_OK,"manual tempo data failed");
        require(f.engine->start(f.clock->now(),f.clock->now())==S_OK,"manual tempo start failed"); f.driver->pump();
        f.engine->abort(); f.engine->reset(); require(f.engine->set_rate(1.0)==S_OK,"exact speed restoration failed"); f.engine->set_volume(0);
        require(f.engine->submit(std::as_bytes(std::span(input)),0)==S_OK && f.engine->end_of_stream()==S_OK,"exact restored data failed");
        require(f.engine->start(f.clock->now(),f.clock->now())==S_OK,"exact restored start failed");
        for (int half=0;half<2;++half) {
            f.driver->pump();
            for (int c=0;c<2;++c) for (int i=0;i<64;++i) {
                std::int32_t value{}; std::memcpy(&value,f.driver->last_output(std::size_t(c)).data()+i*4,4);
                require(std::int64_t(value)==std::int64_t(input[2*(half*64+i)+c])*65536,"returning to 1x retained tempo processing or dither");
            }
        }
        f.engine->close();
        require(f.driver->wrong_thread_calls == 0 && f.driver->panel_calls == 0, "driver apartment/control-panel contract broken");
        channel_contracts();
        {
            Fixture seek; seek.engine->set_smoothing(true);
            require(seek.engine->open(seek.source,seek.settings)==S_OK,"seek lifecycle setup failed");
            std::vector<std::int16_t> data(512*2,16000);
            require(seek.engine->submit(std::as_bytes(std::span(data)),0)==S_OK,"seek submit failed");
            require(seek.engine->start(seek.clock->now(),seek.clock->now())==S_OK,"seek initial start failed");
            for(int i=0;i<4;++i) seek.driver->pump();
            seek.engine->pause(); seek.driver->pump();
            require(seek.driver->starts==1 && seek.driver->stops==0,"pause restarted the hardware stream");
            std::int32_t previous{}; std::memcpy(&previous,seek.driver->last_output(0).data()+63*4,4);
            seek.engine->abort(); seek.engine->reset();
            std::fill(data.begin(),data.end(),-16000);
            require(seek.engine->submit(std::as_bytes(std::span(data)),0)==S_OK,"seek new segment submit failed");
            require(seek.engine->start(seek.clock->now(),seek.clock->now())==S_OK,"seek resume failed");
            seek.driver->pump();
            std::int32_t first{}; std::memcpy(&first,seek.driver->last_output(0).data(),4);
            require(std::abs(std::int64_t(first)-previous)<=1,"seek boundary jumped instead of fading");
            for(int i=0;i<12;++i) {
                seek.engine->abort(); seek.engine->reset();
                require(seek.engine->submit(std::as_bytes(std::span(data)),0)==S_OK,"repeated seek submit failed");
                require(seek.engine->start(seek.clock->now(),seek.clock->now())==S_OK,"repeated seek restart failed");
                seek.driver->pump();
            }
            require(seek.driver->starts==1 && seek.driver->stops==0 && seek.driver->disposals==0,"seeking reset the ASIO driver");
            seek.engine->set_smoothing(false); seek.engine->set_volume(-10000);
            seek.driver->pump();
            for(const auto b:seek.driver->last_output(0)) require(b==std::byte{},"engine mute failed");
            seek.engine->set_volume(-2795); seek.driver->pump();
            std::int32_t quiet{}; std::memcpy(&quiet,seek.driver->last_output(0).data(),4);
            require(quiet<0 && quiet>-16000*65536,"engine remained muted after 20 percent volume");
            seek.engine->set_volume(0); seek.driver->pump();
            std::int32_t unity{}; std::memcpy(&unity,seek.driver->last_output(0).data(),4);
            require(unity==-16000*65536,"engine did not restore exact full-volume output");
            seek.engine->close();
            require(seek.driver->starts==1 && seek.driver->stops==1 && seek.driver->disposals==1,"final close did not release ASIO exactly once");
        }
        {
            FakeConfig config; config.automatic_callbacks=true;
            Fixture concurrent(config); concurrent.engine->set_smoothing(true);
            require(concurrent.engine->open(concurrent.source,concurrent.settings)==S_OK,"concurrent seek setup failed");
            require(concurrent.engine->start(concurrent.clock->now(),concurrent.clock->now())==S_OK,"concurrent initial start failed");
            std::vector<std::byte> data(60000*4);
            for(int i=0;i<12;++i) {
                auto writer=std::async(std::launch::async,[&]{return concurrent.engine->submit(data,0);});
                Sleep(4); concurrent.engine->abort();
                require(writer.wait_for(std::chrono::seconds(1))==std::future_status::ready && writer.get()==S_FALSE,"seek did not unblock active producer");
                concurrent.engine->reset();
                require(concurrent.engine->status().queued_frames==0,"concurrent reset retained stale audio");
                require(concurrent.engine->start(concurrent.clock->now(),concurrent.clock->now())==S_OK,"concurrent seek resume failed");
            }
            require(concurrent.driver->starts==1 && concurrent.driver->stops==0,"concurrent seek restarted hardware");
            concurrent.engine->close();
            require(concurrent.driver->stops==1 && concurrent.driver->wrong_thread_calls==0,"concurrent seek failed driver cleanup");
        }
        {
            Fixture bad; bad.source.valid_bits = bad.source.container_bits = 32;
            // Float32 cannot exactly represent every 32-bit integer PCM sample.
            FakeConfig config; config.type = ASIOSTFloat32LSB;
            Fixture float_driver(config); float_driver.source = bad.source;
            require(float_driver.engine->open(float_driver.source,float_driver.settings)==S_OK,"Auto rejected native Float32 output");
            require(float_driver.engine->processing_status().precision_reduced,"Auto Float32 conversion reported exact samples");
            const std::vector<std::int32_t> data(128*2,123456789);
            require(float_driver.engine->submit(std::as_bytes(std::span(data)),0)==S_OK && float_driver.engine->end_of_stream()==S_OK,"Auto Float32 submit failed");
            require(float_driver.engine->start(float_driver.clock->now(),float_driver.clock->now())==S_OK,"Auto Float32 start failed");
            float_driver.driver->pump();
            float observed{}; std::memcpy(&observed,float_driver.driver->last_output(0).data(),4);
            require(std::abs(double(observed)-123456789.0/2147483648.0)<1e-8,"Auto Float32 changed the signal level");
            float_driver.engine->close();
        }
        {
            // Opus/AAC/MP3/Vorbis decode to Float32 in LAV. Native integer
            // output must work with default Auto, including a return to 1x.
            Fixture decoded; decoded.source.kind=SampleKind::float32;
            decoded.source.container_bits=decoded.source.valid_bits=32;
            require(decoded.engine->open(decoded.source,decoded.settings)==S_OK,"Auto rejected floating-point decoder output");
            require(decoded.engine->processing_status().precision_reduced && !decoded.engine->processing_status().resampling,"Auto float conversion status is wrong");
            std::vector<float> data(128*2);
            for (std::size_t i=0;i<128;++i) { data[2*i]=0.123456f; data[2*i+1]=-0.234567f; }
            for (int pass=0;pass<2;++pass) {
                if (pass) {
                    decoded.engine->abort(); decoded.engine->reset();
                    require(decoded.engine->set_rate(2.0)==S_OK,"floating-point speed change failed");
                    require(decoded.engine->set_rate(1.0)==S_OK,"floating-point return to normal speed failed");
                }
                require(decoded.engine->submit(std::as_bytes(std::span(data)),0)==S_OK && decoded.engine->end_of_stream()==S_OK,"Auto float submit failed");
                require(decoded.engine->start(decoded.clock->now(),decoded.clock->now())==S_OK,"Auto float start failed");
                for (int half=0;half<2;++half) {
                    decoded.driver->pump();
                    for (std::size_t c=0;c<2;++c) for (std::size_t i=0;i<64;++i) {
                        std::int32_t observed{}; std::memcpy(&observed,decoded.driver->last_output(c).data()+i*4,4);
                        require(std::abs(double(observed)/2147483648.0-double(data[c]))<=2.0/2147483648.0,"Auto floating-point conversion lost audio, sign or level");
                    }
                }
            }
            decoded.engine->close();
        }
        {
            Fixture keep; keep.source.rate = 44100; keep.settings.keep_device_rate = TRUE;
            require(keep.engine->open(keep.source, keep.settings)==S_OK, "keep-rate SRC failed");
            require(keep.engine->status().sample_rate==48000 && keep.engine->processing_status().resampling,"keep-rate mode changed the device rate or omitted SRC");
            keep.settings.keep_device_rate = FALSE;
            require(keep.engine->open(keep.source, keep.settings) == S_OK, "exact source rate negotiation failed");
            require(keep.engine->status().sample_rate == 44100, "negotiated rate wrong");
        }
        {
            Fixture bad; bad.settings.buffer_frames = 65;
            require(FAILED(bad.engine->open(bad.source, bad.settings)), "invalid buffer size accepted");
            bad.settings.buffer_frames = 0; bad.settings.first_channel = 8;
            require(FAILED(bad.engine->open(bad.source, bad.settings)), "invalid output range accepted");
        }
        {
            Fixture first, second;
            require(first.engine->open(first.source, first.settings) == S_OK, "first callback owner failed");
            require(FAILED(second.engine->open(second.source, second.settings)), "two active callback owners accepted");
            first.engine->close(); require(second.engine->open(second.source, second.settings) == S_OK, "callback owner not released");
        }
        {
            FakeConfig config; config.init_ok = false; Fixture failed(config);
            require(FAILED(failed.engine->open(failed.source, failed.settings)), "failed driver init accepted");
        }
        {
            FakeConfig config; config.start_result = ASE_HWMalfunction; Fixture failed(config);
            require(failed.engine->open(failed.source, failed.settings) == S_OK, "start failure setup failed");
            require(FAILED(failed.engine->start(failed.clock->now(), failed.clock->now())), "driver start failure ignored");
        }
        {
            Fixture aborted; require(aborted.engine->open(aborted.source, aborted.settings) == S_OK, "abort setup failed");
            std::vector<std::byte> large(20000 * 4);
            auto submit = std::async(std::launch::async, [&] { return aborted.engine->submit(large, 0); });
            Sleep(20); aborted.engine->abort();
            require(submit.wait_for(std::chrono::seconds(1)) == std::future_status::ready && submit.get() == S_FALSE, "flush did not unblock back-pressure");
            aborted.engine->reset(); require(aborted.engine->status().queued_frames == 0, "flush retained old audio");
            aborted.driver->notify_rate(44100);
            const auto deadline = GetTickCount64() + 1000;
            while (SUCCEEDED(aborted.engine->error()) && GetTickCount64() < deadline) Sleep(1);
            require(FAILED(aborted.engine->error()), "external rate change not reported");
        }
        for (const auto algorithm : {SrcAlgorithm::r8brain,SrcAlgorithm::sinc}) {
        for (const auto rates : {std::pair{44100u,48000u},std::pair{48000u,44100u}}) {
            FakeConfig config; config.sample_rate=rates.second;
            Fixture converted(config); converted.source.rate=rates.first; converted.settings.keep_device_rate=TRUE;
            require(converted.engine->open(converted.source,converted.settings,{24},{algorithm})==S_OK,"converted engine open failed");
            const auto src=converted.engine->resampling_status();
            require(src.active && src.algorithm==algorithm,"engine did not report the active SRC algorithm");
            require(converted.driver->rate_changes==0,"keeping device rate called setSampleRate");
            std::vector<std::int16_t> signal(4096*2,10000);
            require(converted.engine->submit(std::as_bytes(std::span(signal)),0)==S_OK,"converted submit failed");
            require(converted.engine->end_of_stream()==S_OK,"converted EOS drain failed");
            const auto total=(4096ull*rates.second+rates.first/2)/rates.first;
            require(converted.engine->start(converted.clock->now(),converted.clock->now())==S_OK,"converted start failed");
            for (std::uint64_t i=0;i<(total+63)/64+4;++i) {
                converted.driver->pump();
                for (const auto c : {0u,1u}) for (std::size_t sample=0;sample<64;++sample) {
                    std::uint32_t value{}; std::memcpy(&value,converted.driver->last_output(c).data()+sample*4,4);
                    require((value&255)==0,"24-bit PCM was not padded correctly in a 32-bit ASIO buffer");
                }
            }
            auto status=converted.engine->status();
            require(status.delivered_frames==total && status.sample_rate==rates.second && status.underruns==0 && converted.engine->drained(),"converted duration, output clock or EOS was wrong");
            converted.engine->abort(); converted.engine->reset(); std::fill(signal.begin(),signal.end(),0);
            require(converted.engine->submit(std::as_bytes(std::span(signal)),0)==S_OK && converted.engine->end_of_stream()==S_OK,"converted seek submit failed");
            require(converted.engine->start(converted.clock->now(),converted.clock->now())==S_OK,"converted seek resume failed");
            for (std::uint64_t i=0;i<(total+63)/64+3;++i) {
                converted.driver->pump();
                for (auto b:converted.driver->last_output(0)) require(b==std::byte{},"SRC seek replayed filter history");
            }
            require(converted.driver->starts==1 && converted.driver->stops==0,"converted seek restarted ASIO");
            converted.engine->close();
        }
        {
            FakeConfig config; config.automatic_callbacks=true;
            Fixture converted(config); converted.source.rate=44100; converted.settings.keep_device_rate=TRUE;
            require(converted.engine->open(converted.source,converted.settings,{16},{algorithm})==S_OK,"concurrent SRC open failed");
            require(converted.engine->start(converted.clock->now(),converted.clock->now())==S_OK,"concurrent SRC start failed");
            // Exceed both queue capacity and the steep Sinc filter's startup
            // buffering, so this genuinely exercises blocked producer aborts.
            std::vector<std::byte> signal(480000*4);
            for (int i=0;i<6;++i) {
                auto writer=std::async(std::launch::async,[&]{return converted.engine->submit(signal,0);});
                Sleep(5); converted.engine->abort();
                require(writer.wait_for(std::chrono::seconds(1))==std::future_status::ready && writer.get()==S_FALSE,"SRC producer failed to abort under back-pressure");
                converted.engine->reset(); require(converted.engine->status().queued_frames==0,"SRC reset kept queued samples");
                require(converted.engine->start(converted.clock->now(),converted.clock->now())==S_OK,"concurrent SRC seek failed");
            }
            require(converted.driver->starts==1 && converted.driver->rate_changes==0,"concurrent SRC changed or restarted the device");
            converted.engine->close();
        }
        }
        {
            FakeConfig config; config.type=ASIOSTInt16LSB;
            Fixture narrow(config);
            require(FAILED(narrow.engine->open(narrow.source,narrow.settings,{24})),"unsupported selected precision silently reduced");
            require(narrow.engine->open(narrow.source,narrow.settings,{16})==S_OK,"supported 16-bit PCM rejected");
        }
        // A live segment can change rate without disposing ASIO buffers. This
        // also returns to the original integer queue for exact unity playback.
        {
            FakeConfig config; config.automatic_callbacks=true; Fixture speed(config);
            require(speed.engine->open(speed.source,speed.settings)==S_OK,"live speed setup failed");
            require(speed.engine->start(speed.clock->now(),speed.clock->now())==S_OK,"live speed start failed");
            std::vector<std::int16_t> signal(24000*2,8000);
            for (const auto rate : {0.5,2.0,1.0}) {
                speed.engine->abort(); speed.engine->reset(); require(speed.engine->set_rate(rate)==S_OK,"live segment speed failed");
                require(speed.engine->start(speed.clock->now(),speed.clock->now())==S_OK,"live speed restart failed");
                require(speed.engine->submit(std::as_bytes(std::span(signal)),0)==S_OK && speed.engine->end_of_stream()==S_OK,"live speed data or EOS failed");
                const auto deadline=GetTickCount64()+3000;
                bool drained=false;
                while (!(drained=speed.engine->drained()) && GetTickCount64()<deadline) Sleep(1);
                const auto status=speed.engine->status();
                if (!drained || status.delivered_frames!=std::uint64_t(24000/rate) || status.sample_rate!=48000)
                    std::cerr << "Live rate " << rate << ": " << status.delivered_frames << " frames at " << status.sample_rate << " Hz, drained " << drained << ", queued " << status.queued_frames << ", error " << std::hex << unsigned(status.error) << std::dec << '\n';
                require(drained && status.delivered_frames==std::uint64_t(24000/rate) && status.sample_rate==48000,"live speed duration or clock wrong");
            }
            require(speed.driver->starts==1 && speed.driver->stops==0 && speed.driver->disposals==0 && speed.driver->rate_changes==0,"segment speed restarted or changed ASIO hardware");
            speed.engine->close();
        }
        auto mt = pcm_media_type(2,48000,24);
        require(parse_media_type(mt).has_value(), "valid Wave format rejected");
        mt.cbFormat = 16;
        require(parse_media_type(mt).has_value(), "Legacy 16-byte PCM header rejected");
        mt.cbFormat = 17; require(!parse_media_type(mt), "Truncated cbSize accepted");
        mt.cbFormat = sizeof(WAVEFORMATEX);
        reinterpret_cast<WAVEFORMATEX*>(mt.pbFormat)->nBlockAlign = 5;
        require(!parse_media_type(mt), "malformed block alignment accepted"); free_media_type(mt);
        // DirectShow clock advisories and cancellation use actual Windows handles.
        ComPtr<IReferenceClock> clock; clock.Attach(new ReferenceClock(std::make_shared<ClockTimeline>()));
        Handle event(CreateEventW(nullptr, FALSE, FALSE, nullptr));
        REFERENCE_TIME now{}; clock->GetTime(&now); DWORD_PTR cookie = 0;
        require(clock->AdviseTime(now, 100000, reinterpret_cast<HEVENT>(event.get()), &cookie) == S_OK, "clock advice failed");
        require(WaitForSingleObject(event.get(), 1000) == WAIT_OBJECT_0, "clock event was not delivered");
        clock->GetTime(&now); require(clock->AdviseTime(now, 1000000, reinterpret_cast<HEVENT>(event.get()), &cookie) == S_OK, "cancel advice failed");
        require(clock->Unadvise(cookie) == S_OK && WaitForSingleObject(event.get(), 150) == WAIT_TIMEOUT, "cancelled clock event fired");
        std::cout << "ASIO exact callbacks, Auto decoder conversion, lifecycle, rate/precision validation, flush, EOS, clock and apartment tests passed.\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
