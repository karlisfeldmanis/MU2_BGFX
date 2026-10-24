import json,glob,subprocess,sys,re,collections
added={}
log=subprocess.run(['git','log','--diff-filter=A','--format=%x01%h|%ad','--date=short','--name-only','--','content/items'],capture_output=True,text=True).stdout
for b in log.split('\x01')[1:]:
    l=b.strip().split('\n'); h,d=l[0].split('|')
    for f in l[1:]:
        f=f.removeprefix('MU2_BGFX/')
        if f.endswith('.json') and not f.endswith('.rig.json'): added[f]=(h,d)
def anc(h,r): return subprocess.run(['git','merge-base','--is-ancestor',h,r]).returncode==0
cache={}
def rel(h):
    if h not in cache:
        if not anc(h,'2b237b5c'): cache[h]='next'
        elif anc(h,'2eec46a1'): cache[h]='21'
        else: cache[h]='23'
    return cache[h]
DORM={'8a1d6e55','b00f0eb9'}
CL={'knight':'DK','wizard':'DW','elf':'FE','gladiator':'MG'}
SLOT={2:'Helms',3:'Armour',4:'Pants',5:'Gloves',6:'Boots'}
items=[]
for f in sorted(glob.glob('content/items/*/*.json')):
    if f.endswith('.rig.json'): continue
    d=json.load(open(f)); st=d.get('stats')
    if not st or 'mu_name' not in d: continue
    fol=f.split('/')[2]; key=d['item']; rq=st.get('requires',{})
    if fol=='weapons':
        m=re.match(r'[A-Za-z]+',key).group(0); sub={'Sword':'Swords','Axe':'Axes','Mace':'Maces','Spear':'Spears','Bow':'Bows','Staff':'Staffs','Arrows':'Ammo'}.get(m,m)
        if m=='Bow' and 'crossbow' in d['mu_name'].lower(): sub='Crossbows'
        cat='Weapons'
    elif fol=='armor': cat='Armour'; sub=SLOT.get(st.get('slot'),'Armour')
    elif fol=='shields': cat='Shields'; sub='Shields'
    elif fol=='wings': cat='Wings'; sub='Wings'
    elif fol=='pets': cat='Pets'; sub='Pets'
    elif fol=='jewellery': cat='Jewellery'; sub='Rings' if 'Ring' in key else 'Pendants'
    else:
        cat='Other'; n=d['mu_name'].lower()
        sub='Jewels' if 'jewel' in n or 'rune of' in n else 'Potions' if 'potion' in n or 'apple' in n else 'Orbs' if 'orb' in n else 'Scrolls' if 'scroll' in n or 'book' in n else 'Other'
    h,dt=added.get(f,(None,None))
    since = 'base' if (not dt or dt<'2026-10-15') else rel(h)
    if since=='next': continue
    cls=[CL[c] for c in st.get('classes',[]) if c in CL]
    two=bool(d.get('two_handed'))
    aff=[]
    if cat in('Weapons','Armour','Shields') and sub!='Ammo': aff.append('runes')
    if cat=='Weapons' and sub in('Swords','Axes','Maces') and not two and ('DK' in cls or 'MG' in cls): aff.append('dual')
    it={'k':key,'n':d['mu_name'],'c':cat,'s':sub,'lv':rq.get('level',0),'dl':st.get('drop_level',0),
        'str':rq.get('strength',0),'agi':rq.get('agility',0),'ene':rq.get('energy',0),'cls':cls,
        'add':since,'aff':aff}
    if since!='base': it['addd']=dt
    if 'minimum_damage' in st: it['dmg']=[st['minimum_damage'],st['maximum_damage']]
    if 'magic_power' in st: it['mp']=st['magic_power']
    if 'attack_speed' in st and cat=='Weapons': it['sp']=st['attack_speed']
    if 'defense' in st: it['def']=st['defense']
    if two: it['two']=1
    if st.get('durability'): it['dur']=st['durability']
    if st.get('width'): it['sz']=[st['width'],st.get('height',1)]
    it['g']=st.get('group');it['num']=st.get('number')
    it['mob']=1 if st.get('drops_from_monsters') else 0
    if st.get('defense_rate'): it['dr']=st['defense_rate']
    if h in DORM: it['dorm']=1
    if st.get('class_level',1)>=2: it['cl2']=1
    if 'resistance' in st: it['res']=st['resistance']
    items.append(it)
SRC=json.load(open(sys.argv[1].replace('db.json','sources.json')))
for it in items:
    e=SRC.get(it['k'],{});f=''
    for key,ch in (('monsters','d'),('shops','s'),('crafts','c'),('quests','q'),('events','e'),('boxes','b')):
        if e.get(key): f+=ch
    it['src']=f
json.dump(items,open(sys.argv[1],'w'),separators=(',',':'))
print(len(items),collections.Counter(i['add'] for i in items),sum(1 for i in items if i.get('dorm')),sum(1 for i in items if 'dual' in i['aff']))
print([ (i['n'],i['addd']) for i in items if i['add']=='21'][:8])
