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
