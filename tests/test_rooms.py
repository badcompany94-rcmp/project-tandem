import concurrent.futures
import importlib.util
import json
from pathlib import Path
import sys
import threading
import unittest
import urllib.error
import urllib.request

spec=importlib.util.spec_from_file_location('room_service',Path(__file__).resolve().parents[1]/'services/rooms.py')
service=importlib.util.module_from_spec(spec)
sys.modules[spec.name]=service
spec.loader.exec_module(service)

class RoomTests(unittest.TestCase):
    def setUp(self):
        self.store=service.Rooms()
        self.host=self.store.request(dict(op='create',name='Host',compatibility='build-1'))

    def join(self,**extra):
        return self.store.request(dict(op='join',name='Friend',compatibility='build-1',
                                       code=self.host['room']['code'],**extra))

    def action(self,member,op,**kwargs):
        token=member['token']
        room=self.store.request(dict(op='state',token=token))['room']
        return self.store.request(dict(op=op,token=token,revision=room['revision'],**kwargs))

    def test_codes_sessions_and_builds(self):
        code=self.host['room']['code']
        joined=self.store.request(dict(op='join',name='Friend',compatibility='build-1',
                                       code=(code[:4]+'-'+code[4:]).lower()))
        self.assertEqual(joined['room']['code'],code)
        self.assertNotIn('token',json.dumps(joined['room']))
        with self.assertRaisesRegex(service.RoomError,'incompatible-build'):
            self.store.request(dict(op='join',name='Friend',compatibility='build-2',code=code))
        with self.assertRaisesRegex(service.RoomError,'invalid-session'):
            self.store.request(dict(op='state',token='guess'))

    def test_capacity_under_concurrent_joins(self):
        def attempt(_):
            try: return self.join()['token']
            except service.RoomError as e: return str(e)
        with concurrent.futures.ThreadPoolExecutor(max_workers=12) as pool:
            results=list(pool.map(attempt,range(12)))
        self.assertEqual(results.count('room-full'),5)
        room=self.store.request(dict(op='state',token=self.host['token']))['room']
        self.assertEqual(len(room['members']),8)
        self.assertEqual({m['seat'] for m in room['members']},set(range(8)))

    def test_host_authority_and_revisions(self):
        friend=self.join()
        with self.assertRaisesRegex(service.RoomError,'host-only'):
            self.action(friend,'rules',mode='shine',layout='coop')
        with self.assertRaisesRegex(service.RoomError,'stale-room'):
            self.store.request(dict(op='ready',token=self.host['token'],revision=1,ready=True))
        self.action(friend,'ready',ready=True)
        room=self.action(self.host,'rules',mode='shine',layout='coop')['room']
        self.assertEqual((room['mode'],room['layout']),('shine','coop'))
        self.assertFalse(any(m['ready'] for m in room['members']))

    def test_all_eight_mode_layout_combinations(self):
        members=[self.host]+[self.join() for _ in range(7)]
        for mode in sorted(service.MODES):
            for layout in ('solo','coop'):
                room=self.action(self.host,'rules',mode=mode,layout=layout)['room']
                with self.assertRaisesRegex(service.RoomError,'players-not-ready'):
                    self.action(self.host,'start')
                for member in members: self.action(member,'ready',ready=True)
                room=self.action(self.host,'start')['room']
                self.assertEqual(room['state'],'running')
                self.assertEqual(len({m['kart'] for m in room['members']}),8 if layout=='solo' else 4)
                if layout=='coop':
                    for kart in range(4):
                        self.assertEqual({m['role'] for m in room['members'] if m['kart']==kart},
                                         {'driver','passenger'})
                self.assertEqual(room['launch']['compatibility'],'build-1')
                with self.assertRaisesRegex(service.RoomError,'event-started'): self.join()
                self.action(self.host,'finish')

    def test_seat_reservation_race(self):
        friends=[self.join(),self.join()]
        revision=self.store.request(dict(op='state',token=self.host['token']))['room']['revision']
        def reserve(member):
            try:
                self.store.request(dict(op='seat',seat=7,token=member['token'],revision=revision))
                return True
            except service.RoomError: return False
        with concurrent.futures.ThreadPoolExecutor(max_workers=2) as pool:
            self.assertEqual(sum(pool.map(reserve,friends)),1)

    def test_two_player_fields_and_battle_opponents(self):
        self.action(self.host,'ready',ready=True)
        with self.assertRaisesRegex(service.RoomError,'players-not-ready'):
            self.action(self.host,'start')
        friend=self.join()
        for member in (self.host,friend): self.action(member,'ready',ready=True)
        launch=self.action(self.host,'start')['room']['launch']
        self.assertEqual((len(launch['roster']),launch['human_karts'],launch['cpu_karts']),(2,2,6))
        self.action(self.host,'finish')
        self.action(self.host,'rules',mode='grand-prix',layout='coop')
        for member in (self.host,friend): self.action(member,'ready',ready=True)
        launch=self.action(self.host,'start')['room']['launch']
        self.assertEqual((launch['human_karts'],launch['cpu_karts']),(1,7))
        self.assertEqual([m['role'] for m in launch['roster']],['driver','passenger'])
        self.action(self.host,'finish')
        self.action(self.host,'rules',mode='balloon',layout='coop')
        for member in (self.host,friend): self.action(member,'ready',ready=True)
        with self.assertRaisesRegex(service.RoomError,'battle-needs-opponents'):
            self.action(self.host,'start')
        self.action(friend,'seat',seat=7)
        for member in (self.host,friend): self.action(member,'ready',ready=True)
        launch=self.action(self.host,'start')['room']['launch']
        self.assertEqual((launch['human_karts'],launch['cpu_karts']),(2,0))
        self.assertEqual([m['game_seat'] for m in launch['roster']],[0,2])
        self.assertEqual([m['role'] for m in launch['roster']],['solo','solo'])

    def test_variable_population_and_ready_invalidation(self):
        self.action(self.host,'ready',ready=True)
        friend=self.join()
        self.assertFalse(any(m['ready'] for m in friend['room']['members']))
        for mode in sorted(service.MODES):
            for layout in ('solo','coop'):
                for count in range(2,9):
                    store=service.Rooms()
                    host=store.request(dict(op='create',name='Host',compatibility='build-1',mode=mode,layout=layout))
                    members=[host]+[store.request(dict(op='join',name='Friend',compatibility='build-1',code=host['room']['code'])) for _ in range(count-1)]
                    def action(member,op,**values):
                        revision=store.request(dict(op='state',token=member['token']))['room']['revision']
                        return store.request(dict(op=op,token=member['token'],revision=revision,**values))
                    for member in members:action(member,'ready',ready=True)
                    if mode!='grand-prix' and layout=='coop' and count==2:
                        with self.assertRaisesRegex(service.RoomError,'battle-needs-opponents'):action(host,'start')
                        continue
                    launch=action(host,'start')['room']['launch']
                    humans=count if layout=='solo' else (count+1)//2
                    self.assertEqual(len(launch['roster']),count)
                    self.assertEqual(launch['human_karts'],humans)
                    self.assertEqual(launch['cpu_karts'],8-humans if mode=='grand-prix' else 0)
                    self.assertEqual(len({m['game_seat'] for m in launch['roster']}),count)

    def test_competition_rules(self):
        room=self.action(self.host,'rules',mode='grand-prix',layout='solo',
                         rules=dict(engine='mirror',cup='all-cup'))['room']
        self.assertEqual(room['rules'],dict(engine='mirror',cup='all-cup'))
        with self.assertRaisesRegex(service.RoomError,'invalid-rules'):
            self.action(self.host,'rules',mode='grand-prix',layout='solo',rules=dict(rounds=3))
        room=self.action(self.host,'rules',mode='bob-omb',layout='coop',
                         rules=dict(arena=59,rounds=5))['room']
        self.assertEqual(room['rules'],dict(arena=59,rounds=5))
        with self.assertRaisesRegex(service.RoomError,'invalid-rules'):
            self.action(self.host,'rules',mode='balloon',layout='solo',rules=dict(arena=True))

    def test_leave_and_expiry(self):
        friend=self.join()
        self.action(friend,'leave')
        with self.assertRaisesRegex(service.RoomError,'invalid-session'):
            self.store.request(dict(op='state',token=friend['token']))
        room=self.store.rooms[self.host['room']['code']]
        room.touched-=3600
        with self.assertRaisesRegex(service.RoomError,'invalid-session'):
            self.store.request(dict(op='state',token=self.host['token']))
        self.assertFalse(self.store.rooms)

    def test_unlocked_content_and_shared_garage(self):
        friend=self.join()
        # A brand-new room needs no save, progression record or unlock flags.
        for mode in ('balloon','shine','bob-omb'):
            for arena in service.ARENAS:
                self.assertEqual(self.action(self.host,'rules',mode=mode,layout='solo',rules=dict(arena=arena))['room']['rules']['arena'],arena)
        for invalid in (55,57,60,True):
            with self.assertRaisesRegex(service.RoomError,'invalid-rules'):
                self.action(self.host,'rules',mode='balloon',layout='solo',rules=dict(arena=invalid))
        for cup,courses in service.CUP_COURSES.items():
            for course in courses:
                self.action(self.host,'rules',mode='grand-prix',layout='solo',rules=dict(engine='mirror',cup=cup,course=course))
        for k in range(21):
            choice=dict(kart=k,driver=1,partner=2)
            room=self.action(friend,'kart',choice=choice)['room']
            self.assertEqual(next(m for m in room['members'] if m['you'])['choice'],choice)
        self.action(self.host,'rules',mode='grand-prix',layout='coop')
        choice=dict(kart=20,driver=19,partner=20)
        room=self.action(friend,'kart',choice=choice)['room']
        self.assertEqual([m['choice'] for m in room['members']],[choice,choice])
        self.assertFalse(any(m['ready'] for m in room['members']))
        for member in (self.host,friend):self.action(member,'ready',ready=True)
        launch=self.action(self.host,'start')['room']['launch']
        self.assertEqual(launch['karts'][0],choice)
        self.assertEqual((launch['human_karts'],launch['cpu_karts']),(1,7))
        with self.assertRaisesRegex(service.RoomError,'event-started'):
            self.action(friend,'kart',choice=dict(kart=0,driver=7,partner=8))
        self.action(self.host,'finish')
        for invalid in (dict(kart=21,driver=1,partner=2),dict(kart=True,driver=1,partner=2),
                        dict(kart=3,driver=19,partner=20),dict(kart=20,driver=1,partner=1)):
            with self.assertRaisesRegex(service.RoomError,'invalid-kart'):
                self.action(friend,'kart',choice=invalid)

    def test_existing_choice_survives_a_coop_partner_arriving(self):
        self.action(self.host,'rules',mode='grand-prix',layout='coop')
        self.action(self.host,'seat',seat=7)
        choice=dict(kart=20,driver=19,partner=20)
        self.action(self.host,'kart',choice=choice)
        friend=self.join()
        room=self.action(friend,'seat',seat=6)['room']
        self.assertEqual([m['choice'] for m in room['members']],[choice,choice])
        self.action(self.host,'ready',ready=True);self.action(friend,'ready',ready=True)
        launch=self.action(self.host,'start')['room']['launch']
        self.assertEqual(launch['karts'][0],choice)

    def test_frozen_launch_admission_and_seats(self):
        self.action(self.host,'seat',seat=7)
        members=[self.host]+[self.join() for _ in range(7)]
        for member in members: self.action(member,'ready',ready=True)
        with self.assertRaisesRegex(service.RoomError,'invalid-local-endpoint'):
            self.action(self.host,'start',endpoint='192.0.2.1:28121')
        result=self.action(self.host,'start',endpoint='127.0.0.1:28000')
        launch=result['room']['launch']
        self.assertEqual([m['seat'] for m in launch['roster']],[7,0,1,2,3,4,5,6])
        self.assertEqual(result['connection']['slot'],0)
        self.assertEqual(len(set(result['connection']['admissions'])),8)
        for member in members[1:]:
            reply=self.store.request(dict(op='state',token=member['token']))
            slot=reply['connection']['slot']
            self.assertNotIn('admissions',reply['connection'])
            self.assertEqual(reply['connection']['admission'],result['connection']['admissions'][slot])
            self.assertEqual(reply['room']['launch'],launch)
        self.assertNotIn('admission',json.dumps(launch))
        launch['roster'][0]['seat']=3
        self.assertEqual(self.store.request(dict(op='state',token=self.host['token']))['room']['launch']['roster'][0]['seat'],7)
        self.action(self.host,'finish')
        self.assertNotIn('connection',self.store.request(dict(op='state',token=self.host['token'])))

    def test_local_http_boundary(self):
        http=service.server()
        thread=threading.Thread(target=http.serve_forever,daemon=True)
        thread.start()
        try:
            url=f'http://127.0.0.1:{http.server_port}/v1/room'
            with urllib.request.urlopen(f'http://127.0.0.1:{http.server_port}/health',timeout=5) as response:
                self.assertEqual(json.load(response),dict(service='mkdd-local-rooms',protocol=2,scope='loopback-playtest'))
            def post(data):
                request=urllib.request.Request(url,json.dumps(data).encode(),
                                               {'Content-Type':'application/json'})
                with urllib.request.urlopen(request,timeout=5) as response:
                    return json.load(response)
            room=post(dict(op='create',name='HTTP Host',compatibility='build-1'))
            joined=post(dict(op='join',name='HTTP Friend',compatibility='build-1',code=room['room']['code']))
            self.assertEqual(len(joined['room']['members']),2)
            with self.assertRaises(urllib.error.HTTPError) as error: post([])
            self.assertEqual(error.exception.code,400)
        finally:
            http.shutdown(); http.server_close(); thread.join(timeout=5)

if __name__=='__main__':
    unittest.main()
