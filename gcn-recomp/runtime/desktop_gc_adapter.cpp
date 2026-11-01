#include "desktop_gc_adapter.h"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <cwctype>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <thread>
#include <vector>
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <setupapi.h>
#include <winusb.h>
#endif

namespace gc_adapter {
Pads::Pads(int number) {
    for(int i=0;i<4;++i)slots[i].name="GameCube USB Adapter "+std::to_string(number)+" Port "+std::to_string(i+1);
}
Pads::~Pads(){clear();}
int SDLCALL Pads::rumble(void* userdata,Uint16 low,Uint16 high) {
    auto& slot=*static_cast<Slot*>(userdata);slot.motor=(low||high);return 0;
}
bool Pads::attach(Slot& slot) {
    SDL_VirtualJoystickDesc desc{};desc.version=SDL_VIRTUAL_JOYSTICK_DESC_VERSION;
    desc.type=SDL_JOYSTICK_TYPE_GAMECONTROLLER;desc.naxes=6;desc.nbuttons=15;
    desc.vendor_id=0x057e;desc.product_id=0x0337;desc.name=slot.name.c_str();
    desc.userdata=&slot;desc.Rumble=rumble;
    const int index=SDL_JoystickAttachVirtualEx(&desc);
    if(index<0)return false;
    slot.id=SDL_JoystickGetDeviceInstanceID(index);
    char guid[33];SDL_JoystickGetGUIDString(SDL_JoystickGetDeviceGUID(index),guid,sizeof(guid));
    const std::string mapping=std::string(guid)+","+slot.name+
        ",a:b0,b:b1,x:b2,y:b3,start:b6,rightshoulder:b10,dpup:b11,dpdown:b12,dpleft:b13,dpright:b14,"
        "leftx:a0,lefty:a1,rightx:a2,righty:a3,lefttrigger:a4,righttrigger:a5,";
    if(SDL_GameControllerAddMapping(mapping.c_str())<0){detach(slot);return false;}
    slot.joy=SDL_JoystickOpen(index);
    if(!slot.joy){detach(slot);return false;}
    fprintf(stderr,"[input] %s connected\n",slot.name.c_str());return true;
}
void Pads::detach(Slot& slot) {
    slot.motor=false;slot.power=false;
    if(slot.joy){SDL_JoystickClose(slot.joy);slot.joy=nullptr;}
    if(slot.id>=0)for(int i=0;i<SDL_NumJoysticks();++i)if(SDL_JoystickGetDeviceInstanceID(i)==slot.id){SDL_JoystickDetachVirtual(i);break;}
    slot.id=-1;
}
void Pads::clear(){for(auto& slot:slots)detach(slot);}
void Pads::update(const Report& report) {
    const int bits[]={0,1,2,3,-1,-1,8,-1,-1,-1,9,7,6,4,5};
    for(int i=0;i<4;++i) {
        auto& slot=slots[i];const auto& port=report[i];
        if(!port.connected){detach(slot);continue;}
        if(!slot.joy && !attach(slot))continue;
        slot.power=port.rumble_power;
        for(int b=0;b<15;++b)SDL_JoystickSetVirtualButton(slot.joy,b,bits[b]>=0 && (port.buttons&(1<<bits[b]))?1:0);
        for(int a=0;a<6;++a) {
            int value=a<4?std::clamp((int(port.axes[a])-128)*32767/100,-32768,32767):int(port.axes[a])*65535/255-32768;
            if(a==1 || a==3)value=std::clamp(-value,-32768,32767);
            SDL_JoystickSetVirtualAxis(slot.joy,a,Sint16(value));
        }
    }
}
std::array<uint8_t,5> Pads::rumble_packet() const {
    std::array<uint8_t,5> result{0x11,0,0,0,0};
    for(int i=0;i<4;++i)result[i+1]=slots[i].power && slots[i].motor?1:0;
    return result;
}
}

#ifdef _WIN32
namespace {
using Clock=std::chrono::steady_clock;
struct State {gc_adapter::Report report{};std::array<uint8_t,5> rumble{0x11,0,0,0,0};bool alive=false;Clock::time_point received{};};
std::mutex state_mutex;
std::map<std::wstring,State> states;
std::map<std::wstring,std::unique_ptr<gc_adapter::Pads>> pads;
std::atomic<bool> stopping{false};
std::thread worker;
int next_adapter=1;
// Standard USB interface and Zadig/libusb device interface. Access only the
// Nintendo-compatible 057e:0337 device, with its existing WinUSB driver.
const GUID interfaces[]={
    {0xa5dcbf10,0x6530,0x11d2,{0x90,0x1f,0x00,0xc0,0x4f,0xb9,0x51,0xed}},
    {0xdee824ef,0x729b,0x4a0e,{0x9c,0x14,0xb7,0x11,0x7d,0x33,0xa8,0x17}}};
std::set<std::wstring> enumerate() {
    std::set<std::wstring> result;
    std::set<std::wstring> seen;
    for(int g=1;g>=0;--g) {
        const auto& guid=interfaces[g];
        auto list=SetupDiGetClassDevsW(&guid,nullptr,nullptr,DIGCF_PRESENT|DIGCF_DEVICEINTERFACE);
        if(list==INVALID_HANDLE_VALUE)continue;
        for(DWORD i=0;;++i) {
            SP_DEVICE_INTERFACE_DATA data{};data.cbSize=sizeof(data);
            if(!SetupDiEnumDeviceInterfaces(list,nullptr,&guid,i,&data))break;
            DWORD bytes=0;SetupDiGetDeviceInterfaceDetailW(list,&data,nullptr,0,&bytes,nullptr);
            if(!bytes || bytes>65536)continue;
            std::vector<unsigned char> buffer(bytes);auto* detail=reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W*>(buffer.data());detail->cbSize=sizeof(*detail);
            if(!SetupDiGetDeviceInterfaceDetailW(list,&data,detail,bytes,nullptr,nullptr))continue;
            std::wstring path=detail->DevicePath,lower=path;
            for(auto& c:lower)c=towlower(c);
            const auto key=lower.substr(0,lower.rfind(L"#{"));
            if(lower.find(L"vid_057e&pid_0337")!=std::wstring::npos && seen.insert(key).second)result.insert(path);
        }
        SetupDiDestroyDeviceInfoList(list);
    }
    return result;
}
struct Device {
    HANDLE file=INVALID_HANDLE_VALUE;WINUSB_INTERFACE_HANDLE usb=nullptr;
    UCHAR input=0,output=0;
    bool initialized=false;
    std::array<uint8_t,5> last_rumble{0x11,0,0,0,0};
    ~Device(){if(usb){if(initialized){UCHAR off[5]={0x11,0,0,0,0};ULONG n;WinUsb_WritePipe(usb,output,off,5,&n,nullptr);}WinUsb_Free(usb);}if(file!=INVALID_HANDLE_VALUE)CloseHandle(file);}
    bool open(const std::wstring& path) {
        file=CreateFileW(path.c_str(),GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,FILE_FLAG_OVERLAPPED,nullptr);
        if(file==INVALID_HANDLE_VALUE || !WinUsb_Initialize(file,&usb))return false;
        USB_DEVICE_DESCRIPTOR device{};ULONG size=0;
        if(!WinUsb_GetDescriptor(usb,USB_DEVICE_DESCRIPTOR_TYPE,0,0,reinterpret_cast<PUCHAR>(&device),sizeof(device),&size) || size!=sizeof(device) || device.idVendor!=0x057e || device.idProduct!=0x0337)return false;
        USB_INTERFACE_DESCRIPTOR descriptor{};if(!WinUsb_QueryInterfaceSettings(usb,0,&descriptor))return false;
        for(int i=0;i<descriptor.bNumEndpoints;++i){WINUSB_PIPE_INFORMATION pipe{};if(!WinUsb_QueryPipe(usb,0,i,&pipe))continue;if(pipe.PipeType!=UsbdPipeTypeInterrupt && pipe.PipeType!=UsbdPipeTypeBulk)continue;if(pipe.PipeId&0x80)input=pipe.PipeId;else output=pipe.PipeId;}
        if(!input || !output)return false;
        ULONG timeout=25;if(!WinUsb_SetPipePolicy(usb,input,PIPE_TRANSFER_TIMEOUT,sizeof(timeout),&timeout) || !WinUsb_SetPipePolicy(usb,output,PIPE_TRANSFER_TIMEOUT,sizeof(timeout),&timeout))return false;
        UCHAR init=0x13;ULONG written=0;initialized=WinUsb_WritePipe(usb,output,&init,1,&written,nullptr) && written==1;return initialized;
    }
};
void run() {
    std::map<std::wstring,std::unique_ptr<Device>> devices;
    auto scan=Clock::now();std::set<std::wstring> warned;
    while(!stopping) {
        if(Clock::now()>=scan) {
            const auto found=enumerate();
            for(const auto& path:found)if(!devices.count(path)) {
                auto device=std::make_unique<Device>();
                if(device->open(path)){devices[path]=std::move(device);warned.erase(path);}
                else if(warned.insert(path).second)fprintf(stderr,"[input] GameCube adapter found but cannot open WinUSB. Close Dolphin/other adapter users and see docs/GAMECUBE-ADAPTERS.md. No drivers were changed.\n");
            }
            scan=Clock::now()+std::chrono::seconds(2);
        }
        for(auto it=devices.begin();it!=devices.end();) {
            auto& device=*it->second;uint8_t bytes[64]{};ULONG size=0;
            const bool read=WinUsb_ReadPipe(device.usb,device.input,bytes,sizeof(bytes),&size,nullptr)!=0;
            const auto error=read?0:GetLastError();
            gc_adapter::Report report{};
            if(read && gc_adapter::decode(bytes,size,report)) {
                std::lock_guard<std::mutex> lock(state_mutex);auto& state=states[it->first];state.report=report;state.alive=true;state.received=Clock::now();
            }
            std::array<uint8_t,5> rumble{};
            {std::lock_guard<std::mutex> lock(state_mutex);rumble=states[it->first].rumble;}
            if(rumble!=device.last_rumble){ULONG written=0;if(WinUsb_WritePipe(device.usb,device.output,rumble.data(),5,&written,nullptr) && written==5)device.last_rumble=rumble;}
            if(!read && error!=ERROR_SEM_TIMEOUT && error!=ERROR_TIMEOUT) {
                {std::lock_guard<std::mutex> lock(state_mutex);states[it->first].alive=false;}
                device.last_rumble={0x11,0,0,0,0};it=devices.erase(it);
            }else ++it;
        }
        if(devices.empty())std::this_thread::sleep_for(std::chrono::milliseconds(40));
    }
    for(auto& entry:devices)entry.second->last_rumble={0x11,0,0,0,0};
}
}
#endif

void desktop_gc_adapter_update() {
#ifdef _WIN32
    if(!SDL_WasInit(SDL_INIT_JOYSTICK))return;
    if(!worker.joinable()) {
        if(const char* enabled=SDL_getenv("GCN_GAMECUBE_ADAPTER");enabled && !strcmp(enabled,"0"))return;
        stopping=false;worker=std::thread(run);
    }
    std::lock_guard<std::mutex> lock(state_mutex);
    for(auto& entry:states) {
        auto& state=entry.second;
        if(!state.alive || Clock::now()-state.received>std::chrono::milliseconds(250)){if(pads.count(entry.first))pads[entry.first]->clear();state.rumble={0x11,0,0,0,0};continue;}
        auto& ports=pads[entry.first];if(!ports)ports=std::make_unique<gc_adapter::Pads>(next_adapter++);
        ports->update(state.report);state.rumble=ports->rumble_packet();
    }
#endif
}
void desktop_gc_adapter_shutdown() {
#ifdef _WIN32
    stopping=true;if(worker.joinable())worker.join();pads.clear();states.clear();next_adapter=1;
#endif
}
