#include "gx/scanout_overlay.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
namespace gx {Batch::~Batch()=default;}
static void require(bool ok,const char* why){if(!ok){fprintf(stderr,"FAIL: %s\n",why);exit(1);}}
int main() {
    gx::Batch b;gx::PixelState s{};
    s.proj[0]=2.f/640;s.proj[1]=-1;s.proj[2]=-2.f/480;s.proj[3]=1;s.proj[6]=1;
    s.viewport[0]=320;s.viewport[1]=-240;s.viewport[3]=320;s.viewport[4]=240;
    b.states.push_back(s);
    const float positions[4][2]={{16,16},{624,16},{624,464},{16,464}};
    for(const auto& p:positions){gx::GpuVertex v;std::memset(&v,0,sizeof(v));v.pos[0]=p[0];v.pos[1]=p[1];b.verts.push_back(v);}
    b.indices={0,1,2,0,2,3};gx::Cmd c{};c.type=gx::CmdType::Draw;c.count=6;
    gx::EfbCopyCmd screen{};screen.src_x=16;screen.src_y=16;screen.src_w=608;screen.src_h=448;
    require(gx::covers_scanout(b,c,screen),"award panel covers cropped scanout despite inset projection bounds");
    screen.src_x=screen.src_y=0;screen.src_w=640;screen.src_h=480;
    require(!gx::covers_scanout(b,c,screen),"inset panel does not cover uncropped image");
    screen.src_x=16;screen.src_y=16;screen.src_w=608;screen.src_h=448;
    b.verts[0].pos[0]=40;b.verts[3].pos[0]=40;
    require(!gx::covers_scanout(b,c,screen),"ordinary dialog stays at its original proportions");
    b.verts[0].pos[0]=b.verts[3].pos[0]=16;
    b.states[0].proj[6]=0;require(!gx::covers_scanout(b,c,screen),"perspective geometry excluded");b.states[0].proj[6]=1;
    c.prim=1;require(!gx::covers_scanout(b,c,screen),"outlines cannot count as filled overlays");c.prim=0;
    b.indices={0,1,0,2,3,2};require(!gx::covers_scanout(b,c,screen),"sparse geometry with full-frame bounds cannot stretch HUD elements");
    b.indices={0,1,2,0,2,3};
    b.states[0].proj[0]=2.f/608;b.states[0].proj[2]=-2.f/448;
    b.states[0].viewport[0]=304;b.states[0].viewport[1]=-224;
    b.states[0].viewport[3]=646;b.states[0].viewport[4]=566;b.states[0].bp[0x59]=0x02acab;
    screen.src_x=screen.src_y=0;
    const float award[4][2]={{0,34},{608,34},{608,414},{0,414}};
    for(unsigned i=0;i<4;++i){b.verts[i].pos[0]=award[i][0];b.verts[i].pos[1]=award[i][1];b.verts[i].col[0][3]=190;}
    require(gx::covers_scanout(b,c,screen),"actual award viewport and letterboxed dim panel extend horizontally");
    b.states[0].num_texgens=1;
    require(!gx::covers_scanout(b,c,screen),"a letterboxed title texture is not stretched with the dim panel");
    printf("PASS: cropped scanout overlays, normal dialogs, perspective and sparse HUD exclusions\n");
}
