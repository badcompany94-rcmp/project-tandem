import importlib.util
from pathlib import Path
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('build_identity',ROOT/'tools/build_identity.py')
build=importlib.util.module_from_spec(spec);spec.loader.exec_module(build)

class IdentityTests(unittest.TestCase):
    def test_relocated_sources_match_but_code_and_policy_changes_do_not(self):
        with tempfile.TemporaryDirectory() as directory:
            roots=[Path(directory)/name for name in ('first install','another install')]
            for index,root in enumerate(roots):
                for name in ('CMakeLists.txt','tools/build_identity.py','gcn-recomp/CMakeLists.txt','src/fixture.cpp','analysis/symbols.txt','analysis/splits.txt','analysis/config.yml'):
                    path=root/name;path.parent.mkdir(parents=True,exist_ok=True)
                    path.write_bytes(b'original fixture\r\n' if index else b'original fixture\n')
            policy=dict(c='Clang-23.1.4',cxx='Clang-23.1.4',build_type='RelWithDebInfo')
            first=build.identity(roots[0],policy)['id']
            self.assertEqual(first,build.identity(roots[1],policy)['id'])
            self.assertEqual(first,build.identity(roots[1],dict(policy,gl_es='OFF'))['id'])
            self.assertNotEqual(first,build.identity(roots[1],dict(policy,c='different compiler'))['id'])
            (roots[1]/'src/fixture.cpp').write_text('changed original fixture\n')
            self.assertNotEqual(first,build.identity(roots[1],policy)['id'])
            generated=roots[0]/'build/gen';generated.mkdir(parents=True)
            (generated/'fixture.c').write_text('excluded original fixture\n')
            self.assertEqual(first,build.identity(roots[0],policy)['id'])

if __name__=='__main__':unittest.main()
