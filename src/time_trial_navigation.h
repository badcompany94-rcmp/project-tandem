#pragma once

#include <array>
#include <cstdint>
#include <optional>

namespace mkdd::trials {
inline constexpr std::array<unsigned,16> menu_courses={
    36,34,33,50,40,37,35,42,51,41,38,45,43,44,47,49};
inline std::optional<unsigned> course_slot(unsigned course) {
    for(unsigned i=0;i<menu_courses.size();++i)if(menu_courses[i]==course)return i;
    return {};
}

// A stationary mouse must not undo a controller selection on the following
// frame. Actual movement or a click hands focus back to the mouse immediately.
class MenuPointerFocus {
public:
    bool controller_active()const{return controller_;}
    int hover(int hovered,float x,float y,bool controller,bool clicked) {
        if(controller)controller_=true;
        else if(clicked || x!=x_ || y!=y_)controller_=false;
        x_=x;y_=y;
        return controller_?-1:hovered;
    }
private:
    bool controller_=false;
    float x_=0,y_=0;
};

// Host-menu navigation only. Independent of retail repeat pulses and render
// rate: one nudge, a deliberate hold delay, then bounded scrolling. Separate
// engage/release thresholds and a short neutral dwell reject stick chatter.
class MenuAxisRepeat {
public:
    int step(int axis,bool negative,bool positive,uint64_t now) {
        if(axis<=-48)axis_= -1;
        else if(axis>=48)axis_=1;
        else if(axis>=-22 && axis<=22)axis_=0;
        const int direction=negative||positive?(negative==positive?0:negative?-1:1):axis_;
        if(!direction){
            if(!neutral_){neutral_=true;neutral_since_=now;}
            return 0;
        }
        if(neutral_ && now-neutral_since_>=50)held_=0;
        neutral_=false;
        if(direction!=held_){
            if(now<cooldown_)return 0;
            held_=direction;next_=now+400;cooldown_=now+120;return direction;
        }
        if(now>=next_){next_=now+130;cooldown_=now+120;return direction;}
        return 0;
    }
private:
    int axis_=0,held_=0;
    bool neutral_=false;
    uint64_t neutral_since_=0,next_=0,cooldown_=0;
};

// Drive only the existing, ready course-selector states. Character selection,
// resource loading, race creation and ghost playback remain owned by the game.
class GhostCourseRoute {
public:
    enum class Action { Wait,SetCup,SetCourse,Confirm,Cancelled,Failed };
    bool begin(unsigned course) {
        slot_=course_slot(course);ticks_=0;return slot_.has_value();
    }
    void clear(){slot_.reset();ticks_=0;}
    bool active()const{return slot_.has_value();}
    unsigned cup()const{return *slot_/4;}
    unsigned index()const{return *slot_%4;}
    Action step(unsigned cup,unsigned course,unsigned phase,bool ready,bool cancel) {
        if(!active())return Action::Wait;
        if(cancel){clear();return Action::Cancelled;}
        if(++ticks_>1200){clear();return Action::Failed;}
        if(phase>2 || !ready)return Action::Wait;
        if(cup!=this->cup())return phase==0?Action::SetCup:Action::Failed;
        if(phase && course!=index())return phase==1?Action::SetCourse:Action::Failed;
        return Action::Confirm;
    }
private:
    std::optional<unsigned> slot_;
    unsigned ticks_=0;
};
}
