"""Pinned original screen-effect pixel shader and CPU owner evidence.

Read-only. Nine records drawn with VS821529C8 by the phase passes 82754288
(Dof), 82754C90 (Blur), 82755508 (Bloom, via 82755A70), 8276C930 (Fog
linear/exp/exp2) and 8276FE60 (Sat), plus Screen_Xenon PSFlat for the 82756268
letterbox bars and PSModulatedFlat for the 82755FD0 query-modulated overlay. Record and executable hashes pin every
instruction bit; tools/xenos_pixel_reference.py must be able to execute each
straight-line program, and --verify pins the reviewed static HLSL. Native
arithmetic, sampling and packing are policy, not console-precision claims.
"""
import argparse, hashlib, json, struct
import analyze_screen_shaders as screen
import analyze_fourtap_shaders as four
import xenos_pixel_reference as reference
need=screen.require
sha=lambda data:hashlib.sha256(data).hexdigest()
HLSL_SHA="2b1b37de2ea4b20ab03b029ca5d2dd8f9e9cfc407671263e8385f3627c44a575"
# name, address, bytes, code offset, handle global, record hash, code hash, trailer
PROFILES=(
 ('PSDof',0x821517A0,0x4B0,0x1C8,0x82CF22D4,'f26f543553239e78af94cc005f4e34e1380b500269081781d1bb794f3b8687dc','9807690ddd5cf5d660309dd3b017cfc25dd6492f5a8cf2f328c38b3d206a2b6f',(0x4E4A0000,0xE630E459,0x3AC8DAEC)),
 ('PSBlur',0x82151C50,0x168,0x12C,0x82CF22E4,'e1e6b1d344da7d332a6667fd10e28369d8594607feab68f19ae2509d0f13c2e5','f137d7d1690b7e078475533133afcb74b79e2ef3e38a90ec5c5554fd1ea52788',(0x4E4A0000,0x66A50C4D,0x31E4EE0E)),
 ('PSBloom',0x82151DB8,0x560,0x1F4,0x82CF22F4,'bf3799e6048b0fae1257ea4f2f5fb1ea6207472154e443ffae652355d59d3f48','df6b96f2781bdb9c10fece36ad1b6d891f16dd8b26139d4219902204ea9c9410',(0x4E4A0000,0x202C3646,0x9B4A385E)),
 ('PSFogLinear',0x82153E40,0x260,0x1D0,0x82CF2408,'979f9d0d8b230c6e5f021e6a1216171e03a453cb2dffd4d9cd23366d797708d6','7b0b1b009127e430343267eae31870f4b5c1c3efb1853a9e60ca54115b4ae57c',(0x4E4A0000,0xBE8951B7,0xEBE05432)),
 ('PSFogExp',0x821540A0,0x28C,0x1CC,0x82CF2414,'cab9f7372f09ce58ecd16dfa5f2428854d5e1871924487698f212570c131d718','295629ddc9539c7ce90f72f66aab8552921ee75433e6a460f27291175426052b',(0x4E4A0000,0x97D373B0,0xD1805672)),
 ('PSFogExp2',0x82154330,0x2A4,0x1CC,0x82CF2420,'99a44a5d6f589dcc782a15a064a48f3e9f1c80be128025f5fd4d446279a4ae3c','9297640f9e0a9956017cbde8e6f71a5dfeaf20e1cbdcef5afbbe4bc758e2ab14',(0x4E4A0000,0xAF79A9E4,0x19993FEA)),
 ('PSSat',0x821557C8,0x20C,0x1B8,0x82CF24B4,'a0e4f6a1c968213aed05eb8a0a7371ab23477de77b3f5e02a4f1040e9848b0e3','ff78f3a2b896cca6c45048b7feb7e50a129953b99451525e4a6fa2fb8be1cd38',(0x4E4A0000,0x7C3FBA10,0xF9B5F4CE)),
 ('PSFlatEffect',0x821524C8,0x120,0xFC,0x82CF2310,'bd254ae4adf48d461ab2ddc1958156ca0dafa3d2a1bc4ed67d1a9a3238ae1709','330176d22bbebed0e816527efef7a193b584fc3c5e74b3bf90eca59316efc978',(0x4E4A0000,0x2F849B43,0x07BA8A64)),
 ('PSModulatedFlat',0x82152318,0x1AC,0x164,0x82CF2304,'9621a422930978329f5bba51ddd067bb0f1e779478c4da1635694cff99fe8deb','b0b7ecf8cf20acb57466bf8526bc8e731ecbc09de5d9ba7aa34df1ac316b75f4',(0x4E4A0000,0x1EE6C917,0x2CC100A7)))
# Original owners: post depth-copy dispatcher, the five passes and Bloom's parent.
CPU=((0x82751700,0x78,'6c8f0b0d47c77a314cd4f65d46ca8703f9cc28ea0d932e4ad0faf41f622bac2e'),
     (0x82754288,0x3BC,'3c9f228fe8acc2cb007396c6874337a95474a129634a90c97601b7a12d07e3f5'),
     (0x82754C90,0x2EC,'7bc978cc201e4fed2b813460dd021bfc9781bcecbae4cf624809a155d49443dc'),
     (0x82755508,0x3A0,'067dbc6b14084f7630b643ccfca893f9fffecc2372ae11969ecd20a1661282a9'),
     (0x82755A70,0xD8,'7cdcdf1db4609cdddc8fc8ce5f4770af5bf1e2356dfa39a2a2f4dfffb999e3fe'),
     (0x8276C930,0x3A0,'258beb0e70e0ed6c0e4dec097c41bdd9994c6c889a17e7e8c6498a1d9e73baee'),
     (0x8276FE60,0x2E0,'16050d35539ba5aa3f7daf9267969d56eee4ad70a13728b9587c5f240cd0e95b'),
     (0x82756268,0x218,'8525d75158c83bfd6b345a3fdbd94b4c735711ac48a7807ab7ccb863da1da0f0'),
     (0x82755FD0,0x294,'d3cfc39930e9d77e53d435ed61e67b4a21f6c15b85eafc98faead3bedc6ec0c0'))

def words(data,offset=0,count=3):return struct.unpack_from('>'+str(count)+'I',data,offset)
def program(record_bytes,profile):
 name,address,size,offset,handle,digest,code_hash,trailer=profile
 code=record_bytes[offset:]
 need(len(record_bytes)==size and sha(record_bytes)==digest and sha(code)==code_hash,name+' record/executable identity differs')
 need(words(code,len(code)-12)==trailer,name+' trailer differs')
 # The independent reference must accept every issue of the straight-line program.
 return reference.parse(record_bytes,address)

def qualify(image,source):
 need(len(image)==screen.IMAGE_SIZE and sha(image)==screen.IMAGE_SHA256,'Original image differs')
 need(sha(source.replace('\r\n','\n').encode())==HLSL_SHA,'Reviewed screen-effect HLSL differs')
 need(sha(four.UCODE.read_bytes())==four.UCODE_SHA,'Declarative ALU/export reference differs')
 take=lambda address,size:image[address-screen.BASE:address-screen.BASE+size]
 for address,size,digest in CPU:need(sha(take(address,size))==digest,'Screen-effect CPU owner span differs')
 need(words(take(0x82CF234C,12))==(0,0x821529C8,0),'Shared VS821529C8 handle differs')
 report=[]
 for profile in PROFILES:
  name,address,size,offset,handle,digest,code_hash,trailer=profile;data=take(address,size);header=words(data,0,9)
  need(words(take(handle,12))==(0,address,0),name+' handle registration differs')
  prefix,code_bytes=words(data,header[6],2)
  need(header[0]==0x102A1100 and header[1]+header[2]==size and header[1]+prefix==offset and code_bytes==size-offset,name+' material envelope differs')
  shader=program(data,profile)
  kinds=[entry[0] for entry in shader['program']]
  report.append({'name':name,'address':f'{address:08X}','bytes':size,'sha256':digest,'handle':f'{handle:08X}',
                 'fetches':kinds.count('fetch'),'alu':kinds.count('alu'),'literal_constants':{f'c{k}':v for k,v in shader['literals'].items()}})
 return {'shaders':report,'cpu':[{'address':f'{a:08X}','bytes':n,'sha256':h} for a,n,h in CPU]}

def self_test(image,source):
 checks=0
 for profile in PROFILES:
  name,address,size,offset,*_=profile;data=image[address-screen.BASE:address-screen.BASE+size]
  for i in range(offset,size):
   changed=bytearray(data);changed[i]^=1
   try:program(bytes(changed),profile)
   except ValueError:checks+=1
   else:raise ValueError('Changed executable accepted')
 for at in [p[1] for p in PROFILES]+[p[4]+4 for p in PROFILES]+[a for a,_,_ in CPU]+[a+n-1 for a,n,_ in CPU]+[0x82CF2350]:
  changed=bytearray(image);changed[at-screen.BASE]^=1
  try:qualify(bytes(changed),source)
  except ValueError:checks+=1
  else:raise ValueError('Changed original evidence accepted')
 for before,after in (('dofLegacyProduct(postC[1].w,offset)','postC[1].w*offset'),('depth>0?saturate(abs(focus)):0.0','depth>=0?saturate(abs(focus)):0.0'),('dofTap(input.uv,radius,-tapD,tapC)','dofTap(input.uv,radius,tapD,tapC)'),
                      ('saturate(tap-postC[1].rgb)','(tap-postC[1].rgb)'),('max(modulate,postC[1].w)','min(modulate,postC[1].w)'),
                      ('mad(-near,postC[2].w,postC[2].y)','mad(near,postC[2].w,postC[2].y)'),('-density*density','density*density'),
                      ('mad(postC[1].w,8.0,postC[1].rgb)','mad(postC[1].w,4.0,postC[1].rgb)'),('history*postC[0].rgb,postC[0].w','history*postC[0].rgb,1.0'),
                      ('destinationCode==7u?1.0-source.a','destinationCode==7u?source.a'),
                      ('postC[1].xy).x;','postC[1].yx).x;'),('return visibility*postC[0];','return postC[0];')):
  need(before in source,'Mutation anchor absent')
  try:qualify(image,source.replace(before,after))
  except ValueError:checks+=1
  else:raise ValueError('Changed HLSL accepted')
 return checks
if __name__=='__main__':
 p=argparse.ArgumentParser();p.add_argument('--verify',action='store_true');p.add_argument('--self-test',action='store_true');args=p.parse_args()
 image=(screen.ROOT/'analysis/simpsons.pe').read_bytes();source=(screen.ROOT/'renderer/screen_effects.hlsl').read_text(encoding='utf-8');result=qualify(image,source)
 if not args.verify:print(json.dumps(result,indent=2))
 if args.self_test:print('PASS',self_test(image,source),'screen-effect shader/CPU/HLSL mutation checks')
 print('PASS exact screen-effect records, handles, CPU owners, reference-executable programs and reviewed HLSL')
