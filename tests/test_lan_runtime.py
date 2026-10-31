"""Real helper/socket integration. Set TANDEM_LAN_EXE to the private helper build."""
import concurrent.futures
import json
import csv
import importlib.util
import os
from pathlib import Path
import socket
import re
import struct
import subprocess
import tempfile
import time
import unittest
import sys
import urllib.error
import urllib.request

ROOT = Path(__file__).resolve().parents[1]
EXE = os.environ.get('TANDEM_LAN_EXE')


@unittest.skipUnless(EXE, 'TANDEM_LAN_EXE not set')
class LanIntegration(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix='tandem-lan-', dir=os.environ.get('TANDEM_TEST_WORK'))
        self.folder = Path(self.temporary.name)
        self.processes = []
        self.files = []
        self.clients = []
        self.native = []
        self.lan_port = 29122

    def tearDown(self):
        for stream in self.native:
            stream.close()
        for process in reversed(self.processes):
            if process.poll() is None:
                process.terminate()
            process.wait(timeout=10)
        for file in self.files:
            file.close()
        result = self._outcome.result if self._outcome else None
        if result and any(error for _, error in getattr(result, 'failures', []) + getattr(result, 'errors', [])):
            for file in self.folder.glob('*.log'):
                print(file.name, file.read_text(errors='replace')[-2000:])
        self.temporary.cleanup()

    def spawn(self, command, name):
        log = (self.folder / (name + '.log')).open('w')
        self.files.append(log)
        process = subprocess.Popen(command, stdout=log, stderr=subprocess.STDOUT,
                                   creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))
        self.processes.append(process)
        return process

    def ready(self, file, process):
        until = time.monotonic() + 10
        while True:
            self.assertIsNone(process.poll(), f'helper exited; logs in {self.folder}')
            self.assertLess(time.monotonic(), until, 'readiness timeout')
            try:
                return json.loads(file.read_text())
            except (OSError, ValueError):
                pass
            time.sleep(.01)

    def helper(self, discover=False):
        n = len(self.clients)
        ready = self.folder / f'up-{n}.json'
        process = self.spawn([os.sys.executable, str(ROOT / 'services/rooms.py'), '--port', '0',
                              '--ready-file', str(ready)], f'rooms-{n}')
        upstream = self.ready(ready, process)
        ready = self.folder / f'lan-{n}.json'
        fixture = getattr(self, 'hybrid_fixture', False)
        command = [EXE, '--hybrid-test' if fixture else '--serve', str(upstream['port']), str(ready),
                   '-' if discover else '127.0.0.1', str(self.lan_port)]
        if fixture:command.append(str(self.lan_port+2))
        process = self.spawn(command, f'lan-{n}')
        state = self.ready(ready, process)
        client = dict(port=state['port'], token=None, process=process)
        self.clients.append(client)
        return client

    def request(self, client, op, **values):
        body = dict(op=op, **values)
        if client['token']:
            body['token'] = client['token']
        request = urllib.request.Request(f"http://127.0.0.1:{client['port']}/v1/room",
                                         json.dumps(body).encode(), {'Content-Type': 'application/json'})
        opener = urllib.request.build_opener(urllib.request.ProxyHandler({}))
        try:
            with opener.open(request, timeout=12) as response:
                result = json.load(response)
        except urllib.error.HTTPError as error:
            result = json.load(error)
        if 'token' in result:
            client['token'] = result['token']
        return result

    def action(self, client, op, **values):
        state = self.request(client, 'state')
        self.assertNotIn('error', state)
        result = self.request(client, op, revision=state['room']['revision'], **values)
        self.assertNotIn('error', result)
        return result

    @staticmethod
    def packet(kind, tick=0, checksum=0):
        data = bytearray(64)
        struct.pack_into('>4sHHQQ', data, 0, b'GCI1', 2, kind, tick, checksum)
        return data

    @staticmethod
    def read(stream):
        data = bytearray()
        while len(data) < 64:
            part = stream.recv(64 - len(data))
            if not part:
                raise AssertionError('native socket closed before bundle')
            data.extend(part)
        return data

    def test_eight_player_direct_inputs_discovery_and_lifecycle(self):
        host = self.helper()
        created = self.request(host, 'create', name='HOST', compatibility='0123456789abcdef')
        self.assertNotIn('error', created)
        code = created['room']['code']
        for n in range(1, 8):
            client = self.helper(discover=n == 1)
            extra = {}
            if getattr(self, 'hybrid_fixture', False) and n%2==0:
                extra['target']=dict(network='internet',address='127.0.0.1',port=self.lan_port+2,id=created['room']['members'][0]['id'])
                # Transport identity differs from member identity. Obtain the
                # host discovery identity by briefly making the room public.
                self.action(host,'lobby-settings',visibility='public')
                public=self.request(client,'browse',compatibility='0123456789abcdef')['lobbies']
                self.assertEqual(len(public),1)
                extra['target']['id']=public[0]['id']
                self.assertEqual(public[0]['network'],'lan')
            joined = self.request(client, 'join', name=f'P{n}', code=code, compatibility='0123456789abcdef', **extra)
            self.assertNotIn('error', joined)
            self.assertEqual(len(joined['room']['members']), n + 1)
        for client in self.clients:
            self.action(client, 'ready', ready=True)
        # Privacy changes travel through the real authenticated control path.
        # Eight connected players must retain their seats and ready state, then
        # still be able to start and exchange the exact same gameplay inputs.
        before = self.request(host, 'state')['room']
        for visibility in ('public', 'private'):
            changed = self.action(host, 'lobby-settings', visibility=visibility)
            self.assertNotIn('error', changed)
            self.assertEqual(changed['room']['members'], before['members'])
            if getattr(self, 'hybrid_fixture', False):
                visible=self.request(self.clients[1],'browse',compatibility='0123456789abcdef')['lobbies']
                self.assertEqual(len(visible),1 if visibility=='public' else 0)
                if visible:
                    self.assertEqual(visible[0]['players'],8)
                    self.assertGreaterEqual(visible[0]['ping_us'],0)
            for client in self.clients:
                state = self.request(client, 'state')['room']
                self.assertEqual(state['visibility'], visibility)
                self.assertEqual(state['code'], code)
        guest_revision = self.request(self.clients[1], 'state')['room']['revision']
        denied = self.request(self.clients[1], 'lobby-settings', revision=guest_revision, visibility='public')
        self.assertEqual(denied.get('error'), 'host-only')
        self.action(host, 'start')
        for client in self.clients:
            response = self.request(client, 'state')
            connection = response['connection']
            self.assertTrue(connection['mesh'])
            self.assertTrue(connection['prediction'])
            expected_delay=3 if getattr(self, 'hybrid_fixture', False) else 1
            self.assertEqual(connection['input_delay'], expected_delay)
            self.assertTrue(connection['endpoint'].startswith('127.0.0.1:'))
            stream = socket.create_connection(('127.0.0.1', int(connection['endpoint'].split(':')[1])), timeout=10)
            self.native.append(stream)
            hello = self.packet(1, checksum=0x0123456789abcdef)
            hello[32] = connection['slot']
            hello[34:37] = bytes([expected_delay, 24, 1])
            hello[40:56] = bytes.fromhex(connection['admission'])
            stream.sendall(hello)
        for n, stream in enumerate(self.native):
            welcome = self.read(stream)
            self.assertEqual(welcome[6:8], bytes([0, 2]))
            self.assertEqual(welcome[32:37], bytes([n, 8, expected_delay, 24, 1]))
        # Real direct UDP input path, with all 28 peer pairs contributing.
        for tick in range(expected_delay+1, expected_delay+121):
            for n, stream in enumerate(self.native):
                data = self.packet(3, tick, tick * 29)
                data[24:26] = bytes([1 + n % 3, n % 2])
                data[32:34] = bytes([n + tick & 255, n * 3])
                stream.sendall(data)
            try:
                bundles = [self.read(stream) for stream in self.native]
            except TimeoutError:
                raise AssertionError(f'input bundle timeout at tick {tick}') from None
            self.assertTrue(all(bundle == bundles[0] for bundle in bundles))
            self.assertEqual(struct.unpack_from('>Q', bundles[0], 8)[0], tick)
            self.assertEqual(bundles[0][24:26], bytes([3, 0]))
            for n in range(8):
                self.assertEqual(bundles[0][32 + 2*n:34 + 2*n], bytes([n + tick & 255, n*3]))
        # Returning to the same room preserves members, then refreshing the code
        # invalidates the old invitation without changing existing membership.
        self.action(host, 'abort', event=self.request(host, 'state')['room']['launch']['id'])
        before = self.request(host, 'state')['room']
        after = self.action(host, 'refresh-code')['room']
        self.assertNotEqual(before['code'], after['code'])
        self.assertEqual(len(after['members']), 8)
        for client in self.clients[1:]:
            state = self.request(client, 'state')
            self.assertEqual(state['room']['code'], after['code'])
        self.request(host, 'leave')
        time.sleep(.05)
        self.assertIn('error', self.request(self.clients[1], 'state'))

    def test_rejections_and_rejoin(self):
        host, client = self.helper(), self.helper()
        created = self.request(host, 'create', name='HOST', compatibility='good')
        code = created['room']['code']
        self.assertEqual(self.request(client, 'join', name='P2', code=code, compatibility='wrong')['error'], 'incompatible-build')
        self.assertNotIn('error', self.request(client, 'join', name='P2', code=code, compatibility='good'))
        self.assertEqual(self.request(client, 'create', name='P2', compatibility='good')['error'], 'already-in-room')
        self.assertNotIn('error', self.request(client, 'leave'))
        client['token'] = None
        self.assertNotIn('error', self.request(client, 'join', name='P2', code=code, compatibility='good'))
        self.assertEqual(len(self.request(host, 'state')['room']['members']), 2)

    def test_unified_mixed_routes_browser_and_continuity(self):
        self.hybrid_fixture=True
        self.test_eight_player_direct_inputs_discovery_and_lifecycle()

    def test_browser_lock_stale_entries_and_same_room_after_event(self):
        self.hybrid_fixture=True
        host,guest=self.helper(),self.helper()
        created=self.request(host,'create',name='TESTROOM',visibility='public',compatibility='same')
        code=created['room']['code']
        listing=self.request(guest,'browse',compatibility='same')['lobbies']
        self.assertEqual(len(listing),1)
        target=listing[0]
        self.assertEqual(target['network'],'lan')
        self.assertGreaterEqual(target['ping_us'],0)
        self.action(host,'lobby-settings',locked=True)
        for public in (False,True):
            denied=self.request(guest,'join',code=code,name='GUEST',compatibility='same',public_join=public,target=target)
            self.assertEqual(denied.get('error'),'room-locked')
        self.assertEqual(len(self.request(host,'state')['room']['members']),1)
        self.action(host,'lobby-settings',locked=False,visibility='private')
        denied=self.request(guest,'join',code=code,name='GUEST',compatibility='same',public_join=True,target=target)
        self.assertEqual(denied.get('error'),'room-private')
        self.assertEqual(self.request(guest,'browse',compatibility='same')['lobbies'],[])
        denied=self.request(guest,'join',code=code,name='GUEST',compatibility='different')
        self.assertEqual(denied.get('error'),'incompatible-build')
        joined=self.request(guest,'join',code=code,name='GUEST',compatibility='same')
        self.assertNotIn('error',joined)
        revision=joined['room']['revision']
        self.assertEqual(self.request(guest,'lobby-settings',revision=revision,locked=True).get('error'),'host-only')
        self.action(host,'lobby-settings',locked=True,visibility='public')
        for client in (host,guest):self.action(client,'ready',ready=True)
        before=self.request(host,'state')['room']
        started=self.action(host,'start')
        self.assertEqual(started['connection']['input_delay'],1)
        self.assertEqual(self.request(guest,'state')['connection']['input_delay'],1)
        running=started['room']
        self.action(host,'lobby-settings',visibility='private')
        after=self.action(host,'abort',event=running['launch']['id'])['room']
        self.assertEqual(after['code'],code)
        self.assertEqual(after['lobby_name'],before['lobby_name'])
        self.assertTrue(after['locked'])
        self.assertEqual(after['visibility'],'private')
        self.assertEqual([m['id'] for m in after['members']],[m['id'] for m in before['members']])
        self.assertEqual(self.request(guest,'state')['room']['state'],'forming')

    @unittest.skipUnless(os.environ.get('TANDEM_LAN_NATIVE'), 'full native game fixture not requested')
    def test_native_grand_prix(self):
        self.hybrid_fixture=os.environ.get('TANDEM_NATIVE_UNIFIED')=='1'
        spec = importlib.util.spec_from_file_location('lan_native_client', ROOT / 'services/room_client.py')
        native_client = importlib.util.module_from_spec(spec)
        sys.modules[spec.name] = native_client
        spec.loader.exec_module(native_client)
        executable = Path(os.environ['TANDEM_LAN_NATIVE'])
        build = native_client.fingerprint(executable)
        host=self.helper()
        created = self.request(host, 'create', name='HOST', visibility='public', compatibility=build)
        self.assertNotIn('error', created)
        players=int(os.environ.get('TANDEM_LAN_NATIVE_PLAYERS','2'))
        for number in range(1,players):
            client=self.helper(discover=number==1);extra={}
            if self.hybrid_fixture and number%2==0:
                listing=self.request(client,'browse',compatibility=build)['lobbies'][0]
                extra['target']=dict(listing,network='internet',address='127.0.0.1',port=self.lan_port+2)
            self.assertNotIn('error', self.request(client, 'join', name=f'P{number+1}', code=created['room']['code'], compatibility=build,**extra))
        self.action(host,'lobby-settings',locked=True)
        self.action(host, 'rules', mode='grand-prix', layout='solo', rules=dict(engine='150cc', cup='flower'))
        for player in self.clients:
            self.action(player, 'ready', ready=True)
        self.action(host, 'start')
        evidence = Path(os.environ['TANDEM_LAN_EVIDENCE'])
        evidence.mkdir(exist_ok=False)
        jobs = []
        delays=[]
        for slot, player in enumerate(self.clients):
            response = self.request(player, 'state')
            delays.append(response['connection']['input_delay'])
            run = evidence / str(slot)
            run.mkdir()
            environment = native_client.native_environment(response, executable)
            environment.update(MKDD_NET_TICKS='1440', MKDD_NET_LOCAL_INPUT='0001' if slot == 0 else '0000',
                               MKDD_PREDICTION_START='1080', MKDD_PREDICTION_TRACE=str(run / 'confirmed.csv'),
                               GCN_REFRESH_TRACE=str(run / 'refresh.csv'), SDL_AUDIODRIVER='dummy')
            # All configuration and saves stay in the isolated fixture folder.
            for variable, filename in [('GCN_INPUT_CONFIG', 'controllers.cfg'), ('GCN_DISPLAY_CONFIG', 'display.cfg'),
                                       ('GCN_SOUND_CONFIG', 'sound.cfg'), ('GCN_PRESENTATION_CONFIG', 'presentation.cfg'),
                                       ('GCN_OVERLAY_CONFIG', 'overlay.cfg')]:
                environment[variable] = str(run / filename)
            output, errors = (run / 'stdout.log').open('w'), (run / 'stderr.log').open('w')
            self.files.extend([output, errors])
            process = subprocess.Popen([str(executable), '--hidden', '--widescreen', '--resolution=1920x1080',
                                        '--scale=4', '--no-input-log'], cwd=run, env=environment, stdout=output,
                                       stderr=errors, creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))
            self.processes.append(process)
            jobs.append(process)
        codes = [process.wait(timeout=150) for process in jobs]
        traces = []
        for slot in range(players):
            with (evidence / str(slot) / 'confirmed.csv').open() as file:
                traces.append(list(csv.DictReader(file)))
        completions = []
        pacing = []
        for slot in range(players):
            text = (evidence / str(slot) / 'stderr.log').read_text(errors='replace')
            found = re.search(r'COMPLETE confirmed=(\d+) final_hash=([0-9a-f]+) rollbacks=(\d+) replayed=(\d+)', text)
            completions.append(dict(ticks=int(found[1]), checksum=found[2], rollbacks=int(found[3]), replayed=int(found[4])) if found else None)
            pacing.append([dict(fps=float(m[1]), p95_ms=float(m[2]), p99_ms=float(m[3]), max_ms=float(m[4])) for m in
                           re.finditer(r'\[race-pacing\].*?fps=([\d.]+) p95_ms=([\d.]+) p99_ms=([\d.]+) max_ms=([\d.]+)', text)])
        equal=all(trace==traces[0] for trace in traces)
        expected_ticks=list(range(1080+delays[0]+1,1441))
        complete_traces=all([int(row['tick']) for row in trace]==expected_ticks for trace in traces)
        passed = codes == [0]*players and equal and complete_traces and len(set(delays))==1 and all(completions)
        if all(completions):
            passed = passed and all(c['checksum']==completions[0]['checksum'] and c['ticks']==1440 for c in completions)
        before=self.request(host,'state')['room']
        after=self.action(host,'abort',event=before['launch']['id'])['room']
        continuity=all(after[key]==before[key] for key in ('code','lobby_name','visibility','locked')) and [m['id'] for m in after['members']]==[m['id'] for m in before['members']]
        passed=passed and continuity
        summary = dict(passed=bool(passed), compatibility=build, exit_codes=codes, confirmed_rows=[len(t) for t in traces],
                       confirmed_equal=equal, complete_traces=complete_traces,input_delay=delays[0],completions=completions, pacing=pacing,lobby_continuity=continuity,
                       transport='unified mixed-route socket fixture' if self.hybrid_fixture else 'real LAN helpers and direct UDP',same_pc=True,physical_second_pc_pending=True)
        (evidence / 'result.json').write_text(json.dumps(summary, indent=2) + '\n')
        self.assertTrue(passed, json.dumps(summary))
        # Bad/uninvited TCP traffic is isolated; the existing room remains usable.
        with socket.create_connection(('127.0.0.1', self.lan_port), timeout=2) as bad:
            bad.sendall(b'{"op":"packet","from":"spoofed"}\n')
        self.assertEqual(len(self.request(host, 'state')['room']['members']), players)


if __name__ == '__main__':
    unittest.main()
