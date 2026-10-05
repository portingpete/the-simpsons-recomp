"""Independent original-byte finalizer query/publication fixture (test-only)."""
from pathlib import Path
import hashlib,importlib.util,json,struct,sys
sys.dont_write_bytecode=True
ROOT=Path(__file__).resolve().parents[1];OUT=ROOT/'build/effect-finalizer-integration'
sys.path.insert(0,str(ROOT/'tools'))
import analyze_poststart_integration as E
import analyze_post_effect_catalog as P
spec=importlib.util.spec_from_file_location('first_finalizers',ROOT/'build/effect-finalizers/verify.py')
F=importlib.util.module_from_spec(spec);spec.loader.exec_module(F)
image=(ROOT/'analysis/simpsons.pe').read_bytes();E.validate_identity(image)
pdata=E.layout(image)[1]
word=lambda a:E.word(image,a)
def name(a):
    off=a-E.BASE;return image[off:image.index(b'\0',off)].decode('ascii')
def parameter(f,query):
    for shared in (False,True):
        d=f+word(f+(0x10C if shared else 0x108));d=f+word(d) if shared else d
        n=f+word(f+(0x290 if shared else 0x288));n=f+word(n) if shared else n
        at=d+8;leaf=0
        for _ in range(word(f+(0x114 if shared else 0x110))):
            text=name(n);w0,w1=word(at),word(at+4);span=w1&65535 if w0&3 else 1
            if text==query:return ((at-d)//8<<18)|(leaf<<1)|shared
            leaf+=sum(not word(p)&3 for p in range(at,at+8*span,8));at+=8*span;n+=len(text)+1
    return 0
def technique(f,query):
    table=f+word(f+0x200)
    for i in range(word(f+0x20C)):
        t=f+word(table+4*i)
        if name(f+word(t))==query:return (i<<18)|0x3FFFC
    return 0
def publications(start):
    result=[];size=pdata[start][0]
    owners=[]
    for at in range(start,start+64,4):
        w=word(at)
        if w>>26==31 and (w>>1&1023)==444 and (w>>21&31)==3 and (w>>11&31)==3 and (w>>16&31)>=14:
            owners.append(w>>16&31)
    assert owners,hex(start)
    owner=owners[0]
    for pc,target,linked in F.edges(image,start,size):
        if target not in F.QUERY:continue
        pointer=F.constant(image,start,pc,4);assert pointer is not None
        tagged={3};found=[]
        for at in range(pc+4,start+size,4):
            w=word(at);op=w>>26;rt=w>>21&31;ra=w>>16&31;rb=w>>11&31
            edge=E.branch(at,w)
            if (edge and edge[1]) or w==0x4E800421:break
            if op==36 and rt in tagged:
                if ra==owner:found.append((at,w&65535))
                elif ra!=1:
                    base=F.constant(image,start,at,ra)
                    if base is not None:
                        immediate=w&65535;immediate-=65536 if immediate&32768 else 0
                        found.append((at,(base+immediate)&0xFFFFFFFF))
            if op==31 and (w>>1&1023)==444:
                if rt==rb and rt in tagged:tagged.add(ra)
                else:tagged.discard(ra)
            elif op in (14,15,32,33,34,35,40,41,42,43,58):tagged.discard(rt)
            elif op in (20,21,23,24,25,26,27,28,29):tagged.discard(ra)
        assert len(found)==1,(hex(start),hex(pc),name(pointer),found)
        at,field=found[0]
        result.append(dict(pc=E.hx(pc),api=F.QUERY[target],name_pointer=E.hx(pointer),name=name(pointer),store_pc=E.hx(at),field=field))
    return result
types={k:(v[0],v[3]) for k,v in F.TYPES.items()}|{k:(v[0],v[2]) for k,v in P.TYPES.items()}
rows=[];functions={};lines=['30'] #48 typed rows, all numbers hexadecimal
for i in range(49):
    at=(0x82CEFD20+16*i) if i<25 else (0x82CD1448+16*(i-25))
    f=word(at)+12;callback=word(at+12)
    if not callback:assert i==2;continue
    vt,finalizer=types[callback];assert word(vt+12)==finalizer
    queries=publications(finalizer)
    for q in queries:q['expected']=technique(f,q['name']) if q['api']=='technique' else parameter(0x820D573C if q['api']=='real_pool_parameter' else f,q['name'])
    rows.append(dict(index=i,name=name(word(at+4)),source=E.hx(f-12),vtable=E.hx(vt),finalizer=E.hx(finalizer),queries=queries))
    lines.append(f'{i:x} {vt:x} {finalizer:x} {len(queries):x}')
    for q in queries:lines.append(f"{q['field']:x} {q['expected']:x}")
    functions[finalizer]=dict(address=E.hx(finalizer),bytes=pdata[finalizer][0],sha256=E.sha(E.span(image,finalizer,pdata[finalizer][0])))
# Original shadow inline extent and three retained real-pool helpers.
for at in (0x826B7218,0x827058B0,0x827059B0,0x82705AA0):
    functions[at]=dict(address=E.hx(at),bytes=pdata[at][0],sha256=E.sha(E.span(image,at,pdata[at][0])))
assert E.span(image,0x82061428,8)==bytes([128,64,32,16,8,4,2,1])
assert parameter(0x820C055C,'kShadowBackDepthSampler')==0x148009E
assert parameter(0x820C055C,'kFirstDepthSampler')==0x14C00A0
report=dict(image_sha256=E.IMAGE_SHA,rows=rows,functions=list(functions.values()),
            shadow_replacement=dict(start='0x82706BD8',resume='0x82706CDC',bytes=0x104,
                sha256=E.sha(E.span(image,0x82706BD8,0x104)),
                private_sampler_handles=['0x0148009E','0x014C00A0'],private_slots=[277,278],
                shared_slots=dict(kShadowDepthSampler=15,kShadowCharDepthSampler=16,kShadowEdgeSampler=17,kShadowAmt=18,kIsShadowReceiver=19),
                live_registers=['r29 bit LUT','r31 typed object'],
                dead_before_read=['r27 until ABI restore','r28 overwritten82706D08','r30 overwritten82706CE4'],
                getter='827225B0 ignores input registers and loads global pool'),
            manager_queries=publications(0x826B7218))
OUT.mkdir(exist_ok=True,parents=True)
(OUT/'fixture.txt').write_text('\n'.join(lines)+'\n',encoding='utf-8')
(OUT/'evidence.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
print('PASS original finalizer fixture:',len(rows),'typed rows,',sum(len(r['queries']) for r in rows),'query publications,',len(functions),'complete functions')
