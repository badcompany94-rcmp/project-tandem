#include "copy_encode.h"
#include <algorithm>
namespace gx {
bool encode_copy(const EfbCopyCmd& c,unsigned s,std::span<const uint8_t> rgba,std::vector<uint8_t>& output){
    const uint32_t fmt=c.format&15,w=c.dst_w,h=c.dst_h;
    const bool one=fmt==1 || (fmt>=7 && fmt<=10),two=fmt==3 || fmt==11 || fmt==12,color=fmt==4 || fmt==5 || fmt==6;
    if(!w || !h || s<1 || s>8 || w>16384/s || h>16384/s || !(one || two || (color && !c.depth)))return false;
    if(rgba.size()!=size_t(w)*h*s*s*4)return false;
    const uint32_t tw=one?8:4,bw=(w+tw-1)/tw,bh=(h+3)/4;
    std::vector<uint8_t> bytes(size_t(bw)*bh*(fmt==6?64:32));
    auto at=[&](uint32_t x,uint32_t y){x=std::min(x,w-1);y=std::min(y,h-1);return rgba.data()+(((size_t(y)*s+s/2)*w*s+size_t(x)*s+s/2)*4);};
    for(uint32_t by=0;by<bh;++by)for(uint32_t bx=0;bx<bw;++bx){auto* block=bytes.data()+(size_t(by)*bw+bx)*(fmt==6?64:32);
        if(one){for(uint32_t i=0;i<32;++i)block[i]=at(bx*8+i%8,by*4+i/8)[0];continue;}
        for(uint32_t i=0;i<16;++i){const auto* p=at(bx*4+i%4,by*4+i/4);
            if(two){block[i*2]=p[3];block[i*2+1]=p[0];continue;}
            if(fmt==6){block[i*2]=p[3];block[i*2+1]=p[0];block[32+i*2]=p[1];block[33+i*2]=p[2];continue;}
            uint16_t value;if(fmt==4)value=uint16_t((p[0]>>3)<<11|(p[1]>>2)<<5|(p[2]>>3));
            else if(p[3]>=0xe0)value=uint16_t(0x8000|(p[0]>>3)<<10|(p[1]>>3)<<5|(p[2]>>3));
            else value=uint16_t((p[3]>>5)<<12|(p[0]>>4)<<8|(p[1]>>4)<<4|(p[2]>>4));
            block[i*2]=uint8_t(value>>8);block[i*2+1]=uint8_t(value);}}
    output=std::move(bytes);return true;
}
}
