"""Qualify the generated Exit Game row against original retail assets."""
import itertools
import struct
import unittest
from build_native_video_menu import ROOT as PROJECT_ROOT, build, u
from build_native_main_menu import (patch_main_menu, ROOT, MENU, INIT_CONTROL,
    ORIGINAL_INIT, ORIGINAL_METHODS_END, ROW_NAMES, ROW_IDS, TEXT_NAMES,
    SOURCE_PLACEMENTS, ROW_TOP, ROW_SPACING, ACTIVATE_CONTROL, ORIGINAL_ACTIVATE, ORIGINAL_ACTIVATE_END)
from build_native_mouse_menu import patch_mouse, ui_chunks, constant_references
from test_native_video_menu import NativeActions, resources
from test_native_mouse_menu import MouseProgram, clip, typed_constants, unloaded_indices


class MainMenuProgram(NativeActions):
    def __init__(self,uix):
        chunks=dict(ui_chunks(uix));self.apt=chunks[b'apti'];cons=chunks[b'cons'];self.constants=[]
        for i in range(u(cons,24)):
            kind,value=u(cons,32+8*i),u(cons,36+8*i)
            if kind==1:value=cons[value:cons.index(0,value)].decode('ascii')
            elif kind!=7:value=(kind,value)
            self.constants.append(value)
        self.begin=u(self.apt,INIT_CONTROL+4)+ORIGINAL_METHODS_END-ORIGINAL_INIT

    def initialize(self,availability,initial=''):
        stack=[];scope={};calls=[];root={'_screen':availability};scope['_root']=root
        def initialize():
            calls.append('initialize')
            for row in scope['RemoveItems']:
                i=scope['MenuItemIds'].index(row)
                for array in ('MenuItemButtons','TextRefs','MenuItemIds'):scope[array].pop(i)
        root.update(initializeButtons=initialize,getInitialSelection=lambda:initial,
                    setInitialSelection=lambda value:calls.append(('initial',value)))
        at=self.begin
        while True:
            op,arg,at=self.instruction(at)
            if op==0:break
            if op==0x96:stack.extend(arg)
            elif op==0x1c:stack.append(scope.get(stack.pop()))
            elif op==0x1d:
                value,name=stack.pop(),stack.pop();scope[name]=value
            elif op==0x4e:
                name,obj=stack.pop(),stack.pop();stack.append(obj.get(name))
            elif op==0x40:
                constructor,count=stack.pop(),stack.pop();self.assert_array(constructor)
                stack.append([stack.pop() for _ in range(count)])
            elif op==0x52:
                method,obj,count=stack.pop(),stack.pop(),stack.pop();args=[stack.pop() for _ in range(count)]
                if isinstance(obj,list) and method=='push':obj.extend(args);value=len(obj)
                else:value=obj[method](*args)
                stack.append(value)
            elif op==0x17:stack.pop()
            elif op==0x49:
                right,left=stack.pop(),stack.pop();stack.append(left==right)
            elif op==0x12:stack.append(not bool(stack.pop()))
            elif op in (0x99,0x9d):
                if op==0x99 or bool(stack.pop()):at+=arg
            else:raise AssertionError('Unexpected main menu opcode '+hex(op))
        if stack:raise AssertionError('Main menu initializer left a value on its stack')
        return scope,calls

    @staticmethod
    def assert_array(name):
        if name!='Array':raise AssertionError('Unexpected main menu constructor')


class NativeMainMenuTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.package=PROJECT_ROOT/'Simpsons Game, The (USA)/frontend/frontend.str'
        cls.original=resources(cls.package.read_bytes())['frontend.swf']
        cls.patched=patch_main_menu(cls.original)
        cls.mouse=patch_mouse(cls.patched)

    def test_every_retail_availability_combination_keeps_exit_last(self):
        program=MainMenuProgram(self.mouse)
        for flags in itertools.product((False,True),repeat=3):
            with self.subTest(flags=flags):
                methods=('IsSavedGameAvailable','IsDebugEnabled','AreExtrasAvailable')
                availability={method:lambda value=value:value for method,value in zip(methods,flags)}
                for remembered in ('','Options'):
                    scope,calls=program.initialize(availability,remembered)
                    removed={row for row,available in zip(('Continue','Debug_replay','Extras'),flags) if not available}
                    expected=[i for i,row in enumerate(ROW_IDS) if row not in removed]
                    self.assertEqual(scope['MenuItemIds'],[ROW_IDS[i] for i in expected])
                    self.assertEqual(scope['MenuItemButtons'],['btn_'+ROW_NAMES[i] for i in expected])
                    self.assertEqual(scope['TextRefs'],['text_'+TEXT_NAMES[i] for i in expected])
                    self.assertEqual(scope['MenuItemIds'][-1],'NativeExitGame')
                    self.assertEqual(scope['currentSelection'],0)
                    self.assertEqual(calls,['initialize']+([('initial','Continue')] if not remembered else []))

    def test_authored_font_geometry_methods_and_other_resources_preserved(self):
        before=dict(ui_chunks(self.original));after=dict(ui_chunks(self.patched));old=before[b'apti'];apt=after[b'apti']
        for tag,data in before.items():
            if tag not in (b'apti',b'cons',b'alig'):self.assertEqual(after[tag],data)
        changed=set(range(INIT_CONTROL+4,INIT_CONTROL+8))|set(range(ROOT+20,ROOT+28))|set(range(ACTIVATE_CONTROL+4,ACTIVATE_CONTROL+8))
        for at in SOURCE_PLACEMENTS:changed.update(range(at+36,at+40))
        frames=u(old,MENU+12);changed.update(range(frames,frames+8))
        self.assertTrue(all(value==apt[i] or i in changed for i,value in enumerate(old)))
        stream=u(apt,INIT_CONTROL+4)
        self.assertEqual(apt[stream:stream+ORIGINAL_METHODS_END-ORIGINAL_INIT],old[ORIGINAL_INIT:ORIGINAL_METHODS_END])
        self.assertEqual(u(apt,ROOT+20),u(old,ROOT+20)+2)
        chars=u(apt,ROOT+24);text=u(apt,chars+4*u(old,ROOT+20))
        self.assertEqual(apt[text:text+52],old[0x4D0C:0x4D0C+52])
        self.assertEqual(apt[u(apt,text+52):u(apt,text+56)],b'Exit Game\0')
        self.assertEqual(apt[u(apt,text+56)],0)
        placement=u(apt,u(apt,frames+4))
        self.assertEqual(apt[placement+16:placement+36],old[0xF19C+16:0xF19C+36])
        self.assertEqual(apt[u(apt,placement+52):apt.index(0,u(apt,placement+52))],b'text_exitgame')
        self.assertEqual(u(apt,placement+8),25)
        # The original MenuStroke remains at depth 21, and all source/footer/
        # title placements retain distinct display depths after the addition.
        controls=[u(apt,u(apt,frames+4)+4*i) for i in range(u(apt,frames))]
        depths=[u(apt,at+8) for at in controls if u(apt,at)==3]
        self.assertEqual(len(depths),len(set(depths)))
        self.assertEqual(u(apt,0xF1DC+8),21)
        self.assertLess(controls.index(placement),controls.index(INIT_CONTROL))
        y=[struct.unpack_from('>f',apt,at+36)[0] for at in (*SOURCE_PLACEMENTS,placement)]
        self.assertEqual(y,[ROW_TOP+ROW_SPACING*i for i in range(8)])
        title_y=struct.unpack_from('>f',apt,0xF21C+36)[0]
        footer_y=struct.unpack_from('>f',apt,0xEF9C+36)[0]
        # Use the actual retail menu title/footer origins, with room for their
        # line extents and the existing pulsing row/font geometry.
        self.assertGreater(y[0]-title_y,60)
        self.assertGreater(footer_y-y[-1],40)

    def test_literal_caption_follows_original_button_activation(self):
        program=MainMenuProgram(self.mouse);apt=program.apt
        stream=u(apt,ACTIVATE_CONTROL+4);prefix=ORIGINAL_ACTIVATE_END-ORIGINAL_ACTIVATE
        original=dict(ui_chunks(self.original))[b'apti']
        # Constant canonicalization changes only serialized constant indices.
        before=bytearray(original[ORIGINAL_ACTIVATE:ORIGINAL_ACTIVATE_END])
        after=bytearray(apt[stream:stream+prefix])
        for slot in constant_references(apt,ROOT):
            if stream<=slot<stream+prefix:after[slot-stream:slot-stream+4]=before[slot-stream:slot-stream+4]
        self.assertEqual(after,before)
        button={'variable_text':'retail','text_str':'Dynamic Text',
                'DynamicText_mc':{'dynamicText':{'variable':'retail','text':'Dynamic Text'}}}
        NativeActions.evaluate(program,stream+prefix,len(apt),{'btn_ExitGame':button})
        self.assertEqual(button['variable_text'],'')
        self.assertEqual(button['text_str'],'Exit Game')
        self.assertEqual(button['DynamicText_mc']['dynamicText'],{'variable':'','text':'Exit Game'})

    def test_mouse_hover_click_and_constant_unload_reload_use_the_authored_row(self):
        program=MouseProgram(self.mouse);menu={'_visible':True,'MenuItemButtons':['btn_Options','btn_ExitGame'],
            'MenuItemIds':['Options','NativeExitGame'],'currentSelection':0,
            'btn_Options':clip((150,100,362,120)),'btn_ExitGame':clip((150,140,362,160))}
        root={'ActiveMenu':menu,'OkayToAcceptUserInput':True,'InitialSelection':'Options'};root['_root']=root
        root['activateMenuButtons']=lambda index:index
        self.assertEqual(program.call('nativeMouseQuery',root,('256','150','1','256')),106)
        self.assertEqual(menu['currentSelection'],1)
        self.assertEqual(program.call('nativeMouseQuery',root,('256','150','3','256')),106)
        self.assertEqual(root['InitialSelection'],'Options')
        reloaded=unloaded_indices(self.mouse)
        self.assertEqual(typed_constants(dict(ui_chunks(reloaded))[b'cons']),typed_constants(dict(ui_chunks(self.mouse))[b'cons']))
        self.assertEqual(self.mouse,reloaded)

    def test_real_frontend_package_contains_native_menu(self):
        native=resources(build(self.package))
        self.assertEqual(native['frontend.swf'],self.mouse)
        self.assertIn(b'NativeExitGame\0',native['frontend.swf'])


if __name__=='__main__':unittest.main()
