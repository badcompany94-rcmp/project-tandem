#include "time_trial_navigation.h"
#include <cstdio>
#include <cstdlib>
using mkdd::trials::GhostCourseRoute;
using Action=GhostCourseRoute::Action;
#define CHECK(x) do{if(!(x)){std::fprintf(stderr,"line %d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
int main(){
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
