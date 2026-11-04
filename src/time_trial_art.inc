// Original wordmarks and glyphs are extracted locally during the ISO build.
// Compose/cache only in menus, reuse the existing native J2D picture path, and
// release host textures and guest descriptors before character/race loading.
#if __has_include("trial_menu_art.h")
#include "trial_menu_art.h"
struct TrialArtImage {unsigned width=0,height=0;std::vector<uint32_t> pixels;};
struct TrialArtPicture {uint32_t resource=0;unsigned width=0,height=0;};
struct TrialArtCache {
    uint64_t generation=~uint64_t(0);
    std::map<std::string,TrialArtImage> originals;
    std::map<std::string,TrialArtPicture> pictures;
} trial_art;
void trial_art_sync(){if(trial_art.generation!=classic_generation){trial_art={};trial_art.generation=classic_generation;}}
const TrialArtImage& trial_art_original(const std::string& name){
    trial_art_sync();auto [it,added]=trial_art.originals.try_emplace(name);
    if(added)for(const auto& asset:trial_disc_art::assets)if(name==asset.name){
        const auto* b=asset.bytes;
        auto u16=[&](unsigned p){return unsigned(b[p])*256+b[p+1];};
        auto u32=[&](unsigned p){return uint32_t(b[p])<<24|uint32_t(b[p+1])<<16|uint32_t(b[p+2])<<8|b[p+3];};
        const unsigned width=u16(2),height=u16(4),offset=u32(28),pal=u32(12),count=u16(10);
        if(offset>asset.size || pal>asset.size || count*2>asset.size-pal)break;
        auto pixels=gx::texture_decode_rgba(b+offset,asset.size-offset,b[0],width,height,
            count?b+pal:nullptr,count*2,b[9]);
        if(pixels.size()==size_t(width)*height)it->second={width,height,std::move(pixels)};
        break;
    }
    return it->second;
}
bool trial_art_picture(CPU* c,const std::string& name,float x,float y,float w,float h,uint32_t tint=0xffffffff){
    trial_art_sync();auto& picture=trial_art.pictures[name];
    if(!picture.resource){const auto& original=trial_art_original(name);if(original.pixels.empty())return false;
        auto pixels=original.pixels;
        if(name=="bar2" || name=="corsor_maru1")for(auto& pixel:pixels)pixel=(pixel&0xff000000u)|0x00ffffffu;
        picture={classic_host_image(c,0,original.width,original.height,std::move(pixels)),original.width,original.height};}
    return native_picture(c,picture.resource,x,y,w,h,tint,true);
}
struct TrialGlyph {char ch;const char* asset;unsigned left,right;};
constexpr TrialGlyph trial_glyphs[]={
    {'G',"entry_grandprix",22,45},{'R',"entry_time_attack",135,159},{'A',"entry_time_attack",169,192},
    {'N',"entry_grandprix",92,115},{'D',"entry_grandprix",116,139},{'P',"entry_grandprix",147,170},
    {'I',"entry_time_attack",42,56},{'X',"entry_grandprix",210,234},{'T',"entry_time_attack",18,41},
    {'M',"entry_time_attack",55,79},{'E',"entry_time_attack",79,101},{'L',"entry_time_attack",192,215},
    {'S',"entry_time_attack",215,239},{'O',"selectmode",304,325},{'C',"selectcourse",259,281},
    {'U',"selectcourse",303,324},{'B',"selectbattle",263,284},{'H',"selectcharacter",166,182}
};
// Results use their own stock lettering, including its original luminance gradient.
constexpr TrialGlyph trial_result_glyphs[]={
    {'C',"mozi_change_minigame",12,31},{'H',"mozi_change_minigame",31,50},
    {'A',"mozi_change_minigame",50,69},{'N',"mozi_change_minigame",69,88},
    {'G',"mozi_change_minigame",88,107},{'E',"mozi_change_minigame",107,126},
    {'B',"mozi_change_minigame",135,154},{'T',"mozi_change_minigame",172,191},
    {'L',"mozi_replay",132,151},{'P',"mozi_replay",113,132},
    {'R',"mozi_next_com",60,79},{'D',"mozi_next_com",80,97},
    {'M',"mozi_next_com",180,198},{'O',"mozi_next_com",198,216},
    {'U',"mozi_change_course",164,182},{'S',"mozi_saveghost",39,59}
};
TrialArtPicture trial_art_make_word(CPU* c,const std::string& word,unsigned style){
    trial_art_sync();const auto key="word:"+std::to_string(style)+":"+word;
    auto& picture=trial_art.pictures[key];
    if(!picture.resource){
        std::vector<TrialGlyph> glyphs;
        if(word=="GRAND PRIX")glyphs.push_back({0,"entry_grandprix",19,237});
        else if(word=="TIME TRIALS")glyphs.push_back({0,"entry_time_attack",18,239});
        else if(word=="SELECT MODE")glyphs.push_back({0,"selectmode",140,370});
        else for(char ch:word){
            if(ch==' '){glyphs.push_back({' ',"",0,10});continue;}
            const auto* begin=style==3?std::begin(trial_result_glyphs):std::begin(trial_glyphs);
            const auto* end=style==3?std::end(trial_result_glyphs):std::end(trial_glyphs);
            auto found=std::find_if(begin,end,[&](const auto& g){return g.ch==ch;});
            if(found==end)return {};glyphs.push_back(*found);
        }
        unsigned width=6;for(const auto& g:glyphs)width+=g.right-g.left;
        const unsigned height=style==3?32:38,top=style==3?0:3;std::vector<unsigned char> mask(width*height);unsigned at=3;
        for(const auto& glyph:glyphs){
            if(glyph.ch!=' '){const auto& image=trial_art_original(glyph.asset);
                if(image.height!=32 || image.width<glyph.right)return {};
                const unsigned gw=glyph.right-glyph.left;
                std::vector<unsigned char> part(gw*32);
                for(unsigned yy=0;yy<32;++yy)for(unsigned xx=glyph.left;xx<glyph.right;++xx){
                    const auto pixel=image.pixels[yy*image.width+xx];
                    const unsigned luminance=((pixel&255)*54+((pixel>>8)&255)*183+((pixel>>16)&255)*19)/256;
                    part[yy*gw+xx-glyph.left]=luminance*(pixel>>24)/255;
                }
                if(glyph.ch){
                    // Italic neighbors overlap a rectangular crop. Keep the
                    // letter's main connected white shape, including its soft
                    // edge, rather than copying slivers of the next letter.
                    std::vector<unsigned char> seen(part.size());std::vector<unsigned> best;
                    for(unsigned seed=0;seed<part.size();++seed){
                        if(seen[seed] || part[seed]<80)continue;
                        std::vector<unsigned> group{seed};seen[seed]=1;
                        for(size_t n=0;n<group.size();++n){
                            const int x=group[n]%gw,y=group[n]/gw;
                            for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx){
                                const int xx=x+dx,yy=y+dy;if(xx<0 || xx>=int(gw) || yy<0 || yy>=32)continue;
                                const unsigned i=yy*gw+xx;if(!seen[i] && part[i]>=80){seen[i]=1;group.push_back(i);}
                            }
                        }
                        if(group.size()>best.size())best=std::move(group);
                    }
                    std::vector<unsigned char> keep(part.size());
                    for(auto i:best){const int x=i%gw,y=i/gw;
                        for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx){
                            const int xx=x+dx,yy=y+dy;if(xx>=0 && xx<int(gw) && yy>=0 && yy<32)keep[yy*gw+xx]=1;
                        }
                    }
                    for(unsigned i=0;i<part.size();++i)if(!keep[i])part[i]=0;
                }
                for(unsigned yy=0;yy<32;++yy)for(unsigned xx=0;xx<gw;++xx)
                    mask[(yy+top)*width+at+xx]=part[yy*gw+xx];
            }at+=glyph.right-glyph.left;
        }
        std::vector<uint32_t> pixels(width*height);
        for(unsigned yy=0;yy<height;++yy)for(unsigned xx=0;xx<width;++xx){
            unsigned outline=0;for(int dy=-2;dy<=2;++dy)for(int dx=-2;dx<=2;++dx){
                int px=int(xx)+dx,py=int(yy)+dy;if(px>=0 && py>=0 && px<int(width) && py<int(height))outline=std::max(outline,unsigned(mask[py*width+px]));
            }
            const auto alpha=mask[yy*width+xx];
            const unsigned r=style==2?235:255,g=style==3?255:style==0?250:style==2?175:255-yy*50/height,b=style==3?255:style==0?166:style==2?25:190-yy*165/height;
            // Composite the colored luminance mask over its black outline.
            const unsigned out=alpha+outline*(255-alpha)/255;
            pixels[yy*width+xx]=out?((r*alpha/out)|((g*alpha/out)<<8)|((b*alpha/out)<<16)|(out<<24)):0;
        }
        if(style==3){
            // Native result labels are 256x32; keep its pane geometry, colors,
            // entrance animation and selected-row stars entirely retail-owned.
            std::vector<uint32_t> fitted(256*32);
            const float scale=std::min(250.f/width,32.f/height);
            const unsigned w=unsigned(width*scale),h=unsigned(height*scale);
            for(unsigned yy=0;yy<h;++yy)for(unsigned xx=0;xx<w;++xx)
                fitted[((32-h)/2+yy)*256+(256-w)/2+xx]=pixels[std::min(height-1,unsigned(yy/scale))*width+std::min(width-1,unsigned(xx/scale))];
            picture={classic_host_image(c,0,256,32,std::move(fitted)),256,32};
        }else picture={classic_host_image(c,0,width,height,std::move(pixels)),width,height};
    }
    return picture;
}
bool trial_art_word(CPU* c,const std::string& word,float x,float y,float size,unsigned style=1,bool centered=false){
    const auto picture=trial_art_make_word(c,word,style);if(!picture.resource)return false;
    const float w=picture.width*size/32.f,h=picture.height*size/32.f;
    return native_picture(c,picture.resource,centered?x-w/2:x,y,w,h,0xffffffff,true);
}
uint32_t trial_result_word(CPU* c,const std::string& word){return trial_art_make_word(c,word,3).resource;}

#else
uint32_t trial_result_word(CPU*,const std::string&){return 0;}
bool trial_art_picture(CPU*,const std::string&,float,float,float,float,uint32_t=0xffffffff){return false;}
bool trial_art_word(CPU*,const std::string&,float,float,float,unsigned=1,bool=false){return false;}
#endif
