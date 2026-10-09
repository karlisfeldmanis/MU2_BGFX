import re,sys
# judge.py LOG HOURS -- per bot, within the first HOURS game hours: level, deaths, paid trips,
# trips paid twice within 5 s, hunt flips (a quest's hunt replaced by another's on another map).
log,hours=sys.argv[1],float(sys.argv[2])
B={}
def b(w): return B.setdefault(w,dict(level=1,deaths=0,trips=0,zen=0,double=0,flips=0,restock=0,last=None,hunt=None))
for l in open(log):
    m=re.match(r'\s+\[(\d+):(\d+):(\d+)\] (\w+) (.*)',l)
    if not m: continue
    t=int(m[1])*3600+int(m[2])*60+int(m[3])
    if t>hours*3600: continue
    w,rest=b(m[4]),m[5]
    if (k:=re.match(r'level (\d+)',rest)): w['level']=max(w['level'],int(k[1]))
    if rest.startswith('killed by'): w['deaths']+=1
    if (k:=re.match(r'pays (\d+) zen to travel to (\w+)',rest)):
        w['trips']+=1; w['zen']+=int(k[1])
        if w['last'] and t-w['last'][0]<=5 and w['last'][1]==k[2]: w['double']+=1
        w['last']=(t,k[2])
    if (k:=re.match(r'hunts for (.*) \((\w+)\)',rest)):
        if w['hunt'] and w['hunt'][0]!=k[1] and w['hunt'][1]!=k[2] and w['hunt'][2]=='active': w['flips']+=1
        w['hunt']=[k[1],k[2],'active']
    if 'quest done' in rest and w['hunt']: w['hunt'][2]='done'
    if 'potions for the hunt' in rest: w['restock']+=1
for n,w in sorted(B.items()):
    print(f"  {n:6} level {w['level']:3}  deaths {w['deaths']:3}  trips {w['trips']:3} ({w['zen']//1000}k zen)  paid twice {w['double']}  flips {w['flips']}  restocks {w['restock']}")
