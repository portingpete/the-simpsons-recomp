"""Execute the appended native ControlsMenu bytes with independent receivers.

The original AVM/CPU fixture is separate. This bounded evaluator checks generated
actions and actual package structure without launching gameplay.
"""
import struct
import unittest
from build_native_video_menu import ROOT, PACKAGES, patch_options, u
from build_native_control_menu import (patch_controls, MENU, INIT_CONTROL,
    ORIGINAL_INIT, ORIGINAL_METHODS_END, NATIVE_NAMES, ROW_FONT_SIZE, ROW_TOP, ROW_SPACING,
    MOUSE_ACCEPT_OFFSET)
from build_native_mouse_menu import patch_mouse, ui_chunks, constant_references
from test_native_video_menu import resources, NativeActions


class ControlProgram(NativeActions):
    def __init__(self,uix):
        super().__init__(uix)
        self.begin=u(self.apt,INIT_CONTROL+4)+ORIGINAL_METHODS_END-ORIGINAL_INIT
        self.functions={};self.parameters={}
        for op,arg in self.instructions(self.begin,len(self.apt)):
            if op==0x8e:self.functions[arg[0]]=arg[1:3];self.parameters[arg[0]]=arg[3]

    def evaluate(self,begin,end,scope,argument=None):
        stack=[];at=begin;steps=0
        while at<end:
            steps+=1
            if steps>20000:raise AssertionError('Control function did not terminate')
            op,arg,at=self.instruction(at)
            if op==0:break
            if op==0x8e:continue
            if op==0x96:
                for value in arg:
                    if isinstance(value,tuple):
                        if value==(4,1):value=argument
                        elif value[0]==5:value=bool(value[1])
                        else:raise AssertionError('Unsupported control constant '+repr(value))
                    stack.append(value)
            elif op==0x1c:stack.append(scope.get(stack.pop()))
            elif op==0x1d:
                value,name=stack.pop(),stack.pop();scope[name]=value
            elif op==0x4e:
                name,obj=stack.pop(),stack.pop()
                stack.append(obj[name] if isinstance(obj,(list,tuple)) else obj.get(name) if isinstance(obj,dict) else None)
            elif op==0x4f:
                value,name,obj=stack.pop(),stack.pop(),stack.pop()
                if obj is not None:obj[name]=value
            elif op in (0x0a,0x0b,0x0c):
                right,left=stack.pop(),stack.pop();stack.append(left+right if op==0x0a else left-right if op==0x0b else left*right)
            elif op==0x12:stack.append(not bool(stack.pop()))
            elif op in (0x99,0x9d):
                if op==0x99 or bool(stack.pop()):at+=arg
            elif op==0x40:
                constructor,count=stack.pop(),stack.pop()
                if constructor!='Array':raise AssertionError('Unexpected constructor')
                stack.append([stack.pop() for _ in range(count)])
            elif op==0x52:
                method,obj,count=stack.pop(),stack.pop(),stack.pop()
                args=[stack.pop() for _ in range(count)]
                stack.append(obj[method](*args))
            elif op==0x17:stack.pop()
            else:raise AssertionError('Unsupported control opcode '+hex(op))
        if stack:raise AssertionError('Control action left values on its stack')


def scope_for(program):
    exports=[];calls=[]
    scope={'_level0':{'screen':{'SetSafeString':lambda value:exports.append(value)}}}
    root={'ControlsMenu':scope,'InitialSelection':'Controls'};scope['_root']=root
    def destroy():
        calls.append('destroy')
        for name in scope['MenuItemButtons']:scope.pop(name,None)
    def initialize():
        calls.append('initialize')
        for name,source in zip(scope['MenuItemButtons'],scope['TextRefs']):
            scope[name]={'_visible':False,'_y':scope[source].get('_y'),
                         'DynamicText_mc':{'dynamicText':{'variable':'retail','text':'Dynamic Text'}}}
        # Deliberately expose sources while rebuilding wrappers: configure must
        # hide them after the shared layout/activation calls, not just before.
        for row in (*NATIVE_NAMES,'inverty','invertx','rumble'):scope['text_'+row]['_visible']=True
        # initializeButtons copies the hidden authored text into each button.
    def activate(index):
        calls.append(('activate',index))
        for name in scope['MenuItemButtons']:
            scope[name]['_visible']=True
            # A shared button frame can instantiate its text child during
            # activation. Explicit localized labels must follow that frame.
            scope[name]['DynamicText_mc']={'dynamicText':{'variable':'retail','text':'Dynamic Text'}}
        return index
    root.update(destroyActiveMenuButtons=destroy,initializeButtons=initialize,activateGizmos=lambda:calls.append('gizmos'),
                activateMenuButtons=activate,
                _screen={'SaveControlsSettings':lambda:calls.append('save')})
    for row in NATIVE_NAMES:scope['text_'+row]={'text_entry':{},'_visible':True}
    for row,label in (('inverty','Invert Y axis'),('invertx','Invert X axis'),('rumble','Vibration')):
        scope['text_'+row]={'text_entry':{'text':label},'_visible':True}
    for clip in ('invertXSelector','invertYSelector','rumbleSelector'):scope[clip]={'currentState':0,'_visible':True}
    root['gizmoMoveLeft']=lambda:scope[scope['Gizmos'][scope['currentSelection']]].update(currentState=1)
    root['gizmoMoveRight']=lambda:scope[scope['Gizmos'][scope['currentSelection']]].update(currentState=0)
    program.evaluate(program.begin,len(program.apt),scope)
    return scope,exports,calls


class NativeControlMenuTests(unittest.TestCase):
    def originals(self):
        for package in PACKAGES:
            with self.subTest(package=package):
                package_resources=resources((ROOT/'Simpsons Game, The (USA)'/package).read_bytes())
                self.shared_library=dict(ui_chunks(package_resources['SharedLibrary.swf']))[b'apti']
                yield package_resources['options.swf']

    def test_native_controls_actions_and_pages(self):
        for original in self.originals():
            program=ControlProgram(patch_mouse(patch_controls(patch_options(original))))
            scope,exports,calls=scope_for(program)
            self.assertEqual(len(scope['MenuItemButtons']),10)
            self.assertEqual(scope['MenuItemButtons'][0],'btn_ctlpage')
            self.assertEqual(scope['MenuItemButtons'][-1],'btn_ctlaccept')
            self.assertEqual(scope['GizmoTypes'],[])
            self.assertEqual(scope['nativeMousePageActive'],0)
            self.assertEqual([scope['btn_'+row]['_y'] for row in NATIVE_NAMES[:-1]],
                             [-98,-72,-46,-20,6,32,58,84,110])
            self.assertEqual(scope['text_ctlaccept']['_y'],136)
            self.assertEqual(scope['btn_ctlaccept']['_y'],136)
            for row in (*NATIVE_NAMES,'inverty','invertx','rumble'):self.assertFalse(scope['text_'+row]['_visible'])
            for row in range(10):
                scope['currentSelection']=row
                for name,direction in (('moveLeft',0),('moveRight',1)):
                    program.call(name,scope);self.assertEqual(scope['nativeAction'],100+2*row+direction)
                    self.assertEqual(calls[-1],'save')
                    program.call('getNativeAction',scope);self.assertEqual(exports[-1],100+2*row+direction)
                    self.assertEqual(scope['nativeAction'],0)
                program.call('getNativeSelection',scope);self.assertEqual(exports[-1],200+row)
            program.call('nativeFooterAccept',scope);program.call('getNativeSelection',scope)
            self.assertEqual(exports[-1],1209);self.assertEqual(scope['nativeAccept'],0)
            program.call('getNativeSelection',scope);self.assertEqual(exports[-1],209)
            program.call('nativeMousePage',scope)
            self.assertEqual(calls[-4:],['destroy','initialize','gizmos',('activate',0)])
            self.assertEqual(scope['currentSelection'],0)
            self.assertEqual(len(scope['MenuItemButtons']),9)
            self.assertEqual(scope['nativeMousePageActive'],1)
            self.assertEqual([scope['btn_'+row]['_y'] for row in NATIVE_NAMES[:5]],
                             [-98,-74,-50,-26,-2])
            self.assertEqual(scope['text_ctlaccept']['_y'],136+MOUSE_ACCEPT_OFFSET)
            self.assertEqual(scope['btn_ctlaccept']['_y'],136+MOUSE_ACCEPT_OFFSET)
            self.assertEqual(scope['MenuItemButtons'][5:8],['btn_Inverty','btn_Invertx','btn_Rumble'])
            for button,label in (('btn_Inverty','Invert Y axis'),('btn_Invertx','Invert X axis'),('btn_Rumble','Vibration')):
                self.assertEqual(scope[button]['text_str'],label)
                self.assertEqual(scope[button]['DynamicText_mc']['dynamicText']['text'],label)
                self.assertEqual(scope[button]['DynamicText_mc']['dynamicText']['variable'],'')
            for row in (*NATIVE_NAMES,'inverty','invertx','rumble'):self.assertFalse(scope['text_'+row]['_visible'])
            self.assertEqual(scope['GizmoTypes'],['']*5+['selector']*3+[''])
            self.assertEqual(scope['_root']['InitialSelection'],'Controls')
            for row,name in ((5,'invertYSelector'),(6,'invertXSelector'),(7,'rumbleSelector')):
                scope['currentSelection']=row
                self.assertEqual(scope[name]['currentState'],0)
                program.call('nativeControllerToggle',scope);self.assertEqual(scope[name]['currentState'],1)
                program.call('nativeControllerToggle',scope);self.assertEqual(scope[name]['currentState'],0)
            program.call('nativeKeyboardPage',scope)
            self.assertEqual(calls[-4:],['destroy','initialize','gizmos',('activate',0)])
            self.assertEqual(len(scope['MenuItemButtons']),10)
            self.assertEqual(scope['nativeMousePageActive'],0)
            self.assertEqual(scope['text_ctlaccept']['_y'],136)
            self.assertEqual(scope['btn_ctlaccept']['_y'],136)
            self.assertEqual(scope['currentSelection'],0)
            self.assertFalse(scope['invertXSelector']['_visible'])
            for row in NATIVE_NAMES:
                label='Native row '+row
                program.call('set_'+row,scope,label)
                self.assertEqual(scope['btn_'+row]['text_str'],label)
                self.assertEqual(scope['btn_'+row]['DynamicText_mc']['dynamicText']['text'],label)
                self.assertEqual(scope['text_'+row]['text_entry']['text'],label)

    def test_original_selectors_and_packaging_preserved(self):
        for original in self.originals():
            video=patch_options(original);before=dict(ui_chunks(video));patched=patch_controls(video);after=dict(ui_chunks(patched))
            old=before[b'apti'];apt=after[b'apti'];count=u(old,0x60C)
            self.assertEqual(u(apt,0x60C),count+20)
            self.assertEqual(apt[u(apt,INIT_CONTROL+4):u(apt,INIT_CONTROL+4)+ORIGINAL_METHODS_END-ORIGINAL_INIT],old[ORIGINAL_INIT:ORIGINAL_METHODS_END])
            changed=set(range(INIT_CONTROL+4,INIT_CONTROL+8))|set(range(0x60C,0x614))|set(range(0x1708,0x1710))
            for at in (0x3CB4,0x3DB4,0x3E74,0x3DF4,0x3E34,0x3EB4,0x3CF4,0x3D34,0x3D74,0xE50,0xEA0,0xEF0):changed.update(range(at+36,at+40))
            self.assertTrue(all(value==apt[i] or i in changed for i,value in enumerate(old)))
            frames=u(apt,MENU+12);self.assertEqual(u(apt,frames),24)
            table=u(apt,frames+4)
            for i,row in enumerate(NATIVE_NAMES):
                at=u(apt,table+4*(14+i))
                self.assertEqual(apt[u(apt,at+52):apt.index(0,u(apt,at+52))].decode(),'text_'+row)
                self.assertEqual(struct.unpack_from('>f',apt,at+36)[0],ROW_TOP+ROW_SPACING*i)
            characters=u(apt,0x610)
            for i in range(count,count+20,2):
                at=u(apt,characters+4*i)
                self.assertEqual(u(apt,at),2);self.assertEqual(struct.unpack_from('>f',apt,at+36)[0],ROW_FONT_SIZE)
                self.assertEqual(apt[u(apt,at+56)],0)
            # Actual stock placements retain their caption/selector relation,
            # with 38-unit row pitch rather than the native caption-only pitch.
            caption_y=[struct.unpack_from('>f',apt,at+36)[0] for at in (0x3DB4,0x3E74,0x3CB4)]
            selector_y=[struct.unpack_from('>f',apt,at+36)[0] for at in (0x3E34,0x3DF4,0x3EB4)]
            self.assertEqual(caption_y,[16,54,92])
            self.assertEqual(selector_y,[26,64,102])
            self.assertEqual([b-a for a,b in zip(caption_y,caption_y[1:])],[38,38])
            # Font-size budgets use the actual retail transforms, including
            # initializeButton's 1.2 vertical scale and the selector's larger
            # 36-point On/Off type. Every controller row has room for both.
            caption_height=ROW_FONT_SIZE*struct.unpack_from('>f',apt,0x3DB4+28)[0]*1.2
            selector_height=struct.unpack_from('>f',apt,0xA30+36)[0]*max(
                struct.unpack_from('>f',apt,at+28)[0] for at in (0x26DC,0x271C))
            accept_height=ROW_FONT_SIZE*struct.unpack_from('>f',apt,0x3AA4+28)[0]*1.2
            self.assertGreater(38,caption_height+selector_height)
            mouse_accept=136+MOUSE_ACCEPT_OFFSET
            self.assertGreater(mouse_accept-caption_y[-1],caption_height+selector_height+accept_height)
            # Bound the entire actual imported pulsing-button text rectangle
            # against the earliest actual Accept/Cancel footer text rectangle.
            # This also covers the spare authored whitespace around the label.
            shared=self.shared_library
            button_scale=struct.unpack_from('>f',apt,0x3AA4+28)[0]*1.2
            # Use the complete imported button text bounds, including its
            # spare authored whitespace, to keep the first page row beneath
            # the real MenuStroke shape rather than guessing from font size.
            stroke_bottom=struct.unpack_from('>f',apt,0x3C74+36)[0]+(
                struct.unpack_from('>f',apt,0x3C74+28)[0]
                *struct.unpack_from('>f',shared,0x35F34+20)[0])
            page_top=ROW_TOP+button_scale*(
                struct.unpack_from('>f',shared,0x4AEC8+36)[0]
                +struct.unpack_from('>f',shared,0x4AEC8+28)[0]
                *struct.unpack_from('>f',shared,0x35ED0+12)[0])
            self.assertGreater(page_top,stroke_bottom)
            accept_bottom=mouse_accept+button_scale*(
                struct.unpack_from('>f',shared,0x4AEC8+36)[0]
                +struct.unpack_from('>f',shared,0x4AEC8+28)[0]
                *struct.unpack_from('>f',shared,0x35ED0+20)[0])
            footer_tops=[]
            for outer,inner,text in ((0x3BB4,0x49B90,0x35C90),
                                     (0x3BB4,0x49B50,0x35C54),
                                     (0x3BF4,0x49790,0x35830),
                                     (0x3BF4,0x49750,0x357F4)):
                footer_tops.append(struct.unpack_from('>f',apt,outer+36)[0]
                    +struct.unpack_from('>f',apt,outer+28)[0]*(
                        struct.unpack_from('>f',shared,inner+36)[0]
                        +struct.unpack_from('>f',shared,inner+28)[0]
                        *struct.unpack_from('>f',shared,text+12)[0]))
            self.assertLess(accept_bottom,min(footer_tops))
            # Canonical Xbox unload traversal has a distinct writable constant
            # entry for every relocated Push; run the real canonicalizer.
            final=dict(ui_chunks(patch_mouse(patched)));refs=list(constant_references(final[b'apti'],0x5F8))
            self.assertEqual([u(final[b'apti'],at) for at in refs],list(range(len(refs))))
            self.assertEqual(u(final[b'cons'],24),len(refs))
            with self.assertRaises(ValueError):patch_controls(patched)


if __name__=='__main__':unittest.main()
