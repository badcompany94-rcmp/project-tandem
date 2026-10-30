#pragma once
#include "render.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <unordered_map>

namespace gx {
// Match only unambiguous indexed geometry. Newly visible objects, particles,
// changing topology, projection changes and large discontinuities snap to the
// newest native state. HUD/textures/colors retain their native values.
class Interpolation {
    struct Vertex { uint32_t current; float previous[3]; };
    std::vector<Vertex> vertices;
    static uint64_t key(const Batch& b, const Cmd& c) {
        if(c.type!=CmdType::Draw || !c.position_source || !c.vcount || c.state>=b.states.size())return 0;
        const auto& s=b.states[c.state];
        if(s.proj[6]!=0 || s.view_space)return 0;
        uint64_t hash=1469598103934665603ULL;
        auto add=[&](const void* data,size_t size) {const auto* p=static_cast<const uint8_t*>(data);while(size--) {hash^=*p++;hash*=1099511628211ULL;}};
        add(&c.position_source,sizeof(c.position_source));add(&c.prim,sizeof(c.prim));
        add(&c.count,sizeof(c.count));add(&c.vcount,sizeof(c.vcount));
        add(s.proj,sizeof(s.proj));add(s.viewport,sizeof(s.viewport));
        add(s.tex_id,sizeof(s.tex_id));add(&s.num_texgens,sizeof(s.num_texgens));
        // The same source array can have multiple disjoint pieces.
        for(uint32_t v=0;v<c.vcount;++v) {
            if(size_t(c.vfirst)+v>=b.verts.size())return 0;
            const auto source=b.verts[c.vfirst+v].src_idx;
            if(source==UINT32_MAX)return 0;
            add(&source,sizeof(source));
        }
        return hash?hash:1;
    }
public:
    size_t size() const { return vertices.size(); }
    void clear() { vertices.clear(); }
    void prepare(const Batch& before,const Batch& after) {
        clear();
        if(before.user_state!=after.user_state || before.display_height!=after.display_height)return;
        constexpr size_t ambiguous=std::numeric_limits<size_t>::max();
        std::unordered_map<uint64_t,size_t> old,counts;
        for(size_t i=0;i<before.cmds.size();++i)if(const auto k=key(before,before.cmds[i])) {
            if(!old.emplace(k,i).second)old[k]=ambiguous;
        }
        for(const auto& c:after.cmds)if(const auto k=key(after,c))++counts[k];
        for(const auto& c:after.cmds) {
            const auto k=key(after,c);const auto found=old.find(k);
            if(!k || found==old.end() || found->second==ambiguous || counts[k]!=1)continue;
            const auto& p=before.cmds[found->second];
            // Validate the entire primitive group before accepting any vertex.
            bool safe=true;
            for(uint32_t i=0;i<c.vcount && safe;++i) {
                const auto& a=before.verts[p.vfirst+i];const auto& b=after.verts[c.vfirst+i];
                const float distance=std::max(20.f,std::abs(b.pos[2])*.15f);
                for(int j=0;j<3;++j)if(!std::isfinite(a.pos[j]) || !std::isfinite(b.pos[j]) || std::abs(a.pos[j]-b.pos[j])>distance)safe=false;
                if(a.pos[2]*b.pos[2]<=0)safe=false;
            }
            if(safe)for(uint32_t i=0;i<c.vcount;++i) {
                Vertex v{c.vfirst+i,{}};std::copy_n(before.verts[p.vfirst+i].pos,3,v.previous);vertices.push_back(v);
            }
        }
    }
    void apply(const Batch& native,Batch& output,float alpha) const {
        alpha=std::clamp(alpha,0.f,1.f);
        // Only positions change. Keep native projection, colors, UVs and indices.
        for(const auto& v:vertices)for(int j=0;j<3;++j)
            output.verts[v.current].pos[j]=v.previous[j]+(native.verts[v.current].pos[j]-v.previous[j])*alpha;
        for(const auto& c:output.cmds)if(c.type==CmdType::Draw) {
            const auto stride=packed_stride(draw_ntex(output.states[c.state]));
            for(uint32_t i=0;i<c.vcount;++i) {
                const auto index=c.vfirst+i;
                const auto offset=(int64_t(c.base_vertex)+index)*stride;
                if(offset>=0 && uint64_t(offset)+12<=output.packed.size())
                    std::memcpy(output.packed.data()+offset,output.verts[index].pos,12);
            }
        }
    }
};
}
