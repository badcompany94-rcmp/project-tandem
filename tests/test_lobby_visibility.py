import importlib.util
from pathlib import Path
import sys
import unittest

spec=importlib.util.spec_from_file_location('visibility_rooms',Path(__file__).resolve().parents[1]/'services/rooms.py')
service=importlib.util.module_from_spec(spec);sys.modules[spec.name]=service;spec.loader.exec_module(service)

class LobbyVisibilityTests(unittest.TestCase):
    def test_rename_proposal_is_nonmutating_and_commit_preserves_live_room(self):
        store=service.Rooms();host=store.request(dict(op='create',name='Johnny',compatibility='test',visibility='public',locked=False))
        guest=store.request(dict(op='join',name='P2',compatibility='test',code=host['room']['code']))
        room=store.rooms[host['room']['code']]
        for member in room.members.values():member.ready=True
        store.launch_event(room,'127.0.0.1:28121')
        before=store.request(dict(op='state',token=host['token']))
        edit=dict(op='lobby-settings',token=host['token'],revision=room.revision,lobby_name='Friday Karts')
        self.assertEqual(store.request(dict(edit,propose=True)),dict(lobby_name='Friday Karts'))
        self.assertEqual(store.request(dict(op='state',token=host['token'])),before)
        after=store.request(edit)
        self.assertEqual(after['room']['lobby_name'],'Friday Karts')
        self.assertEqual(after['room']['members'],before['room']['members'])
        self.assertEqual(after['room']['code'],before['room']['code'])
        self.assertEqual(after['room']['launch'],before['room']['launch'])
        self.assertEqual(after['connection'],before['connection'])
        self.assertEqual(store.request(dict(op='state',token=guest['token']))['room']['lobby_name'],'Friday Karts')

    def test_rename_validation_permissions_and_duplicate_allocation(self):
        store=service.Rooms();host=store.request(dict(op='create',name='HOST',compatibility='test'))
        other=store.request(dict(op='create',name='Friday Karts',compatibility='test'))
        guest=store.request(dict(op='join',name='P2',compatibility='test',code=host['room']['code']))
        revision=guest['room']['revision']
        before=store.request(dict(op='state',token=host['token']))
        for values,error in [(dict(token=guest['token'],revision=revision,lobby_name='Hijack'),'host-only'),
                (dict(token=host['token'],revision=revision-1,lobby_name='Stale'),'stale-room'),
                *[(dict(token=host['token'],revision=revision,lobby_name=name),'invalid') for name in ('','   ','A'*25,'bad\nname','🌈')],
                (dict(token=host['token'],revision=revision,lobby_name='friday karts',exact_name=True),'lobby-name-unavailable')]:
            with self.assertRaisesRegex(service.RoomError,error):store.request(dict(op='lobby-settings',**values))
            self.assertEqual(store.request(dict(op='state',token=host['token'])),before)
        renamed=store.request(dict(op='lobby-settings',token=host['token'],revision=revision,lobby_name='friday karts'))
        self.assertEqual(renamed['room']['lobby_name'],'friday karts (1)')
        again=store.request(dict(op='lobby-settings',token=host['token'],revision=renamed['room']['revision'],lobby_name='friday karts (1)'))
        self.assertEqual(again['room']['lobby_name'],'friday karts (1)')

    def test_private_default_and_code_join(self):
        store=service.Rooms();created=store.request(dict(op='create',name='HOST',compatibility='test'))
        self.assertEqual(created['room']['visibility'],'private')
        self.assertEqual(created['room']['lobby_name'],'HOST')
        with self.assertRaisesRegex(service.RoomError,'room-private'):
            store.request(dict(op='join',name='P2',compatibility='test',code=created['room']['code'],public_join=True))
        joined=store.request(dict(op='join',name='P2',compatibility='test',code=created['room']['code']))
        self.assertEqual(len(joined['room']['members']),2)

    def test_named_public_room_capacity_and_private_guard(self):
        store=service.Rooms();created=store.request(dict(op='create',name='HOST',compatibility='test',lobby_name='SATURDAY KARTS',visibility='public'))
        self.assertEqual(created['room']['lobby_name'],'SATURDAY KARTS')
        self.assertEqual(created['room']['visibility'],'public')
        for i in range(1,8):
            response=store.request(dict(op='join',name=f'P{i}',compatibility='test',code=created['room']['code'],public_join=True))
            self.assertEqual(len(response['room']['members']),i+1)
            self.assertEqual(response['room']['capacity'],8)
        with self.assertRaisesRegex(service.RoomError,'room-full'):
            store.request(dict(op='join',name='P9',compatibility='test',code=created['room']['code'],public_join=True))

    def test_invalid_metadata_does_not_create_a_room(self):
        for metadata in [dict(lobby_name=''),dict(lobby_name='A'*25),dict(lobby_name='bad\nname'),dict(lobby_name='🌈'),dict(visibility='secret')]:
            store=service.Rooms()
            with self.assertRaises(service.RoomError):store.request(dict(op='create',name='HOST',compatibility='test',**metadata))
            self.assertEqual(len(store.rooms),0)

    def test_host_can_hide_populated_room_without_changing_members(self):
        store=service.Rooms()
        host=store.request(dict(op='create',name='Johnny',compatibility='test',visibility='public'))
        guest=store.request(dict(op='join',name='P2',compatibility='test',code=host['room']['code'],public_join=True))
        room=store.rooms[host['room']['code']]
        for member in room.members.values():member.ready=True
        before=store.request(dict(op='state',token=host['token']))['room']
        hidden=store.request(dict(op='lobby-settings',token=host['token'],revision=before['revision'],visibility='private'))['room']
        self.assertEqual(hidden['members'],before['members'])
        self.assertEqual(hidden['code'],before['code'])
        self.assertEqual(hidden['lobby_name'],'Johnny')
        self.assertEqual(hidden['revision'],before['revision']+1)
        self.assertEqual(store.request(dict(op='state',token=guest['token']))['room']['visibility'],'private')
        with self.assertRaisesRegex(service.RoomError,'room-private'):
            store.request(dict(op='join',name='P3',compatibility='test',code=hidden['code'],public_join=True))
        joined=store.request(dict(op='join',name='P3',compatibility='test',code=hidden['code']))
        self.assertEqual(len(joined['room']['members']),3)
        shown=store.request(dict(op='lobby-settings',token=host['token'],revision=joined['room']['revision'],visibility='public'))
        self.assertEqual(shown['room']['visibility'],'public')

    def test_guest_and_invalid_or_stale_changes_cannot_change_visibility(self):
        store=service.Rooms()
        host=store.request(dict(op='create',name='HOST',compatibility='test'))
        guest=store.request(dict(op='join',name='P2',compatibility='test',code=host['room']['code']))
        revision=guest['room']['revision']
        for token,rev,value,error in [(guest['token'],revision,'public','host-only'),
                                      (host['token'],revision-1,'public','stale-room'),
                                      (host['token'],revision,'secret','invalid-visibility')]:
            with self.assertRaisesRegex(service.RoomError,error):
                store.request(dict(op='lobby-settings',token=token,revision=rev,visibility=value))
        unchanged=store.request(dict(op='state',token=host['token']))['room']
        self.assertEqual(unchanged['visibility'],'private')
        self.assertEqual(unchanged['revision'],revision)

    def test_visibility_change_preserves_running_event_and_admissions(self):
        store=service.Rooms()
        host=store.request(dict(op='create',name='HOST',compatibility='test'))
        store.request(dict(op='join',name='P2',compatibility='test',code=host['room']['code']))
        room=store.rooms[host['room']['code']]
        store.launch_event(room,'127.0.0.1:28121')
        before=store.request(dict(op='state',token=host['token']))
        after=store.request(dict(op='lobby-settings',token=host['token'],revision=before['room']['revision'],visibility='public'))
        self.assertEqual(after['connection'],before['connection'])
        self.assertEqual(after['room']['launch'],before['room']['launch'])
        self.assertEqual(after['room']['state'],'running')

    def test_duplicate_names_are_numbered_atomically_and_stay_stable(self):
        from concurrent.futures import ThreadPoolExecutor
        store=service.Rooms()
        with ThreadPoolExecutor(max_workers=8) as workers:
            rooms=list(workers.map(lambda _:store.request(dict(op='create',name='Johnny',compatibility='test')),range(8)))
        names={r['room']['lobby_name'] for r in rooms}
        self.assertEqual(names,{'Johnny',*(f'Johnny ({i})' for i in range(1,8))})
        store.request(dict(op='leave',token=rooms[0]['token']))
        for result in rooms[1:]:
            self.assertEqual(store.request(dict(op='state',token=result['token']))['room']['lobby_name'],result['room']['lobby_name'])
        different_case=store.request(dict(op='create',name='johnny',compatibility='test'))
        self.assertNotIn(different_case['room']['lobby_name'].casefold(),{r['room']['lobby_name'].casefold() for r in rooms[1:]})

    def test_numbered_custom_names_fit_display_limit(self):
        store=service.Rooms()
        first=store.request(dict(op='create',name='HOST',compatibility='test',lobby_name='A'*24))
        second=store.request(dict(op='create',name='HOST',compatibility='test',lobby_name='A'*24))
        self.assertEqual(first['room']['lobby_name'],'A'*24)
        self.assertEqual(second['room']['lobby_name'],'A'*20+' (1)')

    def test_lock_blocks_all_new_joins_and_unlock_keeps_players_ready(self):
        store=service.Rooms()
        host=store.request(dict(op='create',name='HOST',compatibility='test',visibility='public'))
        guest=store.request(dict(op='join',name='P2',compatibility='test',code=host['room']['code']))
        room=store.rooms[host['room']['code']]
        for m in room.members.values():m.ready=True
        before=store.request(dict(op='state',token=host['token']))['room']
        locked=store.request(dict(op='lobby-settings',token=host['token'],revision=before['revision'],locked=True))['room']
        self.assertTrue(locked['locked']);self.assertEqual(locked['members'],before['members'])
        self.assertEqual(locked['visibility'],'public')
        for public in (True,False):
            with self.assertRaisesRegex(service.RoomError,'room-locked'):
                store.request(dict(op='join',name='P3',compatibility='test',code=locked['code'],public_join=public))
        store.launch_event(room,'127.0.0.1:28121')
        running=store.request(dict(op='state',token=host['token']))
        changed=store.request(dict(op='lobby-settings',token=host['token'],revision=room.revision,locked=False,visibility='private'))
        self.assertFalse(changed['room']['locked'])
        self.assertEqual(changed['room']['members'],running['room']['members'])
        self.assertEqual(changed['room']['launch'],running['room']['launch'])
        self.assertEqual(changed['connection'],running['connection'])
        self.assertEqual(store.request(dict(op='state',token=guest['token']))['room']['visibility'],'private')

    def test_invalid_lock_is_atomic_and_guests_cannot_unlock(self):
        store=service.Rooms()
        host=store.request(dict(op='create',name='HOST',compatibility='test',locked=True))
        with self.assertRaisesRegex(service.RoomError,'invalid-lock'):
            store.request(dict(op='lobby-settings',token=host['token'],revision=host['room']['revision'],locked='yes',visibility='public'))
        state=store.request(dict(op='state',token=host['token']))['room']
        self.assertEqual(state,host['room'])

if __name__=='__main__':unittest.main()
