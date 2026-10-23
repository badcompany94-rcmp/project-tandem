// Thread-safe application-provided online controls for the desktop frontend.
// The application consumes commands and performs its own authorization checks.
#pragma once
#include <mutex>
#include <optional>
#include <string>
#include <vector>
namespace desktop_online_ui {
struct Member {std::string name;unsigned seat=0;bool you=false,host=false;std::string id;};
struct State {
    bool available=false,can_join=false,can_edit=false,can_kick=false,can_return=false,can_refresh=false;
    std::string name,code,version,status;
    std::vector<Member> members;
};
enum class Action {Join,Name,Kick,Return,Refresh};
struct Command {Action action;std::string text;unsigned seat=0;};
inline std::mutex mutex;
inline State state;
inline std::optional<Command> pending;
inline void publish(State value){std::lock_guard lock(mutex);state=std::move(value);}
inline State read(){std::lock_guard lock(mutex);return state;}
inline bool submit(Command value){std::lock_guard lock(mutex);if(pending)return false;pending=std::move(value);return true;}
inline std::optional<Command> take(){std::lock_guard lock(mutex);auto value=std::move(pending);pending.reset();return value;}
}
