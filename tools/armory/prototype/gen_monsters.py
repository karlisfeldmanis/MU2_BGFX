import json,glob,sys,os,collections
src=json.load(open(sys.argv[1]))
maps=src.get('_maps',{})
nests=collections.defaultdict(list)
for w in glob.glob('content/world/*/nests.json'):
    m=w.split('/')[2]
    if m.startswith('kanturu'): continue
    for n in json.load(open(w)).get('nests',[]):
        nests[n['breed']].append((maps.get(m,m.title()),n.get('count',1)))
out=[]
for f in sorted(glob.glob('content/monsters/*.json')):
    if f.endswith('.rig.json'): continue
    d=json.load(open(f)); c=d.get('combat') or {}
    if 'level' not in c: continue
    num=d.get('object_type'); where=collections.Counter()
    for m,cnt in nests.get(num,[]): where[m]+=cnt
    r=(d.get('rules') or {}).get('drops') or {}
    out.append({'k':d['item'],'n':d.get('mu_name',d['item']),'num':num,'lv':c['level'],'hp':c.get('health'),
      'dmg':[c.get('minimum_damage'),c.get('maximum_damage')],'def':c.get('defense'),'ar':c.get('attack_rate'),'dr':c.get('defense_rate'),
      'mv':c.get('move_range'),'rng':c.get('attack_range'),'view':c.get('view_range'),'spd':c.get('attack_delay'),
      'home':maps.get(d.get('world'),(d.get('world') or '').title()),'where':[{'map':m,'count':n} for m,n in where.most_common()],
      'maxplus':r.get('max_plus'),'money':r.get('money_rate')})
json.dump(out,open(sys.argv[2],'w'),separators=(',',':'))
names=set(m['n'] for m in out)
srcnames=set(x['name'] for k,v in src.items() if not k.startswith('_') for x in v.get('monsters',[]))
miss=[n for n in srcnames if n not in names]
print(len(out),'monsters;',len(srcnames),'names in sources; unmatched',len(miss),sorted(miss)[:25])
