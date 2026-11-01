// Nintendo WUP-028 / compatible Wii U-mode adapter reports. No USB or SDL calls.
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>

namespace gc_adapter {
struct Port {
    bool connected=false,rumble_power=false;
    uint16_t buttons=0;
    std::array<uint8_t,6> axes{128,128,128,128,0,0};
};
using Report=std::array<Port,4>;
inline bool decode(const uint8_t* data,size_t size,Report& result) {
    if(!data || size!=37 || data[0]!=0x21)return false;
    Report next{};
    for(size_t i=0;i<4;++i) {
        const auto* p=data+1+9*i;auto& port=next[i];
        port.connected=(p[0]&0x30)!=0;
        if(!port.connected)continue;
        port.rumble_power=(p[0]&0x04)!=0 && (p[0]&0x20)==0;
        port.buttons=uint16_t(p[1])|(uint16_t(p[2]&0x0f)<<8);
        for(size_t a=0;a<6;++a)port.axes[a]=p[3+a];
        // Preserve the physical trigger's digital click even with a worn pot.
        if(p[2]&0x08)port.axes[4]=255;
        if(p[2]&0x04)port.axes[5]=255;
    }
    result=next;return true;
}
}
