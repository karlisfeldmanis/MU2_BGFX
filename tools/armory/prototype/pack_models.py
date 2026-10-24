import json,os,sys
S=sys.argv[1]; idx=json.load(open(S+'/models/index.json'))
out=S+'/guide/project/models'; os.makedirs(out,exist_ok=True)
for f in os.listdir(out): os.remove(os.path.join(out,f))
entries=[('i',k,v) for k,v in sorted(idx['items'].items()) if v]+[('m',k,v) for k,v in sorted(idx['monsters'].items()) if v]
packs=[];cur=bytearray();table={'i':{},'m':{}};LIM=1_000_000
def flush():
    global cur
    if cur:
        name='p%02d.bin'%len(packs); open(os.path.join(out,name),'wb').write(cur); packs.append(name); cur=bytearray()
for kind,k,v in entries:
    b=open(os.path.join(S,v),'rb').read()
    if len(cur)+len(b)>LIM: flush()
    table[kind][k]=[len(packs),len(cur),len(b)]
    cur+=b
    while len(cur)%4: cur+=b'\0'
flush()
json.dump({'packs':packs,'items':table['i'],'monsters':table['m']},open(os.path.join(out,'index.json'),'w'),separators=(',',':'))
print(len(packs),'packs', sum(os.path.getsize(os.path.join(out,p)) for p in packs),'bytes', len(table['i']),'items',len(table['m']),'monsters')
