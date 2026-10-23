"""Local development room client and native-launch bridge.

This is a developer CLI, not the requested in-game UI. All HTTP/gameplay traffic
is restricted to loopback until the Internet transport/deployment is implemented.
"""
from __future__ import annotations
import argparse
import functools
import ipaddress
import json
import os
import re
from pathlib import Path
import subprocess
import urllib.error
import urllib.parse
import urllib.request

@functools.lru_cache(maxsize=8)
def _fingerprint(path,size,modified):
    data=Path(path).read_bytes()
    identities=set(re.findall(rb'MKDD_BUILD_ID:([0-9a-f]{16})\x00',data))
    if len(identities)>1:raise ValueError('Executable has conflicting build identities')
    if identities:return next(iter(identities)).decode('ascii')
    value=0x4d34453100000001
    for byte in data:
        value=((value^byte)*1099511628211)&0xffffffffffffffff
    return f'{value:016x}'

def fingerprint(executable):
    path=Path(executable).resolve(strict=True)
    info=path.stat()
    return _fingerprint(str(path),info.st_size,info.st_mtime_ns)

class RoomClient:
    def __init__(self,url,token=None):
        parsed=urllib.parse.urlsplit(url)
        try:
            valid=parsed.scheme=='http' and ipaddress.IPv4Address(parsed.hostname).is_loopback and parsed.port
        except (ValueError,TypeError):
            valid=False
        if not valid or parsed.path!='/v1/room' or parsed.query or parsed.fragment or parsed.username or parsed.password:
            raise ValueError('The development client requires a loopback HTTP /v1/room URL')
        self.url=url
        self.token=token

    def request(self,op,**values):
        data=dict(op=op,**values)
        if self.token: data['token']=self.token
        request=urllib.request.Request(self.url,json.dumps(data).encode(),{'Content-Type':'application/json'})
        # A local room request must not be redirected or sent through a proxy.
        class NoRedirect(urllib.request.HTTPRedirectHandler):
            def redirect_request(self,*_): return None
        opener=urllib.request.build_opener(urllib.request.ProxyHandler({}),NoRedirect())
        try:
            with opener.open(request,timeout=10) as response:
                result=json.loads(response.read(65537))
        except urllib.error.HTTPError as e:
            raise ValueError(json.loads(e.read(4096)).get('error','room-request-failed')) from None
        if 'token' in result: self.token=result['token']
        return result

    def action(self,op,**values):
        room=self.request('state')['room']
        return self.request(op,revision=room['revision'],**values)

def native_environment(response,executable):
    """Translate a frozen, authorized room start into the game's boot settings."""
    room=response['room']
    launch=room.get('launch')
    connection=response.get('connection')
    if room['state']!='running' or not launch or not connection:
        raise ValueError('The host must start the ready room before launching')
    if launch['compatibility']!=fingerprint(executable):
        raise ValueError('Native executable changed since joining; rejoin with this build')
    roster=launch['roster']
    count=len(roster)
    mixed=launch['layout']=='mixed'
    if not 2<=count<=8 or [m['slot'] for m in roster]!=list(range(count)) or len({m['seat'] for m in roster})!=count or any(type(m['seat'])!=int or not 0<=m['seat']<(16 if mixed else 8) for m in roster):
        raise ValueError('Invalid frozen roster')
    occupied={m['seat'] for m in roster};expected={}
    if launch['layout']=='solo':
        expected={seat:i for i,seat in enumerate(sorted(occupied))}
    elif launch['layout']=='coop':
        kart=0
        for pair in range(4):
            front,back=(pair,pair+4) if launch.get('seating')=='drivers-first' else (pair*2,pair*2+1)
            group=sorted(occupied & {front,back})
            if not group:continue
            for seat in group:expected[seat]=2*kart+(int(seat==back) if len(group)==2 else 0)
            kart+=1
    elif mixed:
        if launch.get('seating')!='garage':raise ValueError('Invalid mixed seating')
        kart=0;pad=0;kart_map=['-']*8;role_map=['-']*8
        for front in range(8):
            back=front+8
            if back in occupied and front not in occupied:raise ValueError('Passenger has no driver')
            if front not in occupied:continue
            for seat in (front,back):
                if seat not in occupied:continue
                expected[seat]=pad;kart_map[pad]=str(kart);role_map[pad]='P' if seat==back else 'D';pad+=1
            kart+=1
        for m in roster:
            role='passenger' if m['seat']>=8 else 'driver' if m['seat']+8 in occupied else 'solo'
            if m.get('role')!=role or m.get('kart')!=int(kart_map[expected[m['seat']]]):raise ValueError('Invalid mixed rider role')
    else:raise ValueError('Invalid room layout')
    if any(type(m.get('game_seat'))!=int or m['game_seat']!=expected[m['seat']] for m in roster):
        raise ValueError('Invalid native kart seats')
    humans=kart if mixed else len({value//(2 if launch['layout']=='coop' else 1) for value in expected.values()})
    if launch['mode'] not in ('grand-prix','balloon','shine','bob-omb') or (launch['mode']!='grand-prix' and humans<2):
        raise ValueError('Invalid competition field')
    if launch.get('human_karts')!=humans or launch.get('cpu_karts')!=(8-humans if launch['mode']=='grand-prix' else 0):
        raise ValueError('Invalid CPU field')
    own=[m for m in room['members'] if m['you']]
    slot=connection['slot']
    if len(own)!=1 or type(slot)!=int or not 0<=slot<count or roster[slot]['seat']!=own[0]['seat'] or bool(room['host'])!=(slot==0):
        raise ValueError('Room membership does not match the launch slot')
    endpoint=connection['endpoint']
    host,port=endpoint.rsplit(':',1)
    if not ipaddress.IPv4Address(host).is_loopback or not port.isascii() or not port.isdecimal() or not 1<=int(port)<=65535:
        raise ValueError('Development gameplay requires a loopback endpoint')
    rules=launch['rules']
    karts=launch['karts']
    if len(karts)!=8: raise ValueError('Invalid kart roster')
    kart_bytes=bytes(value for kart in karts for value in (kart['kart'],kart['driver'],kart['partner']))
    env={k:v for k,v in os.environ.items() if not k.startswith(('MKDD_NET_','MKDD_PREDICTION_')) and k not in (
        'GCN_CLOCK','GCN_CPU_SCALE','GCN_INPUT','GCN_OWNED_STACKS','GCN_WRITE_WATCH','GCN_RENDER_TARGET_POOL','GCN_REPLAY_QUEUE','GCN_GAME_SCHEDULING')}
    env.update(GCN_GAME_SCHEDULING='1',MKDD_NET_FRESH='1',MKDD_NET_KARTS='1',MKDD_NET_PLAYERS=str(count),
               MKDD_NET_MODE=launch['mode'],MKDD_NET_LAYOUT=launch['layout'],
               MKDD_NET_SEED=str(launch['seed']),MKDD_NET_EPOCH=str(launch['epoch']),
               MKDD_NET_ROOM_ID=launch['id'],MKDD_NET_SLOT=str(slot),
               MKDD_NET_SEATS=''.join(str(m['game_seat']) for m in roster),MKDD_NET_ROSTER=kart_bytes.hex(),
               MKDD_NET_ENGINE=rules.get('engine','150cc'),MKDD_NET_CUP=rules.get('cup','mushroom'),
               MKDD_NET_ARENA=str(rules.get('arena',0x34)),MKDD_NET_ROUNDS=str(rules.get('rounds',3)),
               MKDD_NET_ADMISSION=connection['admission'])
    if connection.get('mesh') is True:
        env.update(MKDD_NET_MESH='1',MKDD_NET_CONNECT=endpoint)
    elif slot==0:
        if len(connection.get('admissions',[]))!=count: raise ValueError('Host is missing admission credentials')
        env['MKDD_NET_LISTEN']=endpoint
        env['MKDD_NET_ADMISSIONS']=''.join(connection['admissions'])
    else: env['MKDD_NET_CONNECT']=endpoint
    if mixed:env.update(MKDD_NET_KART_MAP=''.join(kart_map),MKDD_NET_ROLE_MAP=''.join(role_map))
    if 'input_delay' in connection:
        delay=connection['input_delay']
        if type(delay)!=int or not 0<=delay<=12:raise ValueError('Invalid input delay')
        env['MKDD_NET_INPUT_DELAY']=str(delay)
    if 'course' in rules:env['MKDD_NET_COURSE']=str(rules['course'])
    if all('initials' in member for member in roster):
        tags=[member['initials'] for member in roster]
        if any(not isinstance(tag,str) or not 1<=len(tag)<=7 or any(c not in 'ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789' for c in tag) for tag in tags):raise ValueError('Invalid player names')
        env['MKDD_NET_TAGS']=','.join(tags)
    if 'seating' in launch:env['MKDD_NET_SERIES']='1'
    if 'prediction' in connection:
        if type(connection['prediction'])!=bool:raise ValueError('Invalid prediction mode')
        if connection['prediction']:
            if connection.get('mesh') is not True:raise ValueError('Prediction requires direct mesh inputs')
            env.update(MKDD_NET_PREDICTION='1',MKDD_PREDICTION_START='120',GCN_OWNED_STACKS='1',GCN_WRITE_WATCH='1',
                       MKDD_PREDICTION_ASYNC_RENDER='1',MKDD_PREDICTION_ASYNC_RESTORE='1',
                       GCN_RENDER_TARGET_POOL='1',GCN_REPLAY_QUEUE='4')
    return env

def launch_native(response,executable,run_dir,*,hidden=False,test_environment=None):
    env=native_environment(response,executable)
    if test_environment: env.update(test_environment)
    run=Path(run_dir).resolve()
    run.mkdir(parents=True,exist_ok=True)
    # Keep private session credentials out of arguments, logs and saved manifests.
    with (run/'native-stdout.log').open('w') as out,(run/'native-stderr.log').open('w') as err:
        arguments=[str(Path(executable).resolve()),'--widescreen','--resolution=1920x1080','--no-input-log']
        arguments+=['--hidden','--fast','--scale=1'] if hidden else ['--fullscreen','--scale=4']
        if hidden:
            (run/'frames').mkdir(exist_ok=True)
            arguments+=['--dump-output','--dump-every=2000',f'--dump-dir={run/"frames"}']
        return subprocess.Popen(arguments,cwd=run,env=env,stdout=out,stderr=err,
                                creationflags=subprocess.CREATE_NO_WINDOW if os.name=='nt' else 0)

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--service',default='http://127.0.0.1:28120/v1/room')
    parser.add_argument('--session',type=Path,required=True,help='Private local session file; use a separate file per player')
    parser.add_argument('--exe',type=Path,default=Path(__file__).resolve().parents[1]/'build-windows/mkdd.exe')
    commands=parser.add_subparsers(dest='command',required=True)
    create=commands.add_parser('create');create.add_argument('--name',required=True)
    create.add_argument('--mode',choices=['grand-prix','balloon','shine','bob-omb'],default='grand-prix')
    create.add_argument('--layout',choices=['solo','coop'],default='solo')
    join=commands.add_parser('join');join.add_argument('--name',required=True);join.add_argument('--code',required=True)
    for op in ('state','leave','finish'): commands.add_parser(op)
    ready=commands.add_parser('ready');ready.add_argument('ready',choices=['yes','no'])
    seat=commands.add_parser('seat');seat.add_argument('seat',type=int,choices=range(8))
    rules=commands.add_parser('rules')
    rules.add_argument('--mode',choices=['grand-prix','balloon','shine','bob-omb'],required=True)
    rules.add_argument('--layout',choices=['solo','coop'],required=True)
    rules.add_argument('--engine',choices=['50cc','100cc','150cc','mirror'],default='150cc')
    rules.add_argument('--cup',choices=['mushroom','flower','star','special','all-cup'],default='mushroom')
    rules.add_argument('--arena',type=int,choices=[52,53,54,56,58,59],default=0x34)
    rules.add_argument('--rounds',type=int,choices=[1,3,5,7,9],default=3)
    start=commands.add_parser('start');start.add_argument('--endpoint',default='127.0.0.1:28121')
    launch=commands.add_parser('launch');launch.add_argument('--run-dir',type=Path,required=True)
    args=parser.parse_args()
    new=args.command in ('create','join')
    if new and args.session.exists(): parser.error('Use a new session file; an existing session would be lost')
    saved={} if new else json.loads(args.session.read_text())
    client=RoomClient(args.service if new else saved['service'],saved.get('token'))
    if new:
        values=dict(name=args.name,compatibility=fingerprint(args.exe))
        if args.command=='create': values.update(mode=args.mode,layout=args.layout)
        else: values['code']=args.code
        result=client.request(args.command,**values)
        args.session.parent.mkdir(parents=True,exist_ok=True)
        with args.session.open('x') as f: json.dump(dict(service=client.url,token=client.token),f)
    elif args.command=='launch':
        process=launch_native(client.request('state'),args.exe,args.run_dir)
        print(f'Native game launched (process {process.pid}).');return
    elif args.command=='ready': result=client.action('ready',ready=args.ready=='yes')
    elif args.command=='seat': result=client.action('seat',seat=args.seat)
    elif args.command=='rules':
        values=dict(engine=args.engine,cup=args.cup) if args.mode=='grand-prix' else dict(arena=args.arena,rounds=args.rounds)
        result=client.action('rules',mode=args.mode,layout=args.layout,rules=values)
    elif args.command=='start': result=client.action('start',endpoint=args.endpoint)
    elif args.command in ('finish',): result=client.action(args.command)
    else: result=client.request(args.command)
    # Public room data only: never print tokens or gameplay admission tickets.
    print(json.dumps(result.get('room',result),indent=2))

if __name__=='__main__':
    main()
