// Production layers derived from the approved, authored Classic Blue concepts.
// Coordinates remain in the native 608x448 canvas; its viewport is 16:9.
std::string classic_path(const char* name) {
    const char* root=getenv("MKDD_MENU_ART");
    return std::string(root?root:"assets/classic")+"/"+name;
}
std::vector<uint32_t> classic_allocations;
uint32_t classic_backdrop=0,classic_driver=0;
std::string classic_loaded;
uint32_t classic_allocate(CPU* c,unsigned bytes) {
    const CPU saved=*c;c->r[3]=bytes+31;fn_800155A8(c);
    const auto raw=c->r[3],result=raw?(raw+31)&~31u:0;*c=saved;
    if(valid(result,bytes))classic_allocations.push_back(raw);
    return valid(result,bytes)?result:0;
}
std::vector<uint32_t> classic_external;
uint32_t classic_host_image(CPU* c,uint32_t resource,unsigned width,unsigned height,std::vector<uint32_t> pixels) {
    if(!resource)resource=classic_allocate(c,96);
    if(!resource)return 0;
    // A tiny native descriptor retains J2D UVs; full-resolution pixels are owned
    // by the host renderer and do not consume the console's 24 MB scene heap.
    memset(mem_ptr(resource),0,96);mem_w8(resource,6);mem_w8(resource+1,2);
    mem_w16(resource+2,4);mem_w16(resource+4,4);mem_w8(resource+20,1);mem_w8(resource+21,1);mem_w8(resource+24,1);mem_w32(resource+28,32);
    memset(mem_ptr(resource+32),255,64);
    if(!gx::texture_set_external(resource+32,4,4,width,height,std::move(pixels)))return 0;
    if(std::find(classic_external.begin(),classic_external.end(),resource+32)==classic_external.end())classic_external.push_back(resource+32);
    return resource;
}
uint32_t classic_texture(CPU* c,const char* name,bool background=false) {
    auto& resource=background?classic_backdrop:classic_driver;
    if(resource && (!background || classic_loaded==name))return resource;
    const auto filename=std::string(name);const auto hd=filename.substr(0,filename.find_last_of('.'))+".rgba";
    std::ifstream full(classic_path(hd.c_str()),std::ios::binary);
    uint32_t header[3]{};full.read((char*)header,sizeof(header));
    if(full && header[0]==0x31414752 && header[1]>0 && header[1]<=8192 && header[2]>0 && header[2]<=8192){
        std::vector<uint32_t> pixels(size_t(header[1])*header[2]);full.read((char*)pixels.data(),pixels.size()*4);
        if(full && full.peek()==EOF){resource=classic_host_image(c,resource,header[1],header[2],std::move(pixels));if(resource){if(background)classic_loaded=name;return resource;}}
    }
    // Missing high-resolution assets still have a usable, bounded fallback.
    std::ifstream file(classic_path(name),std::ios::binary);
    std::vector<char> bytes((std::istreambuf_iterator<char>(file)),{});
    const unsigned expected=32+(background?1024*576:128*128)*4;
    if(bytes.size()!=expected || bytes[0]!=6)return 0;
    const unsigned width=background?1024:128,height=background?576:128;
    auto pixels=gx::texture_decode_rgba((const uint8_t*)bytes.data()+32,bytes.size()-32,6,width,height);
    resource=classic_host_image(c,resource,width,height,std::move(pixels));if(background && resource)classic_loaded=name;
    return resource;
}
struct ClassicFont {uint32_t glyph[95]{};float advance[95]{};bool attempted=false;} classic_font;
void classic_release(CPU* c) {
    display_set_ui_render_width(0);
    // Menu artwork must leave the guest scene heap before the retail selection
    // and race scenes reserve their own memory. Keep no guest pointers across it.
    const CPU saved=*c;
    for(auto address:classic_external)gx::texture_remove_external(address);classic_external.clear();
    for(auto raw:classic_allocations){c->r[3]=raw;fn_800157A8(c);}
    classic_allocations.clear();classic_backdrop=classic_driver=0;classic_loaded.clear();classic_font={};
    *c=saved;
}
bool classic_font_load(CPU* c) {
    if(classic_font.attempted)return classic_font.glyph[0]!=0;
    classic_font.attempted=true;
    std::ifstream hd(classic_path("racing-font-hd.bin"),std::ios::binary);uint32_t meta[4]{};hd.read((char*)meta,sizeof(meta));
    if(hd && meta[0]==0x32464e54 && meta[1]==95 && meta[2]==192 && meta[3]==240){
        for(unsigned i=0;i<95;++i){std::vector<uint32_t> pixels(meta[2]*meta[3]);hd.read((char*)&classic_font.advance[i],4);hd.read((char*)pixels.data(),pixels.size()*4);if(!hd)return false;
            classic_font.glyph[i]=classic_host_image(c,0,meta[2],meta[3],std::move(pixels));if(!classic_font.glyph[i])return false;}
        return true;
    }
    std::ifstream file(classic_path("racing-font.bin"),std::ios::binary);
    uint32_t header[5]{};file.read((char*)header,sizeof(header));
    if(!file || header[0]!=0x31464e54 || header[1]!=32 || header[2]!=95 || header[3]!=64 || header[4]!=80)return false;
    constexpr unsigned bytes=32+64*80*4;
    const auto base=classic_allocate(c,95*bytes);if(!base)return false;
    for(unsigned i=0;i<95;++i){file.read((char*)&classic_font.advance[i],4);file.read((char*)mem_ptr(base+i*bytes),bytes);if(!file)return false;classic_font.glyph[i]=base+i*bytes;}
    return true;
}
float classic_text_width(const std::string& s,float size) {
    float width=0;for(unsigned char ch:s)if(ch>=32 && ch<127)width+=classic_font.advance[ch-32]*size/80.f;
    return width;
}
void racing_text(CPU* c,const std::string& s,float x,float y,float size,uint32_t color=0xffffffff,bool centered=false,bool outline=false) {
    if(!classic_font_load(c)){text(c,s.c_str(),x,y,size,color);return;}
    if(centered)x-=classic_text_width(s,size)*.5f;
    for(unsigned char ch:s)if(ch>=32 && ch<127) {
        const auto glyph=classic_font.glyph[ch-32];const float w=size*.8f,h=size*4/3;
        if(ch!=' '){
            if(outline)for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx)if(dx || dy)native_picture(c,glyph,x+dx*.65f,y+dy*.85f,w,h,0x071333ff,true);
            native_picture(c,glyph,x,y,w,h,color,true);
        }
        x+=classic_font.advance[ch-32]*size/80;
    }
}
void smooth_panel(CPU* c,float x,float y,float w,float h,float r,uint32_t color) {
    float points[40][2];for(unsigned i=0;i<40;++i){unsigned corner=i/10;const float a=(-1.5707963f+corner*1.5707963f)+(i%10)*1.5707963f/9;
        points[i][0]=x+(corner<2?w-r:r)+std::cos(a)*r;points[i][1]=y+(corner==0 || corner==3?r:h-r)+std::sin(a)*r;}
    flat_polygon(c,points,40,color);
}
void shine_panel(CPU* c,float x,float y,float w,float h,bool red=false) {
    smooth_panel(c,x,y+2,w,h,6,0x031133aa);smooth_panel(c,x,y,w,h,6,red?0xffc3a3ff:0xa3d4ffff);
    smooth_panel(c,x+1,y+1,w-2,h-2,5,red?0x7b071aff:0x09325dff);
    const float r=4,inside=h-4;
    for(unsigned row=0;row<30;++row){const float t=row/29.f,top=y+2+row*inside/30,mid=(row+.5f)*inside/30;
        const float edge=std::max(0.f,r-std::min(mid,inside-mid));const float inset=r-std::sqrt(std::max(0.f,r*r-edge*edge));
        const uint32_t from=red?(t<.42f?0xff7274ff:0xd21429ff):(t<.42f?0x369bf2ff:0x1455abff);
        const uint32_t to=red?(t<.42f?0xf02032ff:0x850014ff):(t<.42f?0x1064ceff:0x05235aff);
        const float u=t<.42f?t/.42f:(t-.42f)/.58f;uint32_t color=255;
        for(int shift:{24,16,8})color|=(uint32_t)(((from>>shift)&255)*(1-u)+((to>>shift)&255)*u)<<shift;
        box(c,x+2+inset,top,w-4-inset*2,inside/30+.1f,color);
    }
}
void classic_outline(CPU*,float,float,float,float,float slant=0);
bool classic_focused(int action){return online_focus==action;}
void classic_button(CPU* c,const char* value,float x,float y,float w,float h,int action,bool red=false) {
    shine_panel(c,x,y,w,h,red);if(classic_focused(action)){smooth_panel(c,x+2,y+2,w-4,h-4,4,0xffe27435);classic_outline(c,x,y,w,h);}
    classic_font_load(c);const float size=std::min(h*.66f,(w-12)/std::max(1.f,classic_text_width(value,1)));
    racing_text(c,value,x+w/2,y+(h-size*1.05f)/2-2,size,0xfff1bdff,true,true);hit(x,y,w,h,action);
}
void classic_outline(CPU* c,float x,float y,float w,float h,float slant) {
    // A rounded ring leaves the illustrated card visible beneath the glow.
    for(int pass=3;pass>=0;--pass) {
        const float t=pass==0?1.1f:1.5f+pass*1.25f;const uint32_t color=pass==0?0xfff5b3ff:pass==1?0xffd721b0:0xffce2526;
        float p[40][2],normal[40][2];const float r=8;
        for(unsigned i=0;i<40;++i){const unsigned corner=i/10;const float a=(-1.5707963f+corner*1.5707963f)+(i%10)*1.5707963f/9;
            const float py=y+(corner==0 || corner==3?r:h-r)+std::sin(a)*r;
            const float px=x+(corner<2?w-slant-r:r)+std::cos(a)*r;
            p[i][0]=px+slant*(1-(py-y)/h);p[i][1]=py;normal[i][0]=std::cos(a)*t*.75f;normal[i][1]=std::sin(a)*t;}
        for(unsigned i=0;i<40;++i){unsigned j=(i+1)%40;
            const float q[4][2]={{p[i][0]-normal[i][0],p[i][1]-normal[i][1]},{p[j][0]-normal[j][0],p[j][1]-normal[j][1]},{p[j][0]+normal[j][0],p[j][1]+normal[j][1]},{p[i][0]+normal[i][0],p[i][1]+normal[i][1]}};flat_polygon(c,q,4,color);}
    }
}
void classic_room_info(CPU* c) {
    if(!room_reply.has("room"))return;const auto& room=room_reply.at("room");
    const bool lobby=page==Page::Lobby || page==Page::LeaveConfirm || page==Page::KickConfirm;
    const float x=lobby?411:page==Page::Battle?549:540;
    if(lobby)shine_panel(c,352,9,150,29);
    racing_text(c,"ROOM "+room.at("code").str(),x,lobby?16:24,lobby?13:12,0xffee83ff,true,true);
    racing_text(c,std::to_string(room.at("members").list().size())+"/8 PLAYERS",lobby?556:x,lobby?20:43,lobby?17:12,0xe3f2ffff,true,true);
}
void classic_footer(CPU* c,bool lobby=false) {
    racing_text(c,release_label(),10,432,8,0x9cb9ecff);
    if(lobby) {
        bool ready=false;for(const auto& member:room_reply.at("room").at("members").list())if(member.at("you").boolean())ready=member.at("ready").boolean();
        classic_button(c,ready?"UNREADY":"READY",9,402,75,24,32);
        classic_button(c,"BACK",90,402,55,24,35);
        racing_text(c,"F1: SETTINGS / QUICK JOIN",154,407,10,0xb9dbffff);
        classic_button(c,"EDIT NAME",401,398,78,30,34);
        if(room_reply.at("room").at("host").boolean())classic_button(c,"CHOOSE MODE >",487,397,114,32,33,true);
        else racing_text(c,"WAITING FOR HOST",545,405,12,0xffee9aff,true);
    }else {
        classic_button(c,"BACK",510,407,84,27,-2);
        racing_text(c,"ENTER / A: CONFIRM",296,412,11,0xeaf1ffff);
        racing_text(c,"F1: SETTINGS",429,412,11,0xeaf1ffff);
    }
}
void classic_lobby(CPU* c) {
    const auto& room=room_reply.at("room");const auto common=mem_r32(c->r[13]-21928);
    const float centers[]={80,222,356,508};
    // Seat centers measured against the approved lobby artwork (608x448 canvas).
    // The illustrated karts are not an exact grid: each row and rider needs its
    // own anchor. Keep avatar placement separate from the nameplate layout.
    constexpr float seats[8][2][2]={
        {{83.1f,142.0f},{83.1f,184.3f}},
        {{225.6f,142.0f},{226.2f,184.7f}},
        {{361.38f,142.0f},{361.31f,185.9f}},
        {{507.53f,143.5f},{507.21f,185.9f}},
        {{83.1f,292.4f},{83.1f,336.8f}},
        {{226.2f,293.2f},{226.2f,336.8f}},
        {{361.70f,293.2f},{361.62f,336.8f}},
        {{507.21f,294.8f},{506.97f,337.9f}}
    };
    const uint32_t colours[]={0xff4242ff,0x46e238ff,0x3894ffff,0xffc838ff,0xb26dffff,0xff79d7ff,0x48efffff,0xff9c38ff};
    for(unsigned kart=0;kart<8;++kart) {
        const float cx=centers[kart%4],y=kart<4?119:272;
        const Json* riders[2]{};for(const auto& m:room.at("members").list()){const auto s=m.at("seat").integer(0,15);if(s==kart)riders[0]=&m;if(s==kart+8)riders[1]=&m;}
        if(online_focus>=0 && online_focus<16 && unsigned(online_focus)%8==kart && (page==Page::Lobby))classic_outline(c,cx-45,y-29,133,136);
        for(unsigned rider=0;rider<2;++rider) {
            if(rider && !riders[0])continue;
            const float sy=y+rider*45,px=cx+24;const unsigned seat=kart+rider*8;
            if(riders[rider]) {
                if(const auto driver=classic_texture(c,"driver.gctex"))native_picture(c,driver,seats[kart][rider][0]-23,seats[kart][rider][1]-30.5f,46,61,colours[(kart+rider*2)%8],true);
                const float poly[][2]={{px-4,sy+17},{px+7,sy+7},{px+61,sy+7},{px+58,sy+25},{px+7,sy+25}};
                flat_polygon(c,poly,5,0x07173aff);float p[5][2];for(int i=0;i<5;++i){p[i][0]=poly[i][0];p[i][1]=poly[i][1]-1;}flat_polygon(c,p,5,0xffed9aff);
                oval(c,px+5,sy+11,6,8,0x092d20ff);oval(c,px+6,sy+12,4,5,riders[rider]->at("ready").boolean()?0x62ff24ff:0xa7abb8ff);
                const auto name=riders[rider]->at("initials").str();const float name_size=std::min(12.f,44/std::max(1.f,classic_text_width(name,1)));
                racing_text(c,name,px+13,sy+7,name_size,0x081a50ff);
                if(valid(common,0x190))native_picture(c,mem_r32(common+0x18c),px+22,sy+27,11,14,0xffffffff,true);
                racing_text(c,std::to_string(riders[rider]->at("stars").integer(0,INT64_MAX)),px+35,sy+27,12,0xffe44fff,false,true);
            }else {
                shine_panel(c,px+5,sy+6,58,30);const float triangle[][2]={{px-5,sy+21},{px+7,sy+10},{px+7,sy+32}};flat_polygon(c,triangle,3,0xffed91ff);
                racing_text(c,rider?"JOIN":"TAKE",px+34,sy+6,12,0xffffffff,true,true);
                racing_text(c,rider?"BACK":"WHEEL",px+34,sy+19,12,0xffffffff,true,true);
            }
            if(page==Page::Lobby && classic_focused((int)seat)){smooth_panel(c,px+4,sy+5,60,32,5,0xffe27435);classic_outline(c,px+4,sy+5,60,32);}
            hit(cx-25,sy-4,111,43,seat);
        }
    }
    classic_footer(c,true);
}
bool classic_draw(CPU* c) {
    if(page==Page::Rules || page==Page::Kart || page==Page::Title || page==Page::Native)return false;
    const bool lobby=page==Page::Lobby || page==Page::LeaveConfirm || page==Page::KickConfirm;
    const char* filename=lobby?"lobby.gctex":page==Page::HostJoin?"hostjoin.gctex":page==Page::Type?"mode.gctex":page==Page::Engine?"engine.gctex":page==Page::Battle?"battle.gctex":"background.gctex";
    const auto bg=classic_texture(c,filename,true);if(!bg)return false;
    display_set_ui_render_width(608);
    native_picture(c,bg,0,0,608,448,0xffffffff,true);classic_room_info(c);
    if(lobby && !room_reply.has("room")) {
        racing_text(c,"RESTORING ROOM...",304,218,24,0xffee83ff,true,true);
        desktop_pointer::publish("online-restoring",{});return true;
    }
    if(lobby)classic_lobby(c);
    else if(page==Page::HostJoin || page==Page::Type) {
        for(unsigned i=0;i<2;++i){const float x=i?308:63,w=i?230:251;if(classic_focused((int)i))classic_outline(c,x,134,w,181,27);hit(x,134,w,181,i);}
        if(page==Page::HostJoin){racing_text(c,"YOUR NAME: "+initials,112,358,18,0xffed9aff,true,true);classic_button(c,"EDIT NAME",221,352,91,29,34);}
        else {hit(198,343,208,35,38);if(classic_focused(38))classic_outline(c,198,343,208,35);if(!room_reply.at("room").at("host").boolean())racing_text(c,"WAITING FOR HOST",304,322,14,0xffe990ff,true,true);}
        classic_footer(c);
    }else if(page==Page::Engine) {
        const float xs[]={24,157,289,433},widths[]={144,144,154,147};
        for(unsigned i=0;i<4;++i){const float x=xs[i],w=widths[i];if(classic_focused((int)i))classic_outline(c,x,140,w,127,23);hit(x,140,w,127,i);}
        const char* names[]={"50cc","100cc","150cc","MIRROR"};const char* help[]={"RELAXED RACING","CLASSIC RACING","FAST RACING","REVERSED COURSES"};
        racing_text(c,names[std::min(selection,3u)],304,278,32,0xffe54fff,true,true);racing_text(c,help[std::min(selection,3u)],304,312,17,0xffffffff,true,true);
        hit(165,343,280,35,38);if(classic_focused(38))classic_outline(c,165,343,280,35);classic_footer(c);
    }else if(page==Page::Battle) {
        for(unsigned i=0;i<3;++i){const float x=38+i*185;if(classic_focused((int)i))classic_outline(c,x,124,171,190,8);hit(x,124,171,190,i);}
        racing_text(c,"ONE BATTLE, THEN BACK TO YOUR ROOM",202,350,13,0xeaf2ffff,true,true);hit(380,371,212,29,38);if(classic_focused(38))classic_outline(c,380,371,212,29);classic_footer(c);
    }else if(page==Page::Code || page==Page::Initials) {
        const bool naming=page==Page::Initials;
        // A polished panel covers the lobby-specific subtitle on the shared base.
        shine_panel(c,92,48,424,31);racing_text(c,naming?"YOUR NAME":"ENTER ROOM CODE",304,49,23,0xfff2bdff,true,true);
        std::string shown=naming?draft_initials:code;while(shown.size()<(naming?7u:8u))shown+='-';
        shine_panel(c,167,91,274,44);racing_text(c,shown,304,94,31,0xffec83ff,true,true);
        racing_text(c,"TYPE DIRECTLY OR SELECT A KEY",304,143,13,0xe6f1ffff,true);
        for(unsigned i=0;i<36;++i){auto [x,y]=key_position(i);y-=17;const bool allowed=naming || strchr(alphabet,keyboard_keys[i]);shine_panel(c,x-4,y-5,39,31);if(classic_focused((int)i))classic_outline(c,x-4,y-5,39,31);racing_text(c,std::string(1,keyboard_keys[i]),x+15,y-3,23,allowed?0xffffffff:0x6d7b91ff,true);if(allowed)hit(x-4,y-5,39,31,i);}
        classic_button(c,"DELETE",86,336,122,32,36);classic_button(c,naming?"SAVE NAME":"JOIN ROOM",359,336,163,32,37,true);classic_footer(c);
    }
    if(page==Page::LeaveConfirm || page==Page::KickConfirm) {
        clickable.clear();box(c,0,0,608,448,0x010927c9);shine_panel(c,112,137,384,174);
        const bool leave=page==Page::LeaveConfirm;
        racing_text(c,leave?"LEAVE ROOM?":"REMOVE "+kick_name+"?",304,152,29,0xffec8fff,true,true);
        racing_text(c,"ARE YOU SURE?",304,195,18,0xffffffff,true);
        racing_text(c,leave?(room_reply.at("room").at("host").boolean()?"THIS CLOSES THE ROOM FOR EVERYONE":"YOU CAN REJOIN USING THE ROOM CODE"):"THEY CAN REJOIN USING THE ROOM CODE",304,225,12,0xbedfffff,true);
        classic_button(c,leave?"NO, STAY":"KEEP PLAYER",132,262,159,31,0);
        classic_button(c,leave?"YES, LEAVE":"REMOVE PLAYER",315,262,159,31,1,true);

    }
    if(!message.empty()){box(c,45,379,518,18,0x031435e8);racing_text(c,message,304,380,12,0xffed9aff,true);}
    desktop_pointer::publish("online-"+std::to_string((int)page),clickable,16.f/9.f);
    return true;
}
