// Included at the logical-pad boundary. Advance the native main loop and rewind
// late input during a race; use confirmed input across resource transitions.
struct PredictedFrame {
    uint64_t tick,hash;
    std::string drivers;
    mkdd::NativeCheckpoint before;
    net::Inputs used{};
    std::array<uint8_t,2> local{};
    std::array<uint8_t,2> sampled{};
    std::array<PadState,4> pads{};
};
struct NativePrediction {
    bool active=false,replaying=false;
    uint64_t frontier=0,confirmed=0,rollbacks=0,replayed=0;
    double stalled_ms=0,snapshot_ms=0;
    double quiesce_ms=0,restore_ms=0;
    uint64_t correction_started=0;
    double correction_ms=0,maximum_correction_ms=0;
    uint64_t boundaries=0;
    uint64_t snapshots=0;
    uint64_t confirmed_captures_skipped=0;
    double receive_ms=0,canonical_wait_ms=0;
    net::Inputs seed{};
    std::array<uint8_t,2> candidate{},fresh{};
    std::array<PadState,4> physical{};
    std::deque<PredictedFrame> frames;
    std::vector<mkdd::NativeCheckpoint> spares;
    std::map<uint64_t,net::Inputs> actual;
    // Sent inputs outlive the frame that sampled them during presentation
    // buffering. Keep them until the corresponding simulation is confirmed.
    std::map<uint64_t,std::array<uint8_t,2>> submitted;
    struct Submitted {uint64_t at;unsigned lead;};
    std::map<uint64_t,Submitted> submitted_at;
    unsigned shared_delay=1;
    bool pacing_prepared=false;
    uint64_t sent=0,received=0;
    mkdd::online::AdaptiveInputDelay pacing;
    uint64_t pacing_time=0;
    double pacing_work=0;
    ReplayGuard guard;
    FILE* trace=nullptr;
} prediction;
std::string prediction_drivers() {
    std::string result;const auto ctrl=mem_r32(0x803cc588);
    if(!valid_ptr(ctrl,0x220))return result;
    for(unsigned i=0;i<human_karts();++i) {
        const auto body=mem_r32(ctrl+0xa0+i*4);if(!valid_ptr(body,0x600))return {};
        result.push_back(char('0'+mem_r8(body+0x5b2)));
    }
    return result;
}
PredictedFrame& predicted_frame(uint64_t tick) {
    for(auto& frame:prediction.frames)if(frame.tick==tick)return frame;
    fatal("Prediction checkpoint for tick %llu expired",(unsigned long long)tick);
}
void prediction_receive() {
    const auto began=host_ns();
    for(;;) {
        uint64_t tick=0;net::Inputs actual{};net::PeerLink::Pacing pacing{};
        auto result=link->poll_prediction(tick,actual,&pacing);
        if(result==net::PeerLink::Poll::Failed){mkdd::online::recover_room_disconnect();fatal("[native-rollback] %s",link->error().c_str());}
        if(result==net::PeerLink::Poll::Pending) {
            prediction.receive_ms+=(host_ns()-began)/1e6;return;
        }
        const auto found=prediction.submitted.find(tick);const unsigned slot=link->slot();
        if(tick!=prediction.received+1 || found==prediction.submitted.end())fatal("Unexpected confirmed prediction tick");
        if(found->second[0]!=actual[slot*2] || found->second[1]!=actual[slot*2+1])fatal("Confirmed input changed local contribution");
        auto timestamp=prediction.submitted_at.find(tick);
        if(timestamp==prediction.submitted_at.end())fatal("Missing submitted input timestamp");
        const double age=(host_ns()-timestamp->second.at)/1e6;
        prediction.pacing.received(age,pacing.available?timestamp->second.lead:0,pacing.available?pacing.minimum_lead:0);
        if(pacing.available)prediction.shared_delay=pacing.delay;
        prediction.submitted_at.erase(timestamp);
        if(!prediction.actual.emplace(tick,actual).second)fatal("Duplicate confirmed prediction input");
        prediction.received=tick;
    }
}
void prediction_choose(PredictedFrame& frame) {
    auto actual=prediction.actual.find(frame.tick);
    if(actual!=prediction.actual.end())frame.used=actual->second;
    else frame.used=frame.tick==prediction.frames.front().tick?prediction.seed:predicted_frame(frame.tick-1).used;
    frame.used[2*link->slot()]=frame.local[0];frame.used[2*link->slot()+1]=frame.local[1];
    network_pads={};
    for(unsigned i=0;i<link->players();++i) {
        network_pads[2*seats[i]]=frame.used[2*i];network_pads[2*seats[i]+1]=frame.used[2*i+1];
    }
    network_override=true;
    auto& guard=prediction.guard;
    guard.runtime_devices=true;guard.runtime_graphics=true;guard.render_graphics=true;
    guard.record_outputs=false;guard.publish_outputs=!prediction.replaying;
    guard.maximum_edges=g_vcount+10000000;guard.pads=frame.pads;
    gx::pipeline_suppress_present(prediction.replaying);g_replay_guard=&guard;
}
void prediction_capture(mkdd::NativeCheckpoint& target) {
    const auto start=host_ns();target.refresh(true);
    prediction.snapshot_ms+=(host_ns()-start)/1e6;++prediction.snapshots;
}
// Host-only timing survives rewinds. Catch-up includes restore, capture and
// graphics waits: the categories overlap and must not be added together.
// Unlike the adaptive controller, diagnostics retain intervals over 250 ms.
void prediction_timing(bool racing,uint64_t tick) {
    auto& s=prediction;
    static uint64_t last_time=0;
    static std::vector<double> intervals;
    static double last_costs[6]{},totals[6]{};
    static uint64_t last_captures=0,last_skipped=0,captures=0,skipped=0;
    const auto now=host_ns();
    const double costs[]={s.snapshot_ms,s.quiesce_ms,s.restore_ms,s.correction_ms,s.stalled_ms,s.receive_ms};
    if(racing && last_time) {
        intervals.push_back((now-last_time)/1e6);
        for(unsigned i=0;i<6;++i)totals[i]+=costs[i]-last_costs[i];
        captures+=s.snapshots-last_captures;skipped+=s.confirmed_captures_skipped-last_skipped;
        if(intervals.size()==120) {
            double sum=0;unsigned slow=0;
            for(double value:intervals){sum+=value;slow+=value>25;}
            std::sort(intervals.begin(),intervals.end());
            diagnostic_log("[race-pacing] tick=%llu frames=120 fps=%.2f p95_ms=%.2f p99_ms=%.2f max_ms=%.2f over25=%u captures=%llu skipped=%llu capture_ms=%.3f graphics_wait_ms=%.3f restore_ms=%.3f catchup_ms=%.3f network_wait_ms=%.3f poll_ms=%.3f\n",
                (unsigned long long)tick,120000/sum,intervals[113],intervals[118],intervals.back(),slow,
                (unsigned long long)captures,(unsigned long long)skipped,
                totals[0]/120,totals[1]/120,totals[2]/120,totals[3]/120,totals[4]/120,totals[5]/120);
            intervals.clear();std::fill(std::begin(totals),std::end(totals),0);captures=skipped=0;
        }
    } else {
        intervals.clear();std::fill(std::begin(totals),std::end(totals),0);captures=skipped=0;
    }
    last_time=racing?now:0;
    std::copy(std::begin(costs),std::end(costs),last_costs);
    last_captures=s.snapshots;last_skipped=s.confirmed_captures_skipped;
}
void prediction_coordinator(void*) {
    auto& s=prediction;
    // Complete all previous guest-memory reads and GPU writes before copying or
    // restoring any state. Keep the previous frame's side-effect policy until
    // its pending FIFO commands have reached the renderer.
    const auto began=host_ns();checkpoint_graphics_quiesce();
    if(!getenv("MKDD_PREDICTION_ASYNC_RENDER"))gx::pipeline_control([]{});
    g_replay_guard=nullptr;
    s.quiesce_ms+=(host_ns()-began)/1e6;++s.boundaries;
    const uint64_t next=pad_tick+1;
    prediction_hashes[next]=simulation_hash();
    while(prediction_hashes.size()>128)prediction_hashes.erase(prediction_hashes.begin());
    if(!s.active) {
        s.active=true;s.frontier=next;s.confirmed=next-1;s.sent=next-1;s.received=next-1;
        network_input_delay_override.store(0);
        for(unsigned i=0;i<link->players();++i) {
            s.seed[2*i]=logical_pads[seats[i]][0];s.seed[2*i+1]=logical_pads[seats[i]][1];
        }
        if(const char* path=getenv("MKDD_PREDICTION_TRACE")) {
            s.trace=fopen(path,"w");if(!s.trace)fatal("Cannot open confirmed prediction trace");
            fprintf(s.trace,"tick,hash,inputs,drivers\n");
        }
        diagnostic_log("[native-rollback] enabled at tick %llu; window=%u; race input delay=%s\n",
            (unsigned long long)next,prediction_window,getenv("MKDD_PREDICTION_INPUT_DELAY")?getenv("MKDD_PREDICTION_INPUT_DELAY"):"auto (1..16 frames, may recover to 0)");
    }
    if(s.replaying && next<s.frontier) {
        prediction_receive();
        auto& frame=predicted_frame(next);
        if(s.candidate!=frame.sampled)fatal("Historical local controller sample differs at tick %llu",(unsigned long long)next);
        // Ordered authoritative input cannot change once received. A corrected
        // frame with that input will be retired at the frontier, so saving it
        // again wastes a full machine/GPU checkpoint during catch-up. Unknown
        // frames still require fresh checkpoints for subsequent corrections.
        if(s.actual.contains(next))frame.before=mkdd::NativeCheckpoint{};
        else prediction_capture(frame.before);
        frame.hash=prediction_hashes.at(next);frame.drivers=prediction_drivers();
        prediction_choose(frame);++s.replayed;return;
    }
    if(next!=s.frontier)fatal("Native prediction frontier mismatch");
    if(s.replaying && s.candidate!=s.fresh)fatal("Pending local controller changed during correction");
    if(s.replaying) {
        pad_publish_motors();
        const double elapsed=(host_ns()-s.correction_started)/1e6;
        s.correction_ms+=elapsed;s.maximum_correction_ms=std::max(s.maximum_correction_ms,elapsed);
    }
    s.replaying=false;
    static const uint64_t limit=getenv("MKDD_NET_TICKS")?strtoull(getenv("MKDD_NET_TICKS"),nullptr,10):0;
    for(;;) {
        prediction_receive();
        for(auto& frame:s.frames) {
            auto actual=s.actual.find(frame.tick);
            if(actual==s.actual.end() || actual->second==frame.used)continue;
            if(!frame.before.threads)fatal("Speculative frame has no rollback checkpoint");
            s.replaying=true;++s.rollbacks;s.correction_started=host_ns();
            const auto restore_began=host_ns();frame.before.restore();s.restore_ms+=(host_ns()-restore_began)/1e6;
            prediction_choose(frame);++s.replayed;return;
        }
        while(!s.frames.empty()) {
            auto& frame=s.frames.front();auto actual=s.actual.find(frame.tick);
            if(actual==s.actual.end())break;
            if(frame.used!=actual->second)fatal("Uncorrected input cannot be confirmed");
            if(s.trace) {
                fprintf(s.trace,"%llu,%016llx,",(unsigned long long)frame.tick,(unsigned long long)frame.hash);
                for(uint8_t v:frame.used)fprintf(s.trace,"%02x",v);fprintf(s.trace,",%s\n",frame.drivers.c_str());
            }
            s.confirmed=frame.tick;s.seed=frame.used;s.actual.erase(actual);
            s.submitted.erase(frame.tick);
            frame.before.devices.clear();frame.before.threads.reset();frame.before.memory.reset();
            s.spares.push_back(std::move(frame.before));s.frames.pop_front();
        }
        if(limit && next>limit && s.frames.empty()) {
            if(s.trace){fflush(s.trace);fclose(s.trace);s.trace=nullptr;}
            diagnostic_log("[native-rollback] COMPLETE confirmed=%llu final_hash=%016llx rollbacks=%llu replayed=%llu stalled_ms=%.2f capture_ms=%.3f\n",
                (unsigned long long)s.confirmed,(unsigned long long)simulation_hash(),(unsigned long long)s.rollbacks,
                (unsigned long long)s.replayed,s.stalled_ms,s.snapshot_ms/(s.snapshots?s.snapshots:1));
            diagnostic_log("[native-rollback-cost] quiesce_ms=%.3f restore_ms=%.3f\n",s.quiesce_ms/s.boundaries,s.restore_ms/(s.rollbacks?s.rollbacks:1));
            diagnostic_log("[native-rollback-correction] average_ms=%.3f maximum_ms=%.3f\n",s.correction_ms/(s.rollbacks?s.rollbacks:1),s.maximum_correction_ms);
            diagnostic_log("[native-rollback-work] captures=%llu confirmed_skipped=%llu poll_ms=%.3f canonical_wait_ms=%.3f\n",
                (unsigned long long)s.snapshots,(unsigned long long)s.confirmed_captures_skipped,s.receive_ms,s.canonical_wait_ms);
            plat_exit_now(0);
        }
        const auto race=mkdd::race_read();
        const bool canonical=getenv("MKDD_NET_SERIES") && (!race.exists || race.phase!=17);
        if(s.frames.size()<(canonical?1u:prediction_window) && (!limit || next<=limit))break;
        const auto began=host_ns();std::this_thread::sleep_for(std::chrono::milliseconds(1));
        s.stalled_ms+=(host_ns()-began)/1e6;
    }
    PredictedFrame frame;frame.tick=next;frame.hash=prediction_hashes.at(next);
    frame.drivers=prediction_drivers();
    if(!s.spares.empty()){frame.before=std::move(s.spares.back());s.spares.pop_back();}
    const auto race=mkdd::race_read();
    const bool canonical=getenv("MKDD_NET_SERIES") && (!race.exists || race.phase!=17);
    // Authoritative inputs arrive in order and are immutable. If this frame is
    // already confirmed, every earlier frame is confirmed too; none can force
    // a rewind through this boundary. Avoid copying machine/GPU state for it.
    // Keep an opt-out for same-binary correctness and performance comparisons.
    static const bool capture_confirmed=getenv("MKDD_PREDICTION_CAPTURE_CONFIRMED")!=nullptr;
    if(!canonical) {
        if(capture_confirmed || !s.actual.contains(next))prediction_capture(frame.before);
        else {frame.before=mkdd::NativeCheckpoint{};++s.confirmed_captures_skipped;}
    }
    frame.sampled=s.fresh;frame.pads=s.physical;
    // Presentation and loader phases cannot rewind across heap/thread changes.
    // Pipeline confirmed inputs there instead of paying a round trip per frame.
    // On entering the stable race/countdown phase, stop looking ahead; its
    // queued samples drain before GO to the small host-side racing budget.
    static const int fixed_delay=getenv("MKDD_PREDICTION_INPUT_DELAY")?int(number("MKDD_PREDICTION_INPUT_DELAY",0,mkdd::online::AdaptiveInputDelay::maximum)):-1;
    if(fixed_delay<0) {
        // Capturing an unchanged checkpoint is baseline rendering/simulation cost,
        // not evidence that extra input queuing will fix a network hitch.
        const auto now=host_ns();const double work=s.correction_ms+s.stalled_ms;
        const bool racing=race.exists && race.phase==17 && race.run_state==2 && !race.paused;
        // Warm up throughout the countdown, rather than freezing an estimate
        // made before any packets arrived. Peers learn the shared budget before GO.
        if(!racing) {
            const unsigned before=s.pacing.delay();s.pacing.prepare();
            if(before!=s.pacing.delay())diagnostic_log("[adaptive-buffer] warmup delay_frames=%u shared_frames=%u\n",s.pacing.delay(),s.shared_delay);
            s.pacing_prepared=false;
        }else if(!s.pacing_prepared) {
            s.pacing_prepared=true;
            diagnostic_log("[adaptive-buffer] prepared delay_frames=%u from observed input delivery\n",s.pacing.prepare());
            s.pacing_time=0;s.pacing_work=work;
        }
        if(race.exists && race.phase==17 && race.run_state==2 && !race.paused) {
            if(s.pacing_time) {
                const bool unchanged=!s.submitted.empty() && s.submitted.rbegin()->second==s.fresh;
                auto result=s.pacing.observe((now-s.pacing_time)/1e6,std::max(0.0,work-s.pacing_work),unchanged);
                if(result.evaluated)diagnostic_log("[adaptive-buffer] delay_frames=%u changed=%d fps=%.2f slow_percent=%.1f network_work_ms=%.2f network_spike_percent=%.1f shared_frames=%u delivery_frames=%u queued_frames=%llu\n",
                    result.delay,result.changed,result.fps,100*result.slow_fraction,result.network_ms,100*result.network_spike_fraction,
                    s.shared_delay,s.pacing.delivery_delay(),(unsigned long long)(s.sent>=next?s.sent-next:0));
            }
            s.pacing_time=now;s.pacing_work=work;
        }else {s.pacing.reset_window();s.pacing_time=0;s.pacing_work=work;}
    }
    const unsigned race_delay=fixed_delay<0?std::max(s.pacing.delay(),s.shared_delay):unsigned(fixed_delay);
    const unsigned presentation_delay=std::max({prediction_window-1,s.pacing.delivery_delay(),race_delay});
    const uint64_t target=next+(canonical?presentation_delay:race_delay);
    const uint64_t send_to=limit?std::min(target,limit):target;
    while(s.sent<send_to) {
        prediction_receive();
        if(s.sent-s.received>=prediction_inflight) {
            const auto began=host_ns();std::this_thread::sleep_for(std::chrono::milliseconds(1));
            s.stalled_ms+=(host_ns()-began)/1e6;continue;
        }
        const uint64_t tick=s.sent+1,checked=tick-32;
        if(checked>s.confirmed)fatal("Cannot send an unconfirmed state checksum");
        s.submitted.emplace(tick,s.fresh);
        const unsigned lead=unsigned(tick-next);
        s.submitted_at.emplace(tick,NativePrediction::Submitted{host_ns(),lead});
        if(!link->submit_prediction(tick,prediction_hashes.at(checked),s.fresh.data(),fixed_delay<0?s.pacing.delay():race_delay,lead)){mkdd::online::recover_room_disconnect();fatal("[native-rollback] %s",link->error().c_str());}
        s.sent=tick;
    }
    frame.local=s.submitted.at(next);
    s.frames.push_back(std::move(frame));
    ++s.frontier;
    // Results and loaders must consume confirmed input. Their heap/thread
    // transitions cannot be replayed across a speculative race checkpoint.
    if(canonical) {
        while(!s.actual.contains(next)) {
            prediction_receive();
            if(!s.actual.contains(next)) {
                const auto began=host_ns();std::this_thread::sleep_for(std::chrono::milliseconds(1));
                s.canonical_wait_ms+=(host_ns()-began)/1e6;
            }
        }
        s.spares.clear();
    }
    network_input_delay_override.store(int(((s.sent-next)*1000+30)/60));
    static FILE* phase_trace=[] {
        const char* path=getenv("MKDD_PHASE_TRACE");
        if(!path)return (FILE*)nullptr;
        FILE* out=fopen(path,"w");if(!out)fatal("Cannot open phase timing trace");
        fprintf(out,"tick,host_ns,phase,run,mode,canonical,queued,capture_ms,catchup_ms,network_wait_ms,poll_ms,graphics_wait_ms\n");return out;
    }();
    if(phase_trace) {
        fprintf(phase_trace,"%llu,%llu,%d,%d,%d,%d,%llu,%.3f,%.3f,%.3f,%.3f,%.3f\n",(unsigned long long)next,
            (unsigned long long)host_ns(),race.phase,race.run_state,race.mode,canonical,
            (unsigned long long)(s.sent-next),s.snapshot_ms,s.correction_ms,s.stalled_ms,s.receive_ms,s.quiesce_ms);
        fflush(phase_trace);
    }
    prediction_timing(race.exists && race.phase==17 && race.run_state==2 && !race.paused,next);
    prediction_choose(s.frames.back());
}
void prediction_boundary(CPU* c) {
    auto& s=prediction;
    s.candidate=capture_local(c,pad_tick+1);
    if(!s.replaying){s.fresh=s.candidate;s.physical=pad_capture_states();}
    threads_on_coordinator(prediction_coordinator,nullptr);
}
