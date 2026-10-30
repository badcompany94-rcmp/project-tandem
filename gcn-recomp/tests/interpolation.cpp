#include "gx/interpolation.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
namespace gx {Batch::~Batch()=default;}
static void require(bool ok,const char* why) {if(!ok){fprintf(stderr,"FAIL: %s\n",why);exit(1);}}
static gx::Batch triangle(float x) {
    gx::Batch b;b.display_height=448;
    gx::PixelState s{};s.proj[0]=1;s.proj[2]=1;s.proj[4]=-1;s.tex_id[0]=7;
    b.states.push_back(s);
    gx::Cmd c{};c.type=gx::CmdType::Draw;c.position_source=0x1234;c.vcount=3;c.count=3;
    b.cmds.push_back(c);b.indices={0,1,2};
    for(unsigned i=0;i<3;++i) {gx::GpuVertex v;std::memset(&v,0,sizeof(v));v.pos[0]=x+i;v.pos[2]=-100;v.src_idx=i;b.verts.push_back(v);}
    b.packed.resize(3*gx::kPackedHead);for(unsigned i=0;i<3;++i)std::memcpy(b.packed.data()+i*gx::kPackedHead,&b.verts[i],gx::kPackedHead);
    return b;
}
int main() {
    auto before=triangle(0),after=triangle(10),output=after;gx::Interpolation interpolation;
    interpolation.prepare(before,after);require(interpolation.size()==3,"stable indexed geometry matches");
    interpolation.apply(after,output,.5f);require(output.verts[0].pos[0]==5 && after.verts[0].pos[0]==10,"midpoint changes only preview geometry");
    float packed;std::memcpy(&packed,output.packed.data(),4);require(packed==5,"GPU packed positions updated");
    require(output.indices==after.indices && output.states.size()==after.states.size() && output.verts[0].col[0][0]==after.verts[0].col[0][0],"native topology and colors preserved");
    interpolation.apply(after,output,1);require(output.verts[0].pos[0]==10,"native endpoint exact");
    auto cut=triangle(1000);interpolation.prepare(before,cut);require(!interpolation.size(),"large camera/object discontinuity snaps");
    after=triangle(10);after.cmds.push_back(after.cmds[0]);interpolation.prepare(before,after);require(!interpolation.size(),"ambiguous repeated draw cannot morph into wrong object");
    after=triangle(10);after.states[0].proj[6]=1;interpolation.prepare(before,after);require(!interpolation.size(),"HUD remains native");
    after=triangle(10);after.verts[1].src_idx=55;interpolation.prepare(before,after);require(!interpolation.size(),"topology/source change snaps");
    after=triangle(10);after.user_state=1;interpolation.prepare(before,after);require(!interpolation.size(),"scene metadata boundary clears matches");
    after=triangle(10);after.cmds[0].position_source=0;interpolation.prepare(before,after);require(!interpolation.size(),"unidentified direct geometry remains native");
    printf("PASS: geometric midpoint, packed GPU data, native isolation and discontinuity fallbacks\n");
}
