#include "graphics/d3d11_cache.h"
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>
using namespace desktop_d3d11;
namespace {
void check(bool ok,const std::string& error){if(!ok)throw std::runtime_error(error);}
bool same(const CompiledProgram& a,const CompiledProgram& b){return a.vertex==b.vertex && a.pixel==b.pixel && a.uniforms==b.uniforms && a.samplers==b.samplers && a.uniform_bytes==b.uniform_bytes;}
void write(const std::filesystem::path& file,const std::vector<uint8_t>& bytes){std::ofstream f(file,std::ios::binary);f.write(reinterpret_cast<const char*>(bytes.data()),bytes.size());check(bool(f),"fixture write failed");}
uint64_t hash(const std::vector<uint8_t>& b){uint64_t h=1469598103934665603ull;for(size_t i=24;i<b.size();++i)h=(h^b[i])*1099511628211ull;return h;}
void checksum(std::vector<uint8_t>& bytes){auto h=hash(bytes);for(unsigned i=0;i<8;++i)bytes[16+i]=uint8_t(h>>(8*i));}
}
int main(int argc,char** argv)try{
    check(argc==2,"Pass a fresh scratch cache filename");const std::string file=argv[1];check(!std::filesystem::exists(file),"Scratch cache already exists");
    gx::ShaderKey key{};key.alpha_func=0xff;key.efb_has_alpha=1;std::string error;
    ShaderCache cold;check(cold.open(file,error),error);CompiledProgram original,again,flipped,logic;
    check(cold.get(key,{},original,error),error);check(cold.get(key,{},again,error),error);
    check(same(original,again) && cold.compiled()==1 && cold.cache_hits()==1,"In-memory hit changed program");
    check(cold.get(key,{true},flipped,error),error);check(flipped.vertex!=original.vertex,"Orientation key collided");
    check(cold.get(key,{true,6},logic,error),error);check(logic.pixel!=flipped.pixel,"Logic key collided");
    check(cold.save(error),error);
    std::ifstream input(file,std::ios::binary);std::vector<uint8_t> valid((std::istreambuf_iterator<char>(input)),{});input.close();
    ShaderCache warm;check(warm.open(file,error),error);check(warm.keys().size()==3,"Lost cache variants");
    check(warm.get(key,{},again,error) && same(original,again),error);check(warm.get(key,{true},again,error) && same(flipped,again),error);
    check(warm.get(key,{true,6},again,error) && same(logic,again),error);check(warm.compiled()==0 && warm.cache_hits()==3,"Warm cache compiled shaders");
    auto corrupt=valid;corrupt.back()^=1;write(file,corrupt);check(!warm.open(file,error) && !error.empty(),"Checksum failure accepted");
    check(warm.get(key,{},again,error) && same(original,again),"Invalid cache destroyed working data");
    corrupt=valid;corrupt.resize(23);write(file,corrupt);check(!warm.open(file,error),"Truncated cache accepted");
    // Change the first record's constant-buffer size with a valid checksum:
    // parser bounds validation must still reject it.
    corrupt=valid;const size_t metadata=24+sizeof(gx::ShaderKey)+8+8+4;
    for(unsigned i=0;i<4;++i)corrupt[metadata+i]=0xff;checksum(corrupt);write(file,corrupt);check(!warm.open(file,error),"Invalid uniform metadata accepted");
    auto bad=key;bad.num_texgens=9;again=original;check(!warm.get(bad,{},again,error) && same(original,again),"Invalid key replaced output");
    write(file,valid);check(warm.open(file,error),error);warm.invalidate(key,{});check(warm.get(key,{},again,error) && same(original,again),error);check(warm.compiled()==1,"Invalidated bytecode not rebuilt");check(warm.save(error),error);
    // A valid record with a stale source fingerprint must recompile only that key.
    corrupt=valid;corrupt[24+sizeof(gx::ShaderKey)+8]^=1;checksum(corrupt);write(file,corrupt);
    ShaderCache stale;check(stale.open(file,error),error);check(stale.get(key,{},again,error) && same(original,again),error);check(stale.compiled()==1,"Stale compiler/source cache accepted");
    check(stale.save(error),error);
    std::vector<uint8_t> gl(16+sizeof(key)+16,0);auto put=[&](size_t at,uint32_t v){for(unsigned i=0;i<4;++i)gl[at+i]=uint8_t(v>>(8*i));};
    put(0,0x43535257);put(4,(3u<<16)|sizeof(key));std::memcpy(gl.data()+16,&key,sizeof(key));
    auto glfile=file+".gl";write(glfile,gl);std::vector<gx::ShaderKey> imported;check(read_opengl_shader_keys(glfile,imported,error) && imported.size()==1 && imported[0]==key,error);
    std::ifstream reread(glfile,std::ios::binary);std::vector<uint8_t> untouched((std::istreambuf_iterator<char>(reread)),{});reread.close();check(untouched==gl,"GL import modified source cache");
    gl.pop_back();write(glfile,gl);check(!read_opengl_shader_keys(glfile,imported,error) && imported.size()==1 && imported[0]==key,"Truncated GL key import replaced valid output");
    std::puts("PASS native shader cache: exact disk roundtrip, warm zero-compilation, separate orientation/logic variants, checksum/truncation/metadata rejection, source invalidation, read-only GL key import and working-output preservation");return 0;
}catch(const std::exception& e){std::fprintf(stderr,"FAIL %s\n",e.what());return 1;}
