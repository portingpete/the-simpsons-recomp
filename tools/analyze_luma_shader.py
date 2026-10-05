"""Pinned original Luma_Xenon_PS821559D8 instruction and native-source evidence.

Read-only. The CPU owner 82770AF0 normalizes and uploads three {color,gb}
layers (add c0..1, sub c2..3, lrp c4..5), resolves the scene to viewport
color texture 82DFE360 and draws a four-vertex float2 strip with VS821529C8.
--verify pins the reviewed static HLSL. Native arithmetic and sampling follow
the shared post-filter policy; no console rounding equivalence is claimed.
"""
import argparse, hashlib, json, struct
import analyze_screen_shaders as screen
import analyze_edge_shaders as edge
import analyze_fourtap_shaders as four
need=screen.require
sha=lambda data:hashlib.sha256(data).hexdigest()
HLSL_SHA="f4b2c67a7cf9728dd011c58521ccf3920d843ef94a325eaa3d91065f404b8032"
# name, address, bytes, code offset, handle global, record hash, code hash
PROFILE=("PSLuma",0x821559D8,0x388,0x1E4,0x82CF24E4,
 "e13870ceb2a28c283841eca64fa2891f35a1bc59d61ee94ae73ce7b6025a74b1",
 "d3c74ab3a1d19f5da689f98807df9c53d92200c648178f8fe1876dea6aa8d295")
CF=((0x11004,0x1200),(0,0xC400),(0x6005,0x1200),(0x600B,0x1200),(0x6011,0x1200),(0x6017,0x1200),(0x501D,0x2200),(0,0))
# c252..c255 as stored: zero; (0,-1,1/4,1); (0.11,0.3,0.59,2); (-1/2,0,0,0).
LITERALS=(0,0,0,0,0,0xBF800000,0x3E800000,0x3F800000,0x3DE147AE,0x3E99999A,0x3F170A3D,0x40000000,0xBF000000,0,0,0)
# slot: vector opcode, mask, destination, scalar opcode, mask, destination, vector clamp, export
SIGNATURES=((5,1,4,5,2,1,5,0,0),(6,1,4,6,2,8,5,0,0),(7,16,8,1,5,0,0,1,0),(8,0,1,0,3,8,0,0,0),(9,11,3,3,50,0,0,0,0),
 (10,1,2,5,5,0,0,0,0),(11,1,3,1,3,4,1,0,0),(12,1,6,0,43,4,3,0,0),(13,1,3,6,42,1,4,0,0),(14,1,3,0,42,2,4,0,0),
 (15,1,12,8,43,8,3,0,0),(16,1,12,3,42,1,8,0,0),(17,0,1,7,42,2,8,0,0),(18,1,3,0,2,4,0,0,0),(19,0,7,4,5,0,0,0,0),
 (20,1,1,1,1,2,7,0,0),(21,1,6,1,5,0,0,0,0),(22,11,13,6,50,0,0,0,0),(23,1,2,6,1,4,7,0,0),(24,0,7,0,5,0,0,0,0),
 (25,1,4,6,3,1,6,0,0),(26,1,7,4,0,8,0,0,0),(27,11,7,1,50,0,0,0,0),(28,11,7,0,50,0,0,1,0),(29,1,1,1,42,8,0,0,0),
 (30,11,7,1,50,0,0,0,0),(31,11,7,1,50,0,0,0,0),(32,11,7,0,50,0,0,0,0),(33,11,15,0,50,8,0,0,1))
# Layer notation: add (x,y)=c1.xy, sub=c3.xy, lrp=c5.xy; L=luma, ps=previous scalar.
EXPR=(
 "r2.xyz=sample0(uv).rgb",
 "r5.z=sub.x^2; r5.x=add.x^2",
 "r6.z=add.y*-1/2; r5.w=lrp.x^2",
 "r1.w=L=saturate(dot(rgb.zxy,c254.xyz)); ps=sub.y",
 "r0.x=1-L; r0.w=sub.y*-1/2",
 "r3.xy=(2L-1,2L)",
 "r5.y=t=2L*(1-L); ps=lrp.y",
 "r1.xy=(t*lrp.x,lrp.x^3); r1.z=lrp.y*-1/2",
 "r0.yz=(sub.x^3,t*sub.x); r3.z=lrp.x^4 (MUL_CONST_1)",
 "r6.xy=(add.x^3,t*add.x); r4.x=sub.x^4 (MUL_CONST_0)",
 "r0.xy=(add.y^2*-1/2,add.x^4); r4.y=sub.y^2*-1/2",
 "r8.zw=(sub.x^5,sub.y^3*-1/2); r3.w=lrp.y^2*-1/2 (MUL_CONST_1)",
 "r3.zw=(lrp.x^5,lrp.y^3*-1/2); r8.x=add.x^5",
 "r7.x=t*sub.x+sub.x^5; r8.y=add.y^3*-1/2",
 "r0.xy=(sub.x^10,add.x^10); r0.z=lrp.x^10",
 "r4.xyz=1-x^10 (sub,add,lrp); ps=t*lrp.x",
 "r1.x=p(lrp); r7.y=lrp.x^5+ps",
 "r1.yz=(p(sub),p(add)); ps=t*add.x",
 "r6.xzw=1/4-p^2 (sub,lrp,add)",
 "r6.y=q(add)*-add.y; r7.z=add.x^5+ps",
 "r0.xyz=L+r7.xyz; ps=r6.x",
 "r6.z=q(lrp)*-lrp.y; r6.x=-sub.y*ps",
 "r4.xyz=r6.xyz*r4.xyz; r0.w=2t",
 "r1.xyz=mad(r4.zxy,2t,p)",
 "r0.xyz=saturate(mad(2L-1,r1.yzx,r0.xzy)) = weights (sub,add,lrp)",
 "r1.x=add weight*add.a; r0.w=sub.a*sub weight (MUL_CONST_0)",
 "r1.xyz=mad(r1.x,add.rgb,rgb)",
 "r1.xyz=mad(-r0.w,sub.rgb,r1.xyz)",
 "r0.xyz=mad(lrp weight,lrp.rgb,-r1.xyz)",
 "color.xyz=mad(r0.xyz,lrp.a,r1.xyz); color.w=1 (overlapping export masks)")
# Original CPU owner spans: pass, layer activity test, descriptor registration,
# PS constant upload and the phase-one dispatcher's call order.
CPU=((0x82770AF0,0x4E0,"70e53ac943ef330fdae588a2fcbb4f3901bb9e90f7526d6d6f215c819bb941e2"),
     (0x82770870,0x50,"840e52b8b0ac782e33e8a16d17c2ce4a8b76d5838f0a191c2c9f64af17e1a5fb"),
     (0x82770720,0x20,"83bf1b67217c6927712e3d7b72834b0e7d94b6d08d33f1dd9f2b1574dd20a7e9"),
     (0x82444DF8,0x20,"ecd9da647cb75b90db04a29e1d461063e1971dca09510d21c2a97d6d5d443b0f"),
     (0x82751778,0x80,"0e7725be8a6bb3a253d5998bbb035fa9c68443c3d8983e3e100616181d69808c"))
# 0.0 zero/weight test, 1/127 alpha threshold, strip corner -1, one, default c0/c2/c4.
FLOATS=((0x821DD0D8,0),(0x821DD350,0x3C010204),(0x821DD110,0xBF800000),(0x82000BB0,0x3F800000),
        (0x82152E20,0),(0x82152E24,0),(0x82152E28,0),(0x82152E2C,0))

def words(data,offset=0,count=3):return struct.unpack_from('>'+str(count)+'I',data,offset)
def program(code,profile=PROFILE):
 name,address,size,offset,handle,digest,code_hash=profile
 need(len(code)==size-offset and sha(code)==code_hash,"Luma executable identity differs")
 cf=[]
 for i in range(4):
  a,b,c=words(code,i*12);cf.extend(((a,b&65535),((b>>16|c<<16)&0xFFFFFFFF,c>>16)))
 need(tuple(cf)==CF,"Luma control flow differs")
 schedule=[]
 for lo,hi in cf:
  if hi>>12 in (1,2):
   first,count,seq=lo&4095,lo>>12&7,lo>>16&4095
   need(0<count<=6 and seq>>(2*count)==0,"Luma exec extent differs")
   schedule.extend((first+i,bool(seq&(1<<(2*i))),bool(seq&(2<<(2*i)))) for i in range(count))
 need([slot for slot,_,_ in schedule]==list(range(4,len(code)//12-1)),"Luma has unaccounted slots")
 need(len(schedule)==len(EXPR) and not any(serial for _,_,serial in schedule),"Luma expression count/serialization differs")
 need(words(code,len(code)-12)==(0x4E4A0000,0xA2B5D049,0xF80C2587),"Luma trailer differs")
 rows=[]
 for (slot,fetch,_),expression in zip(schedule,EXPR):
  raw=words(code,slot*12)
  if fetch:
   need(slot==4,"Luma fetch slot differs")
   fields=screen.decode_fetch(raw)
   expected={'kind':'texture_fetch','source_register':0,'destination_register':2,'destination_swizzle':[0,1,2,7],
    'fetch_constant_index':0,'normalized_coordinates':True,'dimension_field':1,'mag_filter':3,'min_filter':3,'mip_filter':3,
    'anisotropy':7,'arbitrary_filter':0,'volume_mag_filter':3,'volume_min_filter':3,'computed_lod':True,'register_lod':False,
    'register_gradients':False,'fetch_valid_only':True,'sample_location':0,'lod_bias_field':0,'offset_fields':[0,0,0]}
   need(all(fields[k]==v for k,v in expected.items()) and fields['source_components'][:2]==[0,1],"Luma sampling profile differs")
  else:
   fields=edge.alu(*raw)
   signature=(slot,fields['vector_opcode'],fields['vector_mask'],fields['vector_destination'],fields['scalar_opcode'],
              fields['scalar_mask'],fields['scalar_destination'],int(fields['vector_clamp']),int(fields['export']))
   need(signature in SIGNATURES,"Luma ALU operation differs")
   need(not any(fields[k] for k in ('scalar_clamp','absolute_constants','vector_destination_relative','predicated','predicate_condition',
        'constant_address_register_relative','constant_0_relative','constant_1_relative')),"Luma ALU modifier differs")
   need(fields['scalar_destination_relative_or_export_zero']==(slot==33),"Luma export constant selection differs")
   for source in fields['sources']:need(not source['absolute_temporary'] and not source['relative_temporary'],"Luma operand modifier differs")
  rows.append({"slot":slot,"words":[f"{v:08X}" for v in raw],"fields":fields,"expression":expression})
 return rows

def qualify(image,source):
 need(len(image)==screen.IMAGE_SIZE and sha(image)==screen.IMAGE_SHA256,"Original image differs")
 need(sha(source.replace('\r\n','\n').encode())==HLSL_SHA,"Reviewed luma HLSL differs")
 need(sha(four.UCODE.read_bytes())==four.UCODE_SHA,"Declarative scalar/export reference differs")
 take=lambda address,size:image[address-screen.BASE:address-screen.BASE+size]
 name,address,size,offset,handle,digest,code_hash=PROFILE;record=take(address,size);header=words(record,0,9)
 need(sha(record)==digest and words(take(handle,12))==(0,address,0),"Luma record/handle differs")
 need(header[0]==0x102A1100 and header[1]+header[2]==size and header[1]+64==offset and words(record,header[6],2)==(64,size-offset),"Luma material envelope differs")
 need(words(record,header[1],16)==LITERALS,"Luma literal bank differs")
 for a,n,h in CPU:need(sha(take(a,n))==h,"Luma original CPU span differs")
 for a,value in FLOATS:need(words(take(a,4),0,1)==(value,),"Luma CPU literal differs")
 # The shared VS821529C8 record is qualified by analyze_post_filter_shaders.py.
 need(words(take(0x82CF234C,12))==(0,0x821529C8,0),"Luma vertex handle differs")
 return {"shader":{"name":name,"address":f"{address:08X}","bytes":size,"sha256":digest,"handle":f"{handle:08X}","slots":program(record[offset:])},
  "constants":{"c0":"add.color","c1.xy":"add.gb","c2":"sub.color","c3.xy":"sub.gb","c4":"lrp.color","c5.xy":"lrp.gb",
   "inactive":"82770AF0 stores zero in the layer's color register only; its gb register keeps prior contents, which the zero color cancels"},
  "strip":{"primitive":6,"count":4,"stride":8,"position":[[-1,-1],[1,-1],[-1,1],[1,1]]},
  "cpu":[{"address":f"{a:08X}","bytes":n,"sha256":h} for a,n,h in CPU]}

def self_test(image,source):
 checks=0
 name,address,size,offset,*_=PROFILE;code=image[address-screen.BASE+offset:address-screen.BASE+size]
 for i in range(len(code)):
  changed=bytearray(code);changed[i]^=1
  try:program(bytes(changed))
  except ValueError:checks+=1
  else:raise ValueError('Changed executable accepted')
 for at in [address,address+offset-1,PROFILE[4]+4]+[a for a,_,_ in CPU]+[a+n-1 for a,n,_ in CPU]+[a for a,_ in FLOATS]:
  changed=bytearray(image);changed[at-screen.BASE]^=1
  try:qualify(bytes(changed),source)
  except ValueError:checks+=1
  else:raise ValueError('Changed original evidence accepted')
 for before,after in (('source.zxy','source.xyz'),('0.30000001192092896,0.5899999737739563','0.5899999737739563,0.30000001192092896'),
                      ('gb.y*-0.5','gb.y*0.5'),('mad(-bend,bend,0.25)','mad(bend,bend,0.25)'),('postC[3].xy','postC[1].xy'),
                      ('mad(-subtractAmount','mad(subtractAmount'),('postC[4].w,subtracted','postC[4].w,added'),('return float4(mad(difference,postC[4].w,subtracted),1.0)','return float4(mad(difference,postC[4].w,subtracted),postC[4].w)')):
  need(before in source,'Mutation anchor absent')
  try:qualify(image,source.replace(before,after))
  except ValueError:checks+=1
  else:raise ValueError('Changed HLSL accepted')
 return checks
if __name__=='__main__':
 p=argparse.ArgumentParser();p.add_argument('--verify',action='store_true');p.add_argument('--self-test',action='store_true');args=p.parse_args()
 image=(screen.ROOT/'analysis/simpsons.pe').read_bytes();source=(screen.ROOT/'renderer/luma_shader.hlsl').read_text(encoding="utf-8");result=qualify(image,source)
 if not args.verify:print(json.dumps(result,indent=2))
 if args.self_test:print('PASS',self_test(image,source),'luma shader/CPU/HLSL mutation checks')
 print('PASS exact luma record, 30 issue slots, literal bank, handle, CPU spans and reviewed HLSL')
