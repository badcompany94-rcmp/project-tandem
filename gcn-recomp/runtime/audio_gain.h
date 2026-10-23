#pragma once
#include <algorithm>
#include <atomic>
#include <cstdint>

// Host preferences stay outside guest/checkpoint state. Read once per mixed
// output block so changing volume cannot affect deterministic simulation.
namespace audio_gain {
inline std::atomic<int> percent{100};
inline void set(int value){percent.store(std::clamp(value,0,100),std::memory_order_relaxed);}
inline int get(){return percent.load(std::memory_order_relaxed);}
inline void apply(int16_t* samples,int frames) {
    const int gain=get();if(gain==100)return;
    for(int i=0;i<frames*2;++i)samples[i]=(int16_t)((int32_t)samples[i]*gain/100);
}
}
