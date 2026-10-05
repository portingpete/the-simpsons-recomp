"""Read-only exact-record qualification for standalone original82773D40 post filtering.

No runtime translation or original writes. --verify pins reviewed static HLSL.
Sampler and blend precision are the explicit native policy, not console parity.
"""
import argparse, hashlib, json, struct, sys
from pathlib import Path
sys.dont_write_bytecode=True
import analyze_screen_shaders as screen
import analyze_edge_shaders as edge
import analyze_fourtap_shaders as four
ROOT=Path(__file__).resolve().parents[1]
need=screen.require
sha=lambda b:hashlib.sha256(b).hexdigest()
# Reviewed current packed-blend adapter uses rounded reciprocals, matching the
# independent CPU oracle in test_post_filter_shaders.cpp and test_post_filter.cpp.
HLSL_SHA='2791d2a61fc44d8826af7adf29cf880c38e452f62f71ece81447f92e39d7425c'
# name, address, bytes, code offset, CF pairs, complete record hash, code hash, handle global
PROFILES=(
 ('VSPostAutoTextured',0x821529C8,0x1A0,0x14C,3,'614b202400472622f158bacbbdf85f7ad1e858e07f97c3e2894ac72e4add3d98','e22b241d4e601273eb433e6baed25bdd62a99b240f12e488d78ca845b94c2fa5',0x82CF234C),
 ('PSPostAlphaToRGBA',0x821583B8,0x140,0x104,2,'68e04d6e31cf1589505521c90776d4fb25d00098d2a0d925fac9b1fbd85d176f','e152a266f81dba0eb4f60ef2550552fcc18e4a2702f196722cfb7c315a0ad4cf',0x82CF261C),
 ('PSPostFiltered',0x82155F28,0x228,0x198,2,'79a5ed98ef041d70881017e11afc24fe145bf87b4880c3e62c84fcd2d79ebb18','08ab9f808943a9b44010a14b1f7f9c0901ef225de6a45ea53f7dcd2c4dd34967',0x82CF2590),
 ('PSPostGlow',0x82158118,0x2A0,0x1B0,3,'e31b962ae3b94d98b18501ab3609c439b8221b3b6082d8ebf697beacf30344cb','40fafeea8dee581042eca147ab47dd27ab7f850e6a201dfcc4aaf00579339899',0x82CF2610))
CPU=((0x82773CD0,0x70,'b9785387388366360775d455ee865aa4e4e0376bc5a8dee61a347e25c97fad7c'),
     (0x82773D40,0x4E8,'46621097addbf9c7e07e09fa56e6aa6c26bdea067ce19531605e0475bb5361a0'),
     (0x8243CB80,0x58,'8341788bc93808f3271ba9acc1b8966493d2c8bb12f5b9a2380b5a140354a466'))
CF={
 'VSPostAutoTextured':((0x10011003,0x1200),(0,0xC200),(0x1004,0x1200),(0,0xC400),(0x1005,0x2200),(0,0)),
 'PSPostAlphaToRGBA':((0x11002,0x1200),(0,0xC400),(0x1003,0x2200),(0,0)),
 'PSPostFiltered':((0x1545002,0x1200),(0,0xC400),(0x4007,0x2200),(0,0)),
 'PSPostGlow':((0x5006003,0x1200),(0x153009,0x1200),(0,0xC400),(0x600C,0x1200),(0x1012,0x2200),(0,0))}
EXPR={
 'VSPostAutoTextured':('r0=fetch95.xy01','position=r0','uv.xy=mad(r0.xy,c255.xy,c255.xx)'),
 'PSPostAlphaToRGBA':('r0.x=sample0(r0.xy).a','color=r0.xxxx'),
 'PSPostFiltered':('r2=mad(c9.yyxx,c255.yxxy,r0.yyxx)','r0=sample0(r2.zx)','r1=sample0(r2.wx)','r3=sample0(r2.zy)','r2=sample0(r2.wy)','r2=r3+r2','r1=r2+r1','r0=r1+r0','color=r0*c255.zzzz'),
 'PSPostGlow':('r1.yz=mad(c1.xx,c255.xy,r0.yx)','r0.z=mad(c1.y,c255.y,r0.y)',
 'r2.y=r0.y+c1.y; r2.x=MAXs(r0.x,r0.x)','r1.x=r0.x+c1.x; r1.w=MAXs(r0.y,r0.y)',
 'r0.w=sample0(r0.xy).r','r0.y=sample0(r0.xz).r','r0.x=sample0(r2.xy).r','r1.x=sample0(r1.xw).r','r0.z=sample0(r1.zy).r',
 'r0.z=r0.z+r1.x','r0.x=r0.z+r0.x','r0.x=r0.x+r0.y','r0.x=r0.x+r0.w','r0.x=MUL_CONST_0(c255.z,r0.x)','r0.x=min(r0.x,c255.w)','color=r0.xxxx*c0')}
LITERALS={'VSPostAutoTextured':(0x3F000000,0xBF000000,0,0),'PSPostFiltered':(0x3F000000,0xBF000000,0x3E800000,0),'PSPostGlow':(0,0xBF800000,0x3ECCCCCD,0x3F800000)}
WORDS=(0x10001,0x1000B,0x10101,0x1010B,0x10181,0x1018B,0x10701,0x1070B)
def words(b,o=0,n=3):return struct.unpack_from('>'+str(n)+'I',b,o)
def program(code,profile):
 name,a,n,off,pairs,digest,code_hash,handle=profile
 need(len(code)==n-off and sha(code)==code_hash,'Post executable identity differs')
 cf=[]
 for i in range(pairs):
  x,y,z=words(code,i*12);cf.extend(((x,y&65535),((y>>16|z<<16)&0xFFFFFFFF,z>>16)))
 need(tuple(cf)==CF[name],'Post control flow differs')
 schedule=[]
 for lo,hi in cf:
  if hi>>12 in (1,2):
   start,count,seq=lo&4095,lo>>12&7,lo>>16&4095
   need(0<count<=6 and seq>>(2*count)==0,'Post schedule count differs')
   schedule.extend((start+i,bool(seq&(1<<(i*2))),bool(seq&(2<<(i*2)))) for i in range(count))
 need([i for i,_,_ in schedule]==list(range(pairs,len(code)//12-1)),'Post unaccounted issue slots')
 need(len(schedule)==len(EXPR[name]),'Post annotation count differs')
 rows=[]
 for (i,fetch,serial),expression in zip(schedule,EXPR[name]):
  raw=words(code,i*12);f=screen.decode_fetch(raw) if fetch else edge.alu(*raw)
  if fetch and f['kind']=='texture_fetch':
   expected={'fetch_constant_index':0,'normalized_coordinates':True,'dimension_field':1,'mag_filter':3,'min_filter':3,'mip_filter':3,'anisotropy':7,'arbitrary_filter':0,'volume_mag_filter':3,'volume_min_filter':3,'computed_lod':True,'register_lod':False,'register_gradients':False,'fetch_valid_only':True,'sample_location':0,'lod_bias_field':0,'offset_fields':[0,0,0]}
   need(all(f[k]==v for k,v in expected.items()),'Post sampling profile differs')
  elif fetch:
   need(f['destination_swizzle']==[0,1,4,5] and f['fetch_constant_index']==95 and not any(f[k] for k in ('source_component','format_field','stride_dwords','offset_field')),'Post vertex fetch differs')
  else:
   need(not any(f[k] for k in ('vector_clamp','scalar_clamp','absolute_constants','vector_destination_relative','scalar_destination_relative_or_export_zero','predicated','predicate_condition','constant_address_register_relative','constant_0_relative','constant_1_relative')),'Post ALU modifier differs')
   need(f['vector_opcode'] in (0,1,2,3,11) and f['scalar_opcode'] in (5,42,50),'Post ALU opcode differs')
   for src in f['sources']:need(not any(src[k] for k in ('absolute_temporary','relative_temporary','negated')),'Post operand modifier differs')
   if f['scalar_opcode']==42:
    # UCODE scalar a=(3+swizzle[7:6])&3, b=swizzle[1:0].
    sw=raw[1]&255;temp=(42&1)|((raw[2]>>29&1)<<1)|(sw&0x3C)
    need(temp==0 and ((3+(sw>>6))&3)==2 and sw&3==0 and f['sources'][2]['register']==255,'Post MUL_CONST operands differ')
  rows.append({'slot':i,'serialize':serial,'raw':[f'{w:08X}' for w in raw],'fields':f,'expression':expression})
 return rows

def qualify(image,source):
 take=lambda a,n:image[a-screen.BASE:a-screen.BASE+n]
 need(len(image)==screen.IMAGE_SIZE and sha(image)==screen.IMAGE_SHA256,'Post original image differs')
 for path,digest in ((four.UCODE,four.UCODE_SHA),(four.XENOS,four.XENOS_SHA),(four.XENOS.parent/'registers.h','2ccf7732db706133acfec34f7d70659e3d12b0533bc51e8b95ca8137cc4d1c40')):
  need(sha(path.read_bytes())==digest,'Post declarative reference differs')
 for a,n,h in CPU:need(sha(take(a,n))==h,'Post original CPU span differs')
 need(words(take(0x821DD110,4),0,1)==(0xBF800000,) and words(take(0x82000BB0,4),0,1)==(0x3F800000,),'Post quad literals differ')
 result=[]
 for profile in PROFILES:
  name,a,n,off,pairs,digest,code_hash,handle=profile;r=take(a,n)
  need(sha(r)==digest,'Post original material differs')
  need(words(take(handle,12))==(0,a,0),'Post handle association differs')
  header=words(r,0,9);prefix=0 if name=='PSPostAlphaToRGBA' else 64
  need(header[0]==(0x102A1101 if name.startswith('VS') else 0x102A1100) and header[1]+header[2]==n and header[1]+prefix==off,'Post material envelope differs')
  need(words(r,header[6],2)==(prefix,n-off),'Post code metadata differs')
  if prefix:need(words(r,header[1],16)==(0,)*12+LITERALS[name],'Post literal bank differs')
  result.append({'name':name,'address':f'{a:08X}','bytes':n,'sha256':digest,'handle':f'{handle:08X}','instructions':program(r[off:],profile)})
 # Existing textured record is qualified independently by its established analyzer.
 p=screen.PROFILES['PSTextured'];screen.inspect_record(take(p[0],p[1]+p[2]),'PSTextured')
 need(words(take(0x82CF2334,12))==(0,p[0],0),'Post textured handle differs')
 need(sha(source.replace('\r\n','\n').encode())==HLSL_SHA,'Reviewed post HLSL differs')
 return {'shaders':result,'quad':{'primitive':8,'stride':8,'count':3,'position':[[-1,1],[1,1],[-1,-1]],'adapter':'Expand RECTLIST to four corners before native strip draw; no halftexel shader shift.'},'constants':{'native_b0_float4_count':10,'PSPostAlphaToRGBA':[],'PSPostTextured':['c0 color'],'PSPostFiltered':['c9.xy normalized scaling'],'PSPostGlow':['c0 color','c1.xy normalized radius']},'sampling':'texture0, normalized, implicit derivatives/LOD; filter/address from supplied sampler; no encoded offsets/bias','blend':{'words':[f'{w:08X}' for w in WORDS],'src11':'1-destination alpha','dst7':'1-source alpha','op4':'destination term - source term','alpha':'source alpha','native':'float source and unpacked destination, one saturate/round-to-even RGB10A2 pack; no console rounding claim'},'cpu':[{'address':f'{a:08X}','bytes':n,'sha256':h} for a,n,h in CPU]}

def self_test(image,source):
 checks=0
 def reject(fn):
  nonlocal checks
  try:fn()
  except (ValueError,struct.error):checks+=1;return
  raise ValueError('Changed post evidence accepted')
 for profile in PROFILES:
  _,a,n,off,_,_,_,handle=profile;code=image[a-screen.BASE+off:a-screen.BASE+n]
  for i in range(len(code)):
   bad=bytearray(code);bad[i]^=1;reject(lambda:program(bad,profile))
  for at in (a,a+off-1,handle+4):
   bad=bytearray(image);bad[at-screen.BASE]^=1;reject(lambda:qualify(bad,source))
 for a,n,_ in CPU:
  for at in (a,a+n-1):
   bad=bytearray(image);bad[at-screen.BASE]^=1;reject(lambda:qualify(bad,source))
 for old,new in (('sum*0.4','sum*0.2'),('postC[9]','postC[8]'),('1.0-destination.a','1.0-source.a'),('float2(0.5,-0.5)','float2(0.5,0.5)')):
  need(old in source,'Post mutation anchor missing');reject(lambda:qualify(image,source.replace(old,new)))
 return checks
if __name__=='__main__':
 args=argparse.ArgumentParser();args.add_argument('--verify',action='store_true');args.add_argument('--self-test',action='store_true');a=args.parse_args()
 image=(ROOT/'analysis/simpsons.pe').read_bytes();source=(ROOT/'renderer/post_filter_shader.hlsl').read_text(encoding="utf-8");report=qualify(image,source)
 if not a.verify:print(json.dumps(report,indent=2))
 if a.self_test:print('PASS',self_test(image,source),'post shader/CPU/HLSL mutation checks')
 print('PASS exact post shader records, 30 issue slots, CPU spans, RECTLIST, sampler and blend contracts')
