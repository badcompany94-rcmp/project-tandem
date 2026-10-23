#include "diagnostic_log.h"
#include "desktop_stats.h"
#include "performance_stats.h"
#include "gx/gl.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

namespace {
uint64_t milliseconds() {
    return (uint64_t)std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}
PerformanceStats stats(milliseconds());
bool enabled=true,dirty=true;
GLuint program,vao,vbo,cache_texture,cache_fbo;
GLint size_uniform=-1;
GLsizei vertex_count=0;
int geometry_w=0,geometry_h=0;
std::vector<std::string> geometry_lines;
int cache_w,cache_h;
uint64_t drawn_at,network_read_at,sample_at,log_at;
double fps=-1;int buffer_ms=-1;
const char* network_path(){return std::getenv("GCN_NET_STATS");}

// Preserve the renderer's GL state, including the state behind its caches.
struct State {
    GLint read,draw,prog,vertex,array,texture,viewport[4],src_rgb,dst_rgb,src_a,dst_a,eq_rgb,eq_a;
    GLboolean blend,depth,cull,scissor,stencil,mask[4];
#ifndef GCN_GL_ES
    GLboolean logic;
#endif
    State() {
        glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING,&read);glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING,&draw);
        glGetIntegerv(GL_CURRENT_PROGRAM,&prog);glGetIntegerv(GL_VERTEX_ARRAY_BINDING,&vertex);
        glGetIntegerv(GL_ARRAY_BUFFER_BINDING,&array);glGetIntegerv(GL_TEXTURE_BINDING_2D,&texture);
        glGetIntegerv(GL_VIEWPORT,viewport);glGetBooleanv(GL_COLOR_WRITEMASK,mask);
        glGetIntegerv(GL_BLEND_SRC_RGB,&src_rgb);glGetIntegerv(GL_BLEND_DST_RGB,&dst_rgb);
        glGetIntegerv(GL_BLEND_SRC_ALPHA,&src_a);glGetIntegerv(GL_BLEND_DST_ALPHA,&dst_a);
        glGetIntegerv(GL_BLEND_EQUATION_RGB,&eq_rgb);glGetIntegerv(GL_BLEND_EQUATION_ALPHA,&eq_a);
        blend=glIsEnabled(GL_BLEND);depth=glIsEnabled(GL_DEPTH_TEST);cull=glIsEnabled(GL_CULL_FACE);
        scissor=glIsEnabled(GL_SCISSOR_TEST);stencil=glIsEnabled(GL_STENCIL_TEST);
#ifndef GCN_GL_ES
        logic=glIsEnabled(GL_COLOR_LOGIC_OP);glDisable(GL_COLOR_LOGIC_OP);
#endif
        glDisable(GL_SCISSOR_TEST);glDisable(GL_DEPTH_TEST);glDisable(GL_CULL_FACE);glDisable(GL_STENCIL_TEST);
        glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);
    }
    static void restore(GLenum flag,bool value){if(value)glEnable(flag);else glDisable(flag);}
    ~State() {
        glBindFramebuffer(GL_READ_FRAMEBUFFER,read);glBindFramebuffer(GL_DRAW_FRAMEBUFFER,draw);
        glUseProgram(prog);glBindVertexArray(vertex);glBindBuffer(GL_ARRAY_BUFFER,array);glBindTexture(GL_TEXTURE_2D,texture);
        glViewport(viewport[0],viewport[1],viewport[2],viewport[3]);glColorMask(mask[0],mask[1],mask[2],mask[3]);
        glBlendFuncSeparate(src_rgb,dst_rgb,src_a,dst_a);glBlendEquationSeparate(eq_rgb,eq_a);
        restore(GL_BLEND,blend);restore(GL_DEPTH_TEST,depth);restore(GL_CULL_FACE,cull);
        restore(GL_SCISSOR_TEST,scissor);restore(GL_STENCIL_TEST,stencil);
#ifndef GCN_GL_ES
        restore(GL_COLOR_LOGIC_OP,logic);
#endif
    }
};
GLuint shader(GLenum kind,const char* source) {
    GLuint value=glCreateShader(kind);glShaderSource(value,1,&source,nullptr);glCompileShader(value);
    GLint ok=0;glGetShaderiv(value,GL_COMPILE_STATUS,&ok);
    if(!ok){char log[512]{};glGetShaderInfoLog(value,sizeof(log),nullptr,log);diagnostic_log("[stats] shader: %s\n",log);glDeleteShader(value);return 0;}
    return value;
}
bool initialize() {
    if(program)return true;
    const auto vs=shader(GL_VERTEX_SHADER,GCN_GLSL_VERSION
        "layout(location=0) in vec2 p; layout(location=1) in vec4 c; uniform vec2 size; out vec4 colour;"
        "void main(){gl_Position=vec4(p.x/size.x*2.0-1.0,1.0-p.y/size.y*2.0,0,1);colour=c;}");
    const auto fs=shader(GL_FRAGMENT_SHADER,GCN_GLSL_VERSION
        "in vec4 colour; out vec4 pixel; void main(){pixel=colour;}");
    if(!vs || !fs){if(vs)glDeleteShader(vs);if(fs)glDeleteShader(fs);return false;}
    program=glCreateProgram();glAttachShader(program,vs);glAttachShader(program,fs);glLinkProgram(program);
    glDeleteShader(vs);glDeleteShader(fs);GLint ok=0;glGetProgramiv(program,GL_LINK_STATUS,&ok);
    if(!ok){glDeleteProgram(program);program=0;return false;}
    size_uniform=glGetUniformLocation(program,"size");
    glGenVertexArrays(1,&vao);glGenBuffers(1,&vbo);glBindVertexArray(vao);glBindBuffer(GL_ARRAY_BUFFER,vbo);
    glEnableVertexAttribArray(0);glVertexAttribPointer(0,2,GL_FLOAT,GL_FALSE,6*sizeof(float),nullptr);
    glEnableVertexAttribArray(1);glVertexAttribPointer(1,4,GL_FLOAT,GL_FALSE,6*sizeof(float),(void*)(2*sizeof(float)));
    return true;
}
// Original 5x7 diagnostic glyphs; no font or texture assets are required.
std::array<unsigned char,7> glyph(char c) {
    switch(c) {
    case '0':return {14,17,19,21,25,17,14};case '1':return {4,12,4,4,4,4,14};
    case '2':return {14,17,1,2,4,8,31};case '3':return {30,1,1,14,1,1,30};
    case '4':return {2,6,10,18,31,2,2};case '5':return {31,16,16,30,1,1,30};
    case '6':return {14,16,16,30,17,17,14};case '7':return {31,1,2,4,8,8,8};
    case '8':return {14,17,17,14,17,17,14};case '9':return {14,17,17,15,1,1,14};
    case 'B':return {30,17,17,30,17,17,30};case 'E':return {31,16,16,30,16,16,31};
    case 'F':return {31,16,16,30,16,16,16};case 'I':return {14,4,4,4,4,4,14};
    case 'L':return {16,16,16,16,16,16,31};case 'M':return {17,27,21,21,17,17,17};
    case 'N':return {17,25,21,19,17,17,17};case 'O':return {14,17,17,17,17,17,14};
    case 'P':return {30,17,17,30,16,16,16};case 'R':return {30,17,17,30,20,18,17};
    case 'S':return {15,16,16,14,1,1,30};case 'T':return {31,4,4,4,4,4,4};
    case 'U':return {17,17,17,17,17,17,14};case '-':return {0,0,0,31,0,0,0};
    case '.':return {0,0,0,0,0,12,12};default:return {};
    }
}
void quad(std::vector<float>& vertices,float x,float y,float w,float h,const std::array<float,4>& colour) {
    const float points[12]={x,y,x+w,y,x,y+h,x,y+h,x+w,y,x+w,y+h};
    for(int i=0;i<6;++i){vertices.push_back(points[i*2]);vertices.push_back(points[i*2+1]);vertices.insert(vertices.end(),colour.begin(),colour.end());}
}
void text(std::vector<float>& vertices,const std::string& value,int x,int y,int scale,const std::array<float,4>& colour) {
    for(char c:value){const auto rows=glyph(c);for(int row=0;row<7;++row)for(int col=0;col<5;++col)
        if(rows[row]&(1<<(4-col)))quad(vertices,x+col*scale,y+row*scale,scale,scale,colour);x+=6*scale;}
}
bool number(const std::string& json,const char* key,double& value) {
    const auto at=json.find(std::string("\"")+key+"\"");if(at==std::string::npos)return false;
    const auto colon=json.find(':',at);if(colon==std::string::npos)return false;
    const char* start=json.c_str()+colon+1;char* end=nullptr;value=std::strtod(start,&end);
    return end!=start && std::isfinite(value);
}
void read_network(uint64_t now) {
    const char* path=network_path();if(!path || !*path || now<network_read_at+500)return;
    network_read_at=now;std::ifstream input(path,std::ios::binary);char bytes[1025]{};input.read(bytes,1024);
    if(input.bad() || input.gcount()==1024)return;
    const std::string json(bytes,(size_t)input.gcount());double format,sequence,rtt,age,buffer;
    if(number(json,"format",format) && format==1 && number(json,"sequence",sequence) && sequence>=1 && sequence<9e15 &&
       number(json,"rtt_ms",rtt) && number(json,"rtt_age_ms",age) && age>=0 && age<9e15 &&
       number(json,"buffer_ms",buffer) && buffer>=0 && buffer<=1000) {
        stats.network((uint64_t)sequence,rtt,(uint64_t)age,now);buffer_ms=(int)buffer;
    }
}
bool draw(int width,int height,bool fresh,bool host_ui=false) {
    const auto now=milliseconds();
    if(fresh)stats.frame(now);
    if(width<=0 || height<=0 || (!fresh && (!cache_fbo || (!host_ui && !enabled && !dirty) || (!dirty && now<drawn_at+(host_ui?16:100)))))return false;
    if(!fresh && !cache_fbo)return false;
    State saved;
    if(fresh) {
        if(!cache_texture){glGenTextures(1,&cache_texture);glGenFramebuffers(1,&cache_fbo);}
        if(cache_w!=width || cache_h!=height) {
            cache_w=width;cache_h=height;glBindTexture(GL_TEXTURE_2D,cache_texture);
            glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,width,height,0,GL_RGBA,GL_UNSIGNED_BYTE,nullptr);
            glBindFramebuffer(GL_DRAW_FRAMEBUFFER,cache_fbo);
            glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,cache_texture,0);
        }
        glBindFramebuffer(GL_READ_FRAMEBUFFER,0);glBindFramebuffer(GL_DRAW_FRAMEBUFFER,cache_fbo);
        glBlitFramebuffer(0,0,width,height,0,0,width,height,GL_COLOR_BUFFER_BIT,GL_NEAREST);
    } else {
        glBindFramebuffer(GL_READ_FRAMEBUFFER,cache_fbo);glBindFramebuffer(GL_DRAW_FRAMEBUFFER,0);
        glBlitFramebuffer(0,0,cache_w,cache_h,0,0,width,height,GL_COLOR_BUFFER_BIT,GL_NEAREST);
    }
    drawn_at=now;dirty=false;
    if(!enabled)return true;
    if(!initialize()){enabled=false;diagnostic_log("[stats] overlay unavailable\n");return true;}
    read_network(now);
    if(const int effective=network_input_delay_override.load();effective>=0)buffer_ms=effective;
    if(now>=sample_at+500){fps=stats.fps(now);sample_at=now;}
    char label[64];std::vector<std::string> lines;
    if(fps<0)lines.emplace_back("FPS --");else {snprintf(label,sizeof(label),"FPS %.1f",fps);lines.emplace_back(label);}
    const double rtt=stats.rtt(now);const bool online=network_path() && *network_path();
    if(!online)lines.emplace_back("OFFLINE");
    else {
        if(rtt<0)lines.emplace_back("RTT --");else {snprintf(label,sizeof(label),"RTT %.0f MS",rtt);lines.emplace_back(label);}
        if(buffer_ms>=0){snprintf(label,sizeof(label),"BUF %d MS",buffer_ms);lines.emplace_back(label);}
    }
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER,0);glViewport(0,0,width,height);
    glUseProgram(program);
    glBindVertexArray(vao);glBindBuffer(GL_ARRAY_BUFFER,vbo);
    // Values normally change twice a second, while the overlay is drawn for
    // every frame. Keep the immutable glyph geometry in its own GL buffer
    // until its text or drawable size changes; avoid per-frame allocations
    // and driver buffer replacement in the presentation path.
    if(width!=geometry_w || height!=geometry_h || lines!=geometry_lines) {
        const int scale=std::clamp(height/540,1,3),padding=4*scale,gap=2*scale;
        size_t longest=0;for(const auto& line:lines)longest=std::max(longest,line.size());
        const int box_w=(int)longest*6*scale+padding*2,box_h=(int)lines.size()*(7*scale+gap)-gap+padding*2;
        const int x=std::max(0,width-box_w-4*scale),y=4*scale;
        std::vector<float> vertices;vertices.reserve(24000);
        quad(vertices,x,y,box_w,box_h,{0,0,0,.48f});
        for(size_t i=0;i<lines.size();++i)text(vertices,lines[i],x+padding,y+padding+(int)i*(7*scale+gap),scale,
            i==0?std::array<float,4>{.55f,1,.35f,1}:std::array<float,4>{.85f,.9f,.82f,1});
        glBufferData(GL_ARRAY_BUFFER,(GLsizeiptr)(vertices.size()*sizeof(float)),vertices.data(),GL_STREAM_DRAW);
        glUniform2f(size_uniform,(float)width,(float)height);vertex_count=(GLsizei)(vertices.size()/6);
        geometry_w=width;geometry_h=height;geometry_lines=std::move(lines);
    }
    glEnable(GL_BLEND);glBlendEquationSeparate(GL_FUNC_ADD,GL_FUNC_ADD);
    glBlendFuncSeparate(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA,GL_ONE,GL_ONE_MINUS_SRC_ALPHA);
    glDrawArrays(GL_TRIANGLES,0,vertex_count);
    if(now>=log_at+2000){diagnostic_log("[desktop-stats] fps=%.1f rtt_ms=%.1f buffer_ms=%d\n",fps,online?rtt:-1,buffer_ms);log_at=now;}
    return true;
}
}
void desktop_stats_enable(bool value){enabled=value;dirty=true;}
void desktop_stats_toggle(){desktop_stats_enable(!enabled);}
void desktop_stats_present(int width,int height){draw(width,height,true);}
bool desktop_stats_idle(int width,int height,bool host_ui){return draw(width,height,false,host_ui);}
void desktop_stats_shutdown() {
    if(program)glDeleteProgram(program);if(vao)glDeleteVertexArrays(1,&vao);if(vbo)glDeleteBuffers(1,&vbo);
    if(cache_texture)glDeleteTextures(1,&cache_texture);if(cache_fbo)glDeleteFramebuffers(1,&cache_fbo);
    program=vao=vbo=cache_texture=cache_fbo=0;
    size_uniform=-1;vertex_count=geometry_w=geometry_h=0;geometry_lines.clear();
}
