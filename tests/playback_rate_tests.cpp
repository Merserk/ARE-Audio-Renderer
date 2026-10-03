#include "core/resampler.hpp"
#include <algorithm>
#include <iostream>
#include <limits>
#include <numbers>
#include <stdexcept>
#include <vector>

using namespace are;
void require(bool okay,const char* message) { if (!okay) throw std::runtime_error(message); }
std::vector<double> render(SampleRateConverter& converter,std::span<const double> input,std::size_t channels,std::size_t packet=2048) {
    std::vector<double> output;
    while (!input.empty()) {
        const auto count=std::min({input.size()/channels,packet,converter.block_frames()})*channels;
        const auto block=converter.process(std::as_bytes(input.first(count)));
        output.insert(output.end(),block.begin(),block.end()); input=input.subspan(count);
    }
    do { const auto block=converter.drain(); output.insert(output.end(),block.begin(),block.end()); } while (!converter.done());
    require(output.size()/channels==converter.target_frames(),"speed converter lost its EOS duration");
    return output;
}
int main() {
    try {
        for (const auto algorithm:{SrcAlgorithm::r8brain,SrcAlgorithm::sinc}) {
            std::cout<<(algorithm==SrcAlgorithm::sinc ? "Sinc" : "r8brain")<<'\n';
            const SourceFormat source{SampleKind::float64,48000,2,64,64,0};
            SampleRateConverter converter;
            converter.configure(source,48000,algorithm,1.0);
            require(!converter.active(),"normal speed must restore exact bypass");
            converter.configure(source,96000,algorithm,2.0);
            require(!converter.active(),"effective matching rates need no sample conversion");
            for (const double speed:{0.0,-1.0,0.049,128.001,std::numeric_limits<double>::infinity(),std::numeric_limits<double>::quiet_NaN()}) {
                bool rejected=false;
                try { converter.configure(source,48000,algorithm,speed); } catch (const std::invalid_argument&) { rejected=true; }
                require(rejected,"invalid playback speed accepted");
            }
            for (const auto rates:{std::pair{48000u,48000u},std::pair{44100u,48000u},std::pair{48000u,44100u}}) {
                const SourceFormat format{SampleKind::float64,rates.first,2,64,64,0};
                std::vector<double> signal(std::size_t(format.rate)*4*2);
                for (std::size_t i=0;i<signal.size()/2;++i) {
                    signal[2*i]=0.25*std::sin(2*std::numbers::pi*1000*double(i)/format.rate);
                    signal[2*i+1]=-signal[2*i];
                }
                // Include a fractional effective Hz value; rounding to an
                // integer input rate would cause frequency/phase drift.
                for (const double speed:{0.5,0.8,0.999,1.001,1.25,2.0,4.0}) {
                    converter.configure(format,rates.second,algorithm,speed);
                    const auto output=render(converter,signal,2);
                    require(output.size()/2==std::uint64_t(std::round(signal.size()/2.0*rates.second/(format.rate*speed))),"wrong speed duration");
                    const auto margin=std::size_t(rates.second/4);
                    double peak{},coherence{},packet_error{};
                    for (std::size_t i=margin;i<output.size()/2-margin;++i) {
                        const auto expected=0.25*std::sin(2*std::numbers::pi*1000*speed*double(i)/rates.second);
                        peak=std::max(peak,std::abs(output[2*i]-expected));
                        coherence=std::max(coherence,std::abs(output[2*i]+output[2*i+1]));
                    }
                    converter.clear(); const auto split=render(converter,signal,2,137);
                    require(output.size()==split.size(),"packetization changed speed duration");
                    for (std::size_t i=0;i<output.size();++i) packet_error=std::max(packet_error,std::abs(output[i]-split[i]));
                    std::cout<<format.rate<<" -> "<<rates.second<<", "<<speed<<"x, "<<1000*speed<<" Hz: waveform error "<<peak<<", packet error "<<packet_error<<'\n';
                    require(peak<2e-8,"speed output differs from the natural rate-change waveform");
                    require(coherence<1e-12,"speed processing changed stereo phase coherence");
                    require(packet_error<1e-10,"speed processing depends on packet boundaries");
                    converter.clear(); std::vector<double> zeros(2051*2);
                    const auto reset=render(converter,zeros,2,37);
                    require(std::ranges::all_of(reset,[](double value){return value==0;}),"seek or EOS replayed the previous signal");
                }
            }
            // Faster playback moves high frequencies above device Nyquist.
            // They must be filtered rather than fold down as distortion.
            std::vector<double> high(48000*4*2);
            for (std::size_t i=0;i<high.size()/2;++i) high[2*i]=high[2*i+1]=0.5*std::sin(2*std::numbers::pi*15000*double(i)/48000);
            converter.configure(source,48000,algorithm,2.0);
            const auto rejected=render(converter,high,2);
            double alias{};
            for (std::size_t i=12000;i<rejected.size()/2-12000;++i) alias=std::max(alias,std::abs(rejected[2*i]));
            std::cout<<"2x above-Nyquist alias peak: "<<alias<<'\n';
            require(alias<2e-8,"faster playback introduced an alias");
            for (const double speed:{0.05,0.25,0.8,1.25,16.0,128.0}) for (const auto frames:{0u,1u,7u,211u,2051u}) {
                converter.configure(source,48000,algorithm,speed); std::vector<double> zeros(frames*2);
                const auto output=render(converter,zeros,2,37);
                require(output.size()/2==std::uint64_t(std::round(frames/speed)),"short/extreme speed duration was wrong");
                require(std::ranges::all_of(output,[](double value){return value==0;}),"silent speed drain contained samples");
            }
            // Worst supported combined SRC/speed ratios use small bounded
            // producer blocks, including the 32-channel upsampling case.
            for (const auto rates:{std::pair{8000u,768000u},std::pair{768000u,8000u}}) {
                const double speed=rates.first==8000 ? 0.05 : 128.0;
                converter.configure({SampleKind::float64,rates.first,32,64,64,0},rates.second,algorithm,speed);
                require(converter.block_frames()<=2048,"unbounded producer input block");
                std::vector<double> zeros(7*32);
                const auto output=render(converter,zeros,32);
                require(output.size()/32==std::uint64_t(std::round(7.0*rates.second/(rates.first*speed))),"combined SRC/speed extreme duration wrong");
            }
            const SourceFormat surround{SampleKind::float64,96000,6,64,64,0};
            converter.configure(surround,48000,algorithm,1.25);
            std::vector<double> channels(30000*6,0.1);
            const auto multichannel=render(converter,channels,6);
            require(multichannel.size()==12000*6,"multichannel speed duration wrong");
            for (std::size_t i=0;i<multichannel.size();i+=6) for (std::size_t c=1;c<6;++c)
                require(multichannel[i]==multichannel[i+c],"multichannel timing diverged");
            converter.configure(source,48000,algorithm,1.0);
            require(!converter.active(),"return to normal retained speed filtering");
        }
        std::cout<<"Default-style speed/pitch, waveform accuracy, alias rejection, fractional rates, packet boundaries, seek reset, EOS, extreme ratios and multichannel passed.\n";
        return 0;
    } catch (const std::exception& error) { std::cerr<<error.what()<<'\n'; return 1; }
}
