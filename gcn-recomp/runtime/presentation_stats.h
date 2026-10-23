// Bounded, host-only timing summaries. Repeated idle UI draws are not frames.
#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <optional>
class PresentationStats {
public:
    struct Work { double events=0,wait=0,render=0,overlay=0,swap=0,trace=0,pace_overrun=0; };
    struct Summary { double fps=0,p95=0,p99=0,maximum=0;unsigned over25=0;Work maximum_work; };
    static constexpr unsigned window=120;
private:
    bool previous_=false;
    uint64_t last_=0;
    unsigned count_=0,over25_=0;
    double total_=0;
    Work maximum_;
    std::array<double,window> intervals_{};
    void clear(){count_=over25_=0;total_=0;maximum_={};}
public:
    std::optional<Summary> frame(uint64_t now,Work work) {
        if(!previous_ || now<=last_){previous_=true;last_=now;clear();return {};}
        const double interval=(now-last_)/1e6;last_=now;
        intervals_[count_++]=interval;total_+=interval;over25_+=interval>25;
        maximum_.events=std::max(maximum_.events,work.events);maximum_.wait=std::max(maximum_.wait,work.wait);
        maximum_.render=std::max(maximum_.render,work.render);maximum_.overlay=std::max(maximum_.overlay,work.overlay);
        maximum_.swap=std::max(maximum_.swap,work.swap);maximum_.trace=std::max(maximum_.trace,work.trace);
        maximum_.pace_overrun=std::max(maximum_.pace_overrun,work.pace_overrun);
        if(count_<window)return {};
        auto ordered=intervals_;std::sort(ordered.begin(),ordered.end());
        Summary out{1000*window/total_,ordered[113],ordered[118],ordered.back(),over25_,maximum_};
        clear();return out;
    }
};
