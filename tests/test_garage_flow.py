import importlib.util
from pathlib import Path
import sys
import unittest

spec=importlib.util.spec_from_file_location('garage_rooms',Path(__file__).resolve().parents[1]/'services/rooms.py')
service=importlib.util.module_from_spec(spec);sys.modules[spec.name]=service;spec.loader.exec_module(service)

class GarageTests(unittest.TestCase):
    def create(self,n=8):
        self.store=service.Rooms()
        host=self.store.request(dict(op='create',name='HOST',initials='HOST',compatibility='test',flow='garage',layout='mixed'))
        self.players=[host]+[self.store.request(dict(op='join',name=f'G{i}',initials=f'G{i}',compatibility='test',code=host['room']['code'])) for i in range(1,n)]
        self.assertRegex(host['room']['code'],r'^[23456789ABCDEFGHJKLMNPQRSTUVWXYZ]{8}$')
    def act(self,i,op,**values):
        token=self.players[i]['token'];state=self.store.request(dict(op='state',token=token))
        if op=='kick' and 'member' not in values:
            values['member']=next((m['id'] for m in state['room']['members'] if m['seat']==values.get('seat')),'')
        return self.store.request(dict(op=op,token=token,revision=state['room']['revision'],**values))
    def choose_mode(self,mode):
        for i in range(len(self.players)):self.act(i,'ready',ready=True)
        self.assertEqual(self.act(0,'start')['room']['state'],'mode')
        self.act(0,'rules',mode=mode,rules={})
        self.assertEqual(self.act(0,'start')['room']['state'],'characters')
    def select_all(self):
        for i in range(len(self.players)):
            own=next(m for m in self.act(i,'state')['room']['members'] if m['you'])
            chars=[i+1] if own['role']!='solo' else [i+1,i+11]
            self.act(i,'character',characters=chars)
            if own['role']!='passenger':self.act(i,'selection-kart',kart=20)
        for i in range(len(self.players)):self.act(i,'selection-done')
    def test_mixed_seating_all_modes_and_room_retention(self):
        for mode in service.MODES:
            with self.subTest(mode=mode):
                self.create()
                # Three pairs plus two solo karts: eight humans in five karts.
                self.act(5,'seat',seat=8);self.act(6,'seat',seat=9);self.act(7,'seat',seat=10)
                self.choose_mode(mode);self.select_all()
                launch=self.act(0,'start')['room']['launch']
                self.assertEqual(launch['human_karts'],5)
                self.assertEqual(launch['cpu_karts'],3 if mode=='grand-prix' else 0)
                self.assertEqual(set(m['game_seat'] for m in launch['roster']),set(range(8)))
                self.assertEqual(sum(m['role']=='solo' for m in launch['roster']),2)
                self.assertEqual(sum(m['role']=='passenger' for m in launch['roster']),3)
                count=8 if mode=='grand-prix' else 5
                result=self.act(0,'finish',event=launch['id'],results=list(range(count)))
                self.assertEqual(result['room']['state'],'forming')
                self.assertEqual(len(result['room']['members']),8)
                self.assertEqual(result['room']['code'],self.players[0]['room']['code'])
    def test_back_seat_requires_driver_and_driver_departure_promotes_passenger(self):
        self.create(3)
        with self.assertRaisesRegex(service.RoomError,'driver-required'):self.act(1,'seat',seat=15)
        self.act(1,'seat',seat=8)
        self.act(0,'seat',seat=7)
        own=next(m for m in self.act(1,'state')['room']['members'] if m['you'])
        self.assertEqual((own['seat'],own['role']),(0,'solo'))
    def test_single_shared_kart_can_race_cpus_but_cannot_battle(self):
        self.create(2);self.act(1,'seat',seat=8)
        for i in range(2):self.act(i,'ready',ready=True)
        self.act(0,'start')
        for mode in ('balloon','shine','bob-omb'):
            with self.assertRaisesRegex(service.RoomError,'battle-needs-opponents'):self.act(0,'rules',mode=mode,rules={})
        self.act(0,'rules',mode='grand-prix',rules={});self.act(0,'start');self.select_all()
        self.assertEqual(self.act(0,'start')['room']['launch']['cpu_karts'],7)
    def test_kick_and_profile_ownership(self):
        self.create(3);self.act(2,'seat',seat=9)
        with self.assertRaisesRegex(service.RoomError,'host-only'):self.act(1,'kick',seat=0)
        with self.assertRaisesRegex(service.RoomError,'cannot-kick-host'):self.act(0,'kick',seat=0)
        self.act(1,'ready',ready=True);self.act(1,'profile',initials='FRIEND7')
        own=next(m for m in self.act(1,'state')['room']['members'] if m['you'])
        self.assertEqual(own['name'],'FRIEND7');self.assertFalse(own['ready'])
        result=self.act(0,'kick',seat=1)
        self.assertEqual(len(result['room']['members']),2)
        self.assertEqual(next(m for m in self.act(2,'state')['room']['members'] if m['you'])['seat'],1)
        with self.assertRaisesRegex(service.RoomError,'invalid-session'):self.act(1,'state')

    def test_kick_rejoin_same_code_and_stale_confirmation(self):
        self.create(3)
        old=next(m for m in self.act(0,'state')['room']['members'] if m['seat']==1)
        self.act(1,'seat',seat=7);self.act(2,'seat',seat=1)
        with self.assertRaisesRegex(service.RoomError,'player-changed'):
            self.act(0,'kick',seat=1,member=old['id'])
        self.assertEqual(len(self.act(0,'state')['room']['members']),3)
        self.act(0,'kick',seat=7,member=old['id'])
        joined=self.store.request(dict(op='join',name='G1',initials='G1',compatibility='test',code=self.players[0]['room']['code']))
        self.assertNotEqual(joined['token'],self.players[1]['token'])
        self.assertEqual(len(joined['room']['members']),3)

    def test_preview_does_not_change_revision_or_commit_selection(self):
        self.create(3);self.choose_mode('grand-prix')
        before=self.act(0,'state')['room']
        reply=self.store.request(dict(op='preview',token=self.players[1]['token'],character=20,revision=-100))
        own=next(m for m in reply['room']['members'] if m['you'])
        self.assertEqual(reply['room']['revision'],before['revision'])
        self.assertEqual(own['hover'],20);self.assertEqual(own['characters'],[])
        self.select_all()
        with self.assertRaisesRegex(service.RoomError,'host-only'):self.act(1,'preview',scene=3,index=4)
        self.assertEqual(self.act(0,'preview',scene=3,index=4)['room']['preview'],dict(scene=3,index=4))

    def test_character_preview_in_flight_when_last_player_finishes(self):
        self.create(2);self.choose_mode('grand-prix')
        before=self.act(1,'state')['room']
        # A guest sends this while still seeing character selection. The host
        # can confirm the final kart before the request reaches the service.
        pending=dict(op='preview',token=self.players[1]['token'],character=7)
        self.select_all()
        snapshot=self.act(1,'state')['room']
        self.assertEqual(snapshot['state'],'course')
        result=self.store.request(pending)['room']
        self.assertEqual(result,snapshot)
        self.assertGreater(result['revision'],before['revision'])
        # A further delayed preview must deliver the frozen launch to the
        # guest rather than strand it on the old screen with event-started.
        self.act(0,'start')
        running=self.act(1,'state')['room']
        self.assertEqual(self.store.request(pending)['room'],running)
        self.assertEqual(running['state'],'running')

    def test_course_preview_in_flight_when_host_returns_to_characters(self):
        self.create(2);self.choose_mode('grand-prix');self.select_all()
        pending=dict(op='preview',token=self.players[0]['token'],scene=3,index=4)
        self.act(0,'back')
        before=self.act(0,'state')['room']
        self.assertEqual(self.store.request(pending)['room'],before)
        self.assertEqual(before['state'],'characters')
        self.assertTrue(all(m['hover']==0 for m in before['members']))

    def test_abort_returns_everyone_without_awarding_stars(self):
        self.create(3);self.choose_mode('grand-prix');self.select_all()
        event=self.act(0,'start')['room']['launch']['id']
        with self.assertRaisesRegex(service.RoomError,'host-only'):self.act(1,'abort',event=event)
        with self.assertRaisesRegex(service.RoomError,'no-event'):self.act(0,'abort',event='old-event')
        self.act(0,'abort',event=event)
        for i in range(3):
            room=self.act(i,'state')['room']
            self.assertEqual(room['state'],'forming');self.assertIsNone(room['launch'])
            self.assertTrue(all(m['stars']==0 and not m['ready'] for m in room['members']))

    def test_leave_promotes_passenger_without_changing_identity(self):
        self.create(3);self.act(2,'seat',seat=9)
        before=next(m for m in self.act(2,'state')['room']['members'] if m['you'])
        self.act(1,'leave')
        after=next(m for m in self.act(2,'state')['room']['members'] if m['you'])
        self.assertEqual(after['id'],before['id']);self.assertEqual(after['seat'],1)

    def test_refresh_code_preserves_members_and_old_invitation_stops_joining(self):
        self.create(3);self.act(2,'seat',seat=9);self.act(1,'ready',ready=True)
        before=self.act(0,'state')['room'];old=before['code']
        with self.assertRaisesRegex(service.RoomError,'host-only'):self.act(1,'refresh-code')
        proposal=self.act(0,'refresh-code',propose=True)['code']
        self.assertEqual(self.act(0,'state')['room']['code'],old)
        room=self.act(0,'refresh-code',code=proposal)['room']
        self.assertNotEqual(room['code'],old);self.assertEqual(room['members'],before['members'])
        for i in range(3):self.assertEqual(self.act(i,'state')['room']['code'],proposal)
        with self.assertRaisesRegex(service.RoomError,'room-not-found'):
            self.store.request(dict(op='join',code=old,name='LATE',initials='LATE',compatibility='test'))
        joined=self.store.request(dict(op='join',code=proposal,name='LATE',initials='LATE',compatibility='test'))
        self.assertEqual(len(joined['room']['members']),4)

if __name__=='__main__':unittest.main()
