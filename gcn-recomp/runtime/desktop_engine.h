#pragma once
#include <cstdlib>
#include <cstring>

namespace desktop_engine {
inline bool valid(int cc){return cc==0 || (cc>=50 && cc<=250 && cc%25==0);}
inline int requested(){
    const char* text=std::getenv("TANDEM_ENGINE_CC");
    if(!text || !*text)return 0;
    char* end=nullptr;long cc=std::strtol(text,&end,10);
    return end && !*end && cc>=0 && cc<=250 && valid(int(cc))?int(cc):-1;
}
// Preserve the retail 50/100/150 coefficients; interpolate intermediate classes.
// Higher classes extend 150's top-speed coefficient, not the simulation clock.
inline float coefficient(int cc){
    if(cc<=100)return .9f+(cc-50)*(.1f/50.f);
    if(cc<=150)return 1.f+(cc-100)*(.15f/50.f);
    return 1.15f*(float(cc)/150.f);
}
inline float stock(unsigned slot){return slot==0?.9f:slot==1?1.f:1.15f;}
}
