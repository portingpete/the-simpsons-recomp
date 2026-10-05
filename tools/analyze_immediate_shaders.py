"""Offline evidence for the qualified original 32-byte immediate geometry shaders."""
from pathlib import Path
import hashlib
import json
import struct
import analyze_screen_shaders as screen
import analyze_edge_shaders as edge
import analyze_rigid_shader as rigid
import analyze_polygon_offset as polygon

PROFILES={
 'VS':(0x821511D8,476,'1c1dba8b408e655abf59080e7269f82475ac017a03c47e1822f862f3d25a95e6'),
 'PS':(0x821509D8,320,'72598ead0b2bfdd687aefe4b133485032bbf30422859ac2e72da53dba18b7ef5'),
 'PSDual':(0x82150B18,0x180,'1e2b6d7a9c6f1ffd9e4662d1cb326c6baa43d6222ff26c80d0d62dd69c4b55a3')}
DECLARATION=(0,0x002A23B9,0,12,0x002C83A4,0x000A0000,16,0x001A23A6,0x00050000,0x00FF0000,0xFFFFFFFF,0)

def inspect(image):
 need=screen.require
 need(struct.unpack_from('>12I',image,0x821516F0-screen.BASE)==DECLARATION,'Immediate declaration changed')
 result={'polygon_offset':polygon.inspect(image)}
 # The six-face queue may deliberately omit its first texture bind when the
 # identity is zero. Pin the same reference used for original depth sampling:
 # invalid/unbound fetches select a null descriptor with all four channels zero.
 reference=rigid.DEPTH_REFERENCE.read_bytes()
 need(hashlib.sha256(reference).hexdigest()==rigid.DEPTH_REFERENCE_SHA,'Immediate null-fetch reference changed')
 null_descriptor=reference.split(b'D3D12_SHADER_RESOURCE_VIEW_DESC null_srv_desc;',1)[1].split(b'null_srv_desc.ViewDimension',1)[0]
 need(null_descriptor.count(b'D3D12_SHADER_COMPONENT_MAPPING_FORCE_VALUE_0')==4,'Immediate null-fetch channels differ')
 binding=reference.split(b'void D3D12TextureCache::WriteActiveTextureBindfulSRV(',1)[1].split(b'auto device = provider.GetDevice();',1)[0]
 need(b'uint32_t descriptor_index = UINT32_MAX;' in binding and
      b'if (descriptor_index != UINT32_MAX)' in binding and
      b'provider.OffsetViewDescriptor(null_srv_descriptor_heap_start_' in binding,
      'Immediate missing-fetch descriptor routing differs')
 result['null_fetch']={'reference_sha256':rigid.DEPTH_REFERENCE_SHA,'rgba':[0,0,0,0]}
 for stage,(address,size,digest) in PROFILES.items():
  record=image[address-screen.BASE:address-screen.BASE+size]
  need(hashlib.sha256(record).hexdigest()==digest,'Immediate '+stage+' record changed')
  header=screen.words(record,0,9)
  need(header[1]+header[2]==size and screen.words(record,header[6],2)==(0,header[2]),'Immediate framing changed')
  code=record[header[1]:]
  schedule,slots=screen.decode_schedule(code,3 if stage=='VS' else 2)
  rows={i:{'raw':screen.words(code,12*i),'fields':edge.alu(*screen.words(code,12*i))} for i,fetch,_ in slots if not fetch}
  if stage=='VS':
   need([i for i,_,_ in slots]==list(range(3,11)),'Immediate VS schedule changed')
   need(rigid.scalar(rows[9],'VS')=='vc[4].w*r0.y','Immediate alpha multiplication changed')
   need(all(rows[i]['fields']['vector_opcode']==11 for i in (6,7,8)),'Immediate matrix MAD changed')
  elif stage=='PS':
   need([i for i,_,_ in slots]==[2,3],'Immediate PS schedule changed')
   fetch=screen.decode_fetch(screen.words(code,24))
   need(fetch['fetch_constant_index']==0 and fetch['destination_swizzle']==[0,1,2,3] and fetch['computed_lod'],'Immediate texture sample changed')
   need(rows[3]['fields']['vector_opcode']==1 and rows[3]['fields']['vector_mask']==15,'Immediate RGBA modulation changed')
  else:
   need([i for i,_,_ in slots]==[2,3,4,5],'Immediate dual PS schedule changed')
   fetch1=screen.decode_fetch(screen.words(code,24));fetch0=screen.decode_fetch(screen.words(code,36))
   need(fetch0['fetch_constant_index']==0 and fetch1['fetch_constant_index']==1 and
        fetch0['source_register']==fetch1['source_register'] and
        fetch0['source_components'][:2]==[0,1] and fetch1['source_components'][:2]==[2,3] and
        fetch0['destination_register']==0 and fetch1['destination_register']==2 and
        fetch0['destination_swizzle']==[0,1,2,3] and fetch1['destination_swizzle']==[0,1,2,3] and
        fetch0['normalized_coordinates'] and fetch1['normalized_coordinates'] and
        fetch0['dimension_field']==1 and fetch1['dimension_field']==1 and
        fetch0['computed_lod'] and fetch1['computed_lod'],'Immediate dual texture samples changed')
   need(all(rows[i]['fields']['vector_opcode']==1 and rows[i]['fields']['vector_mask']==15 for i in (4,5)),
        'Immediate dual texture/color multiplication chain changed')
  result[stage]={'address':hex(address),'bytes':size,'sha256':digest,'schedule':schedule}
 return result

if __name__=='__main__':print(json.dumps(inspect((screen.ROOT/'analysis/simpsons.pe').read_bytes())))
