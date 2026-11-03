// Compare real GX shaders on hidden OpenGL and native Direct3D hardware targets.
// Raw EFB rows must agree, including asymmetric geometry and clipping. This is
// a correctness fixture; it does not measure game performance or netplay.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d11.h>
#include <glad/gl.h>
#include "graphics/d3d11_device.h"
#include "graphics/d3d11_program.h"
#include "gx/shadergen.h"
#include "gx/render_d3d11.h"
#include "gx/copy_shaders.h"
#include "gx/copy_encode.h"
#include <array>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <vector>
gx::Batch::~Batch()=default; // Isolated fixture, without the guest batch queue.
namespace {
constexpr unsigned side=64;
template<class T>struct Com {T* p=nullptr;~Com(){if(p)p->Release();}T* operator->()const{return p;}T** out(){return &p;}};
void require(bool ok,const std::string& error){if(!ok)throw std::runtime_error(error);}
void checked(HRESULT result,const char* error){require(SUCCEEDED(result),error);}
struct Window {
    HWND handle=nullptr;
    Window(){WNDCLASSW c{};c.style=CS_OWNDC;c.lpfnWndProc=DefWindowProcW;c.hInstance=GetModuleHandleW(nullptr);c.lpszClassName=L"TandemRasterParity";
        require(RegisterClassW(&c)!=0 || GetLastError()==ERROR_CLASS_ALREADY_EXISTS,"Register hidden window");
        handle=CreateWindowExW(0,c.lpszClassName,L"Tandem hidden raster parity",WS_OVERLAPPEDWINDOW,0,0,128,128,nullptr,nullptr,c.hInstance,nullptr);
        require(handle!=nullptr,"Create hidden window");}
    ~Window(){if(handle)DestroyWindow(handle);}
};
GLADapiproc load_gl(const char* name){
    auto result=wglGetProcAddress(name);
    if(!result || uintptr_t(result)<=3 || uintptr_t(result)==~uintptr_t(0))result=GetProcAddress(GetModuleHandleW(L"opengl32.dll"),name);
    return reinterpret_cast<GLADapiproc>(result);
}
using Matrix=std::array<float,16>;
Matrix identity(){Matrix m{};m[0]=m[5]=m[10]=m[15]=1;return m;}
gx::ShaderKey color_key(){gx::ShaderKey k{};k.alpha_func=0xff;k.efb_has_alpha=1;
    k.cenv[0]=(15u<<12)|(15u<<8)|(15u<<4)|10u|(1u<<19);
    k.aenv[0]=(7u<<13)|(7u<<10)|(7u<<7)|(5u<<4)|(1u<<19);k.ksel[0]=4;k.ksel[1]=14;return k;}
struct State {bool scissor=false,depth=false;int cull=0;float vp_a[4]={0,1,0,1};float vp_b[4]={1,2,0,0};};
using Pixels=std::vector<uint8_t>;
std::array<gx::GpuVertex,6> rectangle(float left,float bottom,float right,float top,float z,uint8_t r,uint8_t g,uint8_t b){
    std::array<gx::GpuVertex,6> v;std::memset(v.data(),0,sizeof(v));
    const float positions[6][2]={{left,bottom},{right,bottom},{right,top},{left,bottom},{right,top},{left,top}};
    for(unsigned i=0;i<6;++i){v[i].pos[0]=positions[i][0];v[i].pos[1]=positions[i][1];v[i].pos[2]=z;
        for(auto& color:v[i].col){color[0]=r;color[1]=g;color[2]=b;color[3]=255;}}
    return v;
}
class Reference {
    Window window;HDC dc=nullptr;HGLRC context=nullptr;GLuint program=0,copy_program=0,vao=0,vbo=0,fbo=0,color=0,depth=0;
    unsigned rw,rh;
public:
    Reference(unsigned w=side,unsigned h=side):rw(w),rh(h){
        dc=GetDC(window.handle);PIXELFORMATDESCRIPTOR p{};p.nSize=sizeof(p);p.nVersion=1;
        p.dwFlags=PFD_DRAW_TO_WINDOW|PFD_SUPPORT_OPENGL|PFD_DOUBLEBUFFER;p.iPixelType=PFD_TYPE_RGBA;p.cColorBits=32;p.cDepthBits=24;p.cStencilBits=8;
        const int format=ChoosePixelFormat(dc,&p);require(format!=0 && SetPixelFormat(dc,format,&p),"Set hidden OpenGL pixel format");
        auto legacy=wglCreateContext(dc);require(legacy && wglMakeCurrent(dc,legacy),"Create bootstrap OpenGL context");
        using Create=HGLRC(WINAPI*)(HDC,HGLRC,const int*);
        auto create=reinterpret_cast<Create>(wglGetProcAddress("wglCreateContextAttribsARB"));require(create!=nullptr,"OpenGL core context entry missing");
        const int attributes[]={0x2091,3,0x2092,3,0x9126,1,0};context=create(dc,nullptr,attributes);
        wglMakeCurrent(nullptr,nullptr);wglDeleteContext(legacy);require(context && wglMakeCurrent(dc,context),"Create OpenGL 3.3 core reference");
        require(gladLoadGL(load_gl)!=0,"Load OpenGL reference entry points");
        std::printf("OpenGL reference %s / %s\n",glGetString(GL_RENDERER),glGetString(GL_VERSION));
        const auto vertex=gx::gen_vertex_shader(),pixel=gx::gen_pixel_shader(color_key());
        auto compile=[&](GLenum type,const std::string& source){GLuint shader=glCreateShader(type);const auto* text=source.c_str();glShaderSource(shader,1,&text,nullptr);glCompileShader(shader);
            GLint valid=0;glGetShaderiv(shader,GL_COMPILE_STATUS,&valid);if(!valid){char log[8192];glGetShaderInfoLog(shader,sizeof(log),nullptr,log);throw std::runtime_error(log);}return shader;};
        const auto vs=compile(GL_VERTEX_SHADER,vertex),ps=compile(GL_FRAGMENT_SHADER,pixel);program=glCreateProgram();glAttachShader(program,vs);glAttachShader(program,ps);glLinkProgram(program);
        GLint linked=0;glGetProgramiv(program,GL_LINK_STATUS,&linked);require(linked!=0,"Link OpenGL reference GX shader");glDeleteShader(vs);glDeleteShader(ps);
        const auto cvs=compile(GL_VERTEX_SHADER,gx::copy_vertex_shader()),cps=compile(GL_FRAGMENT_SHADER,gx::copy_pixel_shader());
        copy_program=glCreateProgram();glAttachShader(copy_program,cvs);glAttachShader(copy_program,cps);glLinkProgram(copy_program);
        glGetProgramiv(copy_program,GL_LINK_STATUS,&linked);require(linked!=0,"Link reference GX copy shader");glDeleteShader(cvs);glDeleteShader(cps);
        glGenVertexArrays(1,&vao);glBindVertexArray(vao);glGenBuffers(1,&vbo);glBindBuffer(GL_ARRAY_BUFFER,vbo);
        for(unsigned i=0;i<11;++i){glEnableVertexAttribArray(i);const size_t offset=i==0?0:i==1?12:i==2?16:20+(i-3)*12;
            glVertexAttribPointer(i,(i==1 || i==2)?4:3,(i==1 || i==2)?GL_UNSIGNED_BYTE:GL_FLOAT,(i==1 || i==2)?GL_TRUE:GL_FALSE,sizeof(gx::GpuVertex),reinterpret_cast<const void*>(offset));}
        glGenFramebuffers(1,&fbo);glBindFramebuffer(GL_FRAMEBUFFER,fbo);glGenTextures(1,&color);glBindTexture(GL_TEXTURE_2D,color);
        glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,rw,rh,0,GL_RGBA,GL_UNSIGNED_BYTE,nullptr);glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,color,0);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
        glGenTextures(1,&depth);glBindTexture(GL_TEXTURE_2D,depth);glTexImage2D(GL_TEXTURE_2D,0,GL_DEPTH_COMPONENT32F,rw,rh,0,GL_DEPTH_COMPONENT,GL_FLOAT,nullptr);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
        glFramebufferTexture2D(GL_FRAMEBUFFER,GL_DEPTH_ATTACHMENT,GL_TEXTURE_2D,depth,0);require(glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE,"OpenGL reference target incomplete");
        glViewport(0,0,rw,rh);glUseProgram(program);const auto m=identity();for(const auto* name:{"u_proj","u_view","u_crop"})glUniformMatrix4fv(glGetUniformLocation(program,name),1,GL_FALSE,m.data());
        glUniform1i(glGetUniformLocation(program,"u_vr"),0);glUniform4f(glGetUniformLocation(program,"u_vp_b"),1,2,0,0);
    }
    ~Reference(){wglMakeCurrent(dc,context);glDeleteProgram(program);glDeleteProgram(copy_program);glDeleteBuffers(1,&vbo);glDeleteVertexArrays(1,&vao);glDeleteFramebuffers(1,&fbo);glDeleteTextures(1,&color);glDeleteTextures(1,&depth);
        wglMakeCurrent(nullptr,nullptr);wglDeleteContext(context);ReleaseDC(window.handle,dc);}
    void begin(const State& state){
        glBindFramebuffer(GL_FRAMEBUFFER,fbo);glViewport(0,0,rw,rh);glUseProgram(program);
        glDisable(GL_SCISSOR_TEST);glDepthMask(GL_TRUE);glClearColor(0,0,0,1);glClearDepth(1);glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
        if(state.scissor){glEnable(GL_SCISSOR_TEST);glScissor(9,13,27,31);}
        if(state.depth){glEnable(GL_DEPTH_TEST);glDepthFunc(GL_LESS);}else glDisable(GL_DEPTH_TEST);
        if(state.cull){glEnable(GL_CULL_FACE);glCullFace(state.cull==1?GL_BACK:GL_FRONT);glFrontFace(GL_CCW);}else glDisable(GL_CULL_FACE);
        glEnable(GL_DEPTH_CLAMP);
        for(unsigned i=4;i<6;++i)glEnable(GL_CLIP_DISTANCE0+i);
        glUniform4fv(glGetUniformLocation(program,"u_vp_a"),1,state.vp_a);
        glUniform4fv(glGetUniformLocation(program,"u_vp_b"),1,state.vp_b);
    }
    void draw(const std::array<gx::GpuVertex,6>& v){glBufferData(GL_ARRAY_BUFFER,sizeof(v),v.data(),GL_STREAM_DRAW);glDrawArrays(GL_TRIANGLES,0,6);}
    void primitive(std::span<const gx::GpuVertex> vertices,unsigned prim,float width){
        glEnable(GL_PROGRAM_POINT_SIZE);glUniform1f(glGetUniformLocation(program,"u_point_size"),width);glLineWidth(width);
        glBufferData(GL_ARRAY_BUFFER,vertices.size_bytes(),vertices.data(),GL_STREAM_DRAW);glDrawArrays(prim==2?GL_POINTS:GL_LINES,0,GLsizei(vertices.size()));}
    Pixels read(){Pixels p(size_t(rw)*rh*4);glReadPixels(0,0,rw,rh,GL_RGBA,GL_UNSIGNED_BYTE,p.data());require(glGetError()==GL_NO_ERROR,"OpenGL reference draw error");return p;}
    void clear_raw(uint32_t rgba,float z){glBindFramebuffer(GL_FRAMEBUFFER,fbo);glDisable(GL_SCISSOR_TEST);glDepthMask(GL_TRUE);glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);
        glClearColor(float(rgba>>24)/255,float((rgba>>16)&255)/255,float((rgba>>8)&255)/255,float(rgba&255)/255);glClearDepth(z);glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);}
    Pixels copy(const gx::EfbCopyCmd& command){
        GLuint destination=0,output=0;glGenTextures(1,&destination);glBindTexture(GL_TEXTURE_2D,destination);
        glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,command.dst_w,command.dst_h,0,GL_RGBA,GL_UNSIGNED_BYTE,nullptr);
        glGenFramebuffers(1,&output);glBindFramebuffer(GL_FRAMEBUFFER,output);glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,destination,0);
        require(glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE,"GL copy reference target incomplete");
        glViewport(0,0,command.dst_w,command.dst_h);glDisable(GL_SCISSOR_TEST);glDisable(GL_CULL_FACE);glDisable(GL_DEPTH_TEST);glDisable(GL_BLEND);glUseProgram(copy_program);
        glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,color);glUniform1i(glGetUniformLocation(copy_program,"u_src"),0);
        glActiveTexture(GL_TEXTURE1);glBindTexture(GL_TEXTURE_2D,depth);glUniform1i(glGetUniformLocation(copy_program,"u_depth"),1);
        glUniform4f(glGetUniformLocation(copy_program,"u_rect"),float(command.src_x)/rw,float(command.src_y)/rh,float(command.src_x+command.src_w)/rw,float(command.src_y+command.src_h)/rh);
        glUniform1i(glGetUniformLocation(copy_program,"u_mode"),int(command.format)|(command.depth?32:0));glDrawArrays(GL_TRIANGLE_STRIP,0,4);
        Pixels pixels(size_t(command.dst_w)*command.dst_h*4);glReadPixels(0,0,command.dst_w,command.dst_h,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());
        require(glGetError()==GL_NO_ERROR,"GL copy reference error");glBindFramebuffer(GL_FRAMEBUFFER,fbo);glDeleteFramebuffers(1,&output);glDeleteTextures(1,&destination);return pixels;
    }
};
class Native {
    Window window;desktop_d3d11::Device graphics;desktop_d3d11::Program program;
    Com<ID3D11Texture2D> color,depth,staging;Com<ID3D11RenderTargetView> target;Com<ID3D11DepthStencilView> depth_view;
    Com<ID3D11Buffer> geometry;Com<ID3D11InputLayout> layout;Com<ID3D11RasterizerState> raster;Com<ID3D11DepthStencilState> ds;
    std::string error;
public:
    Native(){require(graphics.initialize(window.handle,side,side,error),error);auto* d=graphics.device();auto* c=graphics.context();
        desktop_d3d11::CompiledProgram compiled;require(desktop_d3d11::compile_gx_program(color_key(),compiled,error,{true}),error);
        require(program.initialize(d,std::move(compiled),error),error);
        D3D11_TEXTURE2D_DESC t{};t.Width=t.Height=side;t.MipLevels=t.ArraySize=1;t.Format=DXGI_FORMAT_R8G8B8A8_UNORM;t.SampleDesc.Count=1;t.BindFlags=D3D11_BIND_RENDER_TARGET;
        checked(d->CreateTexture2D(&t,nullptr,color.out()),"Create native parity color");checked(d->CreateRenderTargetView(color.p,nullptr,target.out()),"Create native parity target");
        t.BindFlags=0;t.Usage=D3D11_USAGE_STAGING;t.CPUAccessFlags=D3D11_CPU_ACCESS_READ;checked(d->CreateTexture2D(&t,nullptr,staging.out()),"Create native parity staging");
        t.Usage=D3D11_USAGE_DEFAULT;t.CPUAccessFlags=0;t.BindFlags=D3D11_BIND_DEPTH_STENCIL;t.Format=DXGI_FORMAT_D24_UNORM_S8_UINT;
        checked(d->CreateTexture2D(&t,nullptr,depth.out()),"Create native parity depth");checked(d->CreateDepthStencilView(depth.p,nullptr,depth_view.out()),"Create native parity depth view");
        D3D11_BUFFER_DESC b{};b.ByteWidth=6*sizeof(gx::GpuVertex);b.Usage=D3D11_USAGE_DEFAULT;b.BindFlags=D3D11_BIND_VERTEX_BUFFER;
        checked(d->CreateBuffer(&b,nullptr,geometry.out()),"Create native parity geometry");
        std::array<D3D11_INPUT_ELEMENT_DESC,11> attributes{};for(unsigned i=0;i<11;++i){auto& a=attributes[i];a.SemanticName="TEXCOORD";a.SemanticIndex=i;
            a.Format=(i==1 || i==2)?DXGI_FORMAT_R8G8B8A8_UNORM:DXGI_FORMAT_R32G32B32_FLOAT;a.AlignedByteOffset=i==0?0:i==1?12:i==2?16:20+(i-3)*12;a.InputSlotClass=D3D11_INPUT_PER_VERTEX_DATA;}
        const auto code=program.vertex_code();checked(d->CreateInputLayout(attributes.data(),UINT(attributes.size()),code.data(),code.size(),layout.out()),"Create native parity layout");
        const auto m=identity();for(const auto* name:{"u_proj","u_view","u_crop"})require(program.floats(program.uniform(name),m,error),error);
        const std::array<int32_t,1> mode={0};const std::array<float,4> vp_b={1,2,0,0};
        require(program.integers(program.uniform("u_vr"),mode,error) && program.floats(program.uniform("u_vp_b"),vp_b,error),error);
        D3D11_VIEWPORT viewport{};viewport.Width=viewport.Height=side;viewport.MaxDepth=1;c->RSSetViewports(1,&viewport);
        UINT stride=sizeof(gx::GpuVertex),offset=0;c->IASetInputLayout(layout.p);c->IASetVertexBuffers(0,1,&geometry.p,&stride,&offset);c->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        std::printf("Direct3D native %s\n",graphics.info().adapter.c_str());
    }
    void begin(const State& state){auto* d=graphics.device();auto* c=graphics.context();const float black[4]={0,0,0,1};
        c->ClearRenderTargetView(target.p,black);c->ClearDepthStencilView(depth_view.p,D3D11_CLEAR_DEPTH,1,0);
        if(raster.p){raster.p->Release();raster.p=nullptr;}D3D11_RASTERIZER_DESC r{};r.FillMode=D3D11_FILL_SOLID;
        r.CullMode=state.cull==0?D3D11_CULL_NONE:state.cull==1?D3D11_CULL_BACK:D3D11_CULL_FRONT;
        // Offscreen clip-Y inversion preserves GL's raw row addresses. Verify
        // its winding convention against the hardware reference below.
        r.FrontCounterClockwise=FALSE;r.DepthClipEnable=FALSE;r.ScissorEnable=state.scissor;
        checked(d->CreateRasterizerState(&r,raster.out()),"Create native parity raster");c->RSSetState(raster.p);
        const D3D11_RECT rect={9,13,36,44};c->RSSetScissorRects(1,&rect);
        if(ds.p){ds.p->Release();ds.p=nullptr;}D3D11_DEPTH_STENCIL_DESC z{};z.DepthEnable=state.depth;z.DepthWriteMask=D3D11_DEPTH_WRITE_MASK_ALL;z.DepthFunc=D3D11_COMPARISON_LESS;
        checked(d->CreateDepthStencilState(&z,ds.out()),"Create native parity depth state");c->OMSetDepthStencilState(ds.p,0);
        require(program.floats(program.uniform("u_vp_a"),state.vp_a,error) && program.floats(program.uniform("u_vp_b"),state.vp_b,error),error);
        require(program.bind(c,error),error);c->OMSetRenderTargets(1,&target.p,depth_view.p);
    }
    void draw(const std::array<gx::GpuVertex,6>& v){auto* c=graphics.context();c->UpdateSubresource(geometry.p,0,nullptr,v.data(),0,0);c->Draw(6,0);}
    Pixels read(){auto* c=graphics.context();c->OMSetRenderTargets(0,nullptr,nullptr);c->CopyResource(staging.p,color.p);D3D11_MAPPED_SUBRESOURCE m{};
        checked(c->Map(staging.p,0,D3D11_MAP_READ,0,&m),"Read native parity pixels");Pixels p(side*side*4);
        for(unsigned y=0;y<side;++y)std::memcpy(p.data()+y*side*4,static_cast<uint8_t*>(m.pData)+y*m.RowPitch,side*4);c->Unmap(staging.p,0);return p;}
};
void compare(const char* label,const Pixels& gl,const Pixels& native){
    unsigned mismatches=0,first=0;for(unsigned i=0;i<side*side;++i)if(std::memcmp(gl.data()+i*4,native.data()+i*4,4)){if(!mismatches)first=i;++mismatches;}
    if(mismatches){const auto* a=gl.data()+first*4;const auto* b=native.data()+first*4;char message[256];
        std::snprintf(message,sizeof(message),"%s: %u differing pixels, first(%u,%u) GL=%u,%u,%u,%u D3D=%u,%u,%u,%u",label,mismatches,first%side,first/side,a[0],a[1],a[2],a[3],b[0],b[1],b[2],b[3]);throw std::runtime_error(message);}
    unsigned lit=0;for(unsigned i=0;i<side*side;++i)if(gl[i*4] || gl[i*4+1] || gl[i*4+2])++lit;
    std::printf("PASS exact raw-row parity %s (%u nonblack pixels)\n",label,lit);
}
}
int main()try{
    Reference gl;Native native;
    const auto rect=rectangle(-.75f,-.5f,.25f,.75f,-.5f,51,102,204);
    auto run=[&](const char* label,const State& state,const auto& first,const auto* second){
        gl.begin(state);gl.draw(first);if(second)gl.draw(*second);const auto expected=gl.read();
        native.begin(state);native.draw(first);if(second)native.draw(*second);compare(label,expected,native.read());};
    const std::array<gx::GpuVertex,6>* none=nullptr;State s;
    run("asymmetric GX desktop geometry",s,rect,none);s.scissor=true;run("asymmetric scissor",s,rect,none);
    s.scissor=false;s.vp_a[0]=.25f;s.vp_a[1]=.5f;s.vp_a[2]=-.125f;s.vp_a[3]=.5f;run("GX viewport transform",s,rect,none);
    s={};s.cull=1;run("back-face culling",s,rect,none);s.cull=2;run("front-face culling",s,rect,none);
    s={};s.depth=true;const auto nearer=rectangle(-.5f,-.75f,.5f,.25f,-.75f,204,51,102);run("GX depth overlap",s,rect,&nearer);
    const auto farther=rectangle(-.5f,-.75f,.5f,.25f,-.25f,204,51,102);run("GX depth rejection",s,rect,&farther);
    s={};const auto behind=rectangle(-.75f,-.5f,.25f,.75f,.25f,51,102,204);run("GX pre-viewport near clipping",s,behind,none);
    const auto beyond=rectangle(-.75f,-.5f,.25f,.75f,-1.25f,51,102,204);run("GX pre-viewport far clipping",s,beyond,none);
    s.vp_b[0]=3;s.vp_b[1]=2;run("viewport depth clamping",s,rect,none);
    Reference copy_reference(640,528);Window copy_window;desktop_d3d11::Device copy_device;gx::D3D11Renderer renderer;std::string error;
    require(copy_device.initialize(copy_window.handle,64,64,error),error);require(renderer.initialize(copy_device,1,error),error);
    unsigned copies=0,writebacks=0;gx::EfbCopyCmd command{};command.src_x=101;command.src_y=157;command.src_w=77;command.src_h=53;command.dst_w=13;command.dst_h=7;command.tex_id=97;
    for(uint32_t rgba:{0x3366ccffu,0x7f80fe40u,0x01020304u,0xfefdfcfbu})for(float z:{0.f,.1234567f,.5f,.99999994f,1.f}){
        copy_reference.clear_raw(rgba,z);renderer.clear(rgba,z);
        for(bool depth:{false,true})for(unsigned mode=0;mode<32;++mode){command.depth=depth;command.format=mode;
            const auto expected=copy_reference.copy(command);require(renderer.copy(command,nullptr,error),error);Pixels actual;require(renderer.read_texture(command.tex_id,actual,error),error);
            require(actual==expected,"GX copy pixel mismatch: color="+std::to_string(rgba)+" depth="+std::to_string(depth)+" z="+std::to_string(z)+" mode="+std::to_string(mode));++copies;
            Pixels encoded;if(gx::encode_copy(command,1,expected,encoded)){Pixels native_bytes;require(renderer.copy(command,&native_bytes,error),error);require(native_bytes==encoded,"GX CPU copy bytes differ across renderers");++writebacks;}}
    }
    std::printf("PASS %u exact GL/native GX copy comparisons and %u tiled CPU writebacks (RGBA and 32-bit depth boundaries)\n",copies,writebacks);
    auto primitive_batch=[&](unsigned prim,unsigned size,std::span<const gx::GpuVertex> vertices){
        gx::Batch batch;batch.states.resize(1);auto& st=batch.states[0];std::memset(&st,0,sizeof(st));
        const float proj[]={1,0,1,0,1,0,1},vp[]={320,264,16777215,320,264,16777215};std::memcpy(st.proj,proj,sizeof(proj));std::memcpy(st.viewport,vp,sizeof(vp));
        st.bp[0x21]=(639<<12)|527;st.bp[0xf3]=0xff<<16;st.bp[0x41]=0x18;st.bp[0x43]=1;
        const auto key=color_key();st.bp[0xc0]=key.cenv[0];st.bp[0xc1]=key.aenv[0];st.bp[0xf6]=4;st.bp[0xf7]=14;st.bp[0x22]=size|(size<<8);
        batch.verts.assign(vertices.begin(),vertices.end());for(unsigned i=0;i<vertices.size();++i){batch.indices.push_back(i);const auto* bytes=reinterpret_cast<const uint8_t*>(&vertices[i]);batch.packed.insert(batch.packed.end(),bytes,bytes+20);}
        gx::Cmd draw{};draw.type=gx::CmdType::Draw;draw.prim=uint8_t(prim);draw.count=unsigned(vertices.size());draw.vcount=draw.count;batch.cmds={draw};return batch;};
    unsigned primitives=0;
    for(unsigned prim:{2u,1u})for(unsigned width:{6u,12u,15u,18u,30u,36u,60u}){
        std::array<gx::GpuVertex,2> v;std::memset(v.data(),0,sizeof(v));for(unsigned i=0;i<2;++i){v[i].pos[0]=.1234f+i*.3f;v[i].pos[1]=-.281f;v[i].pos[2]=-.5f;
            for(auto& c:v[i].col){c[0]=51;c[1]=102;c[2]=204;c[3]=255;}}
        const std::span<const gx::GpuVertex> vertices(v.data(),prim==2?1:2);auto batch=primitive_batch(prim,width,vertices);
        copy_reference.begin({});copy_reference.primitive(vertices,prim,float(width)/6);const auto expected=copy_reference.read();
        require(renderer.prepare(batch,error),error);renderer.clear(0x000000ff,1);require(renderer.draw(batch,batch.cmds[0],1,0,error),error);Pixels actual;require(renderer.read_color(actual,error),error);
        if(expected!=actual){unsigned count=0,first=0;for(unsigned i=0;i<640*528;++i)if(std::memcmp(expected.data()+i*4,actual.data()+i*4,4)){if(!count)first=i;++count;}
            for(const auto* pixels:std::array<const Pixels*,2>{&expected,&actual}){unsigned n=0,x0=640,y0=528,x1=0,y1=0;
                for(unsigned y=0;y<528;++y)for(unsigned x=0;x<640;++x)if((*pixels)[(y*640+x)*4]){++n;x0=std::min(x0,x);x1=std::max(x1,x);y0=std::min(y0,y);y1=std::max(y1,y);}
                std::printf("%s lit=%u box=%u,%u..%u,%u\n",pixels==&expected?"GL":"native",n,x0,y0,x1,y1);}
            throw std::runtime_error("GX primitive="+std::to_string(prim)+" width="+std::to_string(width)+"/6 has "+std::to_string(count)+" different pixels, first="+std::to_string(first%640)+","+std::to_string(first/640));}++primitives;
    }
    std::printf("PASS %u exact GL/native point and horizontal line raster comparisons\n",primitives);
    std::puts("No game, rollback, mixed-renderer online or frame-pacing acceptance claimed.");return 0;
}catch(const std::exception& failure){std::fprintf(stderr,"FAIL %s\n",failure.what());return 1;}
