// Authored live menu geometry; all labels continue to use the disc's font.
void classic_heading(CPU* c,const char* heading,const char* subtitle) {
    box(c,0,0,608,448,0x0743b5df);
    for(int y=0;y<448;y+=28)for(int x=-28;x<608;x+=56) {
        const float shift=(y/28%2)*28.f;
        const float q[][2]={{x+shift+12.f,(float)y},{x+shift+40.f,(float)y},{x+shift+28.f,y+28.f},{x+shift,y+28.f}};
        flat_polygon(c,q,4,0x0b57c670);
    }
    box(c,0,46,608,4,0xf5b548ff);box(c,0,0,608,46,0x9a071eff);box(c,0,3,608,37,0xde1429ff);
    text(c,heading,17,13,25,0x101841ff);text(c,heading,15,11,25,0xffe95cff);
    if(room_reply.has("room")) {
        const auto& room=room_reply.at("room");rounded(c,388,7,209,31,6,0x062358ff);
        text(c,("ROOM "+room.at("code").str()).c_str(),398,11,12,0xffe95cff);
        text(c,(std::to_string(room.at("members").list().size())+" / 8 PLAYERS").c_str(),450,25,9,0xe8f2ffff);
    }
    box(c,0,53,608,29,0xffefbaff);label(c,subtitle,59,18,0x08266bff);
    for(int i=0;i<5;++i){box(c,i*12,56,8,9,0x93b8dcff);box(c,550+i*12,68,8,9,0x93b8dcff);}
    box(c,0,407,608,41,0x071c51ff);box(c,0,406,608,1,0x82b7f5ff);
}
void classic_kart(CPU* c,float x,float y,uint32_t colour,const Json* front,const Json* back) {
    if(const auto image=authored_kart(c)) {
        native_picture(c,image,x-3,y-4,69,117,colour);
        for(unsigned seat=0;seat<2;++seat)if(seat?back:front) {
            const float sy=y+31+seat*34;oval(c,x+19,sy,23,24,0x071328ff);oval(c,x+21,sy+1,19,20,seat?0x4178e6ff:0xe84242ff);oval(c,x+24,sy+3,9,5,0xffffff80);
        }
        return;
    }
    // Four tyres, metallic frame, body stripe, two individual seats and rear engine.
    rounded(c,x+5,y+92,56,13,5,0x03163d85);
    for(unsigned row=0;row<2;++row)for(unsigned side=0;side<2;++side) {
        const float wx=x+side*47,wy=y+14+row*57;
        rounded(c,wx,wy,13,24,4,0x101825ff);box(c,wx+3,wy+3,2,18,0x566172ff);box(c,wx+8,wy+3,1,18,0x67717dff);
    }
    rounded(c,x+10,y,40,103,9,0xb6c9d7ff);rounded(c,x+12,y+2,36,99,8,colour);
    box(c,x+26,y+3,8,94,0xf8f3dfff);
    rounded(c,x+15,y+14,30,31,9,0x101b2fff);rounded(c,x+15,y+52,30,31,9,0x101b2fff);
    for(unsigned seat=0;seat<2;++seat) {
        const float sy=y+17+seat*38;rounded(c,x+19,sy,22,24,6,0x45536bff);rounded(c,x+22,sy+3,16,19,5,0x253149ff);
        const auto* rider=seat?back:front;
        if(rider){oval(c,x+18,sy-1,25,26,0x09152aff);oval(c,x+20,sy,21,22,seat?0x4178e6ff:0xe84242ff);oval(c,x+23,sy+2,10,6,0xffffff80);}
    }
    rounded(c,x+5,y+1,51,9,3,0xededddff);box(c,x+10,y+3,41,4,colour);
    rounded(c,x+12,y+88,36,12,3,0x444f60ff);for(unsigned i=0;i<4;++i)box(c,x+17+i*7,y+90,3,7,0xa6b4bfff);
    oval(c,x+23,y+101,6,6,0xbac4ceff);oval(c,x+36,y+101,6,6,0xbac4ceff);
}
void classic_flag(CPU* c,float x,float y,float size) {
    box(c,x,y,3,size*1.3f,0xdbdce3ff);
    for(unsigned row=0;row<4;++row)for(unsigned col=0;col<5;++col)box(c,x+3+col*size/5,y+row*size/5,size/5,size/5,(row+col)%2?0x111c39ff:0xfff9e5ff);
}
void classic_trophy(CPU* c,float x,float y) {
    classic_flag(c,x-33,y+9,43);classic_flag(c,x+49,y+9,43);
    oval(c,x-9,y+8,75,45,0xa66c12ff);oval(c,x-3,y+14,63,30,0x083483ff);
    rounded(c,x,y,57,52,12,0xffc626ff);rounded(c,x+4,y+3,48,43,10,0xffe17aff);
    box(c,x+24,y+47,10,19,0xf9b929ff);rounded(c,x+8,y+64,43,9,3,0xd1920dff);
    oval(c,x+17,y+13,24,25,0xffbb21ff);
}
void classic_balloons(CPU* c,float x,float y) {
    const uint32_t colors[]={0xea3a43ff,0x1d9df6ff,0x3dd754ff};
    for(unsigned i=0;i<3;++i){const float bx=x+i*30,by=y+(i==1?0:17);box(c,bx+20,by+43,1,29,0xc9e0e7ff);oval(c,bx,by,43,51,0x031b52ff);oval(c,bx+2,by+2,39,46,colors[i]);oval(c,bx+9,by+7,10,16,0xffffff92);}
}
void classic_card(CPU* c,float x,float y,float w,float h,bool selected) {
    if(selected)rounded(c,x-3,y-3,w+6,h+6,10,0xffcf2fff);
    rounded(c,x,y,w,h,8,selected?0xfff4afff:0xb9d4f3ff);rounded(c,x+3,y+3,w-6,h-6,6,0x082d75ff);
}
