"""Execute the generated mouse methods independently of the native AVM."""
import math, struct, unittest
from unittest.mock import patch
from build_native_video_menu import ROOT, PACKAGES, patch_options, u
from build_native_mouse_menu import patch_mouse, ui_chunks, rebuild_ui, constant_references, MENU_NAMES
from test_native_video_menu import resources

class MouseProgram:
    def __init__(self, uix):
        chunks=dict(ui_chunks(uix));self.apt=chunks[b'apti'];cons=chunks[b'cons']
        self.constants=[]
        for i in range(u(cons,24)):
            kind,value=u(cons,32+8*i),u(cons,36+8*i)
            if kind==1:value=cons[value:cons.index(0,value)].decode('ascii')
            elif kind==6:value=struct.unpack('>f',struct.pack('>I',value))[0]
            self.constants.append(value)
        roots=[at for at in range(16,len(self.apt)-36,4) if u(self.apt,at)==9 and u(self.apt,at+4)==0x09876543]
        frames=u(self.apt,roots[0]+12);count=u(self.apt,frames)
        control=u(self.apt,u(self.apt,frames+4)+4*(count-1));at=u(self.apt,control+4)
        self.functions={}
        while self.apt[at]:
            assert self.apt[at]==0x9b
            header=(at+4)&~3;name=self.text(u(self.apt,header));size=u(self.apt,header+12)
            params=u(self.apt,header+8)
            names=[self.text(u(self.apt,params+4*i)) for i in range(u(self.apt,header+4))]
            self.functions[name]=(header+24,header+24+size,names);at=header+24+size

    def text(self,at):return self.apt[at:self.apt.index(0,at)].decode('ascii')
    @staticmethod
    def member(obj,name):
        if isinstance(obj,(list,tuple)):
            if name=='length':return len(obj)
            return obj[int(name)] if isinstance(name,(int,float)) and 0<=int(name)<len(obj) else None
        return obj.get(name) if isinstance(obj,dict) else None
    @staticmethod
    def number(value):
        if value is None:return math.nan
        try:return float(value)
        except (ValueError,TypeError):return math.nan
    def call(self,name,root,args=()):
        begin,end,names=self.functions[name];scope=dict(zip(names,args));stack=[];at=begin;steps=0
        def variable(name):return scope[name] if name in scope else root.get(name)
        while at<end:
            steps+=1
            if steps>20000:raise AssertionError('Mouse function did not terminate')
            op=self.apt[at];at+=1
            if op==0x96:
                at=(at+3)&~3;n=u(self.apt,at);table=u(self.apt,at+4);at+=8
                stack.extend(self.constants[u(self.apt,table+4*i)] for i in range(n))
            elif op in (0x99,0x9d):
                at=(at+3)&~3;delta=struct.unpack_from('>i',self.apt,at)[0];at+=4
                if op==0x99 or bool(stack.pop()):at+=delta
            elif op==0x1c:stack.append(variable(stack.pop()))
            elif op==0x3c:
                value,name=stack.pop(),stack.pop();scope[name]=value
            elif op==0x1d:
                value,name=stack.pop(),stack.pop()
                (scope if name in scope else root)[name]=value
            elif op==0x4e:
                name,obj=stack.pop(),stack.pop();stack.append(self.member(obj,name))
            elif op==0x4f:
                value,name,obj=stack.pop(),stack.pop(),stack.pop();obj[name]=value
            elif op==0x12:stack.append(not bool(stack.pop()))
            elif op==0x4a:stack.append(self.number(stack.pop()))
            elif op==0x50:stack.append(self.number(stack.pop())+1)
            elif op in (0x48,0x49,0x0b,0x47):
                right,left=stack.pop(),stack.pop()
                if op==0x48:value=self.number(left)<self.number(right)
                elif op==0x49:value=(left is right) if isinstance(left,(dict,list)) or isinstance(right,(dict,list)) else left==right
                elif op==0x0b:value=self.number(left)-self.number(right)
                elif isinstance(left,str) or isinstance(right,str):value=str(left)+str(int(right) if isinstance(right,float) and right.is_integer() else right)
                else:value=left+right
                stack.append(value)
            elif op==0x52:
                method,obj,count=stack.pop(),stack.pop(),int(stack.pop());args=[stack.pop() for _ in range(count)]
                if obj is root and method in self.functions:value=self.call(method,root,args)
                else:
                    function=self.member(obj,method);value=function(*args) if callable(function) else None
                stack.append(value)
            elif op==0x17:stack.pop()
            elif op==0x3e:return stack.pop()
            else:raise AssertionError('Unsupported mouse opcode '+hex(op))
        raise AssertionError('Mouse function has no return')

def clip(rect,visible=True):
    return {'_visible':visible,'hitTest':lambda x,y,shape:rect[0]<=float(x)<=rect[2] and rect[1]<=float(y)<=rect[3]}

def movie_root(apt):
    roots=[at for at in range(16,len(apt)-36,4) if u(apt,at)==9 and u(apt,at+4)==0x09876543]
    if len(roots)!=1:raise AssertionError('Expected one serialized Apt root')
    return roots[0]

def typed_constants(cons):
    values=[];table=u(cons,28)
    for i in range(u(cons,24)):
        kind,value=u(cons,table+8*i),u(cons,table+8*i+4)
        if kind==1:value=cons[value:cons.index(0,value)]
        values.append((kind,value))
    return values

def unloaded_indices(uix):
    # The original 827D5BD4 writes the running traversal counter into every
    # encoded constant-table slot, regardless of its first-load serialized
    # index. Other pointer relocations leave this serialized graph unchanged.
    chunks=ui_chunks(uix);apt=bytearray(dict(chunks)[b'apti'])
    for ordinal,slot in enumerate(constant_references(apt,movie_root(apt))):
        struct.pack_into('>I',apt,slot,ordinal)
    return rebuild_ui(uix,[(tag,bytes(apt) if tag==b'apti' else data) for tag,data in chunks])

class NativeMouseActionsTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        originals=resources((ROOT/'Simpsons Game, The (USA)'/PACKAGES[0]).read_bytes())
        cls.program=MouseProgram(patch_mouse(patch_options(originals['options.swf'])))
        cls.popup_program=MouseProgram(patch_mouse(originals['popup.swf']))
        cls.title_program=MouseProgram(patch_mouse(originals['simpsons_start.swf']))
        cls.save_program=MouseProgram(patch_mouse(originals['savegame.swf']))
        cls.language_program=MouseProgram(patch_mouse(originals['language.swf']))
    def ordinary(self,video=False,gizmos=0):
        menu={'_visible':True,'MenuItemButtons':['row0','row1'],'MenuItemIds':['Resume','Options'],
              'currentSelection':0,'GizmoTypes':['slider']*gizmos,
              'row0':clip((150,100,362,120)),'row1':clip((150,130,362,150))}
        root={'ActiveMenu':menu,'OkayToAcceptUserInput':True,'InitialSelection':'Resume'};root['_root']=root
        if video:
            root['VideoMenu']=menu
            menu['TextRefs']=['text0','text1']
            menu['text0']=menu['row0'];menu['text1']=menu['row1']
        changes=[]
        def activate(index):
            self.assertIsInstance(index,(int,float))
            self.assertEqual(root['InitialSelection'],'')
            changes.append(index);return index
        root['activateMenuButtons']=activate
        return root,menu,changes
    def query(self,root,x,y,update=1,program=None):
        return (program or self.program).call('nativeMouseQuery',root,(str(x),str(y),str(update),'256'))
    def test_hover_preserves_original_animation_and_click_event(self):
        root,menu,changes=self.ordinary()
        self.assertEqual(self.query(root,250,140),106)
        self.assertEqual(menu['currentSelection'],1);self.assertEqual(changes,[1])
        self.assertEqual(root['InitialSelection'],'Resume')
        self.assertEqual(self.query(root,250,140),106);self.assertEqual(changes,[1])
        self.assertEqual(self.query(root,450,200),1);self.assertEqual(menu['currentSelection'],1)
    def test_hidden_blank_and_blocked_rows_do_not_activate(self):
        root,menu,changes=self.ordinary()
        menu['row1']['_visible']=False
        self.assertEqual(self.query(root,250,140),1)
        menu['row1']['_visible']=True;root['OkayToAcceptUserInput']=False
        self.assertEqual(self.query(root,250,140),1)
        root['OkayToAcceptUserInput']=True
        self.assertEqual(self.query(root,250,140,0),1);self.assertEqual(changes,[])
        menu['_visible']=False;self.assertEqual(self.query(root,250,140),0)
        root['ActiveMenu']=None;self.assertEqual(self.query(root,250,140),0)
    def test_native_video_and_original_gizmos_change_in_both_directions(self):
        for video,gizmos in ((True,0),(False,2)):
            root,menu,changes=self.ordinary(video,gizmos)
            self.assertEqual(self.query(root,200,140),102)
            self.assertEqual(self.query(root,300,140),103)
            self.assertEqual(menu['currentSelection'],1)
    def test_wheel_only_keeps_list_navigation_but_targets_setting_row(self):
        root,menu,changes=self.ordinary()
        # Simulate consecutive original DOWN events advancing selection while
        # the physical pointer remains over the first row.
        menu['MenuItemButtons'].append('row2');menu['row2']=clip((150,160,362,180))
        for selection in (1,2):
            menu['currentSelection']=selection
            self.assertEqual(self.query(root,250,110,2),106)
            self.assertEqual(menu['currentSelection'],selection)
        rollovers=[];menu['SelectButton']=clip((400,400,470,420))
        menu['SelectButton']['rollOver']=lambda:rollovers.append('select')
        self.assertEqual(self.query(root,435,410,2),1)
        self.assertEqual(rollovers,[]);self.assertEqual(changes,[])
        for video,gizmos in ((True,0),(False,2)):
            root,menu,changes=self.ordinary(video,gizmos)
            self.assertEqual(self.query(root,300,140,2),103)
            self.assertEqual(menu['currentSelection'],1);self.assertEqual(changes,[1])
    def test_video_overlap_uses_visible_button_display_depth(self):
        root,menu,changes=self.ordinary(video=True)
        # Retail text bounds are [-2,-2,255.2,59.85], even after the font is
        # reduced to 20 and copied into pulsingButton. The native rows are 21
        # stage units apart; increasing display depth determines overlap.
        origins=[100,145,166,187,208,229,250]
        menu['MenuItemButtons']=['row'+str(i) for i in range(len(origins))]
        menu['TextRefs']=['text'+str(i) for i in range(len(origins))]
        for i,y in enumerate(origins):
            menu['text'+str(i)]=clip((148,y-2,405.2,y+59.85),visible=False)
            menu['row'+str(i)]=clip((148,y-2,405.2,y+59.85))
        for y,expected in ((146.84,1),(166.76,2),(249.51,6)):
            self.assertEqual(self.query(root,280,y),103)
            self.assertEqual(menu['currentSelection'],expected)
        self.assertEqual(changes,[1,2,6])
        self.assertEqual(self.query(root,100,166.76),1)
        self.assertEqual(self.query(root,280,350),1)
        # The ordinary menu preserves its original traversal on overlap.
        root,menu,changes=self.ordinary()
        menu['row0']=clip((148,100,405.2,190))
        menu['row1']=clip((148,130,405.2,220))
        self.assertEqual(self.query(root,280,140),106)
        self.assertEqual(menu['currentSelection'],0);self.assertEqual(changes,[])
    def test_screen_and_menu_footers_dispatch_existing_events(self):
        root,menu,changes=self.ordinary();rollovers=[]
        menu['CancelButton']=clip((50,400,100,420));root['SelectButton']=clip((400,400,470,420))
        menu['CancelButton']['rollOver']=lambda:rollovers.append('cancel')
        root['SelectButton']['rollOver']=lambda:rollovers.append('select')
        self.assertEqual(self.query(root,75,410),107)
        self.assertEqual(self.query(root,435,410),106)
        self.assertEqual(changes,[]);self.assertEqual(rollovers,['cancel','select'])
    def test_popup_hover_selects_choice_but_accept_remains_original_event(self):
        root={'top':0};root['_root']=root;changes=[]
        popup={'target':{'mov_0':clip((180,180,240,200)),'mov_1':clip((270,180,330,200))},
               'totalOptions':2,'currentOption':0,'reg_Color':10,'sel_Color':20}
        def select(delta,reg,sel):
            popup['currentOption']+=int(delta);changes.append((delta,reg,sel))
        popup['selectText']=select;root['popupScreens']=[popup]
        self.assertEqual(self.query(root,300,190,program=self.popup_program),106)
        self.assertEqual(popup['currentOption'],1);self.assertEqual(changes,[(1,10,20)])
        self.assertEqual(self.query(root,210,190,0,program=self.popup_program),1)
        self.assertEqual(popup['currentOption'],1)
        self.assertEqual(self.query(root,210,190,program=self.popup_program),106)
        self.assertEqual(popup['currentOption'],0)
        self.assertEqual(self.query(root,100,190,program=self.popup_program),1)
        popup['select_cancel']=clip((50,400,100,420))
        popup['select_done']=clip((400,400,470,420))
        self.assertEqual(self.query(root,75,410,program=self.popup_program),107)
        self.assertEqual(self.query(root,435,410,program=self.popup_program),106)
        old_option=popup['currentOption'];old_changes=list(changes)
        self.assertEqual(self.query(root,210,190,2,program=self.popup_program),1)
        self.assertEqual(self.query(root,435,410,2,program=self.popup_program),1)
        self.assertEqual(popup['currentOption'],old_option);self.assertEqual(changes,old_changes)
        popup['select_cancel']=None;popup['select_done']=None
        popup['select_yes']=clip((50,400,100,420))
        popup['select_no']=clip((400,400,470,420))
        # Instance names are swapped: select_yes imports NoButton (cancel),
        # and select_no imports YesButton (accept) in the retail Popup.
        self.assertEqual(self.query(root,75,410,program=self.popup_program),107)
        self.assertEqual(self.query(root,435,410,program=self.popup_program),106)
    def test_title_guard_uses_start_only_while_title_movie_is_visible(self):
        root={'StartScreen':{'_visible':True}};root['_root']=root
        self.assertEqual(self.query(root,10,10,program=self.title_program),110)
        self.assertEqual(self.query(root,10,10,0,program=self.title_program),1)
        root['StartScreen']['_visible']=False
        self.assertEqual(self.query(root,10,10,program=self.title_program),0)
    def test_savegame_cards_use_original_move_and_accept(self):
        cards={'_visible':True,'totalSlots':4,'curSlot':0,
               'slotObjs':[clip((50+i*100,150,140+i*100,220)) for i in range(4)]}
        root={'game_mc':cards};root['_root']=root;moves=[]
        def move(delta):cards['curSlot']+=int(delta);moves.append(delta)
        cards['move']=move
        self.assertEqual(self.query(root,370,180,program=self.save_program),106)
        self.assertEqual(cards['curSlot'],3);self.assertEqual(moves,[3])
        self.assertEqual(self.query(root,370,180,program=self.save_program),106)
        self.assertEqual(moves,[3])
        self.assertEqual(self.query(root,70,180,0,program=self.save_program),1)
        self.assertEqual(cards['curSlot'],3)
        self.assertEqual(self.query(root,70,180,2,program=self.save_program),1)
        self.assertEqual(cards['curSlot'],3)
        self.assertEqual(self.query(root,490,300,program=self.save_program),1)
        root['platform']={'placeHolder_mc':{'btn_0':clip((400,400,420,420)),
                                          'btn_4':clip((50,400,70,420))}}
        self.assertEqual(self.query(root,410,410,program=self.save_program),106)
        self.assertEqual(self.query(root,60,410,program=self.save_program),107)
        self.assertEqual(moves,[3])
        cards['_visible']=False
        self.assertEqual(self.query(root,70,180,program=self.save_program),0)
    def test_language_buttons_use_root_index_and_authored_select(self):
        menu={'_visible':True,'btn_0':clip((100,100,400,120)),'btn_1':clip((100,150,400,170))}
        root={'menu':menu,'numOfLanguages':2,'currentBtn':0};root['_root']=root;changes=[]
        def select(delta):root['currentBtn']+=int(delta);changes.append(delta)
        root['selectBtn']=select
        self.assertEqual(self.query(root,250,160,program=self.language_program),106)
        self.assertEqual(root['currentBtn'],1);self.assertEqual(changes,[1])
        self.assertEqual(self.query(root,250,110,program=self.language_program),106)
        self.assertEqual(root['currentBtn'],0);self.assertEqual(changes,[1,-1])
        self.assertEqual(self.query(root,490,300,program=self.language_program),1)
        self.assertEqual(self.query(root,250,160,0,program=self.language_program),1)
        self.assertEqual(self.query(root,250,160,2,program=self.language_program),1)
        self.assertEqual(root['currentBtn'],0)

    def test_unload_reload_keeps_mouse_and_original_action_values(self):
        audited=0;legacy_mismatches={}
        for package in PACKAGES:
            originals=resources((ROOT/'Simpsons Game, The (USA)'/package).read_bytes())
            for name,original in originals.items():
                if name not in MENU_NAMES:continue
                source=patch_options(original) if name=='options.swf' else original
                # Preserve a real pre-fix producer result to demonstrate why
                # simply retaining first-load semantics is insufficient.
                with patch('build_native_mouse_menu.canonicalize_constants'):
                    legacy=patch_mouse(source)
                native=patch_mouse(source)
                legacy_chunks=dict(ui_chunks(legacy));native_chunks=dict(ui_chunks(native))
                old_apt,new_apt=legacy_chunks[b'apti'],native_chunks[b'apti']
                slots=list(constant_references(new_apt,movie_root(new_apt)))
                before,after=typed_constants(legacy_chunks[b'cons']),typed_constants(native_chunks[b'cons'])
                self.assertEqual(len(slots),len(after),name)
                self.assertEqual([u(new_apt,at) for at in slots],list(range(len(slots))),name)
                # Includes every retail function, native Video function and
                # mouse function, rather than checking only appended code.
                self.assertEqual([before[u(old_apt,at)] for at in slots],after,name)
                self.assertEqual(unloaded_indices(native),native,name)
                legacy_mismatches[name]=sum(before[u(old_apt,at)]!=before[i] for i,at in enumerate(slots))
                audited+=1
        self.assertGreaterEqual(audited,13)
        self.assertGreater(legacy_mismatches['pause.swf'],2000)
        self.assertGreater(legacy_mismatches['options.swf'],4000)

        originals=resources((ROOT/'Simpsons Game, The (USA)'/PACKAGES[1]).read_bytes())
        native=patch_mouse(originals['pause.swf'])
        for data in (native,unloaded_indices(native),unloaded_indices(unloaded_indices(native))):
            root,menu,changes=self.ordinary()
            self.assertEqual(self.query(root,250,140,program=MouseProgram(data)),106)
            self.assertEqual(menu['currentSelection'],1);self.assertEqual(changes,[1])
        with patch('build_native_mouse_menu.canonicalize_constants'):
            legacy=patch_mouse(originals['pause.swf'])
        root,menu,changes=self.ordinary()
        self.assertEqual(self.query(root,250,140,program=MouseProgram(legacy)),106)
        root,menu,changes=self.ordinary()
        try:result=self.query(root,250,140,program=MouseProgram(unloaded_indices(legacy)))
        except (AssertionError,IndexError,KeyError,TypeError,ValueError):result=None
        self.assertNotEqual((result,menu['currentSelection'],changes),(106,1,[1]))

if __name__=='__main__':unittest.main()
