"""Pinned offline radial sprite shader and declaration evidence."""
import hashlib,json,struct
import analyze_screen_shaders as screen
PROFILES={
 'VS':(0x821538E8,668,'e3fbc38bef4533c19bb237326733af0095dd8b4eb8455c3b182e6157da10db4b'),
 'PS':(0x82153B88,244,'cebf30e9fb47af4d4ade02ef67e22a431a4da488c89d2e158670706eeb91adf9'),
 'QUERY':(0x82153C80,340,'fed4ef38b9119751a349dae0ff3e24e2241c9f90ed1a7f1c85a8c6b7e8ccb05d')}
def inspect(image):
 screen.require(struct.unpack_from('>9I',image,0x153DE8)==(0,0x001A2086,0xA0000,4,0x002C23A5,0x50000,0xFF0000,0xFFFFFFFF,0),'Radial declaration changed')
 # SDK8245EED0 loads the declaration type. EF94..EFC0 copies low6 format
 # bits to16..21 and type8/9 to fetch12/13. num_format_all (bit13) is ZERO.
 screen.require(image[0x45EF94:0x45EFC4].hex()=='555563a6a1050000554a05aea0a500023d80bfc07eaa5378618ccfff554a20367ea830ae7fbd60383fe001567d4aeb78','Original vertex fetch type mapping changed')
 result={}
 for name,(a,n,digest) in PROFILES.items():
  record=image[a-screen.BASE:a-screen.BASE+n]
  screen.require(hashlib.sha256(record).hexdigest()==digest,'Radial '+name+' record changed')
  result[name]={'address':hex(a),'bytes':n,'sha256':digest}
 return result
if __name__=='__main__':print(json.dumps(inspect((screen.ROOT/'analysis/simpsons.pe').read_bytes())))
