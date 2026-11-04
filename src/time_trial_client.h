#pragma once
#include "time_trial_record.h"
#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <optional>
#include <thread>
#include <vector>

namespace mkdd::trials {
struct TrialEntry {std::string id,name,digest;unsigned time_ms=0;};
enum class TrialAction {List,Download,Upload};
struct TrialRequest {
    TrialAction action=TrialAction::List;
    unsigned course=0;
    TrialEntry entry;
    Record record{};
    std::string display_name; // Snapshot the current profile when Upload is chosen.
};
struct TrialResponse {
    TrialAction action=TrialAction::List;
    unsigned course=0;
    std::vector<TrialEntry> entries;
    Record record{};
    std::string id,error;
};
struct TrialHttpRequest {std::string method,path,key,build;std::vector<uint8_t> body;std::string display_name;bool public_scope=false;};
struct TrialHttpResponse {unsigned status=0;std::string digest;std::vector<uint8_t> body;};
using TrialTransport=std::function<TrialHttpResponse(const TrialHttpRequest&,const std::function<bool()>&)>;
std::string ghost_digest(std::span<const uint8_t> bytes);
TrialResponse trial_transfer(const TrialRequest&,const std::string& key,const std::string& build,const TrialTransport&,const std::function<bool()>& cancelled,bool public_scope=false);
TrialHttpResponse trial_https(const TrialHttpRequest&,const std::function<bool()>& cancelled);

// One bounded request at a time. No worker accesses guest memory, SDL or menus.
// Cancel invalidates late results immediately; it never joins from a frame.
class TrialClient {
public:
    explicit TrialClient(TrialTransport transport=trial_https):transport_(std::move(transport)){}
    ~TrialClient();
    bool submit(TrialRequest request,std::string config_path,std::string build);
    std::optional<TrialResponse> take();
    void cancel();
    bool busy()const;
private:
    void run();
    struct Job {TrialRequest request;std::string config_path,build;uint64_t generation;};
    TrialTransport transport_;
    mutable std::mutex mutex_;
    std::condition_variable wake_;
    std::thread worker_;
    std::optional<Job> job_;
    std::optional<TrialResponse> response_;
    std::atomic<uint64_t> generation_{0};
    bool working_=false,stop_=false;
};
}
