"""Pin SDK polygon offset setters through their original GPU upload registers."""
from pathlib import Path
import hashlib
import json
import struct
import analyze_screen_shaders as screen

REFERENCES = {
    'include/rex/graphics/register_table.inc': 'c9dfdbfe72763051736230850ee92eaf73b7e2e9be328ac8b83df7d3b5aa9e53',
    'include/rex/graphics/xenos.h': '7857b51de405a2414d1b3f4c6b86854c4471077b0596acd35c78c26295093227',
    'src/graphics/d3d12/command_processor.cpp': '1af3d15bb78c1a55092be530f7fdf871aa75fdc58918d4def033bb8209ea2701',
    'src/graphics/pipeline/shader/dxbc_translator_om.cpp': 'd8704c6bcc04da97a7250f768e65c9f0b25e8ecbce378142722c28a07a046677',
}

def inspect(image):
    need = screen.require
    def words(address, expected):
        need(struct.unpack_from('>' + str(len(expected)) + 'I', image, address-screen.BASE) == expected,
             'Original polygon offset instructions changed at ' + hex(address))
    words(0x8243AAA0, (0x9081001C,0x3D60821E,0xC00BD220,0x3D60821E,0xC1A1001C,
                       0xEC0D0032,0xC1ABD0D8,0xD0032A50,0xD0032A58))
    words(0x821DD220, (0x41800000,))
    words(0x8243AB68, (0x9081001C,0x3D60821E,0xC1832A50,0xC00BD0D8,0xFF0C0000,
                       0xC1A1001C,0xD1A32A54,0xD1A32A5C))
    uploads=(0x8244C1B0,0x8244C3E4,0x8244C674,0x8244CBAC,0x8244D150,
             0x8244D53C,0x8244DA28,0x8244DF4C,0x8245C9A0,0x82464F98,0x8246A968)
    for address in uploads:
        words(address, (0x38DF2A50,0x38A02380)) # source=device+2A50, first register=2380
    sources={}
    reference_root=Path('K:/Simpsons/RexGlueCurrent')
    for relative,digest in REFERENCES.items():
        data=(reference_root/relative).read_bytes()
        need(hashlib.sha256(data).hexdigest()==digest,'Polygon offset reference changed: '+relative)
        sources[relative]=data.decode('utf-8')
    registers=sources['include/rex/graphics/register_table.inc']
    for address,name in ((0x2380,'FRONT_SCALE'),(0x2381,'FRONT_OFFSET'),(0x2382,'BACK_SCALE'),(0x2383,'BACK_OFFSET')):
        need(f'XE_GPU_REGISTER(0x{address:04X}, kFloat, PA_SU_POLY_OFFSET_{name})' in registers,
             'Polygon offset register order changed')
    need('kPolygonOffsetScaleSubpixelUnit = 1.0f / 16.0f' in sources['include/rex/graphics/xenos.h'],
         'Polygon offset slope units changed')
    state=(screen.ROOT/'renderer/engine_state.h').read_text()
    need('SlopeBias=0xCC, DepthBias=0xD0' in state,'Native SDK polygon offset names are reversed')
    shader=(screen.ROOT/'renderer/shadow_mesh.hlsl').read_text()
    for statement in ('constantBias = asfloat(constantBiasBits);',
                      'subpixelSlope = asfloat(slopeBiasBits) * 16.0;',
                      'slopeScale = subpixelSlope * 0.0625;'):
        need(statement in shader,'Native polygon offset arithmetic changed: '+statement)
    return {'slope':{'sdk':'CC','setter':'8243AAA0','storage':['2A50','2A58'],'registers':['2380','2382']},
            'constant':{'sdk':'D0','setter':'8243AB68','storage':['2A54','2A5C'],'registers':['2381','2383']},
            'verified_uploads':len(uploads),'reference_sha256':REFERENCES}

if __name__=='__main__':
    print(json.dumps(inspect((screen.ROOT/'analysis/simpsons.pe').read_bytes())))
