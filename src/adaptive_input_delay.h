// Host-only pacing policy. This state never enters a simulation checkpoint.
#pragma once
#include <algorithm>
#include <cmath>
#include <array>
namespace mkdd::online {
class AdaptiveInputDelay {
    unsigned delay_=1, samples_=0, slow_=0, network_spikes_=0, healthy_=0;
    double elapsed_=0, network_=0;
    std::array<double,64> arrivals_{};
    unsigned arrivals_count_=0,arrivals_next_=0;
public:
    // Extra queuing is a fallback for poor delivery, not a universal latency
    // increase. The transport budget is separate from the rewind history.
    static constexpr unsigned maximum=16;
    struct Result { bool evaluated=false,changed=false;unsigned delay=1;double fps=0,slow_fraction=0,network_ms=0,network_spike_fraction=0; };
    unsigned delay() const {return delay_;}
    // Bundle age includes the difference in how far ahead each peer queued
    // this tick. Remove that difference before estimating delivery. Otherwise
    // increasing local delay itself raises measured "network latency" forever.
    void received(double age_ms,unsigned local_lead=0,unsigned minimum_lead=0) {
        if(!std::isfinite(age_ms) || age_ms<0 || age_ms>1000)return;
        if(local_lead>maximum || minimum_lead>maximum)return;
        age_ms=std::max(0.0,age_ms+(int(minimum_lead)-int(local_lead))*1000.0/60);
        arrivals_[arrivals_next_]=age_ms;arrivals_next_=(arrivals_next_+1)%arrivals_.size();
        arrivals_count_=std::min<unsigned>(arrivals_count_+1,arrivals_.size());
    }
    // During the introduction/countdown we already exchange real input packets.
    // Tail delivery, rather than median RTT, determines whether a frame needs
    // an expensive correction. This includes host polling, not just travel.
    unsigned delivery_delay() const {
        if(arrivals_count_>=16) {
            auto values=arrivals_;std::sort(values.begin(),values.begin()+arrivals_count_);
            const double age=values[(arrivals_count_*95-1)/100];
            return std::clamp(int(std::ceil(age*60/1000-1e-6))+1,1,int(maximum));
        }
        return 1;
    }
    unsigned prepare() {
        if(arrivals_count_>=16)delay_=delivery_delay();
        reset_window();healthy_=0;return delay_;
    }
    void reset_window(){samples_=slow_=network_spikes_=0;elapsed_=network_=0;}
    Result observe(double frame_ms,double network_ms,bool unchanged_input) {
        Result out;out.delay=delay_;
        if(!std::isfinite(frame_ms) || !std::isfinite(network_ms) || frame_ms<=0 || network_ms<0 || frame_ms>2000 || (frame_ms>250 && network_ms<20)) {reset_window();healthy_=0;return out;}
        ++samples_;elapsed_+=frame_ms;network_+=network_ms;if(frame_ms>20)++slow_;
        if(frame_ms>22 && network_ms>=2)++network_spikes_;
        if(samples_<60)return out;
        out.evaluated=true;out.fps=1000*samples_/elapsed_;out.slow_fraction=double(slow_)/samples_;out.network_ms=network_/samples_;
        out.network_spike_fraction=double(network_spikes_)/samples_;
        // Two correction-related hitches per second can feel rough while the
        // average still reads 60. Do not hide that behind mean work/FPS alone.
        const bool repeated_spikes=network_spikes_>=2;
        const bool rough=out.fps<59.5 || out.slow_fraction>=.05 || repeated_spikes;
        // A graphics-limited PC should not accumulate input delay without any
        // sign of network waiting or correction work to remove.
        if(rough && (out.network_ms>=.75 || repeated_spikes)) {healthy_=0;if(delay_<maximum)delay_=std::min(maximum,std::max(delay_+1,delivery_delay()));}
        else if(out.network_ms<.75 && !repeated_spikes) {
            ++healthy_;
            // Lower after three quiet seconds and while the latest
            // sample equals the last queued input, so draining skips no change.
            const unsigned floor=arrivals_count_>=16?delivery_delay():0;
            if(healthy_>=3 && unchanged_input && delay_>floor){--delay_;healthy_=0;}
        }else healthy_=0;
        out.changed=delay_!=out.delay;out.delay=delay_;reset_window();return out;
    }
};
}
