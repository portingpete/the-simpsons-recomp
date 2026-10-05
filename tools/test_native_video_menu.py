"""Check native menu generation against every original frontend resource."""
import struct, unittest
from build_native_video_menu import ROOT, PACKAGES, build, patch_options, u
from inspect_assets import stoc_entries, decode_entry, resource_chunks
from build_native_mouse_menu import MENU_NAMES, patch_mouse, constant_references

def resources(data):
    result={}
    for entry in stoc_entries(data)['entries']:
        decoded,_=decode_entry(data,entry)
        for c in resource_chunks(decoded):
            if c.get('name'):
                at=c['payload_decoded_offset'];result[c['name']]=decoded[at:at+c['payload_size']]
    return result

class NativeActions:
    """Small independent evaluator for appended menu actions, not the original AVM."""
    def __init__(self,uix):
        self.apt=uix[0x20:0x18+u(uix,0x1c)]
        at=uix.index(b'cons',0x20+len(self.apt))
        cons=uix[at+8:at+u(uix,at+4)]
        self.constants=[]
        for i in range(u(cons,24)):
            kind,value=u(cons,32+8*i),u(cons,36+8*i)
            if kind==1:value=cons[value:cons.index(0,value)].decode('ascii')
            elif kind!=7:value=(kind,value)
            self.constants.append(value)
        frames=u(self.apt,0xe0c);control=u(self.apt,u(self.apt,frames+4))
        # The existing preservation test pins this copied retail method prefix.
        self.begin=u(self.apt,control+4)+(0x7f3e-0x7da8)
        self.functions={};self.parameters={}
        for op,arg in self.instructions(self.begin,len(self.apt)):
            if op==0x8e:
                self.functions[arg[0]]=arg[1:3];self.parameters[arg[0]]=arg[3]

    def instructions(self,begin,end):
        apt=self.apt;at=begin
        while at<end:
            op=apt[at];at+=1;arg=None
            if op==0:break
            if op==0x96:
                at=(at+3)&~3;count,table=u(apt,at),u(apt,at+4);at+=8
                arg=[self.constants[u(apt,table+4*i)] for i in range(count)]
            elif op==0x8e:
                at=(at+3)&~3;name=u(apt,at);size=u(apt,at+16);params=u(apt,at+12)
                registers=[u(apt,params+8*i) for i in range(u(apt,at+4))]
                arg=(apt[name:apt.index(0,name)].decode('ascii'),at+28,at+28+size,registers)
                at+=28+size
            yield op,arg

    def evaluate(self,begin,end,scope,argument=None):
        stack=[]
        for op,arg in self.instructions(begin,end):
            if op==0x8e:continue
            if op==0x96:
                for value in arg:
                    if isinstance(value,tuple):
                        if value!=(4,1):raise AssertionError('Unsupported action constant '+repr(value))
                        value=argument
                    stack.append(value)
            elif op==0x1c:stack.append(scope[stack.pop()])
            elif op==0x1d:
                value,name=stack.pop(),stack.pop();scope[name]=value
            elif op==0x4e:
                name,obj=stack.pop(),stack.pop();stack.append(obj[name])
            elif op==0x4f:
                value,name,obj=stack.pop(),stack.pop(),stack.pop();obj[name]=value
            elif op in (0x0a,0x0c):
                right,left=stack.pop(),stack.pop();stack.append(left+right if op==0x0a else left*right)
            elif op==0x40:
                constructor,count=stack.pop(),stack.pop()
                if constructor!='Array':raise AssertionError('Unsupported constructor '+constructor)
                stack.append([stack.pop() for _ in range(count)])
            elif op==0x52:
                method,obj,count=stack.pop(),stack.pop(),stack.pop()
                stack.append(obj[method](*[stack.pop() for _ in range(count)]))
            elif op==0x17:stack.pop()
            else:raise AssertionError('Unsupported action opcode '+hex(op))
        if stack:raise AssertionError('Action left values on its stack')

    def call(self,name,scope,argument=None):
        if self.parameters[name]!=([1] if argument is not None else []):
            raise AssertionError('Unexpected parameter register binding for '+name)
        self.evaluate(*self.functions[name],scope,argument)

class NativeMenuTests(unittest.TestCase):
    def test_only_video_options_change(self):
        for package in PACKAGES:
            with self.subTest(package=package): self.check_package(package)
    def check_package(self,package):
        source=ROOT/'Simpsons Game, The (USA)'/package
        before=source.read_bytes(); original=resources(before); native=resources(build(source))
        self.assertEqual(original.keys(),native.keys())
        for name in original:
            if name not in MENU_NAMES:self.assertEqual(original[name],native[name],name)
            elif name!='options.swf':self.assertEqual(native[name],patch_mouse(original[name]),name)
        self.assertEqual(source.read_bytes(),before)
        self.assertEqual(native['options.swf'],patch_mouse(patch_options(original['options.swf'])))
        for entry in stoc_entries(build(source))['entries']:
            if entry['encoding']=='raw':
                self.assertEqual(entry['word_20'],0)
                self.assertGreaterEqual(entry['word_12'],entry['stored_size'])
        apt=native['options.swf'][0x20:]
        frames=u(apt,0xe0c)
        self.assertEqual(u(apt,frames),20) # Eight existing controls plus twelve ordinary placed rows.
        self.assertIn(b'getNativeAction\0',apt)
        rows=('resolution','windowsize','windowmode','vsync','framecap','filtering','antialiasing',
              'fov','renderscale','bloom','depthoffield','motionblur')
        for row in rows:
            self.assertIn(('text_'+row+'\0').encode(),apt)
            self.assertIn(('set_'+row+'\0').encode(),apt)
        # Existing audio, controls and credits character records stay identical.
        old=original['options.swf'][0x20:0xc4d2]
        changed=set(range(frames,frames+8))|set(range(0x3920,0x3924))|set(range(0x23c8,0x23cc))|set(range(0x5f8+20,0x5f8+28))
        # Original imported footer wrappers have no instance name. Expose the
        # existing glyph/label MovieClip while retaining every geometry byte.
        footers=((0x23cc,'BackButton'),(0x240c,'SelectButton'),
            (0x33d4,'AcceptButton'),(0x3414,'CancelButton'),
            (0x3924,'AcceptButton'),(0x3964,'CancelButton'),
            (0x3bb4,'AcceptButton'),(0x3bf4,'CancelButton'),
            (0x3f7c,'BackButton'),(0x4adc,'BackButton'))
        for at,name in footers:
            self.assertEqual(u(old,at+52),0)
            self.assertEqual(u(apt,at+4),u(old,at+4)|0x20)
            pointer=u(apt,at+52)
            self.assertEqual(apt[pointer:apt.index(0,pointer)],name.encode('ascii'))
            changed.update(range(at+4,at+8));changed.update(range(at+52,at+56))
        self.assertEqual(u(apt,0x5f8+20),116)
        character_table=u(apt,0x5f8+24)
        for i in range(92,116,2):
            text=u(apt,character_table+i*4)
            self.assertEqual(u(apt,text),2)
            self.assertEqual(apt[u(apt,text+56)],0) # No shared Brightness localization binding.
            self.assertEqual(struct.unpack_from('>f',apt,text+36)[0],20)
        control_table=u(apt,frames+4)
        for i,row in enumerate(rows,1):
            placement=u(apt,control_table+(8+i-1)*4)
            self.assertEqual(u(apt,placement+12),93+(i-1)*2)
            self.assertEqual(struct.unpack_from('>f',apt,placement+36)[0],-115+45+21*(i-1))
        self.assertEqual(struct.unpack_from('>f',apt,0xdb0+36)[0],20)
        changed.update(range(0xdb0+36,0xdb0+40))
        aa_text=u(apt,character_table+104*4)
        self.assertTrue(apt[u(apt,aa_text+52):].startswith(b'Antialiasing: Original (restart)\0'))
        options_stream=u(apt,0x23c8)
        self.assertEqual(apt[options_stream:options_stream+0xc6],old[0x74c0:0x7586])
        for at in (0x3a24,0x3a64,0x3aa4):changed.update(range(at+36,at+40))
        root_frames=u(old,0x5f8+12)
        changed.update(range(root_frames,root_frames+8))
        # The native unloader rewrites every constant-table element to its
        # traversal ordinal. These four-byte indices must be canonicalized;
        # opcode bytes, geometry and all timeline structures remain pinned.
        for slot in constant_references(apt,0x5f8):changed.update(range(slot,slot+4))
        self.assertTrue(all(x==apt[i] or i in changed for i,x in enumerate(old)))
    def test_packaged_video_actions(self):
        for package in PACKAGES:
            with self.subTest(package=package):
                source=ROOT/'Simpsons Game, The (USA)'/package
                actions=NativeActions(resources(build(source))['options.swf'])
                exports=[];saves=[]
                scope={'_root':{'_screen':{'SaveVideoSettings':lambda:saves.append(True)},
                                'initializeButtons':lambda:None},
                       '_level0':{'screen':{'SetSafeString':lambda value:exports.append(value)}}}
                actions.evaluate(actions.begin,len(actions.apt),scope)
                self.assertEqual(scope['currentSelection'],0)
                self.assertEqual(scope['nativeAction'],0)
                self.assertEqual(scope['MenuItemIds'],['Brightness','Resolution','WindowSize','WindowMode',
                                                      'VSync','FrameRate','Filtering','Antialiasing',
                                                      'FieldOfView','RenderScale','Bloom','DepthOfField','MotionBlur'])
                self.assertEqual(len(scope['MenuItemButtons']),13)
                for row in range(13):
                    for method,direction in (('moveLeft',0),('moveRight',1)):
                        with self.subTest(row=row,method=method):
                            scope['currentSelection']=row;before=len(saves)
                            actions.call(method,scope)
                            expected=100+row*2+direction # Render row specifically exports 102/103.
                            self.assertIs(type(scope['nativeAction']),int)
                            self.assertEqual(scope['nativeAction'],expected)
                            self.assertEqual(len(saves),before+1)
                            actions.call('getNativeAction',scope)
                            self.assertIs(type(exports[-1]),int)
                            self.assertEqual(exports[-1],expected)
                            self.assertEqual(scope['nativeAction'],0)
                            self.assertEqual(scope['currentSelection'],row)
                            actions.call('getNativeAction',scope)
                            self.assertEqual(exports[-1],0)
                            self.assertEqual(scope['currentSelection'],row)
                for extent in ('2560x1080','3440x1440','3840x1600','5120x1440'):
                    label='Render resolution: '+extent+' Ultrawide (restart)'
                    button={'DynamicText_mc':{'dynamicText':{'variable':'$FE_Brightness'}}}
                    text={'text_entry':{}}
                    scope['_root']['VideoMenu']={'btn_resolution':button,'text_resolution':text}
                    selection=scope['currentSelection']
                    actions.call('set_resolution',scope,label)
                    self.assertEqual(button['text_str'],label)
                    self.assertEqual(button['variable_text'],'')
                    self.assertEqual(button['DynamicText_mc']['dynamicText']['text'],label)
                    self.assertEqual(button['DynamicText_mc']['dynamicText']['variable'],'')
                    self.assertEqual(text['text_entry']['text'],label)
                    self.assertEqual(scope['currentSelection'],selection)
                for row,label in (('fov','FOV (16:9): 110 degrees'),('renderscale','Render scale: 67% (restart)'),
                                  ('bloom','Bloom: Off'),('depthoffield','Depth of field: Off'),('motionblur','Motion blur: Off')):
                    button={'DynamicText_mc':{'dynamicText':{'variable':'$FE_Brightness'}}}
                    text={'text_entry':{}}
                    scope['_root']['VideoMenu']={'btn_'+row:button,'text_'+row:text}
                    actions.call('set_'+row,scope,label)
                    self.assertEqual(button['DynamicText_mc']['dynamicText']['text'],label)
                    self.assertEqual(text['text_entry']['text'],label)
    def test_rejects_modified_asset(self):
        with self.assertRaises(ValueError):patch_options(b'unsupported UIX')

if __name__=='__main__':unittest.main()
