#include "core/resampler.hpp"
#include "core/signal_processor.hpp"
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>
#include <iostream>
#include <numbers>
#include <stdexcept>
#include <vector>

using namespace are;
void require(bool value,const char* detail) { if (!value) throw std::runtime_error(detail); }
std::vector<double> convert(std::uint32_t input_rate,std::uint32_t output_rate,const std::vector<double>& input,std::size_t block,SrcAlgorithm algorithm) {
    SampleRateConverter converter;
    converter.configure({SampleKind::float64,input_rate,2,64,64,0},output_rate,algorithm);
    std::vector<double> result;
    for (std::size_t offset=0; offset<input.size()/2;) {
        const auto frames=std::min({block,converter.block_frames(),input.size()/2-offset});
        const auto converted=converter.process(std::as_bytes(std::span(input).subspan(offset*2,frames*2)));
        result.insert(result.end(),converted.begin(),converted.end()); offset+=frames;
    }
    do { const auto tail=converter.drain(); result.insert(result.end(),tail.begin(),tail.end()); } while (!converter.done());
    require(result.size()/2==converter.target_frames(),"resampler lost or added duration at EOS");
    return result;
}
int main() {
    try {
        for (const auto algorithm : {SrcAlgorithm::r8brain,SrcAlgorithm::sinc}) {
        std::cout<<(algorithm==SrcAlgorithm::sinc ? "Sinc (SoX)" : "r8brain")<<'\n';
        for (const auto rates : {std::pair{44100u,48000u},std::pair{48000u,44100u},std::pair{96000u,44100u},std::pair{96000u,48000u},std::pair{48000u,47999u}}) {
            const auto [input_rate,output_rate]=rates;
            // The narrowest filters need a long settled region, especially
            // near Nyquist; exclude one second of both endpoint transients.
            std::vector<double> source(std::size_t(input_rate)*2*4);
            for (std::size_t i=0; i<std::size_t(input_rate)*4; ++i) {
                source[2*i]=0.25*std::sin(2*std::numbers::pi*1000*double(i)/input_rate);
                source[2*i+1]=0.25*std::sin(2*std::numbers::pi*20000*double(i)/input_rate);
            }
            const auto output=convert(input_rate,output_rate,source,2048,algorithm);
            const auto fragmented=convert(input_rate,output_rate,source,137,algorithm);
            require(output.size()==std::size_t(output_rate)*2*4,"rate conversion changed track duration");
            double error[2]{},chunk_error{};
            for (std::size_t i=output_rate; i<std::size_t(output_rate)*3; ++i) {
                for (std::size_t c=0; c<2; ++c) {
                    const auto expected=0.25*std::sin(2*std::numbers::pi*(c?20000:1000)*double(i)/output_rate);
                    error[c]=std::max(error[c],std::abs(output[2*i+c]-expected));
                }
            }
            for (std::size_t i=0; i<output.size(); ++i) chunk_error=std::max(chunk_error,std::abs(output[i]-fragmented[i]));
            std::cout<<input_rate<<" -> "<<output_rate<<": 1 kHz peak error "<<error[0]<<", 20 kHz "<<error[1]<<", chunk error "<<chunk_error<<'\n';
            require(error[0]<2e-9 && error[1]<2e-8,"SRC altered the passband amplitude, phase or channel routing");
            // Irrational-ratio phase accumulators can differ by a few Float64
            // ulps across long packetizations; bound this below -200 dBFS.
            require(chunk_error<1e-10,"SRC depends on input packet boundaries");
        }
        // A tone above the new Nyquist must disappear instead of aliasing.
        std::vector<double> rejected(48000*2);
        for (std::size_t i=0;i<48000;++i) rejected[2*i]=rejected[2*i+1]=0.5*std::sin(2*std::numbers::pi*23000*double(i)/48000);
        const auto down=convert(48000,44100,rejected,2048,algorithm);
        double alias{};
        for (std::size_t i=8000;i<44100-8000;++i) alias=std::max(alias,std::abs(down[2*i]));
        std::cout<<"23 kHz downsampled alias peak: "<<alias<<'\n';
        require(alias<2e-8,"SRC allowed an audible downsampling alias");
        for (const auto rates : {std::pair{44100u,48000u},std::pair{48000u,44100u},std::pair{8000u,768000u},std::pair{768000u,8000u}})
        for (const std::size_t frames : {0u,1u,7u,211u,2051u}) {
            std::vector<double> source(frames*2,0.25);
            const auto output=convert(rates.first,rates.second,source,37,algorithm);
            require(output.size()/2==(frames*std::uint64_t(rates.second)+rates.first/2)/rates.first,"short file drain duration wrong");
        }
        SampleRateConverter same;
        same.configure({SampleKind::integer,48000,2,24,24,0},48000,algorithm);
        require(!same.active(),"matching rates were unnecessarily resampled");
        SampleRateConverter reset;
        reset.configure({SampleKind::float64,44100,2,64,64,0},48000,algorithm);
        std::vector<double> dirty(2048*2,0.5),zero(2048*2);
        reset.process(std::as_bytes(std::span(dirty))); reset.clear();
        auto after=reset.process(std::as_bytes(std::span(zero)));
        for (auto sample:after) require(sample==0,"seek retained previous filter history");
        do { after=reset.drain(); for (auto sample:after) require(sample==0,"seek leaked filter tail"); } while (!reset.done());
        // Reset after EOS as well as mid-stream; soxr's EOS flag must clear.
        reset.clear(); after=reset.process(std::as_bytes(std::span(zero)));
        do { after=reset.drain(); for (auto sample:after) require(sample==0,"EOS reset leaked filter history"); } while (!reset.done());
        }
        SampleRateConverter invalid;
        bool rejected_algorithm=false;
        try { invalid.configure({SampleKind::integer,48000,2,24,24,0},48000,SrcAlgorithm(42)); }
        catch (const std::invalid_argument&) { rejected_algorithm=true; }
        require(rejected_algorithm,"invalid SRC was accepted even in bypass");
        // Selected PCM precision is applied AFTER volume, once, in every ASIO
        // packing. Wider integer containers must have zero low padding bits.
        std::vector<double> input(8192,0.123456789);
        for (const auto type : {0,1,2,3,4,8,9,10,11,16,17,18,19,20,24,25,26,27}) {
            const auto format=*asio_format(type);
            const auto capacity=format.kind==SampleKind::integer?format.valid_bits:format.kind==SampleKind::float32?24u:32u;
            for (const std::uint16_t bits : {16,24,32}) {
                if (bits>capacity) continue;
                std::vector<std::byte> data(input.size()*format.sample_bytes());
                PlanarBuffer planar{format,data,input}; SignalProcessor processor; processor.configure(48000);
                processor.process({&planar,1},input.size(),0,input.size(),-2000,false,bits,true);
                const auto scale=double(std::uint64_t{1}<<(bits-1)); double sum{};
                for (std::size_t i=0; i<input.size(); ++i) {
                    std::uint64_t raw{};
                    for (std::size_t b=0;b<format.sample_bytes();++b) raw|=std::uint64_t(std::to_integer<unsigned>(data[i*format.sample_bytes()+(format.big_endian?format.sample_bytes()-1-b:b)]))<<(8*b);
                    double value{};
                    if (format.kind==SampleKind::float32) value=std::bit_cast<float>(std::uint32_t(raw));
                    else if (format.kind==SampleKind::float64) value=std::bit_cast<double>(raw);
                    else {
                        const auto native=format.right_justified?format.valid_bits:format.container_bits;
                        const auto sign=std::uint64_t{1}<<(format.container_bits-1);
                        const auto number=std::bit_cast<std::int64_t>((raw^sign)-sign);
                        require((raw&((std::uint64_t{1}<<(native-bits))-1))==0,"selected precision left nonzero low container bits");
                        value=double(number)/double(std::uint64_t{1}<<(native-1));
                    }
                    require(value*scale==std::round(value*scale),"output is not on the selected PCM precision grid");
                    require(std::abs(value-0.0123456789)<2/scale,"PCM quantization exceeded TPDF bounds"); sum+=value;
                }
                require(std::abs(sum/input.size()-0.0123456789)<0.05/scale,"PCM dither introduced a DC bias");
                processor.process({&planar,1},input.size(),0,input.size(),-10000,false,bits,true);
                for (auto b:data) require(b==std::byte{},"dither made mute nonzero");
            }
        }
        std::cout<<"SRC passband, alias rejection, duration, fragmentation, history reset and PCM precision passed.\n";
    } catch (const std::exception& error) { std::cerr<<error.what()<<'\n'; return 1; }
}
