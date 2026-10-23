"""Check a curated source tree for game output, credentials and personal paths."""
from pathlib import Path
import re,sys
root=Path(__file__).resolve().parents[1]
bad=[]
forbidden_parts={'local-data','saves','gen','disc','eos-cache','node_modules','private-verification'}
forbidden_ext={'.iso','.ciso','.rvz','.gcz','.gcm','.dol','.raw','.exe','.dll','.pdb','.obj','.o','.a','.lib','.pem','.key'}
secret=re.compile(rb'-----BEGIN (?:RSA |EC |OPENSSH )?PRIVATE KEY-----|github_pat_[A-Za-z0-9_]{30,}|ghp_[A-Za-z0-9]{30,}|sk-proj-[A-Za-z0-9_-]{30,}')
personal=re.compile(rb'(?i)[a-z]:[/\\]users[/\\][^/\\\s]+')
text_ext={'.md','.txt','.json','.yml','.yaml','.cpp','.c','.h','.inc','.py','.ps1','.cmd','.cmake','.sh'}
for p in root.rglob('*'):
 if not p.is_file():continue
 relative=p.relative_to(root);parts=relative.parts
 if any(x in ('.git','__pycache__') for x in parts):continue
 if any(x in forbidden_parts or x.startswith('build-') or x=='build' for x in parts) or p.suffix.lower() in forbidden_ext:
  bad.append((str(relative),'game/build/private or binary artifact'));continue
 if relative.as_posix() in ('config/eos.json','distribution/config/eos.json','eos-settings.json','distribution/local-settings.json'):
  bad.append((str(relative),'private service/local configuration'));continue
 if p.suffix.lower() in text_ext or p.name in ('LICENSE','CMakeLists.txt'):
  data=p.read_bytes()
  if secret.search(data):bad.append((str(relative),'private key/account token pattern'))
  if personal.search(data):bad.append((str(relative),'personal path/address pattern'))
  if p.name=='eos.example.json':
   import json
   config=json.loads(data)
   if not all(isinstance(v,str) and v.startswith('YOUR_') for v in config.values()):bad.append((str(relative),'example includes non-placeholder configuration'))
if bad:
 for path,reason in bad:print(f'{reason}: {path}',file=sys.stderr)
 raise SystemExit(1)
print('PASS: source boundary and private-key/personal-path pattern checks')
