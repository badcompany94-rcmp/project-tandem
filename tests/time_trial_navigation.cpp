#include "time_trial_navigation.h"
#include <cstdio>
#include <cstdlib>
using mkdd::trials::GhostCourseRoute;
using Action=GhostCourseRoute::Action;
#define CHECK(x) do{if(!(x)){std::fprintf(stderr,"line %d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
int main(){
    using mkdd::trials::MenuAxisRepeat;
    for(unsigned hz:{60u,120u,165u}){
        MenuAxisRepeat input;unsigned moves=0;uint64_t last=0;
        for(unsigned frame=0;frame<hz;++frame){
            const uint64_t now=1000+frame*1000/hz;
            if(input.step(100,false,false,now)){
                if(moves)CHECK(now-last>=130);
                if(moves==1)CHECK(now>=1400);
                last=now;++moves;
            }
            CHECK(input.step(100,false,false,now)==0); // duplicate update
        }
        CHECK(moves==6);
        CHECK(input.step(100,false,false,10000)==1); // no catch-up burst
        CHECK(input.step(100,false,false,10000)==0);
    }
    MenuAxisRepeat input;
    CHECK(input.step(47,false,false,1000)==0);
    CHECK(input.step(48,false,false,1000)==1);
    CHECK(input.step(30,false,false,1010)==0);
    CHECK(input.step(0,false,false,1020)==0);
    CHECK(input.step(60,false,false,1040)==0); // brief neutral chatter
    CHECK(input.step(-90,false,false,1050)==0); // snapback cooldown
    CHECK(input.step(0,false,false,1060)==0);
    CHECK(input.step(-90,false,false,1120)==-1); // deliberate opposite nudge
    CHECK(input.step(0,false,false,1140)==0);
    CHECK(input.step(-90,false,false,1260)==-1); // deliberate second nudge
    MenuAxisRepeat digital;
    CHECK(digital.step(0,true,false,0)==-1);
    CHECK(digital.step(0,true,false,399)==0);
    CHECK(digital.step(0,true,false,400)==-1);
    CHECK(digital.step(0,true,true,410)==0);
    CHECK(digital.step(0,false,true,540)==1);
    std::puts("PASS: menu hold timing at 60/120/165 Hz, duplicate updates, jitter, snapback, digital input and stalls");
    // Every source/destination pair, including non-first courses and cups.
    for(unsigned target:mkdd::trials::menu_courses)for(unsigned from=0;from<16;++from){
        GhostCourseRoute route;CHECK(route.begin(target));
        unsigned cup=from/4,index=from%4,phase=0,confirms=0;
        CHECK(route.step(cup,index,phase,false,false)==Action::Wait);
        for(unsigned tick=0;tick<20 && phase<3;++tick){
            switch(route.step(cup,index,phase,true,false)){
                case Action::SetCup:cup=route.cup();index=0;break;
                case Action::SetCourse:index=route.index();break;
                case Action::Confirm:++confirms;++phase;break;
                default:CHECK(false);
            }
        }
        CHECK(phase==3 && confirms==3);
        CHECK(mkdd::trials::menu_courses[cup*4+index]==target);
        CHECK(route.step(cup,index,3,true,false)==Action::Wait);
        route.clear();CHECK(!route.active());
    }
    GhostCourseRoute route;CHECK(!route.begin(0));CHECK(!route.begin(999));
    CHECK(route.begin(49));CHECK(route.step(0,0,0,true,true)==Action::Cancelled);
    CHECK(!route.active());
    CHECK(route.begin(49));CHECK(route.step(0,0,1,true,false)==Action::Failed);
    CHECK(route.step(3,0,2,true,false)==Action::Failed); // never confirm wrong track
    CHECK(route.begin(36));
    for(unsigned i=0;i<1200;++i)CHECK(route.step(0,0,0,false,false)==Action::Wait);
    CHECK(route.step(0,0,0,false,false)==Action::Failed);CHECK(!route.active());
    std::puts("PASS: 256 ghost-course routes, native readiness, cancel, mismatch and timeout");
}
