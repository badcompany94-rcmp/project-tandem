#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d11.h>
#include "graphics/d3d11_device.h"
#include "gx/render_d3d11.h"
#include <array>
#include <cstdio>
#include <cstring>
#include <stdexcept>
// No guest queue in this isolated fixture.
gx::Batch::~Batch()=default;
namespace {
template<class T>struct Com{T* p=nullptr;~Com(){if(p)p->Release();}T* operator->()const{return p;}T** out(){return &p;}};
void require(bool ok,const std::string& error){if(!ok)throw std::runtime_error(error);}
std::vector<uint8_t> read_window(desktop_d3d11::Device& device){
    Com<ID3D11Resource> source;device.window_target()->GetResource(source.out());Com<ID3D11Texture2D> texture;
    require(SUCCEEDED(source->QueryInterface(IID_ID3D11Texture2D,reinterpret_cast<void**>(texture.out()))),"Read native window texture interface");
    D3D11_TEXTURE2D_DESC d{};texture->GetDesc(&d);d.Usage=D3D11_USAGE_STAGING;d.BindFlags=0;d.CPUAccessFlags=D3D11_CPU_ACCESS_READ;d.MiscFlags=0;
    Com<ID3D11Texture2D> staging;require(SUCCEEDED(device.device()->CreateTexture2D(&d,nullptr,staging.out())),"Create native window readback");
    auto* c=device.context();c->OMSetRenderTargets(0,nullptr,nullptr);c->CopyResource(staging.p,texture.p);std::vector<uint8_t> pixels(size_t(d.Width)*d.Height*4);
    D3D11_MAPPED_SUBRESOURCE m{};require(SUCCEEDED(c->Map(staging.p,0,D3D11_MAP_READ,0,&m)),"Map native window readback");
    for(unsigned y=0;y<d.Height;++y)std::memcpy(pixels.data()+size_t(y)*d.Width*4,static_cast<const uint8_t*>(m.pData)+y*m.RowPitch,d.Width*4);c->Unmap(staging.p,0);return pixels;
}
struct Window{HWND handle=nullptr;Window(){WNDCLASSW w{};w.lpfnWndProc=DefWindowProcW;w.hInstance=GetModuleHandleW(nullptr);w.lpszClassName=L"TandemGXNativeDraw";
    require(RegisterClassW(&w)!=0,"Register hidden GX window");handle=CreateWindowW(w.lpszClassName,L"Hidden GX batch",WS_OVERLAPPEDWINDOW,0,0,128,128,nullptr,nullptr,w.hInstance,nullptr);require(handle!=nullptr,"Create hidden GX window");}
    ~Window(){if(handle)DestroyWindow(handle);}};
void fill(gx::Batch& batch,bool texture){
    batch.states.resize(1);auto& state=batch.states[0];std::memset(&state,0,sizeof(state));
    const float proj[]={1,0,1,0,1,0,1},viewport[]={320,264,16777215,320,264,16777215};
    std::memcpy(state.proj,proj,sizeof(proj));std::memcpy(state.viewport,viewport,sizeof(viewport));
    auto* bp=state.bp;bp[0x21]=(639<<12)|527;bp[0xf3]=0xff<<16;bp[0x41]=0x18;bp[0x43]=1;
    bp[0xc0]=(15<<12)|(15<<8)|(15<<4)|(texture?8:10)|(1<<19);
    bp[0xc1]=(7<<13)|(7<<10)|(7<<7)|((texture?4:5)<<4)|(1<<19);bp[0xf6]=4;bp[0xf7]=14;
    state.num_texgens=texture?1:0;
    if(texture){bp[0x28]=0x40;state.tex_id[0]=7;bp[0x88]=1|(1<<10);
        auto data=std::make_shared<gx::TexData>();data->id=7;data->width=data->height=2;
        // RGBA bytes, one distinct color per source row/column.
        data->levels={{0xff0000ff,0xff00ff00,0xffff0000,0xff00ffff}};batch.new_textures={data};}
    batch.verts.resize(6);std::memset(batch.verts.data(),0,batch.verts.size()*sizeof(gx::GpuVertex));
    const float xy[6][2]={{-.75f,-.5f},{.25f,-.5f},{.25f,.75f},{-.75f,-.5f},{.25f,.75f},{-.75f,.75f}};
    const unsigned stride=gx::packed_stride(state.num_texgens);batch.packed.resize(6*stride);
    for(unsigned i=0;i<6;++i){auto& v=batch.verts[i];v.pos[0]=xy[i][0];v.pos[1]=xy[i][1];v.pos[2]=-.5f;
        for(auto& c:v.col){c[0]=51;c[1]=102;c[2]=204;c[3]=255;}
        v.tex[0][0]=(xy[i][0]+.75f);v.tex[0][1]=(.75f-xy[i][1])/1.25f;v.tex[0][2]=1;
        batch.indices.push_back(i);std::memcpy(batch.packed.data()+i*stride,&v,stride);}
    gx::Cmd draw{};draw.type=gx::CmdType::Draw;draw.count=6;draw.vcount=6;batch.cmds={draw};
}
void check(const std::vector<uint8_t>& p,bool texture){
    require(p.size()==640*528*4,"Native GX readback size mismatch");unsigned changed=0;
    for(unsigned y=0;y<528;++y)for(unsigned x=0;x<640;++x){
        const auto* v=p.data()+(y*640+x)*4;std::array<uint8_t,4> expected={0,0,0,255};
        if(x>=80 && x<400 && y>=66 && y<396){++changed;
            if(!texture)expected={51,102,204,255};
            else if(y<231)expected=x<240?std::array<uint8_t,4>{255,0,0,255}:std::array<uint8_t,4>{0,255,0,255};
            else expected=x<240?std::array<uint8_t,4>{0,0,255,255}:std::array<uint8_t,4>{255,255,0,255};}
        if(std::memcmp(v,expected.data(),4)){char error[200];std::snprintf(error,sizeof(error),"Native GX %s mismatch at %u,%u: %u,%u,%u,%u expected %u,%u,%u,%u",texture?"texture":"raster",x,y,v[0],v[1],v[2],v[3],expected[0],expected[1],expected[2],expected[3]);throw std::runtime_error(error);}}
    require(changed==105600,"Vacuous native GX draw check");
}
}
int main()try{
    Window window;desktop_d3d11::Device device;gx::D3D11Renderer renderer;std::string error;
    require(device.initialize(window.handle,640,528,error),error);require(renderer.initialize(device,1,error),error);
    for(bool texture:{false,true}){gx::Batch batch;fill(batch,texture);require(renderer.prepare(batch,error),error);renderer.clear(0x000000ff,1);
        require(renderer.draw(batch,batch.cmds[0],1,0,error),error);std::vector<uint8_t> pixels;require(renderer.read_color(pixels,error),error);check(pixels,texture);
        // Reuse cached shaders/resources and Map WRITE_DISCARD buffers.
        batch.new_textures.clear();require(renderer.prepare(batch,error),error);renderer.clear(0x000000ff,1);require(renderer.draw(batch,batch.cmds[0],1,0,error),error);
        std::vector<uint8_t> again;require(renderer.read_color(again,error),error);require(again==pixels,"Repeated native GX batch changed pixels");}
    gx::Batch batch;fill(batch,false);require(renderer.prepare(batch,error),error);renderer.clear(0x000000ff,1);require(renderer.draw(batch,batch.cmds[0],1,0,error),error);
    gx::EfbCopyCmd copy{};copy.src_x=100;copy.src_y=150;copy.src_w=32;copy.src_h=20;copy.dst_w=8;copy.dst_h=4;copy.tex_id=91;copy.format=6;
    std::vector<uint8_t> tiled;require(renderer.copy(copy,&tiled,error),error);require(tiled.size()==128,"Native GX RGBA tile count");
    for(unsigned tile=0;tile<2;++tile)for(unsigned i=0;i<16;++i){const auto* b=tiled.data()+tile*64;
        require(b[i*2]==255 && b[i*2+1]==51 && b[32+i*2]==102 && b[33+i*2]==204,"Native GX RGBA tile channels changed");}
    std::vector<uint8_t> copied;require(renderer.read_texture(91,copied,error),error);require(copied.size()==8*4*4,"Native GX copy dimensions");
    for(unsigned i=0;i<copied.size();i+=4)require(copied[i]==51 && copied[i+1]==102 && copied[i+2]==204 && copied[i+3]==255,"Native GX scaled copy orientation/color");
    // Clear only alpha inside the copy's source rectangle. RGB and every pixel
    // outside that rectangle must survive the shader-based partial clear.
    copy.to_xfb=true;copy.clear=true;copy.clear_alpha=true;copy.clear_rgba=0x11223340;
    require(renderer.copy(copy,nullptr,error),error);require(renderer.read_color(copied,error),error);
    for(unsigned y=0;y<528;++y)for(unsigned x=0;x<640;++x){const auto* p=copied.data()+(y*640+x)*4;const bool inside=x>=100 && x<132 && y>=358 && y<378;
        require(p[3]==(inside?64:255),"Native GX partial alpha clear escaped rectangle");
        if(inside)require(p[0]==51 && p[1]==102 && p[2]==204,"Native GX alpha clear overwrote RGB");}
    // The farthest depth copied as Z8 must stay 255, never wrap to zero.
    copy.to_xfb=false;copy.clear=false;copy.depth=true;copy.format=1;
    require(renderer.copy(copy,&tiled,error),error);for(auto value:tiled)require(value==255,"Native GX far-depth copy wrapped");
    gx::Batch frozen;fill(frozen,false);frozen.states.push_back(frozen.states[0]);
    frozen.states[0].bp[0]=3u<<14;frozen.states[0].bp[0x40]=31; // culled plane setter
    frozen.states[1].bp[0]=1u<<19;frozen.states[1].bp[0x40]=31; // z-freeze draw
    for(auto& v:frozen.verts)v.pos[2]=-.75f;
    for(unsigned i=0;i<6;++i){auto v=frozen.verts[i];v.pos[2]=-.25f;frozen.verts.push_back(v);frozen.indices.push_back(6+i);}
    frozen.packed.resize(12*20);for(unsigned i=0;i<12;++i)std::memcpy(frozen.packed.data()+i*20,&frozen.verts[i],20);
    auto second=frozen.cmds[0];second.state=1;second.first=6;second.vfirst=6;frozen.cmds.push_back(second);
    require(renderer.prepare(frozen,error),error);renderer.clear(0x000000ff,1);for(const auto& draw:frozen.cmds)require(renderer.draw(frozen,draw,1,0,error),error);
    copy.format=6;require(renderer.copy(copy,nullptr,error),error);require(renderer.read_texture(copy.tex_id,copied,error),error);
    for(unsigned i=0;i<copied.size();i+=4)require(copied[i]==64 && copied[i+1]==0 && copied[i+2]==0 && copied[i+3]==64,"Native z-freeze did not retain the culled triangle's depth plane");
    for(bool fallback:{false,true}){
        gx::D3D11Renderer logical;require(logical.initialize(device,1,error,fallback),error);
        const std::array<uint8_t,4> src={51,102,204,255},dst={0x5a,0xa5,0x35,0x40};
        for(unsigned op=0;op<16;++op){gx::Batch b;fill(b,false);b.states[0].bp[0x41]=0x1a|(op<<12);
            require(logical.prepare(b,error),error);logical.clear(0x5aa53540,1);require(logical.draw(b,b.cmds[0],1,0,error),error);
            std::vector<uint8_t> pixels;require(logical.read_color(pixels,error),error);
            std::array<uint8_t,4> expected{};for(unsigned channel=0;channel<4;++channel){const auto s=src[channel],d=dst[channel];
                const unsigned values[]={0u,unsigned(s&d),unsigned(s&~d),s,unsigned(~s&d),d,unsigned(s^d),unsigned(s|d),unsigned(~(s|d)),unsigned(~(s^d)),unsigned(~d),unsigned(s|~d),unsigned(~s),unsigned(~s|d),unsigned(~(s&d)),255u};expected[channel]=uint8_t(values[op]);}
            for(unsigned y=0;y<528;++y)for(unsigned x=0;x<640;++x){const auto& e=(x>=80 && x<400 && y>=66 && y<396)?expected:dst;
                if(std::memcmp(pixels.data()+(y*640+x)*4,e.data(),4))throw std::runtime_error("GX logical op="+std::to_string(op)+" fallback="+std::to_string(fallback)+" mismatch at "+std::to_string(x)+","+std::to_string(y));}}
        std::printf("PASS all 16 GX logical operations; forced_fallback=%d native_hardware_logic=%d\n",fallback,logical.native_logic_operations());
    }
    // Restore the original renderer's EFB draw after the separate logic fixtures.
    require(renderer.prepare(batch,error),error);renderer.clear(0x000000ff,1);require(renderer.draw(batch,batch.cmds[0],1,0,error),error);
    gx::EfbCopyCmd screen{};screen.src_w=screen.dst_w=640;screen.src_h=screen.dst_h=528;screen.to_xfb=true;screen.dest=0x100000;bool complete=false;
    require(renderer.scanout(screen,528,640.f/528,true,complete,error) && complete,error);const auto window_pixels=read_window(device);
    std::vector<uint8_t> efb;require(renderer.read_color(efb,error),error);
    for(unsigned y=0;y<528;++y)require(std::memcmp(window_pixels.data()+y*640*4,efb.data()+(527-y)*640*4,640*4)==0,"Native scanout image upside-down or displaced");
    screen.src_h=screen.dst_h=264;renderer.clear(0xff0000ff,1);complete=true;
    require(renderer.scanout(screen,528,640.f/528,true,complete,error) && !complete,error);require(read_window(device)==window_pixels,"Incomplete XFB changed window pixels");
    screen.dest+=640*2*264;renderer.clear(0x0000ffff,1);require(renderer.scanout(screen,528,640.f/528,true,complete,error) && complete,error);
    const auto assembled=read_window(device);for(unsigned y=0;y<528;++y)for(unsigned x=0;x<640;++x){const auto* p=assembled.data()+(y*640+x)*4;
        require(p[0]==(y<264?255:0) && p[1]==0 && p[2]==(y<264?0:255) && p[3]==255,"Native XFB strip assembly order/color");}
    screen.src_h=screen.dst_h=528;require(renderer.scanout(screen,528,4.f/3,true,complete,error) && complete,error);
    const auto boxed=read_window(device);for(unsigned y=0;y<528;++y)for(unsigned x=0;x<640;++x){const auto* p=boxed.data()+(y*640+x)*4;
        require(p[0]==0 && p[1]==0 && p[2]==(y>=24 && y<504?255:0) && p[3]==255,"Native scanout letterbox dimensions");}
    auto snapshot=renderer.capture(true,error);require(bool(snapshot),error);require(renderer.matches(*snapshot,error),error);
    std::vector<uint8_t> original,original_copy;require(renderer.read_color(original,error),error);require(renderer.read_texture(91,original_copy,error),error);
    renderer.clear(0x800040ff,.1234567f);copy.depth=false;copy.format=6;require(renderer.copy(copy,nullptr,error),error);
    require(!renderer.matches(*snapshot,error) && error.empty(),"Native snapshot did not detect changed EFB/copy");
    require(renderer.restore(*snapshot,error),error);require(renderer.matches(*snapshot,error),error);
    std::vector<uint8_t> restored,restored_copy;require(renderer.read_color(restored,error),error);require(renderer.read_texture(91,restored_copy,error),error);
    require(restored==original && restored_copy==original_copy,"Native snapshot failed exact EFB/texture restoration");
    gx::D3D11Renderer different;require(different.initialize(device,1,error),error);require(!different.restore(*snapshot,error) && !error.empty(),"Different renderer generation accepted native snapshot");
    // Save between two XFB strip writes. Later writes must allocate a new
    // target while the snapshot retains the previous texture, then preserve
    // the restored written-row bitmap and its first strip.
    screen.src_h=screen.dst_h=264;screen.dest=0x100000;renderer.clear(0xff0000ff,1);
    require(renderer.scanout(screen,528,640.f/528,true,complete,error) && !complete,error);auto partial=renderer.capture(true,error);require(bool(partial),error);
    screen.dest+=640*2*264;renderer.clear(0x0000ffff,1);require(renderer.scanout(screen,528,640.f/528,true,complete,error) && complete,error);
    require(!renderer.matches(*partial,error) && error.empty(),"Native partial-XFB mutation was not detected");require(renderer.restore(*partial,error),error);require(renderer.matches(*partial,error),error);
    renderer.clear(0x00ff00ff,1);require(renderer.scanout(screen,528,640.f/528,true,complete,error) && complete,error);const auto replayed=read_window(device);
    for(unsigned y=0;y<528;++y)for(unsigned x=0;x<640;++x){const auto* p=replayed.data()+(y*640+x)*4;
        require(p[0]==(y<264?255:0) && p[1]==(y<264?0:255) && p[2]==0 && p[3]==255,"Native restored XFB lost its first strip or completion state");}
    require(renderer.restore(*partial,error),error);require(renderer.matches(*partial,error),error);
    require(!renderer.initialize(device,0,error),"Invalid renderer replacement accepted");require(renderer.width()==640,"Invalid replacement retired native GX backend");
    std::printf("PASS actual GX batch native renderer on %s: packed vertices, TEV uniforms, exact raster/texture rows and cached repeat\n",device.info().adapter.c_str());
    std::puts("PASS native scaled copy, GX RGBA tiled CPU bytes, partial alpha clear and far-depth copy");
    std::puts("PASS native scanout orientation, partial XFB completion/order and letterboxing");
    std::puts("PASS native GPU snapshot restore, independent readback oracle, texture/XFB copy-on-write and generation rejection");
    std::puts("Full game and mixed-renderer rollback races remain separate gates.");return 0;
}catch(const std::exception& failure){std::fprintf(stderr,"FAIL %s\n",failure.what());return 1;}
