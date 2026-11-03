"""Package the native shader compiler for the supported Windows toolchain.

Contains host compiler/runtime code only, never a disc or generated game code.
Normal player builds can link this verified archive without compiling Khronos.
Run only against a completed, fresh source-dependency build.
"""
import argparse,hashlib,json,pathlib,re,shutil,subprocess

PINS={'glslang':'e1b562a8bed273a02f30b59b66a5d499793cede5','spirv-cross':'f11ba9f0b21ba8fc15153d50a2a1ae31ab1cf8f7'}
SOURCES=('graphics/d3d11_shaders.cpp','graphics/d3d11_shaders.h','graphics/d3d11_cache.cpp','graphics/d3d11_cache.h','gx/shadergen.h')
ARCHIVES=('libtandem_d3d11_shaders.a','d3d-glslang/glslang/libglslang.a','d3d-glslang/glslang/libglslang-default-resource-limits.a','d3d-glslang/SPIRV/libSPIRV.a','d3d-spirv-cross/libspirv-cross-core.a','d3d-spirv-cross/libspirv-cross-glsl.a','d3d-spirv-cross/libspirv-cross-hlsl.a')
def digest(file):return hashlib.sha256(file.read_bytes()).hexdigest()
def source_digest(file):return hashlib.sha256(file.read_bytes().replace(b'\r\n',b'\n')).hexdigest()
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--source',type=pathlib.Path,required=True);p.add_argument('--build',type=pathlib.Path,required=True);p.add_argument('--output',type=pathlib.Path,required=True);p.add_argument('--deps',type=pathlib.Path,required=True);p.add_argument('--tool-bin',type=pathlib.Path,required=True);a=p.parse_args()
 for name,pin in PINS.items():
  actual=subprocess.check_output(['git','-C',str(a.deps/name),'rev-parse','HEAD'],text=True).strip()
  if actual!=pin:raise ValueError(f'Wrong {name} revision: {actual}')
  if subprocess.check_output(['git','-C',str(a.deps/name),'status','--porcelain'],text=True).strip():raise ValueError(f'{name} checkout is modified')
 compiler_files=list((a.build/'CMakeFiles').glob('*/CMakeCXXCompiler.cmake'))
 if len(compiler_files)!=1:raise ValueError('Ambiguous compiler metadata')
 compiler=compiler_files[0].read_text()
 def value(key):return re.search(r'set\('+key+r' "([^"]+)"\)',compiler)[1]
 identity=f'{value("CMAKE_CXX_COMPILER_ID")}-{value("CMAKE_CXX_COMPILER_VERSION")}-Windows-8'
 target=subprocess.check_output([value('CMAKE_CXX_COMPILER'),'-dumpmachine'],text=True).strip()
 # A caller must not accidentally bless an out-of-date wrapper archive.
 wrapper=a.build/ARCHIVES[0]
 for name in SOURCES:
  if (a.source/'gcn-recomp/runtime'/name).stat().st_mtime>wrapper.stat().st_mtime:raise ValueError('Rebuild native compiler after source changes')
 # Slim player toolchains may contain only a strip forwarding stub, without
 # llvm-strip itself. Retaining debug information is valid and compresses well.
 strip=a.tool_bin/'llvm-strip.exe'
 a.output.mkdir(parents=True,exist_ok=False);library=a.output/'tandem_d3d11_compiler.a'
 # MRI ADDLIB retains all members even when upstream archives use the same
 # object basename. Merge in a path without spaces; inputs are absolute quoted.
 commands=['CREATE tandem_d3d11_compiler.a']+[f'ADDLIB "{(a.build/name).resolve().as_posix()}"' for name in ARCHIVES]+['SAVE','END','']
 subprocess.run([str(a.tool_bin/'llvm-ar.exe'),'-M'],input='\n'.join(commands),text=True,cwd=a.output,check=True)
 if strip.is_file():subprocess.run([str(strip),'--strip-debug',str(library)],check=True)
 notices=a.output/'licenses';notices.mkdir()
 shutil.copy2(a.deps/'glslang/LICENSE.txt',notices/'glslang-LICENSE.txt');shutil.copy2(a.deps/'spirv-cross/LICENSE',notices/'SPIRV-Cross-LICENSE.txt')
 manifest=dict(schema=1,compiler=identity,target=target,library=library.name,sha256=digest(library),pins=PINS,sources={name:source_digest(a.source/'gcn-recomp/runtime'/name) for name in SOURCES},notices={f.name:digest(f) for f in notices.iterdir()})
 (a.output/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
 print(json.dumps(dict(folder=str(a.output),bytes=library.stat().st_size,sha256=manifest['sha256'],compiler=identity),indent=2))
if __name__=='__main__':main()
