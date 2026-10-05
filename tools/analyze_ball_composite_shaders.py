"""Pinned original distortion encode/composite instruction and native-source evidence."""
import argparse, hashlib, json, struct
import analyze_screen_shaders as screen
import analyze_edge_shaders as edge
import analyze_fourtap_shaders as four
need=screen.require
sha=lambda data:hashlib.sha256(data).hexdigest()
HLSL_SHA="e88e9e40cbbb86c2b61c6ba7ac251fa82cad2213c2918515394cab1b308cba6d"
PROFILES=(
 ("PSBallEncode",0x82156150,0x1F0,0x16C,0x82CF259C,"a5c1986f6ac7013e9d917b60571eb9b4acff9284defa187e11c703c98f9deda4","d3dc686fb1a999c4128482f94d102a420c053ebaee88f8f15b2ba54dd9314f77"),
 ("PSBallComposite",0x82156340,0x204,0x198,0x82CF25A8,"e596b80d58c7f9064a24bb5890109509fb22ecb681237b06691394762a95124b","8f88010b2f3d9114b3801bd0a7d18aec61385da773e89cc36e8edfbec4036d59"))
CF={"PSBallEncode":((0x504002,0x1200),(0,0xC400),(0x4006,0x2200),(0,0)),
    "PSBallComposite":((0x494002,0x1200),(0,0xC400),(0x2006,0x2200),(0,0))}
EXPR={"PSBallEncode":(
 "r1.y=v+c255.x (ADD_CONST_0, offset=1/64)",
 "r0.z=v+c254.w (offset=-1/64); r1.x=u",
 "r1.xy=float2(1,sample0(r1.xy).g)","r0.x=sample0(r0.xz).g",
 "r0.z=r1.y-r0.x; r0.y=c254.z-r0.x (SUB_CONST_0)",
 "r0.x=dot(float2(r0.z,r0.z),float2(r0.z,r0.z))+c254.y",
 "r0.y=r0.y+r1.y; r0.z=r0.z+r1.x",
 "color.xy=r0.yz*c254.x; color.z=sqrt(abs(r0.x)); color.w=1 (overlapping export masks)"),
 "PSBallComposite":(
 "r1.xyz=sample1(uv).brg", "r0.zw=r1.yz-c255.y",
 "r0.xy=mad(r0.wz,-c255.x,uv.yx)","r0.xyz=sample0(r0.yx).rgb",
 "r0.w=saturate(c255.z*r1.x) (MUL_CONST_1)","color=r0")}

def words(data,offset=0,count=3):return struct.unpack_from('>'+str(count)+'I',data,offset)
def program(code,profile):
 name,address,size,offset,handle,digest,code_hash=profile
 need(len(code)==size-offset and sha(code)==code_hash,"Distortion executable identity differs")
 cf=[]
 for i in range(2):
  a,b,c=words(code,i*12);cf.extend(((a,b&65535),((b>>16|c<<16)&0xFFFFFFFF,c>>16)))
 need(tuple(cf)==CF[name],"Distortion control flow differs")
 schedule=[]
 for lo,hi in cf:
  if hi>>12 in (1,2):
   first,count,seq=lo&4095,lo>>12&7,lo>>16&4095
   schedule.extend((first+i,bool(seq&(1<<(2*i)))) for i in range(count))
 need([slot for slot,_ in schedule]==list(range(2,len(code)//12-1)),"Distortion has unaccounted slots")
 need(len(schedule)==len(EXPR[name]),"Distortion expression count differs")
 rows=[]
 for (slot,fetch),expression in zip(schedule,EXPR[name]):
  raw=words(code,slot*12);fields=screen.decode_fetch(raw) if fetch else edge.alu(*raw)
  rows.append({"slot":slot,"words":[f"{v:08X}" for v in raw],"fields":fields,"expression":expression})
 return rows

def qualify(image,source):
 need(len(image)==screen.IMAGE_SIZE and sha(image)==screen.IMAGE_SHA256,"Original image differs")
 need(sha(source.replace('\r\n','\n').encode())==HLSL_SHA,"Reviewed distortion HLSL differs")
 need(sha(four.UCODE.read_bytes())==four.UCODE_SHA,"Declarative scalar/export reference differs")
 take=lambda address,size:image[address-screen.BASE:address-screen.BASE+size]
 profiles=[]
 for profile in PROFILES:
  name,address,size,offset,handle,digest,code_hash=profile;record=take(address,size);header=words(record,0,9)
  need(sha(record)==digest and words(take(handle,12))==(0,address,0),"Distortion record/handle differs")
  need(header[0]==0x102A1100 and header[1]+header[2]==size and header[1]+64==offset and words(record,header[6],2)==(64,size-offset),"Distortion material envelope differs")
  profiles.append({"name":name,"address":f"{address:08X}","bytes":size,"sha256":digest,"slots":program(record[offset:],profile)})
 name='VSTextured';address,size,extra,*_=screen.PROFILES[name];screen.inspect_record(take(address,size+extra),name)
 return profiles

def self_test(image,source):
 checks=0
 for profile in PROFILES:
  _,address,size,offset,*_=profile;code=image[address-screen.BASE+offset:address-screen.BASE+size]
  for i in range(len(code)):
   changed=bytearray(code);changed[i]^=1
   try:program(changed,profile)
   except ValueError:checks+=1
   else:raise ValueError('Changed executable accepted')
 for before,after in (('distortion.b*5.0','distortion.r*5.0'),('difference=up-down','difference=down-up'),('sqrt(abs(squared+squared))','sqrt(abs(squared))'),('-0.020833333954215049','0.020833333954215049')):
  need(before in source,'Mutation anchor absent')
  try:qualify(image,source.replace(before,after))
  except ValueError:checks+=1
  else:raise ValueError('Changed HLSL accepted')
 return checks
if __name__=='__main__':
 p=argparse.ArgumentParser();p.add_argument('--verify',action='store_true');p.add_argument('--self-test',action='store_true');args=p.parse_args()
 image=(screen.ROOT/'analysis/simpsons.pe').read_bytes();source=(screen.ROOT/'renderer/ball_composite.hlsl').read_text();result=qualify(image,source)
 if not args.verify:print(json.dumps(result,indent=2))
 if args.self_test:print('PASS',self_test(image,source),'distortion shader/HLSL mutation checks')
