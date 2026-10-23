#pragma once
#include "eos_runtime.h"
#include <memory>

namespace mkdd::eos {
class Bridge {
public:
    Bridge(Runtime&,unsigned upstream_port);
    ~Bridge();
    online::Json request(const online::Json&);
    void pump();
    online::Json statistics(uint64_t sequence)const;
private:
    struct Impl;std::unique_ptr<Impl> impl_;
};
}
