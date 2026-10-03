#include "core/signal_processor.hpp"
#include <bit>
#include <cmath>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <vector>

using namespace are;
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
std::uint64_t raw(const std::byte* data, const OutputFormat& format) {
    std::uint64_t result{};
    for (std::size_t b=0; b<format.sample_bytes(); ++b)
        result |= std::uint64_t(std::to_integer<unsigned>(data[format.big_endian ? format.sample_bytes()-1-b : b])) << (8*b);
    return result;
}
int main() {
    try {
        // Gain must work on all supported ASIO endian/depth/packing layouts.
        SourceFormat source{SampleKind::integer,48000,1,16,16,0};
        std::vector<std::int16_t> input(1024,16384);
        for (const auto type : {0,1,2,3,4,8,9,10,11,16,17,18,19,20,24,25,26,27}) {
            const auto format=*asio_format(type);
            std::vector<std::byte> data(input.size()*format.sample_bytes());
            convert_channel(source,format,std::as_bytes(std::span(input)),0,data);
            const auto original=data;
            PlanarBuffer buffer{format,data}; SignalProcessor processor; processor.configure(48000);
            processor.process({&buffer,1},input.size(),0,input.size(),0,false);
            require(data==original,"unity processing changed encoded samples");
            processor.process({&buffer,1},input.size(),0,input.size(),-2000,false);
            const auto bits=raw(data.data(),format);
            double sample{};
            if (format.kind==SampleKind::float32) sample=std::bit_cast<float>(std::uint32_t(bits));
            else if (format.kind==SampleKind::float64) sample=std::bit_cast<double>(bits);
            else {
                const auto sign=std::uint64_t{1}<<(format.container_bits-1);
                const auto integer=std::bit_cast<std::int64_t>((bits^sign)-sign);
                const auto depth=format.right_justified?format.valid_bits:format.container_bits;
                sample=double(integer)/double(std::uint64_t{1}<<(depth-1));
            }
            require(std::abs(sample-0.05)<0.00004,"volume used the wrong ASIO encoding or decibel scale");
            processor.process({&buffer,1},input.size(),0,input.size(),-10000,false);
            for (const auto b:data) require(b==std::byte{},"mute did not output exact zero");
            data=original; buffer.samples=data;
            processor.process({&buffer,1},input.size(),0,input.size(),-2795,false);
            require(raw(data.data(),format)!=0,"20 percent volume remained muted");
            data=original; buffer.samples=data;
            processor.process({&buffer,1},input.size(),0,input.size(),0,false);
            require(data==original,"restoring full volume did not restore exact samples");
        }
        // Smoothing must be continuous across callbacks and a seek to an
        // opposite-polarity sample; it must return to the exact unity path.
        const auto format=*asio_format(20);
        std::vector<double> samples(480,0.5);
        PlanarBuffer buffer{format,std::as_writable_bytes(std::span(samples))};
        SignalProcessor smooth; smooth.configure(48000);
        smooth.process({&buffer,1},samples.size(),0,samples.size(),0,true);
        require(samples.front()==0 && samples[240]==0.5 && samples.back()==0.5,"start ramp duration or unity bypass wrong");
        for (std::size_t i=1;i<samples.size();++i) require(std::abs(samples[i]-samples[i-1])<0.004,"start ramp jumped");
        smooth.restart(); std::fill(samples.begin(),samples.end(),-0.5);
        smooth.process({&buffer,1},samples.size(),0,samples.size(),0,true);
        require(samples.front()==0.5 && samples[240]==-0.5 && samples.back()==-0.5,"seek crossfade endpoints wrong");
        for (std::size_t i=1;i<samples.size();++i) require(std::abs(samples[i]-samples[i-1])<0.007,"seek produced an abrupt sample jump");
        std::fill(samples.begin(),samples.end(),0);
        smooth.process({&buffer,1},samples.size(),0,0,0,true);
        require(samples.front()==-0.5 && samples[240]==0 && samples.back()==0,"stop tail did not fade to silence");
        std::fill(samples.begin(),samples.end(),0.5);
        smooth.process({&buffer,1},samples.size(),0,samples.size(),0,true);
        std::fill(samples.begin(),samples.end(),0.5);
        smooth.process({&buffer,1},samples.size(),0,samples.size(),-10000,true);
        require(samples.front()>0.49 && samples[240]==0 && samples.back()==0,"mute gain ramp wrong");
        std::fill(samples.begin(),samples.end(),0.5);
        smooth.process({&buffer,1},samples.size(),0,samples.size(),-2795,true);
        require(samples.back()>0 && samples.back()<0.025,"unmute ramp failed or exceeded requested gain");
        std::fill(samples.begin(),samples.end(),0.5);
        smooth.process({&buffer,1},samples.size(),0,samples.size(),0,true);
        require(samples.back()==0.5 && !smooth.transitioning(),"gain ramp failed to finish at unity");
        // Preserve special floating-point payloads at unity in the strict path.
        std::vector<std::uint64_t> payloads{0x8000000000000000ull,0x7ff8123456789abcull,0x7ff0000000000000ull};
        const auto original=payloads;
        buffer.samples=std::as_writable_bytes(std::span(payloads));
        smooth.process({&buffer,1},payloads.size(),0,payloads.size(),0,false);
        require(payloads==original,"unity bypass changed floating-point payloads");
        std::cout<<"All ASIO gain encodings, mute/unmute, unity bypass and 5 ms transition continuity passed.\n";
    } catch (const std::exception& error) { std::cerr<<error.what()<<'\n'; return 1; }
}
