"""Private-room control service prototype; run locally until Internet deployment.

POST /v1/room accepts an operation and JSON arguments. Session tokens authorize
changes; invitation codes only authorize joining. This service does not yet
carry simulation inputs. It must be behind HTTPS before Internet deployment.
"""
from __future__ import annotations
from dataclasses import dataclass, field
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import argparse
import copy
import ipaddress
import json
import secrets
import threading
import time

ALPHABET = '23456789ABCDEFGHJKLMNPQRSTUVWXYZ'
MODES = {'grand-prix', 'balloon', 'shine', 'bob-omb'}
LAYOUTS = {'solo', 'coop', 'mixed'}
ARENAS = (52,53,54,56,58,59)
CUP_COURSES = {'mushroom':(36,34,33,50),'flower':(40,37,35,42),
               'star':(51,41,38,45),'special':(43,44,47,49)}
CUP_COURSES['all-cup'] = sum(CUP_COURSES.values(),())
DEFAULTS = [(0,7,8),(4,5,6),(1,13,14),(7,15,16),(2,11,12),(3,4,3),(5,1,2),(6,9,10)]
KART_WEIGHT = (1,2,1,0,1,0,2,2,1,0,1,0,1,0,1,0,0,0,2,2,2)
CHAR_WEIGHT = (0,0,0,0,0,1,1,1,1,2,1,1,1,2,0,2,0,0,0,2,2)

class RoomError(Exception):
    pass

@dataclass
class Member:
    token: str
    name: str
    seat: int
    ready: bool = False
    choice: dict | None = None
    initials: str = ''
    characters: list[int] = field(default_factory=list)
    selected: bool = False
    stars: int = 0
    uid: str = field(default_factory=lambda: secrets.token_hex(8))
    hover: int = 0

@dataclass
class Room:
    code: str
    compatibility: str
    host: str
    mode: str
    layout: str
    rules: dict = field(default_factory=dict)
    members: dict[str, Member] = field(default_factory=dict)
    revision: int = 1
    state: str = 'forming'
    launch: dict | None = None
    admissions: dict[str,str] = field(default_factory=dict)
    endpoint: str = ''
    touched: float = field(default_factory=time.monotonic)
    flow: str = 'legacy'
    last_finished: str = ''
    preview: dict = field(default_factory=dict)

class Rooms:
    def __init__(self, ttl=1800, max_rooms=256):
        self.rooms: dict[str, Room] = {}
        self.tokens: dict[str, str] = {}
        self.lock = threading.RLock()
        self.ttl = ttl
        self.max_rooms = max_rooms

    @staticmethod
    def text(value, maximum):
        if not isinstance(value, str) or not value.strip() or len(value) > maximum:
            raise RoomError('invalid-text')
        if any(ord(c)<32 or ord(c)==127 for c in value):
            raise RoomError('invalid-text')
        return value.strip()

    @staticmethod
    def code(value):
        if not isinstance(value,str):
            raise RoomError('invalid-code')
        result=value.replace('-','').replace(' ','').upper()
        if not ((len(result)==4 and result.isascii() and result.isdecimal()) or
                (len(result)==8 and all(c in ALPHABET for c in result))):
            raise RoomError('invalid-code')
        return result

    def remove(self, room):
        for token in room.members:
            self.tokens.pop(token,None)
        self.rooms.pop(room.code,None)

    def expire(self):
        now=time.monotonic()
        for room in list(self.rooms.values()):
            if now-room.touched>self.ttl:
                self.remove(room)

    @staticmethod
    def rules(mode, values):
        if values is None:
            values={}
        if not isinstance(values,dict):
            raise RoomError('invalid-rules')
        if mode=='grand-prix':
            defaults=dict(engine='150cc',cup='mushroom')
            if set(values)-{'engine','cup','course'}:
                raise RoomError('invalid-rules')
            defaults.update(values)
            if defaults['engine'] not in ('50cc','100cc','150cc','mirror') or defaults['cup'] not in ('mushroom','flower','star','special','all-cup'):
                raise RoomError('invalid-rules')
            if 'course' in defaults and (type(defaults['course'])!=int or defaults['course'] not in CUP_COURSES[defaults['cup']]):
                raise RoomError('invalid-rules')
        else:
            defaults=dict(arena=0x34,rounds=3)
            if set(values)-set(defaults):
                raise RoomError('invalid-rules')
            defaults.update(values)
            if type(defaults['arena'])!=int or defaults['arena'] not in ARENAS or type(defaults['rounds'])!=int or defaults['rounds'] not in (1,3,5,7,9):
                raise RoomError('invalid-rules')
        return defaults

    @staticmethod
    def initials(value):
        if not isinstance(value,str) or not 1<=len(value)<=7 or any(c not in 'ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789' for c in value):
            raise RoomError('invalid-initials')
        return value

    @staticmethod
    def pairs(room):
        if room.layout=='mixed':return [(i,i+8) for i in range(8)]
        return [(i,i+4) if room.flow=='staged' else (2*i,2*i+1) for i in range(4)]

    @staticmethod
    def arrangement(room):
        """Keep lobby seats stable; pack occupied karts for native race setup."""
        seats={m.seat for m in room.members.values()}
        result={}
        if room.layout=='solo':
            for kart,seat in enumerate(sorted(seats)):
                result[seat]=dict(game_seat=kart,kart=kart,role='solo')
        else:
            kart=0;pad=0
            for driver,passenger in Rooms.pairs(room):
                occupied=sorted(seats & {driver,passenger})
                if not occupied: continue
                for seat in occupied:
                    offset=int(seat==passenger) if len(occupied)==2 else 0
                    result[seat]=dict(game_seat=pad if room.layout=='mixed' else 2*kart+offset,kart=kart,
                                      role=('driver' if offset==0 else 'passenger') if len(occupied)==2 else 'solo')
                    pad+=1
                kart+=1
        return result

    @staticmethod
    def garage(room):
        arrangement=Rooms.arrangement(room)
        if room.flow in ('staged','garage'):
            result={}
            for member in sorted(room.members.values(),key=lambda m:m.seat):
                group=arrangement[member.seat];kart=group['kart']
                if kart not in result:
                    k,d,p=DEFAULTS[kart];result[kart]=dict(kart=k,driver=d,partner=p)
                choice=result[kart]
                if member.characters:
                    if group['role']=='passenger':choice['partner']=member.characters[0]
                    else:
                        choice['driver']=member.characters[0]
                        if len(member.characters)==2:choice['partner']=member.characters[1]
                if member.choice is not None and group['role']!='passenger':choice['kart']=member.choice['kart']
            return result
        result={}
        chosen=set()
        for member in sorted(room.members.values(),key=lambda m:m.seat):
            kart=arrangement[member.seat]['kart']
            if kart not in result or (member.choice is not None and kart not in chosen):
                k,d,p=DEFAULTS[kart]
                result[kart]=(member.choice or dict(kart=k,driver=d,partner=p)).copy()
                if member.choice is not None:chosen.add(kart)
        return result

    @staticmethod
    def snapshot(room, token):
        arrangement=Rooms.arrangement(room)
        garage=Rooms.garage(room)
        humans=len({m['kart'] for m in arrangement.values()})
        return dict(code=room.code, revision=room.revision, state=room.state,
                    flow=room.flow,seating='garage' if room.layout=='mixed' else 'drivers-first' if room.flow=='staged' and room.layout=='coop' else 'individual' if room.layout=='solo' else 'adjacent',
                    mode=room.mode, layout=room.layout, rules=room.rules.copy(), capacity=8,
                    minimum_players=2,human_karts=humans,cpu_karts=8-humans if room.mode=='grand-prix' else 0,
                    host=token==room.host, launch=copy.deepcopy(room.launch),preview=room.preview.copy(),
                    members=[dict(id=m.uid,hover=m.hover,name=m.name, initials=m.initials or f'P{m.seat+1}',stars=m.stars,seat=m.seat, ready=m.ready,
                                  characters=m.characters.copy(),selected=m.selected,
                                  kart_selected=m.choice is not None,
                                  you=m.token==token, host=m.token==room.host,
                                  kart=m.seat%8 if room.layout=='mixed' else m.seat if room.layout=='solo' else m.seat%4 if room.flow=='staged' else m.seat//2,
                                  choice=garage[arrangement[m.seat]['kart']].copy(),
                                  role=arrangement[m.seat]['role'])
                             for m in sorted(room.members.values(),key=lambda m:m.seat)])

    @classmethod
    def reply(cls,room,token):
        result=dict(room=cls.snapshot(room,token))
        if room.state=='running' and room.launch:
            own=next(m for m in room.launch['roster'] if m['seat']==room.members[token].seat)
            result['connection']=dict(endpoint=room.endpoint,slot=own['slot'],
                                      admission=room.admissions[token])
            if token==room.host:
                by_seat={m.seat:t for t,m in room.members.items()}
                result['connection']['admissions']=[room.admissions[by_seat[m['seat']]] for m in room.launch['roster']]
        return result

    @staticmethod
    def reset_selection(room):
        if room.flow not in ('staged','garage'):return
        room.preview={}
        for member in room.members.values():member.characters=[];member.selected=False;member.choice=None;member.hover=0

    @staticmethod
    def validate_drivers(room):
        if room.layout=='mixed':
            seats={m.seat for m in room.members.values()}
            if any(seat>=8 and seat-8 not in seats for seat in seats):raise RoomError('driver-required')
        if room.layout=='coop' and room.flow=='staged':
            seats={m.seat for m in room.members.values()}
            if any(seat>=4 and seat-4 not in seats for seat in seats):raise RoomError('driver-required')

    @staticmethod
    def complete_selection(room):
        if not all(m.selected for m in room.members.values()):return False
        arrangement=Rooms.arrangement(room)
        claimed=set()
        for member in room.members.values():
            if len(member.characters)!=(2 if arrangement[member.seat]['role']=='solo' else 1):return False
            if claimed.intersection(member.characters):return False
            claimed.update(member.characters)
        for choice in Rooms.garage(room).values():
            k,d,p=(choice[key] for key in ('kart','driver','partner'))
            if d==p or max(CHAR_WEIGHT[d],CHAR_WEIGHT[p])>KART_WEIGHT[k]:return False
        return True

    @staticmethod
    def launch_event(room,endpoint):
        arrangement=Rooms.arrangement(room)
        humans=len({m['kart'] for m in arrangement.values()})
        ordered=[room.members[room.host]]+sorted((m for t,m in room.members.items() if t!=room.host),key=lambda m:m.seat)
        roster=[dict(slot=i,name=m.name,initials=m.initials or f'P{m.seat+1}',seat=m.seat,**arrangement[m.seat]) for i,m in enumerate(ordered)]
        karts=[dict(kart=k,driver=d,partner=p) for k,d,p in DEFAULTS]
        for kart,choice in Rooms.garage(room).items():karts[kart]=choice.copy()
        room.state='running';room.endpoint=endpoint
        room.admissions={m.token:secrets.token_hex(16) for m in ordered}
        room.launch=dict(seed=secrets.randbits(32),epoch=int(time.time()),id=secrets.token_hex(16),compatibility=room.compatibility,
                         mode=room.mode,layout=room.layout,rules=room.rules.copy(),roster=roster,
                         human_karts=humans,cpu_karts=8-humans if room.mode=='grand-prix' else 0,karts=karts)
        if room.flow in ('staged','garage'):room.launch['seating']='garage' if room.layout=='mixed' else 'drivers-first' if room.layout=='coop' else 'individual'

    @staticmethod
    def local_endpoint(value):
        # This prototype has no encrypted public gameplay transport or relay yet.
        if not isinstance(value,str) or value.count(':')!=1:
            raise RoomError('invalid-endpoint')
        host,port=value.split(':')
        try:
            address=ipaddress.IPv4Address(host)
            if not address.is_loopback or not port.isascii() or not port.isdecimal() or not 1<=int(port)<=65535:
                raise ValueError()
        except ValueError:
            raise RoomError('invalid-local-endpoint') from None
        return f'{address}:{int(port)}'

    def request(self, data):
        if not isinstance(data,dict):
            raise RoomError('invalid-request')
        with self.lock:
            self.expire()
            op=data.get('op')
            if op=='create':
                if len(self.rooms)>=self.max_rooms:
                    raise RoomError('service-full')
                name=self.text(data.get('name'),16)
                compatibility=self.text(data.get('compatibility'),128)
                mode,layout=data.get('mode','grand-prix'),data.get('layout','solo')
                if mode not in MODES or layout not in LAYOUTS:
                    raise RoomError('invalid-rules')
                rules=self.rules(mode,data.get('rules'))
                flow=data.get('flow','legacy')
                if flow not in ('legacy','staged','garage') or (layout=='mixed')!=(flow=='garage'):raise RoomError('invalid-rules')
                alphabet,length=ALPHABET,8
                code=''.join(secrets.choice(alphabet) for _ in range(length))
                while code in self.rooms:
                    code=''.join(secrets.choice(alphabet) for _ in range(length))
                token=secrets.token_urlsafe(32)
                room=Room(code,compatibility,token,mode,layout,rules)
                room.flow=flow
                room.members[token]=Member(token,name,0,initials=self.initials(data.get('initials','P1')))
                self.rooms[code]=room
                self.tokens[token]=code
                return dict(token=token,room=self.snapshot(room,token))
            if op=='join':
                code=self.code(data.get('code'))
                room=self.rooms.get(code)
                if not room:
                    raise RoomError('room-not-found')
                if data.get('compatibility')!=room.compatibility:
                    raise RoomError('incompatible-build')
                if room.state!='forming':
                    raise RoomError('event-started')
                if len(room.members)>=8:
                    raise RoomError('room-full')
                name=self.text(data.get('name'),16)
                seat=next(i for i in range(8) if all(m.seat!=i for m in room.members.values()))
                token=secrets.token_urlsafe(32)
                room.members[token]=Member(token,name,seat,initials=self.initials(data.get('initials',f'P{seat+1}')))
                for member in room.members.values(): member.ready=False
                self.reset_selection(room)
                self.tokens[token]=code
                room.revision+=1
                room.touched=time.monotonic()
                return dict(token=token,room=self.snapshot(room,token))
            token=data.get('token')
            if not isinstance(token,str) or token not in self.tokens:
                raise RoomError('invalid-session')
            room=self.rooms[self.tokens[token]]
            member=room.members[token]
            room.touched=time.monotonic()
            if op=='state':
                return self.reply(room,token)
            if op=='leave':
                if token==room.host:
                    self.remove(room)
                    return dict(closed=True)
                del room.members[token]
                del self.tokens[token]
                if room.layout=='mixed' and member.seat<8:
                    for m in room.members.values():
                        if m.seat==member.seat+8:m.seat=member.seat
                room.state='forming'
                room.launch=None
                room.admissions.clear()
                for m in room.members.values():
                    m.ready=False
                self.reset_selection(room)
                room.revision+=1
                return dict(left=True)
            if op!='preview' and data.get('revision')!=room.revision:
                raise RoomError('stale-room')
            if op=='refresh-code':
                if token!=room.host:raise RoomError('host-only')
                if room.state!='forming':raise RoomError('event-started')
                code=data.get('code')
                if code is None:
                    code=''.join(secrets.choice(ALPHABET) for _ in range(8))
                    while code in self.rooms:code=''.join(secrets.choice(ALPHABET) for _ in range(8))
                elif not isinstance(code,str) or len(code)!=8 or any(c not in ALPHABET for c in code):raise RoomError('invalid-code')
                if code in self.rooms:raise RoomError('room-code-in-use')
                if data.get('propose') is True:return dict(code=code)
                self.rooms.pop(room.code);room.code=code;self.rooms[code]=room
                for session in room.members:self.tokens[session]=code
            elif op=='preview' and room.flow=='garage':
                # Preview never commits a selection or changes readiness. It is
                # intentionally revision-free so simultaneous cursor motion does
                # not starve authoritative character/ready operations.
                # The request describes the screen it came from. It can arrive
                # after the last confirmation, launch, or a host Back action.
                # Return the new state for an obsolete preview; never interpret
                # a character cursor as a host course-selection operation.
                if 'character' in data:
                    hover=data.get('character')
                    if type(hover)!=int or not 0<=hover<=20:raise RoomError('invalid-character')
                    if room.state=='characters':member.hover=hover
                else:
                    if token!=room.host:raise RoomError('host-only')
                    scene,index=data.get('scene'),data.get('index')
                    if type(scene)!=int or type(index)!=int or scene not in (3,4) or not 0<=index<(5 if scene==3 else 6):raise RoomError('invalid-preview')
                    if room.state=='course':room.preview=dict(scene=scene,index=index)
                return self.reply(room,token)
            elif op=='abort':
                if token!=room.host:raise RoomError('host-only')
                if room.state!='running' or data.get('event')!=room.launch['id']:raise RoomError('no-event')
                room.last_finished=room.launch['id'];room.state='forming';room.launch=None;room.admissions.clear()
                for m in room.members.values():m.ready=False
                self.reset_selection(room)
            elif op=='finish':
                if token!=room.host:
                    raise RoomError('host-only')
                if room.flow in ('staged','garage') and room.state=='forming' and data.get('event')==room.last_finished and room.last_finished:
                    return self.reply(room,token)
                if room.state!='running':
                    raise RoomError('no-event')
                if 'results' in data:
                    ranking=data['results'];count=room.launch['human_karts']+room.launch['cpu_karts']
                    if data.get('event')!=room.launch['id'] or not isinstance(ranking,list) or len(ranking)!=count or any(type(k)!=int for k in ranking) or set(ranking)!=set(range(count)):
                        raise RoomError('invalid-results')
                    awards={kart:3-rank for rank,kart in enumerate(ranking[:3])}
                    by_seat={m.seat:m for m in room.members.values()}
                    for rider in room.launch['roster']:by_seat[rider['seat']].stars+=awards.get(rider['kart'],0)
                room.last_finished=room.launch['id']
                room.state='forming'
                room.launch=None
                room.admissions.clear()
                for m in room.members.values():
                    m.ready=False
                self.reset_selection(room)
            elif op=='kick' and room.state in ('forming','mode','characters','course'):
                if token!=room.host:raise RoomError('host-only')
                seat=data.get('seat')
                victim=next((m for m in room.members.values() if m.seat==seat),None)
                if type(seat)!=int or victim is None:raise RoomError('invalid-seat')
                if data.get('member')!=victim.uid:raise RoomError('player-changed')
                if victim.token==room.host:raise RoomError('cannot-kick-host')
                del room.members[victim.token];del self.tokens[victim.token]
                if room.layout=='mixed' and seat<8:
                    for m in room.members.values():
                        if m.seat==seat+8:m.seat=seat
                room.state='forming';self.reset_selection(room)
                for m in room.members.values():m.ready=False
            elif op=='back' and room.flow in ('staged','garage') and room.state in ('mode','characters','course'):
                if token!=room.host:raise RoomError('host-only')
                if room.state=='course':
                    room.state='characters'
                    for m in room.members.values():m.selected=False
                else:
                    room.state='forming';self.reset_selection(room)
                    for m in room.members.values():m.ready=False
            elif op in ('character','selection-done','selection-edit','selection-kart') and room.flow in ('staged','garage') and room.state=='characters':
                arrangement=self.arrangement(room);own=arrangement[member.seat]
                if op=='character':
                    chars=data.get('characters');needed=2 if own['role']=='solo' else 1
                    # Reserve each rider as it is confirmed. Empty releases a
                    # choice; a solo player may reserve the first rider while
                    # still choosing the second. Launch requires the full pair.
                    if not isinstance(chars,list) or len(chars)>needed or any(type(v)!=int or not 1<=v<=20 for v in chars) or len(set(chars))!=len(chars):raise RoomError('invalid-character')
                    for other in room.members.values():
                        if other.token!=token and set(chars)&set(other.characters):raise RoomError('character-taken')
                    if member.characters!=chars:
                        member.characters=chars.copy();member.selected=False
                        for other in room.members.values():
                            if arrangement[other.seat]['kart']==own['kart']:other.selected=False
                elif op=='selection-kart':
                    kart=data.get('kart')
                    if own['role']=='passenger':raise RoomError('driver-only')
                    if type(kart)!=int or not 0<=kart<=20:raise RoomError('invalid-kart')
                    member.choice=dict(kart=kart);member.selected=False
                elif op=='selection-edit':member.selected=False
                else:
                    if len(member.characters)!=(2 if own['role']=='solo' else 1):raise RoomError('characters-incomplete')
                    if own['role']!='passenger':
                        if member.choice is None:raise RoomError('kart-not-selected')
                        choice=self.garage(room)[own['kart']]
                        if choice['driver']==choice['partner'] or max(CHAR_WEIGHT[choice['driver']],CHAR_WEIGHT[choice['partner']])>KART_WEIGHT[choice['kart']]:raise RoomError('invalid-kart')
                    member.selected=True
                if self.complete_selection(room):room.state='course'
            elif op=='rules' and room.flow in ('staged','garage') and room.state=='course':
                if token!=room.host:raise RoomError('host-only')
                mode=data.get('mode',room.mode)
                if mode not in MODES or (mode=='grand-prix')!=(room.mode=='grand-prix'):raise RoomError('invalid-rules')
                room.rules=self.rules(mode,data.get('rules'));room.mode=mode
            elif op=='start' and room.flow in ('staged','garage') and room.state=='course':
                if token!=room.host:raise RoomError('host-only')
                if not self.complete_selection(room):raise RoomError('characters-incomplete')
                self.launch_event(room,room.endpoint)
            elif room.flow=='garage' and room.state=='mode' and op in ('rules','start'):
                if token!=room.host:raise RoomError('host-only')
                if op=='rules':
                    mode=data.get('mode')
                    if mode not in MODES:raise RoomError('invalid-rules')
                    rules=self.rules(mode,data.get('rules'))
                    if mode!='grand-prix' and len({m['kart'] for m in self.arrangement(room).values()})<2:raise RoomError('battle-needs-opponents')
                    room.mode=mode;room.rules=rules
                else:
                    if room.mode!='grand-prix' and len({m['kart'] for m in self.arrangement(room).values()})<2:raise RoomError('battle-needs-opponents')
                    room.state='characters';self.reset_selection(room)
            elif room.state!='forming':
                raise RoomError('event-started')
            elif op=='rules':
                if token!=room.host:
                    raise RoomError('host-only')
                mode,layout=data.get('mode'),data.get('layout')
                if mode not in MODES or layout not in LAYOUTS or (layout=='mixed')!=(room.flow=='garage'):
                    raise RoomError('invalid-rules')
                rules=self.rules(mode,data.get('rules'))
                room.mode,room.layout=mode,layout
                room.rules=rules
                for m in room.members.values():
                    m.ready=False
                self.reset_selection(room)
            elif op=='seat':
                seat=data.get('seat')
                if type(seat)!=int or not 0<=seat<(16 if room.layout=='mixed' else 8):
                    raise RoomError('invalid-seat')
                if any(m.seat==seat and m.token!=token for m in room.members.values()):
                    raise RoomError('seat-occupied')
                if room.layout=='mixed':
                    if seat>=8 and not any(m.seat==seat-8 and m.token!=token for m in room.members.values()):raise RoomError('driver-required')
                    if member.seat<8 and seat!=member.seat:
                        for m in room.members.values():
                            if m.seat==member.seat+8:m.seat=member.seat
                member.seat=seat
                self.reset_selection(room)
                for m in room.members.values():
                    m.ready=False
            elif op=='ready':
                if type(data.get('ready'))!=bool:
                    raise RoomError('invalid-ready')
                member.ready=data['ready']
            elif op=='profile':
                member.initials=self.initials(data.get('initials'));member.ready=False
                if room.flow=='garage':member.name=member.initials
            elif op=='kart':
                choice=data.get('choice')
                if not isinstance(choice,dict) or set(choice)!={'kart','driver','partner'} or any(type(v)!=int for v in choice.values()):
                    raise RoomError('invalid-kart')
                k,d,p=(choice[key] for key in ('kart','driver','partner'))
                if not (0<=k<21 and 1<=d<=20 and 1<=p<=20 and d!=p) or max(CHAR_WEIGHT[d],CHAR_WEIGHT[p])>KART_WEIGHT[k]:
                    raise RoomError('invalid-kart')
                arrangement=self.arrangement(room)
                group=arrangement[member.seat]['kart']
                for m in room.members.values():
                    if arrangement[m.seat]['kart']==group:m.choice=choice.copy()
                    m.ready=False
            elif op=='start':
                if token!=room.host:
                    raise RoomError('host-only')
                if len(room.members)<2 or not all(m.ready for m in room.members.values()):
                    raise RoomError('players-not-ready')
                arrangement=self.arrangement(room)
                humans=len({m['kart'] for m in arrangement.values()})
                if room.flow!='garage' and room.mode!='grand-prix' and humans<2:
                    raise RoomError('battle-needs-opponents')
                endpoint=self.local_endpoint(data.get('endpoint','127.0.0.1:28121'))
                if room.flow in ('staged','garage'):
                    self.validate_drivers(room);self.reset_selection(room);room.state='mode' if room.flow=='garage' else 'characters';room.endpoint=endpoint
                else:self.launch_event(room,endpoint)
            else:
                raise RoomError('unknown-operation')
            room.revision+=1
            return self.reply(room,token)

class Handler(BaseHTTPRequestHandler):
    def setup(self):
        super().setup()
        self.connection.settimeout(10)

    def log_message(self, *_):
        pass  # Do not log invitation codes or session tokens.

    def do_GET(self):
        # Lets the local launcher distinguish this service from another app
        # using its port, without creating a room or exposing private state.
        if self.path!='/health':
            self.send_error(404)
            return
        body=b'{"service":"mkdd-local-rooms","protocol":2,"scope":"loopback-playtest"}'
        self.send_response(200)
        self.send_header('Content-Type','application/json')
        self.send_header('Cache-Control','no-store')
        self.send_header('Content-Length',str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def do_POST(self):
        status=200
        try:
            if self.path!='/v1/room':
                raise RoomError('not-found')
            size=int(self.headers.get('Content-Length','0'))
            if not 0<size<=4096:
                raise RoomError('invalid-size')
            data=json.loads(self.rfile.read(size))
            result=self.server.rooms.request(data)
        except (ValueError,UnicodeError,RoomError,TypeError) as e:
            status=400
            result=dict(error=str(e) if isinstance(e,RoomError) else 'invalid-request')
        body=json.dumps(result,separators=(',',':')).encode()
        self.send_response(status)
        self.send_header('Content-Type','application/json')
        self.send_header('Cache-Control','no-store')
        self.send_header('Content-Length',str(len(body)))
        self.end_headers()
        self.wfile.write(body)

def server(host='127.0.0.1',port=0):
    result=ThreadingHTTPServer((host,port),Handler)
    result.rooms=Rooms()
    result.timeout=1
    return result

if __name__=='__main__':
    parser=argparse.ArgumentParser()
    parser.add_argument('--bind',default='127.0.0.1')
    parser.add_argument('--port',type=int,default=28120)
    parser.add_argument('--ready-file',default=None)
    args=parser.parse_args()
    with server(args.bind,args.port) as service:
        if args.ready_file:
            import os
            from pathlib import Path
            ready=Path(args.ready_file);temporary=ready.with_name(ready.name+'.tmp')
            temporary.write_text(json.dumps(dict(process_id=os.getpid(),port=service.server_port,protocol=2)),encoding='utf-8')
            os.replace(temporary,ready)
        print(f'Room service prototype listening on {service.server_address}',flush=True)
        service.serve_forever()
