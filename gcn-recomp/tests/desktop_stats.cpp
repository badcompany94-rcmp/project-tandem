#define SDL_MAIN_HANDLED
#include <SDL.h>
#include "desktop_stats.h"
#include "gx/gl.h"
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <stdexcept>
#include <vector>
bool write_png(const char*,const uint8_t*,int,int);
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
int main(int argc,char** argv) {
    try {
        require(argc==3,"metrics JSON and PNG paths required");
        {std::ofstream file(argv[1]);file<<"{\"format\":1,\"sequence\":1,\"rtt_ms\":40,\"rtt_age_ms\":0,\"buffer_ms\":200}";}
#ifdef _WIN32
        _putenv_s("GCN_NET_STATS",argv[1]);
#else
        setenv("GCN_NET_STATS",argv[1],1);
#endif
        require(SDL_Init(SDL_INIT_VIDEO)==0,SDL_GetError());
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION,3);SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION,3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,SDL_GL_CONTEXT_PROFILE_CORE);
        auto* window=SDL_CreateWindow("stats fixture",0,0,640,360,SDL_WINDOW_OPENGL|SDL_WINDOW_HIDDEN);
        require(window!=nullptr,SDL_GetError());auto context=SDL_GL_CreateContext(window);
        require(context!=nullptr,SDL_GetError());require(gl_load_with(SDL_GL_GetProcAddress)>=33,"GL 3.3 required");
        glBindFramebuffer(GL_FRAMEBUFFER,0);glViewport(0,0,640,360);
        glClearColor(.1f,.2f,.4f,1);glClear(GL_COLOR_BUFFER_BIT);
        glViewport(3,5,300,170);glEnable(GL_DEPTH_TEST);glEnable(GL_CULL_FACE);glEnable(GL_SCISSOR_TEST);
        glScissor(7,9,100,80);glColorMask(GL_FALSE,GL_TRUE,GL_FALSE,GL_TRUE);
        SDL_Delay(550);desktop_stats_present(640,360);
        GLint viewport[4];GLboolean mask[4];glGetIntegerv(GL_VIEWPORT,viewport);glGetBooleanv(GL_COLOR_WRITEMASK,mask);
        require(viewport[0]==3 && viewport[1]==5 && viewport[2]==300 && viewport[3]==170,"viewport restored");
        require(glIsEnabled(GL_DEPTH_TEST) && glIsEnabled(GL_CULL_FACE) && glIsEnabled(GL_SCISSOR_TEST),"enables restored");
        require(!mask[0] && mask[1] && !mask[2] && mask[3],"colour mask restored");
        require(glGetError()==GL_NO_ERROR,"overlay must not produce GL errors");
        // A second fresh game frame with identical labels reuses the glyph
        // buffer while still refreshing the clean idle-background cache.
        glDisable(GL_SCISSOR_TEST);glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);glClear(GL_COLOR_BUFFER_BIT);
        glEnable(GL_SCISSOR_TEST);glColorMask(GL_FALSE,GL_TRUE,GL_FALSE,GL_TRUE);
        desktop_stats_present(640,360);require(glGetError()==GL_NO_ERROR,"cached glyph draw must preserve a valid GL state");
        std::vector<uint8_t> pixels(640*360*4),flipped(pixels.size());glReadPixels(0,0,640,360,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());
        const auto at=[&](int x,int y,int channel){return pixels[(y*640+x)*4+channel];};
        require(at(10,100,0)>=24 && at(10,100,0)<=27 && at(10,100,2)>=100,"pixels outside overlay unchanged");
        unsigned green=0;for(int y=310;y<360;++y)for(int x=500;x<640;++x)if(at(x,y,1)>200 && at(x,y,0)>80 && at(x,y,0)<180)++green;
        require(green>10,"FPS glyphs visible at top right");
        for(int y=0;y<360;++y)for(int x=0;x<640*4;++x)flipped[y*640*4+x]=pixels[(359-y)*640*4+x];
        require(write_png(argv[2],flipped.data(),640,360),"fixture capture saved");
        SDL_GL_SwapWindow(window);SDL_Delay(1100);
        require(desktop_stats_idle(640,360),"counter refreshes while no game frames arrive");
        glReadPixels(0,0,640,360,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());
        for(int y=0;y<360;++y)for(int x=0;x<640*4;++x)flipped[y*640*4+x]=pixels[(359-y)*640*4+x];
        require(write_png((std::string(argv[2])+".idle.png").c_str(),flipped.data(),640,360),"idle capture saved");
        desktop_stats_toggle();require(desktop_stats_idle(640,360),"hide applies during a game stall");
        glReadPixels(0,0,640,360,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());
        require(at(620,345,0)>=24 && at(620,345,0)<=27 && at(620,345,2)>=100,"hiding restores clean cached game frame");
        desktop_stats_shutdown();require(glGetError()==GL_NO_ERROR,"overlay teardown has no GL errors");
        SDL_GL_DeleteContext(context);SDL_DestroyWindow(window);SDL_Quit();
        puts("PASS: actual GL overlay, top-right pixels, GL state preservation, idle refresh, hide and cleanup");
    }catch(const std::exception& error){fprintf(stderr,"FAIL: %s\n",error.what());return 1;}
}
