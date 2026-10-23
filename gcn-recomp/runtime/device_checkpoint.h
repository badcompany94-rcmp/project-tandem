#pragma once
#include <array>
#include <atomic>
#include <cstring>
#include <functional>
#include <memory>
#include <string>
#include <tuple>
#include <type_traits>
#include <vector>

// In-process snapshots of explicitly owned device values. Never copy mutexes,
// file handles or host presentation queues. Call only on a quiescent coordinator.
struct DeviceFragment {
    std::string name;
    std::function<void()> restore;
    std::function<bool()> matches;
    // Optional ordered restore for backends that only consume immutable jobs
    // and never read the caller's live memory. A later matches/restore fences it.
    std::function<void()> restore_enqueued;
};
namespace checkpoint_detail {
template<class T> struct Field {
    using Value=std::remove_cv_t<T>;
    static Value read(const T& v){return v;}
    static void write(T& v,const Value& saved){v=saved;}
    static bool equal(const T& v,const Value& saved){
        const auto current=read(v);
        if constexpr(std::is_arithmetic_v<Value> || std::is_enum_v<Value>)
            return !std::memcmp(&current,&saved,sizeof(Value));
        else if constexpr(requires{current==saved;})return current==saved;
        else {static_assert(std::is_trivially_copyable_v<Value>);return !std::memcmp(&current,&saved,sizeof(Value));}
    }
};
template<class T> struct Field<std::atomic<T>> {
    using Value=T;
    static Value read(const std::atomic<T>& v){return v.load();}
    static void write(std::atomic<T>& v,const Value& saved){v.store(saved);}
    static bool equal(const std::atomic<T>& v,const Value& saved){return v.load()==saved;}
};
template<class T,size_t N> struct Field<T[N]> {
    using Value=std::array<typename Field<T>::Value,N>;
    static Value read(const T(&v)[N]){Value result;for(size_t i=0;i<N;++i)result[i]=Field<T>::read(v[i]);return result;}
    static void write(T(&v)[N],const Value& saved){for(size_t i=0;i<N;++i)Field<T>::write(v[i],saved[i]);}
    static bool equal(const T(&v)[N],const Value& saved){for(size_t i=0;i<N;++i)if(!Field<T>::equal(v[i],saved[i]))return false;return true;}
};
}
template<class... T> DeviceFragment checkpoint_fields(const char* name,T&... values) {
    using Values=std::tuple<typename checkpoint_detail::Field<T>::Value...>;
    auto saved=std::make_shared<const Values>(checkpoint_detail::Field<T>::read(values)...);
    auto pointers=std::tuple<T*...>(&values...);
    return {name,[saved,pointers]{
        [&]<size_t... I>(std::index_sequence<I...>){
            (checkpoint_detail::Field<T>::write(*std::get<I>(pointers),std::get<I>(*saved)),...);
        }(std::index_sequence_for<T...>{});
    },[saved,pointers]{
        return [&]<size_t... I>(std::index_sequence<I...>){
            return (checkpoint_detail::Field<T>::equal(*std::get<I>(pointers),std::get<I>(*saved)) && ...);
        }(std::index_sequence_for<T...>{});
    }};
}
DeviceFragment checkpoint_clock();
DeviceFragment checkpoint_interrupts();
DeviceFragment checkpoint_vi();
DeviceFragment checkpoint_si();
DeviceFragment checkpoint_dsp();
DeviceFragment checkpoint_dsp_hle();
DeviceFragment checkpoint_jaudio();
DeviceFragment checkpoint_ax();
DeviceFragment checkpoint_ai();
DeviceFragment checkpoint_di();
DeviceFragment checkpoint_exi();
DeviceFragment checkpoint_graphics_fifo();
DeviceFragment checkpoint_graphics_skim();
DeviceFragment checkpoint_graphics_commands();
DeviceFragment checkpoint_graphics_frontend();
DeviceFragment checkpoint_graphics_textures();
DeviceFragment checkpoint_graphics_render(bool asynchronous=false);
void checkpoint_graphics_quiesce();
std::vector<DeviceFragment> checkpoint_race_devices();
