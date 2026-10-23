// Original hit regions for the retail character grid. Selection is committed
// through the game's normal A-button path, keeping its validation and animation.
bool native_pane_visible(uint32_t pane) {
    for(unsigned depth=0;depth<24;++depth) {
        if(!valid(pane,0x100) || !mem_r8(pane+0xb0))return false;
        const auto parent=mem_r32(pane+0xec);
        if(!parent)return true;
        if(!valid(parent,12) || parent<0x800000dc)return false;
        pane=parent-0xdc;
    }
    return false;
}
bool native_pane_rect(uint32_t pane,float& x,float& y,float& w,float& h,float padding=0) {
    if(!native_pane_visible(pane))return false;
    float minx=1e9f,miny=1e9f,maxx=-1e9f,maxy=-1e9f;
    for(unsigned i=0;i<4;++i) {
        const float px=read_float(pane+(i&1?0x28:0x20)),py=read_float(pane+(i&2?0x2c:0x24));
        const float sx=read_float(pane+0x80)*px+read_float(pane+0x84)*py+read_float(pane+0x8c);
        const float sy=read_float(pane+0x90)*px+read_float(pane+0x94)*py+read_float(pane+0x9c);
        if(!std::isfinite(sx) || !std::isfinite(sy))return false;
        minx=std::min(minx,sx);miny=std::min(miny,sy);maxx=std::max(maxx,sx);maxy=std::max(maxy,sy);
    }
    if(maxx-minx<1 || maxy-miny<1 || maxx<=0 || minx>=608 || maxy<=0 || miny>=448)return false;
    x=std::max(0.f,minx-padding);y=std::max(0.f,miny-padding);w=std::min(608.f,maxx+padding)-x;h=std::min(448.f,maxy+padding)-y;return true;
}
void native_hit_pane(uint32_t pane,int action,float padding=0) {
    float x,y,w,h;if(native_pane_rect(pane,x,y,w,h,padding))hit(x,y,w,h,action);
}
bool character_pointer_scene(CPU* c,uint32_t app,uint32_t& scene) {
    if(!valid(app,0x4c) || mem_r32(c->r[13]-30528)!=1)return false;
    scene=mem_r32(app+0xc);
    return valid(scene,0x2200) && mem_r32(scene+0x20e0)==13 && mem_r32(scene+0x2144)<=2;
}
struct PointerPadRestore {
    uint32_t pressed=mem_r32(0x803b442c),repeat=mem_r32(0x803b4440);
    ~PointerPadRestore(){mem_w32(0x803b442c,pressed);mem_w32(0x803b4440,repeat);}
};
void native_menu_button(uint32_t button,uint32_t direction=0) {
    mem_w32(0x803b442c,mem_r32(0x803b442c)|button);
    mem_w32(0x803b4440,mem_r32(0x803b4440)|button|direction);
}
unsigned native_mode_value(uint32_t scene,unsigned row) {
    // The one-player layout omits VS; the multiplayer layout omits Time Trials.
    if(native_pane_visible(mem_r32(scene+0xd9c)))return row?2u:0u;
    return row==2?3u:row;
}
bool native_standard_pointer(CPU* c,uint32_t app,bool blocked) {
    static uint32_t pending_mode_scene=0;static int pending_mode=-1;static unsigned mode_wait=0;
    if(!valid(app,0x4c))return false;
    const auto scene=mem_r32(app+0xc),kind=mem_r32(c->r[13]-30528);
    if(!valid(scene,0x100))return false;
    uint32_t selector=0;unsigned count=0;
    if(kind==0) {
        if(mem_r32(scene+0x15c)>=4)return false;
        if(auto click=desktop_pointer::take())if(!blocked && *click==90)native_menu_button(0x1000);
        return true;
    }
    if(kind==1 && valid(scene,0x2200)) {
        switch(mem_r32(scene+0x20e0)) {
            case 1:selector=scene+0x20f4;count=4;break;
            case 4:selector=scene+0x20f8;count=3;break;
            case 7:selector=scene+0x2100;count=3;break;
            case 10:selector=scene+0x2118;count=3;break;
            default:return false;
        }
    }else if(kind==2 && valid(scene,0x4d0) && mem_r32(scene+0x58)==1){selector=scene+0x64;count=7;}
    else if(kind==5 && valid(scene,0x620) && mem_r32(scene+0x84)==1){selector=scene+0x90;count=3;}
    else if(kind!=2 && kind!=5)return false;
    const bool mode_menu=kind==1 && selector==scene+0x2100;
    if(!mode_menu){pending_mode=-1;mode_wait=0;}
    const auto hover=desktop_pointer::read().hover;
    if(!blocked && selector && hover>=100 && unsigned(hover-100)<count) {
        if(!mode_menu)mem_w32(selector,(unsigned)hover-100);
        else if(mem_r32(selector)!=native_mode_value(scene,(unsigned)hover-100))native_menu_button(0,native_mode_value(scene,(unsigned)hover-100)>mem_r32(selector)?0x04000000u:0x08000000u);
    }
    if(auto click=desktop_pointer::take()) {
        if(blocked)return true;
        if(*click==-2){pending_mode=-1;mode_wait=0;native_menu_button(0x200);}
        else if(selector && *click>=100 && unsigned(*click-100)<count){
            if(mode_menu){pending_mode=(int)native_mode_value(scene,(unsigned)*click-100);pending_mode_scene=scene;mode_wait=60;}
            else {mem_w32(selector,(unsigned)*click-100);native_menu_button(0x100);}
        }
        else if(kind==2 && *click>=200 && *click<212){mem_w32(scene+0x64,unsigned(*click-200)/2);native_menu_button(0,(*click%2)?0x02000000u:0x01000000u);}
        else if(*click==150 || *click==151)native_menu_button(0,*click==150?0x01000000u:0x02000000u);
        else if(*click==153 || *click==154)native_menu_button(0,*click==153?0x08000000u:0x04000000u);
        else if(*click==155)native_menu_button(0x100);
        else if(*click==152)native_menu_button(0x100);
    }
    if(mode_menu && mode_wait && pending_mode>=0 && pending_mode_scene==scene) {
        if(mem_r32(selector)==(unsigned)pending_mode){native_menu_button(0x100);pending_mode=-1;mode_wait=0;}
        else if(--mode_wait)native_menu_button(0,(unsigned)pending_mode>mem_r32(selector)?0x04000000u:0x08000000u);
    }
    return true;
}
bool native_standard_regions(CPU* c,uint32_t app) {
    if(!valid(app,0x4c))return false;
    const auto scene=mem_r32(app+0xc),kind=mem_r32(c->r[13]-30528);
    if(!valid(scene,0x100))return false;
    clickable.clear();std::string key="native-"+std::to_string(kind);
    if(kind==0 && valid(scene,0x174) && mem_r32(scene+0x15c)<4)hit(0,0,608,448,90);
    else if(kind==1 && valid(scene,0x2200)) {
        const unsigned phase=mem_r32(scene+0x20e0);key+="-"+std::to_string(phase);
        if(phase==1)for(unsigned i=0;i<4;++i)hit(40,96+86*i,530,65,100+i);
        else if(phase==4)for(unsigned i=0;i<3;++i)hit(40,120+100*i,530,75,100+i);
        else if(phase==7) {
            const auto first=mem_r32(scene+0xd9c);const bool two=native_pane_visible(first);
            const unsigned base=two?0xda8:0xc1c,count=two?2:3;
            // The cursor bar collapses horizontally for the unselected row.
            // Its unscaled bounds still locate the visible label and icon.
            for(unsigned i=0;i<count;++i){const auto pane=mem_r32(scene+base+12*i);if(native_pane_visible(pane))hit(35,read_float(pane+0x34)-55,545,102,100+i);}
            const unsigned selected=mem_r32(scene+0x2100);
            if(selected<count && mem_r8(scene+0x2108+selected)) {
                const auto pane=mem_r32(scene+base+12*selected);const float y=valid(pane,0xb4)?read_float(pane+0x9c)-10:180;
                hit(385,y-20,42,55,150);hit(535,y-20,42,55,151);
            }
            const auto name=mem_r32(scene+0xf58);
            if(selected==2 && native_pane_visible(name)) {
                const float x=read_float(name+0x8c),y=read_float(name+0x9c);
                hit(x-22,y-80,44,35,153);hit(x-22,y+25,44,40,154);
                for(unsigned i=0;i<3;++i)native_hit_pane(mem_r32(scene+0xf64+i*12),155,9);
            }
            native_hit_pane(mem_r32(scene+0x10c0),152,5);
        }else if(phase==10)for(unsigned i=0;i<3;++i)hit(40,105+105*i,530,85,100+i);
        else return false;
    }else if(kind==2 && valid(scene,0x4d0) && mem_r32(scene+0x58)==1) {
        for(unsigned i=0;i<7;++i) {
            const auto pane=mem_r32(scene+0x148+4*i);if(!valid(pane,0xb4))continue;
            const float y=read_float(pane+0x9c);hit(i==6?195:45,y-18,i==6?220:285,36,100+i);
            if(i<6){hit(i==0?320:345,y-20,45,40,200+2*i);hit(i==0?535:510,y-20,45,40,201+2*i);}
        }
    }else if(kind==5 && valid(scene,0x620)) {
        const auto phase=mem_r32(scene+0x84);key+="-"+std::to_string(phase);
        if(phase==1){for(unsigned i=0;i<3;++i)hit(40,90+120*i,530,90,100+i);hit(370,90,48,55,150);hit(550,90,40,55,151);}
        else {hit(15,75,70,70,150);hit(525,75,70,70,151);hit(480,365,110,65,152);}
    }else return false;
    desktop_pointer::publish(key,clickable);return true;
}
bool course_pointer_scene(CPU* c,uint32_t app,uint32_t& scene,unsigned& kind) {
    if(!valid(app,0x4c))return false;
    scene=mem_r32(app+0xc);kind=mem_r32(c->r[13]-30528);
    return (kind==3 && valid(scene,0x398)) || (kind==4 && valid(scene,0x270));
}
void native_set_cup(CPU* c,uint32_t scene,unsigned index) {
    if(index>=5 || mem_r32(c->r[13]-23672)==index)return;
    mem_w32(c->r[13]-23672,index);mem_w32(c->r[13]-23668,0);
    mem_w32(scene+0x354,0);mem_w32(scene+0x348,0);mem_w32(scene+0x34c,0);
    write_float(scene+0x14,read_float(c->r[2]-24564));
}
Json native_course_preview(CPU* c,uint32_t app) {
    uint32_t scene;unsigned kind;
    if(!course_pointer_scene(c,app,scene,kind))return Json::dict({{"op","state"}});
    const unsigned index=kind==3?mem_r32(c->r[13]-23672):mem_r32(scene+0x260);
    if(index>=(kind==3?5u:6u))return Json::dict({{"op","state"}});
    return Json::dict({{"op","preview"},{"scene",(int)kind},{"index",(int)index}});
}
void native_follow_course(CPU* c,uint32_t app) {
    if(!native_selection || native_ui.state!="course" || !room_reply.has("room"))return;
    const auto& room=room_reply.at("room");
    if(room.at("host").boolean() || !room.has("preview"))return;
    const auto& preview=room.at("preview");if(!preview.has("scene") || !preview.has("index"))return;
    uint32_t scene;unsigned kind;if(!course_pointer_scene(c,app,scene,kind) || preview.at("scene").integer(3,4)!=kind)return;
    const auto index=(unsigned)preview.at("index").integer(0,kind==3?4:5);
    if(kind==3)native_set_cup(c,scene,index);else mem_w32(scene+0x260,index);
}
void native_character_pointer(CPU* c,uint32_t app,bool blocked) {
    const auto card_state=mem_r32(c->r[13]-0x5644);if(card_state==3 || card_state==4)return;
    static uint32_t pending_scene=0,pending_cup=0;
    static unsigned pending_ticks=0;
    uint32_t scene;
    if(!character_pointer_scene(c,app,scene)) {
        if(native_standard_pointer(c,app,blocked))return;
        unsigned kind;
        if(!course_pointer_scene(c,app,scene,kind)){pending_ticks=0;return;}
        const auto click=desktop_pointer::take();if(blocked)return;
        const auto hover=desktop_pointer::read().hover;
        if(kind==3 && hover>=30 && hover<35 && mem_r32(scene+0x384)==0 && !pending_ticks)native_set_cup(c,scene,(unsigned)hover-30);
        if(kind==4 && hover>=40 && hover<46 && !mem_r8(scene+0x264))mem_w32(scene+0x260,(unsigned)hover-40);
        if(click) {
            if(kind==3 && *click>=30 && *click<35 && mem_r32(scene+0x384)==0){native_set_cup(c,scene,(unsigned)*click-30);pending_scene=scene;pending_cup=(unsigned)*click-30;pending_ticks=90;}
            if(kind==4 && *click>=40 && *click<46 && !mem_r8(scene+0x264))mem_w32(scene+0x260,(unsigned)*click-40);
            if(*click==-2)pending_ticks=0;
            const uint32_t button=*click==-2?0x200:(*click==22 || (kind==4 && *click>=40 && *click<46))?0x100:0;
            mem_w32(0x803b442c,mem_r32(0x803b442c)|button);mem_w32(0x803b4440,mem_r32(0x803b4440)|button);
        }
        if(pending_ticks) {
            if(kind!=3 || scene!=pending_scene || mem_r32(scene+0x384)!=0 || mem_r32(c->r[13]-23672)!=pending_cup)pending_ticks=0;
            else if(--pending_ticks && read_float(scene+0x14)==read_float(c->r[2]-24576)) {
                mem_w32(0x803b442c,mem_r32(0x803b442c)|0x100);mem_w32(0x803b4440,mem_r32(0x803b4440)|0x100);pending_ticks=0;
            }
        }
        return;
    }
    const auto click=desktop_pointer::take();
    if(native_selection && native_ui.state=="characters" && click && *click==-2) {native_character_cancel(scene);return;}
    if(blocked)return;
    const auto hover=desktop_pointer::read().hover;
    if(hover>=0 && hover<20)mem_w32(scene+0x212c,(unsigned)hover);
    if(click) {
        uint32_t button=0;
        if(*click>=0 && *click<20){mem_w32(scene+0x212c,(unsigned)*click);button=0x100;}
        else if(*click==20){native_menu_button(0,0x01000000u);}
        else if(*click==21){native_menu_button(0,0x02000000u);}
        else if(*click==22)button=0x100;
        else if(*click==-2)button=0x200;
        mem_w32(0x803b442c,mem_r32(0x803b442c)|button);
        mem_w32(0x803b4440,mem_r32(0x803b4440)|button);
    }
}
void native_character_regions(CPU* c,uint32_t app) {
    // The custom title menus already published their hit regions while drawing.
    // Do not erase their controller navigation map in the offline scene pass.
    if(enabled() && !native_selection && page!=Page::Title && page!=Page::Native)return;
    const auto card_state=mem_r32(c->r[13]-0x5644);if(card_state==3 || card_state==4)return;
    uint32_t scene;
    if(!character_pointer_scene(c,app,scene)) {
        if(native_standard_regions(c,app))return;
        unsigned kind;clickable.clear();
        if(course_pointer_scene(c,app,scene,kind)) {
            if(kind==3 && mem_r32(scene+0x384)==0)for(unsigned i=0;i<5;++i)native_hit_pane(mem_r32(scene+0x2dc+4*i),(int)i+30,14);
            else if(kind==4 && !mem_r8(scene+0x264))for(unsigned i=0;i<6;++i)hit(38+184*(i%3),105+155*(i/3),170,145,(int)i+40);
            if(kind==3 && mem_r32(scene+0x384)!=0)native_hit_pane(mem_r32(scene+0x214),22,4);
            else if(kind==4 && mem_r8(scene+0x264))hit(450,65,140,40,22);
            desktop_pointer::publish("course-"+std::to_string(kind)+"-"+std::to_string(kind==3?mem_r32(scene+0x384):mem_r8(scene+0x264)),clickable);
        }else if(valid(app,0x4c) && mem_r32(c->r[13]-30528)!=0)desktop_pointer::publish("native-"+std::to_string(mem_r32(c->r[13]-30528)),{});
        return;
    }
    clickable.clear();
    if(mem_r32(scene+0x2138)>=20 || mem_r32(scene+0x213c)>=20) {
        for(unsigned i=0;i<20;++i)native_hit_pane(mem_r32(scene+0x16d0+(i/2)*0x48+(i%2)*0xc),(int)i,3);
    }else {
        native_hit_pane(mem_r32(scene+0x19c4),20,7);native_hit_pane(mem_r32(scene+0x19d0),21,7);hit(500,205,95,36,22);
    }
    desktop_pointer::publish("character-"+std::to_string(scene)+"-"+std::to_string(mem_r32(scene+0x2144)),clickable);
}
void native_shared_characters(CPU* c,uint32_t app) {
    if(!native_selection || native_ui.state!="characters" || !room_reply.has("room") || mem_r32(c->r[13]-30528)!=1)return;
    const auto scene=mem_r32(app+0xc);if(!valid(scene,0x2200))return;
    const uint32_t colors[]={0xff4848ff,0x489dffff,0xffdb32ff,0x4ce881ff,0xc294ffff,0xffa54fff,0xf4f4f4ff,0x48dfdfff};
    unsigned number=0;
    for(const auto& member:room_reply.at("room").at("members").list()) {
        const auto color=colors[number%8];
        auto mark=[&](unsigned character,bool committed) {
            if(!character || character>20)return;
            const auto index=native_character_index(character);
            float x,y,w,h;
            // Use the actual retail portrait panes, including their animated
            // transforms, so everyone shares the same native grid at any size.
            if(!native_pane_rect(mem_r32(scene+0x16d0+(index/2)*0x48+(index%2)*0xc),x,y,w,h))return;
            if(committed)box(c,x+2,y+2,w-4,h-4,0x05102275);
            const float inset=committed?0.f:2.f+float(number%3)*2;
            x+=inset;y+=inset;w-=2*inset;h-=2*inset;
            const float line=committed?2.5f:1.5f;
            box(c,x-1,y-1,w+2,line+2,0x07112aff);box(c,x-1,y+h-line-1,w+2,line+2,0x07112aff);
            box(c,x-1,y-1,line+2,h+2,0x07112aff);box(c,x+w-line-1,y-1,line+2,h+2,0x07112aff);
            box(c,x,y,w,line,color);box(c,x,y+h-line,w,line,color);
            box(c,x,y,line,h,color);box(c,x+w-line,y,line,h,color);
            const std::string player="P"+std::to_string(number+1);
            box(c,x,y,16,10,color);text(c,player.c_str(),x+2,y,8,0x07112aff);
            if(committed) {
                box(c,x+1,y+h-11,w-2,10,0x07112af0);
                text(c,member.at("initials").str().c_str(),x+3,y+h-11,std::min(8.f,(w-6)/7),0xffffffff);
            }
        };
        for(const auto& character:member.at("characters").list())mark((unsigned)character.integer(1,20),true);
        const bool choosing=!member.at("selected").boolean() && member.at("characters").list().size()<(member.at("role").str()=="solo"?2u:1u);
        if(choosing && member.has("hover")) {
            const auto hover=(unsigned)member.at("hover").integer(0,20);
            if(!native_character_owner(hover))mark(hover,false);
        }
        const float x=9+74*number;
        box(c,x,199,71,12,0x07112ae8);box(c,x,199,2,12,color);
        text(c,("P"+std::to_string(number+1)).c_str(),x+4,200,8,color);
        text(c,member.at("initials").str().c_str(),x+19,200,7,0xffffffff);
        ++number;
    }
}
