// Compose online initials from the original font in each player's own disc.
// Substitute only player marker resources; their native placement/animation stays.
#include "runtime.h"
#include "device_checkpoint.h"
#include "gx/texture.h"
extern "C" {
#include "recomp_funcs.h"
}
#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>
namespace {
std::map<uint64_t,uint32_t> images;
bool valid(uint32_t address,size_t bytes){return address>=0x80000000u && uint64_t(address)+bytes<=0x80000000ull+RAM_SIZE;}
const std::array<std::vector<std::string>,8>& tags() {
    static const auto value=[] {
        std::array<std::vector<std::string>,8> result;
        const char* packed=getenv("MKDD_NET_TAGS"),*seats=getenv("MKDD_NET_SEATS"),*layout=getenv("MKDD_NET_LAYOUT");
        if(!packed || !seats)return result;
        const bool shared=layout && !strcmp(layout,"coop");size_t slot=0,begin=0;std::string list=packed;
        std::array<std::string,8> ordered;
        while(begin<list.size() && slot<strlen(seats)) {
            const auto end=list.find(',',begin);const auto tag=list.substr(begin,end==std::string::npos?end:end-begin);
            const unsigned seat=unsigned(seats[slot]-'0');
            if(seat>=8 || tag.empty() || tag.size()>7 || tag.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789")!=std::string::npos)
                fatal("Invalid online initials roster");
            ordered[seat]=tag;++slot;if(end==std::string::npos)break;begin=end+1;
        }
        const char* karts=getenv("MKDD_NET_KART_MAP");
        for(unsigned seat=0;seat<8;++seat)if(!ordered[seat].empty()) {
            const unsigned kart=layout && !strcmp(layout,"mixed") && karts && strlen(karts)==8?unsigned(karts[seat]-'0'):shared?seat/2:seat;
            if(kart>=8)fatal("Invalid online name kart mapping");
            result[kart].push_back(ordered[seat]);
        }
        return result;
    }();return value;
}
struct Glyph {unsigned width,height;std::vector<uint32_t> pixels;};
Glyph glyph(uint32_t resource) {
    if(!valid(resource,32))throw std::runtime_error("Invalid font image");
    const auto format=mem_r8(resource);
    const auto w=mem_r16(resource+2),h=mem_r16(resource+4);
    if(!w || !h || w>256 || h>256)throw std::runtime_error("Invalid font image size");
    const auto bytes=gx::texture_size_bytes(format,w,h);const uint64_t data=uint64_t(resource)+mem_r32(resource+28);
    const auto palette_bytes=size_t(mem_r16(resource+10))*2;const uint64_t palette=uint64_t(resource)+mem_r32(resource+12);
    if(data>UINT32_MAX || !valid(uint32_t(data),bytes) || (palette_bytes && (palette>UINT32_MAX || !valid(uint32_t(palette),palette_bytes))))throw std::runtime_error("Invalid font image storage");
    return {w,h,gx::texture_decode_rgba(mem_ptr(uint32_t(data)),bytes,format,w,h,palette_bytes?mem_ptr(uint32_t(palette)):nullptr,palette_bytes,mem_r8(resource+9))};
}
int marker(const std::string& path) {
    const auto slash=path.find_last_of('/');const auto name=path.substr(slash==std::string::npos?0:slash+1);
    for(const char* prefix:{"PlayerNumber_","LANPlayerNumber_","Map_Icon_Player","LANMap_Icon_Player"}) {
        const size_t size=strlen(prefix);if(name.compare(0,size,prefix))continue;
        if(name.size()<=size || name[size]<'1' || name[size]>'8')continue;
        const auto tail=name.substr(size+1);if(tail==".bti" || (std::string(prefix)=="PlayerNumber_" && tail=="P.bti"))return name[size]-'1';
    }
    return -1;
}
uint32_t compose(CPU* c,uint32_t original,unsigned kart) {
    const auto& lines=tags()[kart];if(lines.empty())return original;
    const uint64_t key=(uint64_t(original)<<8)|kart;if(auto found=images.find(key);found!=images.end())return found->second;
    const auto common=mem_r32(c->r[13]-21928);if(!valid(common,0x98))return original;
    const unsigned width=mem_r16(original+2),height=mem_r16(original+4);
    if(width<8 || height<8 || width>256 || height>256)return original;
    std::vector<uint32_t> pixels(size_t(width)*height);
    const unsigned line_height=height/unsigned(lines.size());
    for(unsigned row=0;row<lines.size();++row) {
        std::vector<Glyph> letters;unsigned total=0,highest=0;
        for(char letter:lines[row]) {
            const auto pointer=common+(letter>='A'?0x30+4*(letter-'A'):4+4*(letter-'0'));
            auto decoded=glyph(mem_r32(pointer));total+=decoded.width;highest=std::max(highest,decoded.height);letters.push_back(std::move(decoded));
        }
        const float scale=std::min(float(width-4)/float(total),float(line_height-2)/float(highest));
        unsigned x0=unsigned((width-total*scale)/2);
        for(const auto& letter:letters) {
            const unsigned w=std::max(1u,unsigned(letter.width*scale)),h=std::max(1u,unsigned(letter.height*scale));
            const unsigned y0=row*line_height+(line_height-h)/2;
            for(unsigned y=0;y<h && y+y0<height;++y)for(unsigned x=0;x<w && x+x0<width;++x)
                pixels[(y0+y)*width+x0+x]=letter.pixels[std::min(letter.height-1,unsigned(y/scale))*letter.width+std::min(letter.width-1,unsigned(x/scale))];
            x0+=w;
        }
    }
    const auto size=((width+3)/4)*((height+3)/4)*64;
    const CPU saved=*c;c->r[3]=size+95;fn_800155A8(c);const uint32_t image=(c->r[3]+31)&~31u;*c=saved;
    if(!valid(image,size+32))fatal("Cannot allocate online initials image");
    memset(mem_ptr(image),0,size+32);mem_w8(image,6);mem_w8(image+1,2);mem_w16(image+2,width);mem_w16(image+4,height);
    mem_w8(image+20,1);mem_w8(image+21,1);mem_w8(image+24,1);mem_w32(image+28,32);
    uint32_t at=image+32;
    for(unsigned by=0;by<height;by+=4)for(unsigned bx=0;bx<width;bx+=4) {
        for(unsigned i=0;i<16;++i){const auto x=bx+i%4,y=by+i/4;const uint32_t p=x<width && y<height?pixels[y*width+x]:0;
            mem_w8(at+i*2,p>>24);mem_w8(at+i*2+1,p);mem_w8(at+32+i*2,p>>8);mem_w8(at+33+i*2,p>>16);}
        at+=64;
    }
    images.emplace(key,image);return image;
}
}
DeviceFragment mkdd_checkpoint_online_tags(){return checkpoint_fields("online initials resources",images);}
extern "C" void mkdd_online_tags_begin_race(){images.clear();}
extern "C" void hle_fn_801CF5E4(CPU* c) {
    const auto archive=c->r[3],name=c->r[4];std::string path;
    if(archive==5 && getenv("MKDD_NET_TAGS") && valid(name,256))for(unsigned i=0;i<255 && mem_r8(name+i);++i)path+=char(mem_r8(name+i));
    orig_fn_801CF5E4(c);const int kart=marker(path);
    if(kart<0 || !valid(c->r[3],32))return;
    const CPU result=*c;
    try{const auto replacement=compose(c,result.r[3],unsigned(kart));*c=result;c->r[3]=replacement;}
    catch(const std::exception& e){*c=result;fprintf(stderr,"[online-tags] %s\n",e.what());}
}
