// Hidden real-GPU validation of reflected scalar arrays/matrices and uniform
// shadow uploads. Uses native Direct3D only, no game data or OpenGL context.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d11.h>
#include "graphics/d3d11_device.h"
#include "graphics/d3d11_program.h"
#include "gx/shadergen.h"
#include <array>
#include <cstdio>
#include <cstring>
#include <stdexcept>
namespace {
template<class T>struct Com {T* p=nullptr;~Com(){if(p)p->Release();}T* operator->()const{return p;}T** out(){return &p;}};
void require(bool ok,const std::string& error){if(!ok)throw std::runtime_error(error);}
void checked(HRESULT code,const char* error){require(SUCCEEDED(code),error);}
struct Window {
    HWND handle=nullptr;
    Window(){WNDCLASSW c{};c.lpfnWndProc=DefWindowProcW;c.hInstance=GetModuleHandleW(nullptr);c.lpszClassName=L"TandemNativeProgramProbe";
        require(RegisterClassW(&c)!=0,"Register hidden window");
        handle=CreateWindowExW(0,c.lpszClassName,L"Tandem hidden native program",WS_OVERLAPPEDWINDOW,0,0,128,128,nullptr,nullptr,c.hInstance,nullptr);
        require(handle!=nullptr,"Create hidden window");}
    ~Window(){if(handle)DestroyWindow(handle);UnregisterClassW(L"TandemNativeProgramProbe",GetModuleHandleW(nullptr));}
};
}
int main()try{
    Window window;desktop_d3d11::Device graphics;std::string error;
    require(graphics.initialize(window.handle,64,64,error),error);
    auto* d=graphics.device();auto* c=graphics.context();
    const std::string vs="#version 330 core\nlayout(location=0) in vec2 position;void main(){gl_Position=vec4(position,0,1);}\n";
    const std::string ps="#version 330 core\nuniform float values[2];uniform mat4 transform;out vec4 color;void main(){color=vec4(values[0],values[1],transform[3][0],1);}\n";
    desktop_d3d11::CompiledProgram compiled;require(desktop_d3d11::compile_program(vs,ps,compiled,error),error);
    desktop_d3d11::Program program;require(program.initialize(d,std::move(compiled),error),error);
    const auto values=program.uniform("values"),matrix=program.uniform("transform");require(values>=0 && matrix>=0,"Lost uniform names");
    std::array<float,2> input={.2f,.4f};std::array<float,16> transform{};
    transform[0]=transform[5]=transform[10]=transform[15]=1;transform[12]=.8f;
    require(program.floats(values,input,error) && program.floats(matrix,transform,error),error);
    D3D11_TEXTURE2D_DESC texture{};texture.Width=texture.Height=64;texture.MipLevels=texture.ArraySize=1;
    texture.Format=DXGI_FORMAT_R8G8B8A8_UNORM;texture.SampleDesc.Count=1;texture.BindFlags=D3D11_BIND_RENDER_TARGET;
    Com<ID3D11Texture2D> color,staging;Com<ID3D11RenderTargetView> target;
    checked(d->CreateTexture2D(&texture,nullptr,color.out()),"Create native target");
    checked(d->CreateRenderTargetView(color.p,nullptr,target.out()),"Create native view");
    texture.BindFlags=0;texture.Usage=D3D11_USAGE_STAGING;texture.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
    checked(d->CreateTexture2D(&texture,nullptr,staging.out()),"Create native staging texture");
    const float vertices[]={-1,-1,-1,3,3,-1};
    D3D11_BUFFER_DESC buffer{};buffer.ByteWidth=sizeof(vertices);buffer.Usage=D3D11_USAGE_IMMUTABLE;buffer.BindFlags=D3D11_BIND_VERTEX_BUFFER;
    D3D11_SUBRESOURCE_DATA data{};data.pSysMem=vertices;Com<ID3D11Buffer> geometry;
    checked(d->CreateBuffer(&buffer,&data,geometry.out()),"Create native geometry");
    D3D11_INPUT_ELEMENT_DESC attribute={"TEXCOORD",0,DXGI_FORMAT_R32G32_FLOAT,0,0,D3D11_INPUT_PER_VERTEX_DATA,0};
    Com<ID3D11InputLayout> layout;const auto code=program.vertex_code();
    checked(d->CreateInputLayout(&attribute,1,code.data(),code.size(),layout.out()),"Create native input layout");
    D3D11_RASTERIZER_DESC raster{};raster.FillMode=D3D11_FILL_SOLID;raster.CullMode=D3D11_CULL_NONE;raster.DepthClipEnable=TRUE;
    Com<ID3D11RasterizerState> rs;checked(d->CreateRasterizerState(&raster,rs.out()),"Create native raster state");
    c->RSSetState(rs.p);D3D11_VIEWPORT viewport{};viewport.Width=viewport.Height=64;viewport.MaxDepth=1;c->RSSetViewports(1,&viewport);
    UINT stride=2*sizeof(float),offset=0;c->IASetInputLayout(layout.p);c->IASetVertexBuffers(0,1,&geometry.p,&stride,&offset);
    c->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    auto* active=&program;
    auto draw=[&](unsigned r,unsigned g,unsigned b){
        require(active->bind(c,error),error);c->OMSetRenderTargets(1,&target.p,nullptr);c->Draw(3,0);
        c->OMSetRenderTargets(0,nullptr,nullptr);c->CopyResource(staging.p,color.p);D3D11_MAPPED_SUBRESOURCE mapped{};
        checked(c->Map(staging.p,0,D3D11_MAP_READ,0,&mapped),"Read native program pixels");
        bool correct=true;
        for(unsigned y=0;y<64;++y)for(unsigned x=0;x<64;++x){
            const auto* pixel=static_cast<const unsigned char*>(mapped.pData)+y*mapped.RowPitch+x*4;
            correct=correct && pixel[0]==r && pixel[1]==g && pixel[2]==b && pixel[3]==255;
        }
        c->Unmap(staging.p,0);require(correct,"Reflected native uniform values produced incorrect GPU pixels");
    };
    draw(51,102,204);require(program.uniform_uploads()==1,"First native constants not uploaded exactly once");
    require(program.floats(values,input,error) && program.floats(matrix,transform,error),error);draw(51,102,204);
    require(program.uniform_uploads()==1,"Unchanged native constants uploaded again");
    input={.4f,.2f};require(program.floats(values,input,error),error);draw(102,51,204);
    require(program.uniform_uploads()==2,"Changed native constants not uploaded");
    const std::array<float,3> wrong_count={0,0,0};const std::array<int32_t,2> wrong_type={0,0};
    require(!program.floats(values,wrong_count,error) && !program.integers(values,wrong_type,error),"Invalid native uniform updates accepted");
    require(!program.initialize(nullptr,{},error),"Invalid replacement program accepted");draw(102,51,204);
    require(program.uniform_uploads()==2,"Rejected native update changed a working program");
    // Exercise the actual GX integer TEV pipeline and packed console vertex
    // colors, rather than only a synthetic native HLSL/color program.
    gx::ShaderKey key{};key.alpha_func=0xff;key.efb_has_alpha=1;
    key.cenv[0]=(15u<<12)|(15u<<8)|(15u<<4)|10u|(1u<<19); // output raster RGB
    key.aenv[0]=(7u<<13)|(7u<<10)|(7u<<7)|(5u<<4)|(1u<<19); // raster alpha
    key.ksel[0]=4;key.ksel[1]=14; // identity RGBA swap table
    require(desktop_d3d11::compile_gx_program(key,compiled,error),error);
    desktop_d3d11::Program gx_program;require(gx_program.initialize(d,std::move(compiled),error),error);
    std::array<gx::GpuVertex,3> gx_vertices;std::memset(gx_vertices.data(),0,sizeof(gx_vertices));
    for(unsigned i=0;i<3;++i){
        gx_vertices[i].pos[0]=vertices[i*2];gx_vertices[i].pos[1]=vertices[i*2+1];gx_vertices[i].pos[2]=-.5f;
        for(auto& color:gx_vertices[i].col){color[0]=51;color[1]=102;color[2]=204;color[3]=255;}
    }
    buffer.ByteWidth=sizeof(gx_vertices);data.pSysMem=gx_vertices.data();Com<ID3D11Buffer> gx_geometry;
    checked(d->CreateBuffer(&buffer,&data,gx_geometry.out()),"Create GX native geometry");
    std::array<D3D11_INPUT_ELEMENT_DESC,11> attributes{};
    for(unsigned i=0;i<11;++i){auto& a=attributes[i];a.SemanticName="TEXCOORD";a.SemanticIndex=i;
        a.Format=(i==1 || i==2)?DXGI_FORMAT_R8G8B8A8_UNORM:DXGI_FORMAT_R32G32B32_FLOAT;
        a.AlignedByteOffset=i==0?0:i==1?12:i==2?16:20+(i-3)*12;a.InputSlotClass=D3D11_INPUT_PER_VERTEX_DATA;}
    Com<ID3D11InputLayout> gx_layout;const auto gx_code=gx_program.vertex_code();
    checked(d->CreateInputLayout(attributes.data(),UINT(attributes.size()),gx_code.data(),gx_code.size(),gx_layout.out()),"Create GX native input layout");
    stride=sizeof(gx::GpuVertex);c->IASetInputLayout(gx_layout.p);c->IASetVertexBuffers(0,1,&gx_geometry.p,&stride,&offset);
    transform[12]=0;const std::array<float,4> vp_a={0,1,0,1},vp_b={1,2,0,0};
    require(gx_program.floats(gx_program.uniform("u_proj"),transform,error) && gx_program.floats(gx_program.uniform("u_view"),transform,error) &&
        gx_program.floats(gx_program.uniform("u_crop"),transform,error) && gx_program.floats(gx_program.uniform("u_vp_a"),vp_a,error) &&
        gx_program.floats(gx_program.uniform("u_vp_b"),vp_b,error),error);
    active=&gx_program;
    for(int32_t mode:{0,1}){const std::array<int32_t,1> value={mode};require(gx_program.integers(gx_program.uniform("u_vr"),value,error),error);draw(51,102,204);}
    std::printf("PASS native GPU program on %s: std140 scalar-array/matrix layout, exact pixel output, shadow reuse and rejected-update preservation\n",graphics.info().adapter.c_str());
    std::puts("PASS actual GX TEV shader/packed vertex colors in desktop-flat and neutral view modes");
    std::puts("No game rendering, frame-pacing improvement or online compatibility acceptance claimed.");return 0;
}catch(const std::exception& failure){std::fprintf(stderr,"FAIL %s\n",failure.what());return 1;}
