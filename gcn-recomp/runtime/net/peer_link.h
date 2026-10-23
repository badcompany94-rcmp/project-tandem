#pragma once
#include <array>
#include <cstdint>
#include <memory>
#include <string>

namespace net {
using Inputs = std::array<uint8_t, 32>;
using Admission = std::array<uint8_t, 16>;
constexpr unsigned kMaxPeers = Inputs{}.size()/2;
// Experimental ordered-input transport. The host sequences one contribution
// per peer per tick. No prediction, state authority, or invitation service.
class PeerLink {
public:
    PeerLink();
    ~PeerLink();
    // Configure before connecting. Future inputs travel on a worker so a small
    // fixed delay can absorb round-trip time without a network wait every frame.
    bool input_delay(unsigned ticks);
    // Negotiate a bounded asynchronous input window. May use exchange() with a
    // fixed startup delay, then drain_startup() for exactly that many ticks and
    // switch once to submit_prediction()/poll_prediction(). The owner provides
    // hashes of the SAME CONFIRMED simulation tick on every peer, never hashes
    // of predicted state. Transport still rejects every checksum disagreement.
    bool prediction_window(unsigned ticks);
    // A trusted, authenticated mesh adapter on loopback assembles bundles
    // locally. Negotiated explicitly; permits reserved slot zero on join().
    bool local_mesh(bool enabled);
    bool drain_startup(uint64_t tick,Inputs& out);
    enum class Poll {Pending,Ready,Failed};
    // Host-only pacing metadata travels with immutable input, outside game state.
    // A shared maximum prevents one peer from queuing for everybody else while
    // the others continue sending their own input too late for that peer.
    struct Pacing { bool available=false;unsigned delay=0,minimum_lead=0; };
    bool submit_prediction(uint64_t tick,uint64_t confirmed_hash,const uint8_t local[2],unsigned delay=0,unsigned lead=0);
    Poll poll_prediction(uint64_t& tick,Inputs& out,Pacing* pacing=nullptr);
    // Optional private-room admission: roster slots are fixed before connecting,
    // rather than assigned by arrival order. Empty admissions retain test mode.
    bool host(const std::string& endpoint, unsigned players, uint64_t compatibility,
              const Admission* admissions=nullptr);
    bool join(const std::string& endpoint, uint64_t compatibility,
              unsigned requested_slot=kMaxPeers, const Admission* admission=nullptr);
    bool exchange(uint64_t tick, uint64_t previous_hash, const uint8_t local[2], Inputs& out);
    unsigned slot() const;
    unsigned players() const;
    const std::string& error() const;
private:
    void start_worker(); // caller holds the transport mutex
    bool exchange_sync(uint64_t tick,uint64_t previous_hash,const uint8_t local[2],Inputs& out);
    struct Impl;
    std::unique_ptr<Impl> p;
};
}
