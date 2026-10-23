import importlib.util
from pathlib import Path
import sys
import unittest
import concurrent.futures
import threading
spec=importlib.util.spec_from_file_location('selection_rooms',Path(__file__).resolve().parents[1]/'services/rooms.py')
service=importlib.util.module_from_spec(spec);sys.modules[spec.name]=service;spec.loader.exec_module(service)

class SelectionTests(unittest.TestCase):
    def room(self,count=2,layout='coop',mode='grand-prix'):
        self.store=service.Rooms()
        host=self.store.request(dict(op='create',name='Host',initials='JON',compatibility='fixture',flow='staged',layout=layout,mode=mode))
        self.members=[host]+[self.store.request(dict(op='join',name=f'Guest {i}',initials=f'G{i}',compatibility='fixture',code=host['room']['code'])) for i in range(1,count)]
        return host
    def act(self,i,op,**values):
        token=self.members[i]['token'];state=self.store.request(dict(op='state',token=token))
        return self.store.request(dict(op=op,token=token,revision=state['room']['revision'],**values))
    def start(self):
        for i in range(len(self.members)):self.act(i,'ready',ready=True)
        return self.act(0,'start')
    def select(self,i,chars):
        self.act(i,'character',characters=chars)
        if len(chars)==2 or self.act(i,'state')['room']['members'][i]['role']!='passenger':self.act(i,'selection-kart',kart=20)
    def test_eight_shared_seats_and_repeated_cup(self):
        self.room(8);self.assertEqual(self.start()['room']['state'],'characters')
        for i in range(8):self.select(i,[i+1])
        with self.assertRaisesRegex(service.RoomError,'driver-only'):self.act(4,'selection-kart',kart=0)
        for i in range(8):reply=self.act(i,'selection-done')
        self.assertEqual(reply['room']['state'],'course')
        with self.assertRaisesRegex(service.RoomError,'host-only'):self.act(1,'start')
        self.act(0,'rules',mode='grand-prix',rules=dict(engine='150cc',cup='special'))
        launch=self.act(0,'start')['room']['launch']
        self.assertEqual([m['game_seat'] for m in launch['roster']],[0,2,4,6,1,3,5,7])
        self.assertEqual(launch['karts'][0],dict(kart=20,driver=1,partner=5))
        self.assertEqual(launch['rules']['cup'],'special')
        self.assertEqual(launch['roster'][0]['initials'],'JON')
        reply=self.act(0,'finish',event=launch['id'],results=list(range(8)));self.assertEqual(reply['room']['state'],'forming')
        self.assertEqual([m['stars'] for m in reply['room']['members']],[3,2,1,0,3,2,1,0])
        repeated=self.act(0,'finish',event=launch['id'],results=list(range(8)))
        self.assertEqual([m['stars'] for m in repeated['room']['members']],[3,2,1,0,3,2,1,0])
        self.assertEqual(len(reply['room']['members']),8)
        self.assertEqual(reply['room']['members'][0]['initials'],'JON')
        self.assertFalse(any(m['ready'] for m in reply['room']['members']))
        self.assertEqual(self.start()['room']['state'],'characters')
    def test_two_people_together_or_separate(self):
        self.room();self.act(1,'seat',seat=4)
        self.assertEqual(self.start()['room']['human_karts'],1)
        self.select(0,[7]);self.select(1,[8]);self.act(0,'selection-done');self.act(1,'selection-done')
        self.assertEqual(self.act(0,'start')['room']['launch']['cpu_karts'],7)
        self.act(0,'finish');self.act(0,'rules',mode='grand-prix',layout='solo',rules={})
        self.start()
        for i in range(2):self.select(i,[7+i*2,8+i*2]);self.act(i,'selection-done')
        self.assertEqual(self.act(0,'start')['room']['launch']['human_karts'],2)
    def test_orphan_passenger_and_battle_opponents(self):
        self.room(mode='balloon');self.act(1,'seat',seat=4)
        with self.assertRaisesRegex(service.RoomError,'battle-needs-opponents'):self.start()
        self.act(1,'seat',seat=5)
        with self.assertRaisesRegex(service.RoomError,'driver-required'):self.start()
        self.act(1,'seat',seat=1);self.assertEqual(self.start()['room']['human_karts'],2)
    def test_selections_have_ownership_and_gate_course(self):
        self.room();self.act(1,'seat',seat=4);self.start()
        self.select(0,[7])
        with self.assertRaisesRegex(service.RoomError,'character-taken'):self.select(1,[7])
        with self.assertRaisesRegex(service.RoomError,'characters-incomplete'):self.act(1,'selection-done')
        self.select(1,[8]);self.act(0,'selection-done')
        self.assertEqual(self.act(0,'state')['room']['state'],'characters')
        self.act(1,'selection-done');self.assertEqual(self.act(0,'state')['room']['state'],'course')
        self.act(0,'back');self.assertEqual(self.act(0,'state')['room']['state'],'characters')
        self.act(0,'back');self.assertEqual(self.act(0,'state')['room']['state'],'forming')
    def test_initials_validation_and_lobby_edit(self):
        self.room();self.act(1,'profile',initials='ABC')
        self.assertEqual(self.act(1,'state')['room']['members'][1]['initials'],'ABC')
        self.act(1,'profile',initials='ABCDEFG')
        for value in ['','ABCDEFGH','<X>','a','A B',12]:
            with self.assertRaisesRegex(service.RoomError,'invalid-initials'):self.act(1,'profile',initials=value)

    def test_partial_solo_claim_is_global_and_can_be_released(self):
        self.room(3,layout='solo');self.start()
        self.act(0,'character',characters=[9])
        for i in (1,2):
            with self.assertRaisesRegex(service.RoomError,'character-taken'):self.act(i,'character',characters=[9])
        with self.assertRaisesRegex(service.RoomError,'characters-incomplete'):self.act(0,'selection-done')
        self.act(0,'character',characters=[9,10])
        with self.assertRaisesRegex(service.RoomError,'character-taken'):self.act(1,'character',characters=[10,11])
        self.act(0,'character',characters=[])
        self.act(1,'character',characters=[9,10])
        self.assertEqual(self.act(0,'state')['room']['members'][1]['characters'],[9,10])

    def test_simultaneous_shared_dk_claim_has_one_winner_and_loser_can_continue(self):
        self.room(3);self.act(2,'seat',seat=4);self.start()
        barrier=threading.Barrier(2)
        def claim(i):
            state=self.act(i,'state')['room'];barrier.wait()
            data=dict(op='character',token=self.members[i]['token'],revision=state['revision'],characters=[13])
            try:self.store.request(data);return i,'accepted'
            except service.RoomError as e:
                if str(e)=='stale-room':
                    try:self.act(i,'character',characters=[13]);return i,'accepted'
                    except service.RoomError as retry:return i,str(retry)
                return i,str(e)
        with concurrent.futures.ThreadPoolExecutor(2) as pool:results=list(pool.map(claim,(0,2)))
        self.assertEqual(sorted(status for _,status in results),['accepted','character-taken'])
        winner=next(i for i,status in results if status=='accepted');loser=2 if winner==0 else 0
        self.act(loser,'character',characters=[14])
        self.act(1,'character',characters=[7,8]);self.act(1,'selection-kart',kart=20)
        self.act(0,'selection-kart',kart=20)
        for i in (0,1,2):self.act(i,'selection-done')
        self.assertEqual(self.act(0,'start')['room']['state'],'running')

    def test_sixteen_human_riders_have_unique_locks_and_new_event_clears_them(self):
        self.room(8,layout='solo');self.start()
        for i in range(8):
            self.act(i,'character',characters=[i*2+1]);self.act(i,'character',characters=[i*2+1,i*2+2]);self.act(i,'selection-kart',kart=20)
        claims=[c for m in self.act(0,'state')['room']['members'] for c in m['characters']]
        self.assertEqual(len(set(claims)),16)
        for i in range(8):self.act(i,'selection-done')
        self.act(0,'start');self.act(0,'finish');self.start()
        self.assertTrue(all(not m['characters'] for m in self.act(0,'state')['room']['members']))

    def test_cpu_medals_do_not_award_humans_and_totals_follow_players(self):
        self.room(3)
        def event():
            self.start()
            for i in range(3):
                self.select(i,[i*2+1,i*2+2]);self.act(i,'selection-done')
            return self.act(0,'start')['room']['launch']['id']
        first=event()
        # CPU kart 5 wins; only the human silver and bronze medalists score.
        result=self.act(0,'finish',event=first,results=[5,2,0,1,3,4,6,7])
        self.assertEqual([m['stars'] for m in result['room']['members']],[1,0,2])
        tokens=[m['token'] for m in self.members]
        self.act(0,'seat',seat=3)
        second=event()
        # New packed order is guest 1, guest 2, host. Scores follow identities.
        result=self.act(0,'finish',event=second,results=[2,0,1,3,4,5,6,7])
        self.assertEqual({m['initials']:m['stars'] for m in result['room']['members']},dict(JON=4,G1=2,G2=3))
        self.assertEqual([m['token'] for m in self.members],tokens)

    def test_bad_or_unauthorized_results_leave_scores_and_event_unchanged(self):
        self.room();self.start()
        for i in range(2):self.select(i,[i*2+1,i*2+2]);self.act(i,'selection-done')
        event=self.act(0,'start')['room']['launch']['id']
        with self.assertRaisesRegex(service.RoomError,'host-only'):
            self.act(1,'finish',event=event,results=list(range(8)))
        for values in ([0]*8,list(range(7)),[True]+list(range(1,8)),list(range(1,9))):
            with self.assertRaisesRegex(service.RoomError,'invalid-results'):
                self.act(0,'finish',event=event,results=values)
        with self.assertRaisesRegex(service.RoomError,'invalid-results'):
            self.act(0,'finish',event='previous-cup',results=list(range(8)))
        state=self.act(0,'state')['room']
        self.assertEqual(state['state'],'running')
        self.assertEqual([m['stars'] for m in state['members']],[0,0])

if __name__=='__main__':unittest.main()
