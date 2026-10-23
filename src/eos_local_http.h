#pragma once
#include "online_json.h"
#include <functional>
#include <memory>

namespace mkdd::eos {
using online::Json;
// Private, bounded loopback IPC between the game's menus and the EOS helper.
// This listener is never exposed to the Internet or used for NAT traversal.
class LocalHttp {
public:
    explicit LocalHttp(unsigned port=0,bool eos_ready=false);
    ~LocalHttp();
    unsigned port()const;
    void poll(const std::function<Json(const Json&)>& handler);
private:
    struct Impl;std::unique_ptr<Impl> impl_;
};
Json local_request(unsigned port,const Json& request);
}
