#include "desktop_pointer.h"
#include "input_defaults.h"
#include <SDL.h>
#include <algorithm>
#include <cstdlib>
#include <vector>
#include <sstream>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <wincodec.h>
#endif
namespace desktop_pointer {
namespace {
SDL_Cursor *idle=nullptr,*hover=nullptr,*tap=nullptr;
bool loaded=false,mouse_active=true;
Uint64 clicked_at=0;
unsigned last_generation=0;
bool held=false;
void load() {
    if(loaded)return;loaded=true;
    if(input_default("desktop_custom_cursor",0)==0)return;
#ifdef _WIN32
    const auto initialized=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    IWICImagingFactory* factory=nullptr;IWICBitmapDecoder* decoder=nullptr;
    IWICBitmapFrameDecode* frame=nullptr;IWICBitmapScaler* scaler=nullptr;IWICFormatConverter* converter=nullptr;
    std::vector<unsigned char> pixels(64*64*4);
    const char* override=SDL_getenv("GCN_CURSOR_ASSET");
    const std::string path=override?override:"assets/menu-cursor.png";
    const int length=MultiByteToWideChar(CP_UTF8,0,path.c_str(),-1,nullptr,0);
    std::vector<wchar_t> wide(length);MultiByteToWideChar(CP_UTF8,0,path.c_str(),-1,wide.data(),length);
    bool ok=SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory))) &&
        SUCCEEDED(factory->CreateDecoderFromFilename(wide.data(),nullptr,GENERIC_READ,WICDecodeMetadataCacheOnLoad,&decoder)) &&
        SUCCEEDED(decoder->GetFrame(0,&frame)) && SUCCEEDED(factory->CreateBitmapScaler(&scaler)) &&
        SUCCEEDED(scaler->Initialize(frame,64,64,WICBitmapInterpolationModeFant)) &&
        SUCCEEDED(factory->CreateFormatConverter(&converter)) &&
        SUCCEEDED(converter->Initialize(scaler,GUID_WICPixelFormat32bppRGBA,WICBitmapDitherTypeNone,nullptr,0,WICBitmapPaletteTypeCustom)) &&
        SUCCEEDED(converter->CopyPixels(nullptr,64*4,(UINT)pixels.size(),pixels.data()));
    if(converter)converter->Release();if(scaler)scaler->Release();if(frame)frame->Release();if(decoder)decoder->Release();if(factory)factory->Release();
    if(SUCCEEDED(initialized))CoUninitialize();
    if(ok) {
        // The asset's upper-left fingertip is at 12,3 in the 64px cursor.
        auto make=[](std::vector<unsigned char>& rgba,int x,int y){auto* surface=SDL_CreateRGBSurfaceWithFormatFrom(rgba.data(),64,64,32,256,SDL_PIXELFORMAT_RGBA32);if(!surface)return (SDL_Cursor*)nullptr;auto* result=SDL_CreateColorCursor(surface,x,y);SDL_FreeSurface(surface);return result;};
        idle=make(pixels,12,3);tap=make(pixels,10,1);
        hover=make(pixels,12,3);
    }
#endif
}
void position(SDL_Window* window,int x,int y) {
    int w,h;SDL_GetWindowSize(window,&w,&h);if(w<=0 || h<=0)return;
    const float width=std::min(float(w),h*aspect),height=width/aspect;
    state.x=(x-(w-width)*.5f)/width;state.y=(y-(h-height)*.5f)/height;state.hover=-1;
    for(const auto& area:regions)if(state.x>=area.x && state.x<area.x+area.w && state.y>=area.y && state.y<area.y+area.h)state.hover=area.action;
}
}
void event(const SDL_Event& e,bool blocked) {
    if(generation.load()!=last_generation){last_generation=generation;held=e.type!=SDL_MOUSEBUTTONDOWN && SDL_GetMouseState(nullptr,nullptr)!=0;}
    if(e.type==SDL_MOUSEBUTTONUP)held=false;
    if(e.type==SDL_CONTROLLERBUTTONDOWN || e.type==SDL_KEYDOWN || (e.type==SDL_CONTROLLERAXISMOTION && std::abs(e.caxis.value)>12000)){mouse_active=false;std::lock_guard lock(mutex);state.hover=-1;}
    if(e.type==SDL_MOUSEMOTION || e.type==SDL_MOUSEBUTTONDOWN)mouse_active=true;
    if(blocked || !menu_enabled)return;
    if(e.type==SDL_KEYDOWN && !e.key.repeat && e.key.keysym.scancode==SDL_SCANCODE_ESCAPE && input_default("desktop_escape_back",0)!=0){std::lock_guard lock(mutex);clicked=-2;}
    if(text_enabled){std::lock_guard lock(mutex);
        if(e.type==SDL_TEXTINPUT && typed.size()<128)typed+=e.text.text;
        if(e.type==SDL_KEYDOWN && (e.key.keysym.sym==SDLK_BACKSPACE || e.key.keysym.sym==SDLK_RETURN) && typed.size()<128)typed+=e.key.keysym.sym==SDLK_BACKSPACE?'\b':'\r';
    }
    if(e.type==SDL_MOUSEMOTION || e.type==SDL_MOUSEBUTTONDOWN) {
        auto* window=SDL_GetWindowFromID(e.type==SDL_MOUSEMOTION?e.motion.windowID:e.button.windowID);if(!window)return;
        std::lock_guard lock(mutex);position(window,e.type==SDL_MOUSEMOTION?e.motion.x:e.button.x,e.type==SDL_MOUSEMOTION?e.motion.y:e.button.y);
        if(e.type==SDL_MOUSEBUTTONDOWN && !held){held=true;clicked_at=SDL_GetTicks64();if(e.button.button==SDL_BUTTON_LEFT && state.hover!=-1)clicked=state.hover;else if(e.button.button==SDL_BUTTON_RIGHT)clicked=-2;}
    }
}
void script(SDL_Window* window,uint32_t frame) {
    struct Command{uint32_t frame;std::string kind,value;float x=0,y=0;bool done=false;};
    static const char* spec=SDL_getenv("GCN_POINTER_SCRIPT");if(!spec || !window)return;
    static std::vector<Command> commands;
    static const bool parsed=[&]{
        std::istringstream input(spec);std::string line;
        while(std::getline(input,line,',')) {
            const auto first=line.find(':'),second=line.find(':',first==std::string::npos?0:first+1);
            if(first==std::string::npos)continue;
            Command command{};command.frame=(uint32_t)strtoul(line.c_str(),nullptr,10);
            command.kind=line.substr(first+1,second==std::string::npos?std::string::npos:second-first-1);
            if(second!=std::string::npos) {
                command.value=line.substr(second+1);const auto split=command.value.find(':');
                if(split!=std::string::npos){command.x=(float)atof(command.value.c_str());command.y=(float)atof(command.value.c_str()+split+1);}
            }
            commands.push_back(std::move(command));
        }
        return true;
    }();(void)parsed;
    for(auto& command:commands)if(!command.done && frame>=command.frame) {
        command.done=true;SDL_Event e{};const auto id=SDL_GetWindowID(window);
        if(command.kind=="text") {e.type=SDL_TEXTINPUT;e.text.windowID=id;SDL_strlcpy(e.text.text,command.value.c_str(),sizeof(e.text.text));SDL_PushEvent(&e);}
        else if(command.kind=="keydown" || command.kind=="keyup") {
            e.type=command.kind=="keydown"?SDL_KEYDOWN:SDL_KEYUP;e.key.windowID=id;e.key.keysym.scancode=SDL_GetScancodeFromName(command.value.c_str());e.key.keysym.sym=SDL_GetKeyFromScancode(e.key.keysym.scancode);SDL_PushEvent(&e);
        }else if(command.kind=="enter" || command.kind=="backspace" || command.kind=="escape") {
            e.type=SDL_KEYDOWN;e.key.windowID=id;e.key.keysym.sym=command.kind=="escape"?SDLK_ESCAPE:command.kind=="enter"?SDLK_RETURN:SDLK_BACKSPACE;e.key.keysym.scancode=command.kind=="escape"?SDL_SCANCODE_ESCAPE:command.kind=="enter"?SDL_SCANCODE_RETURN:SDL_SCANCODE_BACKSPACE;
            SDL_PushEvent(&e);e.type=SDL_KEYUP;SDL_PushEvent(&e);
        }else if(command.kind=="left" || command.kind=="right" || command.kind=="hover") {
            int w,h;SDL_GetWindowSize(window,&w,&h);float ratio;{std::lock_guard lock(mutex);ratio=aspect;}
            const float width=std::min(float(w),h*ratio),height=width/ratio;
            const int x=int((w-width)*.5f+command.x*width),y=int((h-height)*.5f+command.y*height);
            e.type=SDL_MOUSEMOTION;e.motion.windowID=id;e.motion.x=x;e.motion.y=y;SDL_PushEvent(&e);
            if(command.kind!="hover") {e.type=SDL_MOUSEBUTTONDOWN;e.button.windowID=id;e.button.button=command.kind=="left"?SDL_BUTTON_LEFT:SDL_BUTTON_RIGHT;e.button.x=x;e.button.y=y;SDL_PushEvent(&e);e.type=SDL_MOUSEBUTTONUP;SDL_PushEvent(&e);}
        }
        fprintf(stderr,"[input-pointer] frame %u action %s\n",frame,command.kind.c_str());
    }
}
void update(SDL_Window* window,bool settings_open) {
    if(input_default("desktop_custom_cursor",0)==0)return;
    if(text_enabled && !settings_open && !SDL_IsTextInputActive())SDL_StartTextInput();
    load();const bool visible=settings_open || (menu_enabled && mouse_active);
    if(!settings_open && menu_enabled){int x,y;SDL_GetMouseState(&x,&y);std::lock_guard lock(mutex);
        if(mouse_active && !SDL_getenv("GCN_POINTER_SCRIPT"))position(window,x,y);
        else if(mouse_active){state.hover=-1;for(const auto& area:regions)if(state.x>=area.x && state.x<area.x+area.w && state.y>=area.y && state.y<area.y+area.h)state.hover=area.action;}
        else state.hover=-1;}
    if(visible){auto* next=settings_open?SDL_GetDefaultCursor():SDL_GetTicks64()-clicked_at<110 && tap?tap:read().hover>=0 && hover?hover:idle?idle:SDL_GetDefaultCursor();SDL_SetCursor(next);}
    SDL_ShowCursor(visible?SDL_ENABLE:SDL_DISABLE);
}
void shutdown(){SDL_SetCursor(SDL_GetDefaultCursor());if(idle)SDL_FreeCursor(idle);if(hover)SDL_FreeCursor(hover);if(tap)SDL_FreeCursor(tap);idle=hover=tap=nullptr;loaded=false;}
}
