"""Opt-in real EOS directory test; configuration stays outside the repository.

Set TANDEM_EOS_EXE and TANDEM_EOS_CONFIG. Temporary test rooms are closed on exit.
This checks the actual SDK/service, not Internet latency between physical PCs.
"""
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import time
import unittest
import urllib.error
import urllib.request

ROOT=Path(__file__).resolve().parents[1]

@unittest.skipUnless(os.environ.get('TANDEM_EOS_EXE') and os.environ.get('TANDEM_EOS_CONFIG'),'real EOS test not requested')
class EosBrowser(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory(prefix='tandem-eos-browser-',dir=os.environ.get('TANDEM_TEST_WORK'))
        self.folder=Path(self.temp.name);self.processes=[];self.logs=[];self.clients=[]
        self.opener=urllib.request.build_opener(urllib.request.ProxyHandler({}))
    def tearDown(self):
        for client in reversed(self.clients):
            if client['token']:
                try:self.request(client,'leave')
                except Exception:pass
        for process in reversed(self.processes):
            if process.poll() is None:process.terminate()
            process.wait(timeout=10)
        for stream in self.logs:stream.close()
        result=self._outcome.result if self._outcome else None
        if result and (result.failures or result.errors):
            evidence=Path(os.environ.get('TANDEM_TEST_WORK',self.folder.parent))/'eos-browser-failure-logs'
            evidence.mkdir(exist_ok=True)
            for log in self.folder.glob('*/network.log'):shutil.copyfile(log,evidence/(log.parent.name+'.log'))
        self.temp.cleanup()
    def spawn(self,args,path,environment=None):
        stream=path.open('w');self.logs.append(stream)
        process=subprocess.Popen(args,stdout=stream,stderr=subprocess.STDOUT,env=environment,creationflags=getattr(subprocess,'CREATE_NO_WINDOW',0))
        self.processes.append(process);return process
    def ready(self,path,process):
        until=time.monotonic()+60
        while time.monotonic()<until:
            self.assertIsNone(process.poll(),'helper exited; inspect its private test log')
            try:return json.loads(path.read_text())
            except (OSError,ValueError):time.sleep(.05)
        self.fail('network readiness timeout')
    def helper(self,offline=False,lan_port=None):
        n=len(self.clients);folder=self.folder/str(n);folder.mkdir()
        config=folder/'eos.json'
        if not offline:shutil.copyfile(os.environ['TANDEM_EOS_CONFIG'],config)
        path=folder/'rooms-ready.json'
        process=self.spawn([sys.executable,str(ROOT/'services/rooms.py'),'--port','0','--ready-file',str(path)],folder/'rooms.log')
        port=self.ready(path,process)['port']
        path=folder/'network-ready.json';environment=os.environ.copy()
        # Separate discovery ports force this test through real EOS search/P2P.
        environment['TANDEM_LAN_PORT']=str(lan_port if lan_port is not None else 29222+n*2)
        environment['TANDEM_BROWSER_TRACE']='1'
        environment['TANDEM_BROWSER_SELF_TEST']='1'
        process=self.spawn([os.environ['TANDEM_EOS_EXE'],'--serve',str(config),str(port),str(path)],folder/'network.log',environment)
        state=self.ready(path,process);client=dict(port=state['port'],token=None)
        self.clients.append(client);return client
    def request(self,client,op,**values):
        body=dict(op=op,**values)
        if client['token']:body['token']=client['token']
        req=urllib.request.Request(f"http://127.0.0.1:{client['port']}/v1/room",json.dumps(body).encode(),{'Content-Type':'application/json'})
        try:
            with self.opener.open(req,timeout=35) as response:reply=json.load(response)
        except urllib.error.HTTPError as error:reply=json.load(error)
        if 'token' in reply:client['token']=reply['token']
        return reply
    def action(self,client,op,**values):
        state=self.request(client,'state');self.assertNotIn('error',state)
        result=self.request(client,op,revision=state['room']['revision'],**values)
        self.assertNotIn('error',result);return result
    def listing(self,client,code,locked=None):
        for _ in range(6):
            result=self.request(client,'browse',compatibility='tandem-private-directory-test')
            self.assertNotIn('error',result)
            found=[row for row in result['lobbies'] if row['code']==code and (locked is None or row['locked']==locked)]
            if found:return found[0]
            time.sleep(.2)
        self.fail('test room not found in EOS directory: '+json.dumps(dict(notice=result.get('notice'),rows=len(result.get('lobbies',[])))))
    def same_device_identity(self):
        fingerprints=[]
        for log in sorted(self.folder.glob('*/network.log')):
            for line in log.read_text(errors='replace').splitlines():
                if 'local identity fingerprint=' in line:fingerprints.append(line.split('=')[-1])
        return len(fingerprints)>=2 and fingerprints[0]==fingerprints[1]

    def test_live_directory_publication_and_atomic_duplicate_names(self):
        host,observer=self.helper(),self.helper()
        name='TEST'+str(time.time_ns())[-10:]
        created=self.request(host,'create',name=name,visibility='public',compatibility='tandem-private-directory-test')
        self.assertNotIn('error',created)
        row=self.listing(observer,created['room']['code'])
        self.assertEqual(row['name'],name);self.assertEqual(row['players'],1)
        self.assertEqual(row['network'],'internet');self.assertFalse(row['locked'])
        duplicate=self.request(observer,'create',name=name,compatibility='tandem-private-directory-test')
        self.assertNotIn('error',duplicate)
        self.assertEqual(duplicate['room']['lobby_name'],name+' (1)')
        before=self.request(host,'state')['room']
        renamed=self.action(host,'lobby-settings',lobby_name=name+' (1)')['room']
        self.assertEqual(renamed['lobby_name'],name+' (1) (1)')
        self.assertEqual(renamed['code'],before['code']);self.assertEqual(renamed['members'],before['members'])
        self.assertEqual(self.listing(host,created['room']['code'])['name'],renamed['lobby_name'])
        # The old claim is available immediately after committing the new name.
        reused=self.action(observer,'lobby-settings',lobby_name=name)['room']
        self.assertEqual(reused['lobby_name'],name)
        rejected=self.request(host,'lobby-settings',revision=before['revision'],lobby_name=name+' (1)')
        self.assertEqual(rejected.get('error'),'stale-room')
        freed=self.action(observer,'lobby-settings',lobby_name=name+' (1)')['room']
        self.assertEqual(freed['lobby_name'],name+' (1)')
        self.action(host,'lobby-settings',lobby_name=renamed['lobby_name'].lower())
        self.action(host,'lobby-settings',visibility='private')
        shown=self.action(host,'lobby-settings',visibility='public')['room']
        self.assertEqual(shown['lobby_name'],renamed['lobby_name'].lower())
        self.action(host,'lobby-settings',locked=True)
        # Search the host's own listing via the explicit test-only switch. This
        # does not require inventing a second device identity on the user's PC.
        self.assertTrue(self.listing(host,created['room']['code'],locked=True)['locked'])

    def test_offline_startup_keeps_one_helper_and_lan_play_available(self):
        host,guest=self.helper(offline=True,lan_port=29242),self.helper(offline=True,lan_port=29242)
        created=self.request(host,'create',name='OFFLINE',visibility='public',compatibility='same')
        self.assertNotIn('error',created)
        listing=self.request(guest,'browse',compatibility='same')
        self.assertIn('LAN ONLY',listing['notice']);self.assertEqual(len(listing['lobbies']),1)
        joined=self.request(guest,'join',code=created['room']['code'],name='GUEST',compatibility='same',public_join=True,target=listing['lobbies'][0])
        self.assertNotIn('error',joined)
        self.assertEqual(len(joined['room']['members']),2)

    def test_cross_device_ping_private_lock_and_build_guards(self):
        host,guest=self.helper(),self.helper()
        if self.same_device_identity():self.skipTest('SDK shares one device identity on this PC; cross-device EOS ping/join requires another physical PC')
        name='TEST'+str(time.time_ns())[-10:]
        created=self.request(host,'create',name=name,visibility='public',compatibility='tandem-private-directory-test')
        self.assertNotIn('error',created)
        code=created['room']['code'];row=self.listing(guest,code)
        self.assertEqual(row['network'],'internet');self.assertEqual(row['players'],1)
        # A first probe can be pending while the SDK establishes its connection.
        for _ in range(4):
            if row['ping_us']>=0:break
            row=self.listing(guest,code)
        self.assertGreaterEqual(row['ping_us'],0,'EOS pre-join host RTT was not measured')
        self.action(host,'lobby-settings',visibility='private')
        denied=self.request(guest,'join',code=code,name='GUEST',compatibility='tandem-private-directory-test',public_join=True,target=row)
        self.assertEqual(denied.get('error'),'room-private')
        self.action(host,'lobby-settings',locked=True)
        denied=self.request(guest,'join',code=code,name='GUEST',compatibility='tandem-private-directory-test',target=row)
        self.assertEqual(denied.get('error'),'room-locked')
        self.action(host,'lobby-settings',locked=False)
        denied=self.request(guest,'join',code=code,name='GUEST',compatibility='wrong-build',target=row)
        self.assertEqual(denied.get('error'),'incompatible-build')
        joined=self.request(guest,'join',code=code,name='GUEST',compatibility='tandem-private-directory-test',target=row)
        self.assertNotIn('error',joined)
        before=self.request(host,'state')['room']
        changed=self.action(host,'lobby-settings',visibility='public',locked=True)['room']
        self.assertEqual(changed['members'],before['members'])
        other=self.helper()
        duplicate=self.request(other,'create',name=name,compatibility='tandem-private-directory-test')
        self.assertNotIn('error',duplicate)
        self.assertEqual(duplicate['room']['lobby_name'],name+' (1)')

if __name__=='__main__':unittest.main()
