#pragma once
#include "render.h"
#include <algorithm>
#include <cmath>

namespace gx {
// A full-screen overlay covers the displayed crop, which can be smaller than
// the EFB or the orthographic projection. Compare in uncompressed EFB pixels.
inline bool covers_scanout(const Batch& b,const Cmd& c,const EfbCopyCmd& screen) {
    if(c.type!=CmdType::Draw || c.prim!=0 || c.state>=b.states.size() || !screen.src_w || !screen.src_h)return false;
    const auto& s=b.states[c.state];
    if(s.proj[6]==0 || c.count<6 || size_t(c.first)+c.count>b.indices.size())return false;
    const float xoff=float(s.bp[0x59]&0x3ff)*2,yoff=float((s.bp[0x59]>>10)&0x3ff)*2;
    float x0=INFINITY,x1=-INFINITY,y0=INFINITY,y1=-INFINITY,area=0;
    float triangle[3][2]{};
    for(uint32_t i=0;i<c.count;++i) {
        const auto index=b.indices[c.first+i];if(index>=b.verts.size())return false;
        const auto& v=b.verts[index];
        const float x=s.viewport[3]-xoff+s.viewport[0]*(s.proj[0]*v.pos[0]+s.proj[1]);
        const float y=s.viewport[4]-yoff+s.viewport[1]*(s.proj[2]*v.pos[1]+s.proj[3]);
        if(!std::isfinite(x) || !std::isfinite(y))return false;
        x0=std::min(x0,x);x1=std::max(x1,x);y0=std::min(y0,y);y1=std::max(y1,y);
        triangle[i%3][0]=x;triangle[i%3][1]=y;
        if(i%3==2)area+=std::abs((triangle[1][0]-triangle[0][0])*(triangle[2][1]-triangle[0][1])-
            (triangle[2][0]-triangle[0][0])*(triangle[1][1]-triangle[0][1]))*.5f;
    }
    const bool wide=x0<=screen.src_x+1.f && x1>=screen.src_x+screen.src_w-1.f;
    if(!wide)return false;
    if(y0<=screen.src_y+1.f && y1>=screen.src_y+screen.src_h-1.f &&
       area>=float(screen.src_w)*screen.src_h*.95f)return true;
    // The award screen shades only y=34..414 inside a 608x448 scanout,
    // preserving its cinematic bars. This is a solid, full-width rectangle,
    // not a full-height image. Extend its width without changing that height.
    if(s.num_texgens || c.count!=6 || y0<screen.src_y-1.f || y1>screen.src_y+screen.src_h+1.f ||
       area<(x1-x0)*(y1-y0)*.95f)return false;
    const auto& color=b.verts[b.indices[c.first]].col[0];
    for(uint32_t i=1;i<c.count;++i)for(unsigned channel=0;channel<4;++channel)
        if(b.verts[b.indices[c.first+i]].col[0][channel]!=color[channel])return false;
    return true;
}
}
