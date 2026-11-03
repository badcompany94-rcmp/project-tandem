#include "d3d11_cache.h"
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <array>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <span>
#include <stdexcept>
#ifndef TANDEM_D3D11_COMPILER_ID
#define TANDEM_D3D11_COMPILER_ID "development-layout-v1"
#endif
namespace desktop_d3d11 {
namespace {
constexpr size_t max_file=256u*1024*1024,max_entries=8192;
constexpr unsigned key_words=sizeof(gx::ShaderKey)/4;
using Key=std::array<uint32_t,key_words+2>;
void require(bool ok,const char* error){if(!ok)throw std::runtime_error(error);}
std::filesystem::path path(const std::string& value){return std::filesystem::path(std::u8string(value.begin(),value.end()));}
uint64_t hash(std::span<const uint8_t> bytes,uint64_t value=1469598103934665603ull){for(auto b:bytes)value=(value^b)*1099511628211ull;return value;}
Key make_key(const gx::ShaderKey& shader,ShaderOptions options){Key key{};std::memcpy(key.data(),&shader,sizeof(shader));key[key_words]=options.flip_vertex_y;key[key_words+1]=uint32_t(options.logic_operation+1);return key;}
CachedShaderKey decode_key(const Key& key){CachedShaderKey value{};std::memcpy(&value.shader,key.data(),sizeof(value.shader));require(key[key_words]<=1 && key[key_words+1]<=16,"Invalid native cache options");value.options.flip_vertex_y=key[key_words]!=0;value.options.logic_operation=int(key[key_words+1])-1;return value;}
uint64_t source_hash(const gx::ShaderKey& key,ShaderOptions options){
    const auto vertex=gx::gen_vertex_shader(true),pixel=gx::gen_pixel_shader(key,true);const std::string policy=TANDEM_D3D11_COMPILER_ID;
    uint64_t h=hash({reinterpret_cast<const uint8_t*>(policy.data()),policy.size()});h=hash({reinterpret_cast<const uint8_t*>(vertex.data()),vertex.size()},h);
    h=hash({reinterpret_cast<const uint8_t*>(pixel.data()),pixel.size()},h);const uint8_t flags[]={uint8_t(options.flip_vertex_y),uint8_t(options.logic_operation+1)};return hash(flags,h);
}
struct Writer {
    std::vector<uint8_t> bytes;
    void raw(std::span<const uint8_t> value){require(bytes.size()<=max_file && value.size()<=max_file-bytes.size(),"Native cache exceeds size limit");bytes.insert(bytes.end(),value.begin(),value.end());}
    void u32(uint32_t v){const uint8_t b[]={uint8_t(v),uint8_t(v>>8),uint8_t(v>>16),uint8_t(v>>24)};raw(b);}
    void u64(uint64_t v){u32(uint32_t(v));u32(uint32_t(v>>32));}
    void string(const std::string& value){require(value.size()<=512 && value.find('\0')==std::string::npos,"Invalid native cache name");u32(uint32_t(value.size()));raw({reinterpret_cast<const uint8_t*>(value.data()),value.size()});}
    void blob(const std::vector<uint8_t>& b){u32(uint32_t(b.size()));raw(b);}
};
struct Reader {
    std::span<const uint8_t> bytes;size_t at=0;
    std::span<const uint8_t> raw(size_t size){require(at<=bytes.size() && size<=bytes.size()-at,"Truncated native cache");auto out=bytes.subspan(at,size);at+=size;return out;}
    uint32_t u32(){auto b=raw(4);return uint32_t(b[0])|uint32_t(b[1])<<8|uint32_t(b[2])<<16|uint32_t(b[3])<<24;}
    uint64_t u64(){const auto lo=u32(),hi=u32();return uint64_t(lo)|uint64_t(hi)<<32;}
    std::string string(){const auto size=u32();require(size<=512,"Native cache name too long");const auto b=raw(size);std::string text(reinterpret_cast<const char*>(b.data()),b.size());require(text.find('\0')==std::string::npos,"Native cache name contains NUL");return text;}
    std::vector<uint8_t> blob(){const auto size=u32();require(size>=4 && size<=1024*1024,"Invalid native bytecode length");auto b=raw(size);require(!std::memcmp(b.data(),"DXBC",4),"Invalid native bytecode header");return {b.begin(),b.end()};}
};
void write_program(Writer& w,const CompiledProgram& p){
    w.u32(p.uniform_bytes);w.blob(p.vertex);w.blob(p.pixel);w.u32(uint32_t(p.uniforms.size()));
    for(const auto& u:p.uniforms){w.string(u.name);for(auto v:{u.offset,u.size,u.array_count,u.array_stride,u.matrix_stride,u.components,u.columns,uint32_t(u.scalar)})w.u32(v);}
    w.u32(uint32_t(p.samplers.size()));for(const auto& s:p.samplers){w.string(s.name);w.u32(s.binding);w.u32(s.count);}
}
CompiledProgram read_program(Reader& r){
    CompiledProgram p;p.uniform_bytes=r.u32();require(p.uniform_bytes<=65536 && p.uniform_bytes%16==0,"Invalid native constant buffer size");p.vertex=r.blob();p.pixel=r.blob();
    const auto uniforms=r.u32();require(uniforms<=256,"Too many native uniforms");std::map<std::string,bool> names;
    for(unsigned i=0;i<uniforms;++i){Uniform u;u.name=r.string();u.offset=r.u32();u.size=r.u32();u.array_count=r.u32();u.array_stride=r.u32();u.matrix_stride=r.u32();u.components=r.u32();u.columns=r.u32();const auto scalar=r.u32();
        require(!u.name.empty() && names.emplace(u.name,true).second && scalar<=uint32_t(UniformScalar::Boolean) && u.components>=1 && u.components<=4 && u.columns>=1 && u.columns<=4 && u.array_count>=1 && u.array_count<=4096,"Invalid native uniform metadata");
        require(u.offset<=p.uniform_bytes && u.size<=p.uniform_bytes-u.offset && u.size>0,"Native uniform outside constant buffer");
        const uint64_t a=u.array_count>1?u.array_stride:u.size,c=u.columns>1?u.matrix_stride:u.components*4;
        require((u.array_count==1 || (a>=u.components*4 && a%16==0)) && (u.columns==1 || (c>=u.components*4 && c%16==0)) && uint64_t(u.array_count-1)*a+uint64_t(u.columns-1)*c+u.components*4<=u.size,"Invalid native uniform strides");
        u.scalar=UniformScalar(scalar);p.uniforms.push_back(std::move(u));}
    const auto samplers=r.u32();require(samplers<=17,"Too many native samplers");names.clear();
    for(unsigned i=0;i<samplers;++i){Sampler s;s.name=r.string();s.binding=r.u32();s.count=r.u32();
        require(!s.name.empty() && names.emplace(s.name,true).second && s.count && ((s.name=="u_logic_destination" && s.binding==16 && s.count==1) || (s.binding<16 && s.count<=16-s.binding)),"Invalid native sampler metadata");p.samplers.push_back(std::move(s));}
    return p;
}
}
struct ShaderCache::Impl {
    struct Entry {uint64_t source;CompiledProgram program;};std::map<Key,Entry> entries;
    std::string file;bool dirty=false;uint64_t hits=0,builds=0;
    size_t bytes=0;
};
namespace {size_t entry_bytes(const CompiledProgram& p){Writer w;write_program(w,p);return sizeof(Key)+12+w.bytes.size();}}
ShaderCache::ShaderCache():state(std::make_unique<Impl>()){}ShaderCache::~ShaderCache()=default;
bool ShaderCache::open(const std::string& file,std::string& error){
    error.clear();state->file=file;try{
        std::error_code ec;const auto target=path(file);const bool exists=std::filesystem::exists(target,ec);require(!ec,"Cannot read native shader cache");if(!exists)return true;
        const auto length=std::filesystem::file_size(target,ec);require(!ec && length>=24 && length<=max_file,"Invalid native shader cache length");std::vector<uint8_t> data(size_t(length),0);
        std::ifstream stream(target,std::ios::binary);require(bool(stream.read(reinterpret_cast<char*>(data.data()),data.size())),"Cannot read native shader cache");
        Reader r{data};const auto magic=r.raw(8);require(!std::memcmp(magic.data(),"TD3DSC01",8),"Invalid native shader cache magic");require(r.u32()==1,"Unsupported native shader cache version");const auto count=r.u32();require(count<=max_entries,"Native shader cache has too many entries");const auto checksum=r.u64();require(hash(r.bytes.subspan(r.at))==checksum,"Native shader cache checksum mismatch");
        std::map<Key,Impl::Entry> entries;size_t bytes=0;
        for(unsigned i=0;i<count;++i){Key key;for(auto& word:key)word=r.u32();decode_key(key);const auto fingerprint=r.u64();const auto size=r.u32();require(size<=4*1024*1024,"Native shader record too large");Reader record{r.raw(size)};auto program=read_program(record);require(record.at==record.bytes.size(),"Trailing native shader record data");require(entries.emplace(key,Impl::Entry{fingerprint,std::move(program)}).second,"Duplicate native shader cache key");}
        require(r.at==r.bytes.size(),"Trailing native shader cache data");for(const auto& [key,entry]:entries)bytes+=entry_bytes(entry.program);state->entries=std::move(entries);state->bytes=bytes;state->dirty=false;return true;
    }catch(const std::exception& e){error=e.what();return false;}
}
bool ShaderCache::get(const gx::ShaderKey& shader,ShaderOptions options,CompiledProgram& output,std::string& error){
    error.clear();try{
        require(shader.num_texgens<=8 && ((shader.genmode>>16)&7)<=4 && shader.fg_depth<=1 && shader.dst_alpha<=2 && shader.efb_has_alpha<=1 && options.logic_operation>=-1 && options.logic_operation<=15,"Unsupported native shader cache key");
        const auto key=make_key(shader,options);const auto fingerprint=source_hash(shader,options);auto found=state->entries.find(key);
        if(found!=state->entries.end() && found->second.source==fingerprint){output=found->second.program;++state->hits;return true;}
        CompiledProgram next;if(!compile_gx_program(shader,next,error,options))return false;
        const auto bytes=entry_bytes(next);require(bytes<=max_file-24,"Native shader exceeds cache budget");
        invalidate(shader,options);
        while(!state->entries.empty() && (state->entries.size()>=max_entries || state->bytes>max_file-24-bytes)){auto first=state->entries.begin();state->bytes-=entry_bytes(first->second.program);state->entries.erase(first);}
        state->entries.emplace(key,Impl::Entry{fingerprint,next});state->bytes+=bytes;output=std::move(next);state->dirty=true;++state->builds;return true;
    }catch(const std::exception& e){error=e.what();return false;}
}
void ShaderCache::invalidate(const gx::ShaderKey& shader,ShaderOptions options){auto found=state->entries.find(make_key(shader,options));if(found!=state->entries.end()){state->bytes-=entry_bytes(found->second.program);state->entries.erase(found);state->dirty=true;}}
bool ShaderCache::save(std::string& error){
    error.clear();if(!state->dirty || state->file.empty())return true;try{
        Writer payload;for(const auto& [key,entry]:state->entries){for(auto word:key)payload.u32(word);payload.u64(entry.source);Writer record;write_program(record,entry.program);payload.u32(uint32_t(record.bytes.size()));payload.raw(record.bytes);}
        Writer all;all.raw({reinterpret_cast<const uint8_t*>("TD3DSC01"),8});all.u32(1);all.u32(uint32_t(state->entries.size()));all.u64(hash(payload.bytes));all.raw(payload.bytes);
        const auto target=path(state->file);auto temporary=target;temporary+=".tmp";std::error_code ec;if(!target.parent_path().empty())std::filesystem::create_directories(target.parent_path(),ec);require(!ec,"Cannot create native shader cache folder");
        std::ofstream stream(temporary,std::ios::binary|std::ios::trunc);stream.write(reinterpret_cast<const char*>(all.bytes.data()),all.bytes.size());stream.flush();bool ok=bool(stream);stream.close();require(ok && !stream.fail(),"Cannot write native shader cache");
        require(MoveFileExW(temporary.c_str(),target.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=0,"Cannot replace native shader cache");state->dirty=false;return true;
    }catch(const std::exception& e){error=e.what();return false;}
}
std::vector<CachedShaderKey> ShaderCache::keys()const{std::vector<CachedShaderKey> keys;for(const auto& [key,entry]:state->entries)keys.push_back(decode_key(key));return keys;}
uint64_t ShaderCache::cache_hits()const{return state->hits;}uint64_t ShaderCache::compiled()const{return state->builds;}
bool read_opengl_shader_keys(const std::string& file,std::vector<gx::ShaderKey>& output,std::string& error){
    error.clear();try{
        std::error_code ec;const auto target=path(file);const bool exists=std::filesystem::exists(target,ec);require(!ec,"Cannot read OpenGL shader keys");if(!exists){output.clear();return true;}
        const auto length=std::filesystem::file_size(target,ec);require(!ec && length>=16 && length<=max_file,"Invalid OpenGL key cache length");
        std::vector<uint8_t> bytes(size_t(length),0);std::ifstream input(target,std::ios::binary);require(bool(input.read(reinterpret_cast<char*>(bytes.data()),bytes.size())),"Cannot read OpenGL shader keys");Reader r{bytes};
        require(r.u32()==0x43535257 && r.u32()==((3u<<16)|sizeof(gx::ShaderKey)),"Unsupported OpenGL key cache");r.u64();std::vector<gx::ShaderKey> keys;
        while(r.at<r.bytes.size()){require(keys.size()<max_entries,"Too many OpenGL shader keys");gx::ShaderKey key;auto data=r.raw(sizeof(key));std::memcpy(&key,data.data(),sizeof(key));r.u64();r.u32();const auto length=r.u32();r.raw(length);keys.push_back(key);}
        output=std::move(keys);return true;
    }catch(const std::exception& e){error=e.what();return false;}
}
}
#endif
