import copy
import importlib.util
import json
from pathlib import Path
import sys
import tempfile
import unittest

def module(name,file):
    spec=importlib.util.spec_from_file_location(name,file)
    mod=importlib.util.module_from_spec(spec);sys.modules[name]=mod;spec.loader.exec_module(mod)
    return mod

root=Path(__file__).resolve().parents[1]
service=module('client_room_service',root/'services/rooms.py')
client=module('native_room_client',root/'services/room_client.py')

class ClientTests(unittest.TestCase):
    def test_staged_eight_player_roster_with_host_in_backseat(self):
        validation=root.parent/'validation'
        with tempfile.TemporaryDirectory(prefix='staged-roster-',dir=validation) as directory:
            exe=Path(directory)/'fixture.bin';exe.write_bytes(b'original staged roster fixture')
            store=service.Rooms();build=client.fingerprint(exe)
            host=store.request(dict(op='create',name='Host',initials='HST',compatibility=build,layout='coop',flow='staged'))
            def action(member,op,**values):
                revision=store.request(dict(op='state',token=member['token']))['room']['revision']
                return store.request(dict(op=op,token=member['token'],revision=revision,**values))
            action(host,'seat',seat=4)
            members=[host]+[store.request(dict(op='join',name=f'Guest{i}',initials=f'G{i}',compatibility=build,code=host['room']['code'])) for i in [0,1,2,3,5,6,7]]
            for m in members:action(m,'ready',ready=True)
            action(host,'start')
            for m in members:
                own=next(v for v in store.request(dict(op='state',token=m['token']))['room']['members'] if v['you'])
                action(m,'character',characters=[own['seat']+1])
                if own['role']!='passenger':action(m,'selection-kart',kart=20)
            for m in members:action(m,'selection-done')
            response=action(host,'start');env=client.native_environment(response,exe)
            self.assertEqual(env['MKDD_NET_SEATS'],'10246357')
            self.assertEqual(env['MKDD_NET_TAGS'],'HST,G0,G1,G2,G3,G5,G6,G7')
            self.assertEqual(env['MKDD_NET_SERIES'],'1')

    def test_sparse_two_player_launch(self):
        validation=root.parent/'validation'
        with tempfile.TemporaryDirectory(prefix='small-room-',dir=validation) as directory:
            exe=Path(directory)/'fixture.bin';exe.write_bytes(b'original small-room test data')
            for layout,friend_seat,expected in [('solo',0,'10'),('coop',6,'10'),('coop',0,'20')]:
                store=service.Rooms()
                host=store.request(dict(op='create',name='Host',compatibility=client.fingerprint(exe),layout=layout))
                def action(member,op,**values):
                    revision=store.request(dict(op='state',token=member['token']))['room']['revision']
                    return store.request(dict(op=op,token=member['token'],revision=revision,**values))
                action(host,'seat',seat=7)
                friend=store.request(dict(op='join',name='Friend',compatibility=client.fingerprint(exe),code=host['room']['code']))
                action(friend,'seat',seat=friend_seat)
                for member in (host,friend):action(member,'ready',ready=True)
                response=action(host,'start')
                env=client.native_environment(response,exe)
                self.assertEqual(env['MKDD_NET_PLAYERS'],'2')
                self.assertEqual(env['MKDD_NET_SEATS'],expected)
                self.assertEqual(len(env['MKDD_NET_ADMISSIONS']),64)
                direct=copy.deepcopy(response);direct['connection']['mesh']=True
                direct_env=client.native_environment(direct,exe)
                self.assertEqual(direct_env['MKDD_NET_MESH'],'1')
                self.assertIn('MKDD_NET_CONNECT',direct_env)
                self.assertNotIn('MKDD_NET_LISTEN',direct_env)
                self.assertNotIn('MKDD_NET_ADMISSIONS',direct_env)
                direct['connection']['prediction']=True
                predictive=client.native_environment(direct,exe)
                self.assertEqual(predictive['MKDD_NET_PREDICTION'],'1')
                self.assertEqual(predictive['MKDD_PREDICTION_START'],'120')
                self.assertEqual(predictive['GCN_WRITE_WATCH'],'1')
                direct['connection']['mesh']=False
                with self.assertRaisesRegex(ValueError,'requires direct mesh'):client.native_environment(direct,exe)
                wrong=copy.deepcopy(response);wrong['room']['launch']['roster'][0]['game_seat']=7
                with self.assertRaisesRegex(ValueError,'native kart seats'):client.native_environment(wrong,exe)

    def test_service_boundary(self):
        for url in ('https://127.0.0.1:28120/v1/room','http://192.0.2.1:28120/v1/room',
                    'http://127.0.0.1:28120/wrong','http://user:pass@127.0.0.1:28120/v1/room'):
            with self.assertRaises(ValueError): client.RoomClient(url)

    def test_launch_identity_privacy_and_changed_executable(self):
        # The fixture is original test data, not a game executable or asset.
        validation=root.parent/'validation'
        validation.mkdir(exist_ok=True)
        with tempfile.TemporaryDirectory(prefix='room-client-',dir=validation) as directory:
            exe=Path(directory)/'fixture.bin';exe.write_bytes(b'original room-client build fixture')
            store=service.Rooms()
            host=store.request(dict(op='create',name='Host',compatibility=client.fingerprint(exe),layout='coop'))
            def action(member,op,**extra):
                state=store.request(dict(op='state',token=member['token']))['room']
                return store.request(dict(op=op,token=member['token'],revision=state['revision'],**extra))
            with self.assertRaises(ValueError): client.native_environment(host,exe)
            action(host,'seat',seat=7)
            members=[host]+[store.request(dict(op='join',name=f'Peer {i}',code=host['room']['code'],
                                           compatibility=client.fingerprint(exe))) for i in range(7)]
            for member in members: action(member,'ready',ready=True)
            action(host,'start',endpoint='127.0.0.1:28000')
            replies=[store.request(dict(op='state',token=m['token'])) for m in members]
            envs=[client.native_environment(r,exe) for r in replies]
            self.assertEqual(envs[0]['MKDD_NET_SEATS'],'70123456')
            self.assertIn('MKDD_NET_ADMISSIONS',envs[0])
            for i,env in enumerate(envs[1:],1):
                self.assertNotIn('MKDD_NET_ADMISSIONS',env)
                self.assertEqual(env['MKDD_NET_SLOT'],str(i))
                self.assertNotIn('MKDD_NET_LISTEN',env)
                self.assertEqual(env['MKDD_NET_ROOM_ID'],envs[0]['MKDD_NET_ROOM_ID'])
            wrong=copy.deepcopy(replies[1]);wrong['connection']['slot']=2
            with self.assertRaises(ValueError): client.native_environment(wrong,exe)
            wrong=copy.deepcopy(replies[1]);wrong['connection']['endpoint']='192.0.2.1:28000'
            with self.assertRaises(ValueError): client.native_environment(wrong,exe)
            exe.write_bytes(b'changed build fixture')
            with self.assertRaisesRegex(ValueError,'changed since joining'): client.native_environment(replies[0],exe)

if __name__=='__main__': unittest.main()
