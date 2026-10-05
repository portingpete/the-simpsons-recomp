"""Mouse hit regions use the original Apt display list and menu methods.

The retail Xbox Apt leaves hitTest unimplemented. The host supplies that one
point/bounds operation; these small AVM methods retain authored row visibility,
selection animation, screen ownership, and the existing controller events.
"""
import struct
from build_native_video_menu import Actions, u, put

MENU_NAMES = frozenset(('frontend.swf','simpsons_start.swf','options.swf','pause.swf','popup.swf',
    'savegame.swf','language.swf','profile_chooser.swf','episode_goals.swf','collectibles.swf','episode_complete.swf',
    'cliche_summary.swf','fmv_browser.swf'))

class MouseActions(Actions):
    def __init__(self, apt, cons):
        super().__init__(apt,cons)
        self.named_params=[]
    def function_named(self,name,parameters,body):
        self.op(0x9b);self.aligned();at=len(self.apt)
        self.strings.append((at,name))
        self.apt.extend(struct.pack('>6I',0,len(parameters),0,0,0x98765432,0x12345678))
        self.named_params.append((at+8,parameters))
        begin=len(self.apt);body();put(self.apt,at+12,len(self.apt)-begin)
    def local(self,name,value):
        self.string(name);value();self.op(0x3c)
    def jump(self,op=0x99):
        self.op(op);self.aligned();at=len(self.apt);self.apt.extend(b'\0'*4);return at
    def target(self,at):put(self.apt,at,(len(self.apt)-at-4)&0xffffffff)
    def return_int(self,value):self.integer(value);self.op(0x3e)
    def call(self,receiver,name,arguments=(),discard=False):
        for argument in reversed(arguments):argument()
        self.integer(len(arguments));receiver();self.string(name);self.op(0x52)
        if discard:self.op(0x17)
    def finish(self):
        self.op(0);self.aligned()
        for at,names in self.named_params:
            put(self.apt,at,len(self.apt))
            for name in names:
                self.strings.append((len(self.apt),name));self.apt.extend(b'\0'*4)
        super().finish()

def ui_chunks(payload):
    at=8;result=[]
    while at<len(payload):
        if at+8>len(payload):raise ValueError('Truncated UIX chunk')
        size=u(payload,at+4)
        if size<8 or at+size>len(payload):raise ValueError('Invalid UIX chunk extent')
        result.append((payload[at:at+4],payload[at+8:at+size]));at+=size
    return result

def rebuild_ui(original,chunks):
    out=bytearray(original[:8])
    changed=False
    for tag,data in chunks:
        if tag==b'alig' and changed:continue
        out.extend(tag+struct.pack('>I',len(data)+8)+data)
        if tag==b'apti':changed=True
        if changed:
            pad=(-len(out)-8)%32
            out.extend(b'alig'+struct.pack('>I',pad+8)+b'\0'*pad)
    put(out,4,len(out));return bytes(out)

def name_imported_footers(apt,root):
    """Expose existing unnamed SharedLibrary footer movies to the AVM query.

    The original Back/Select and Accept/Cancel wrappers already contain their
    glyph and localized label. Only the placement's instance-name flag and
    pointer change; character, transform, depth and timeline remain authored.
    """
    footer_names=frozenset(('BackButton','CancelButton','AcceptButton','DoneButton',
        'SelectButton','ContinueButton','SubmitButton','NextButton','DeleteButton',
        'ChangeDeviceButton'))
    original_size=len(apt)
    def string(at):
        if not 0<at<original_size:raise ValueError('Invalid Apt import string')
        end=apt.find(b'\0',at,original_size)
        if end<0:raise ValueError('Unterminated Apt import string')
        return apt[at:end].decode('ascii')
    imports={}
    count,table=u(apt,root+40),u(apt,root+44)
    if table+count*16>original_size:raise ValueError('Invalid Apt import table')
    for i in range(count):
        at=table+16*i
        source,name=string(u(apt,at)),string(u(apt,at+4))
        if source=='SharedLibrary' and name in footer_names:imports[u(apt,at+8)]=name
    if not imports:return
    count,table=u(apt,root+20),u(apt,root+24)
    if table+count*4>original_size:raise ValueError('Invalid Apt character table')
    movies={root}
    for i in range(count):
        at=u(apt,table+4*i)
        if at and at+20<=original_size and u(apt,at)==5:movies.add(at)
    placements={}
    for movie in sorted(movies):
        count,frames=u(apt,movie+8),u(apt,movie+12)
        if frames+count*8>original_size:raise ValueError('Invalid Apt movie frames')
        for i in range(count):
            frame=frames+8*i;controls,table=u(apt,frame),u(apt,frame+4)
            if table+controls*4>original_size:raise ValueError('Invalid Apt frame controls')
            for j in range(controls):
                at=u(apt,table+4*j)
                if at+4>original_size:raise ValueError('Invalid Apt control')
                if u(apt,at)!=3:continue
                if at+64>original_size:raise ValueError('Invalid Apt placement')
                name=imports.get(u(apt,at+12))
                if name and not u(apt,at+52) and not u(apt,at+4)&0x20:placements[at]=name
    pointers={}
    for at,name in sorted(placements.items()):
        if name not in pointers:
            apt.extend(b'\0'*(-len(apt)%4));pointers[name]=len(apt)
            apt.extend(name.encode('ascii')+b'\0')
        put(apt,at+4,u(apt,at+4)|0x20);put(apt,at+52,pointers[name])

def constant_references(apt,root):
    """Yield cons references in the original Apt unload traversal order.

    827DF2C8 starts the counter at zero and walks character IDs. For movies,
    827F0AD8 walks frames, controls, and placement clip events. 827D58D8 scans
    each action linearly, including function and With bodies; only End stops it.
    """
    def span(at,size):
        if at<0 or size<0 or at+size>len(apt):raise ValueError('Invalid Apt traversal extent')
    def array(count,table,stride):
        span(table,count*stride)
        return range(table,table+count*stride,stride)
    aligned_sizes={0x81:4,0x83:8,0x87:4,0x88:8,0x8b:4,0x8c:4,
        0x8e:28,0x8f:20,0x94:4,0x96:8,0x99:4,0x9b:24,0x9d:4,
        0x9f:4,0xa1:4,0xa4:4,0xa5:4,0xa6:4,0xa7:4,0xb8:4}
    packed_sizes={0xa2:1,0xa3:2,0xae:1,0xaf:1,0xb0:1,0xb1:1,
        0xb2:1,0xb3:1,0xb4:4,0xb5:1,0xb6:2,0xb7:4}
    def actions(at):
        while True:
            span(at,1);op=apt[at];at+=1
            if not op:return
            if op in aligned_sizes:
                at=(at+3)&~3;span(at,aligned_sizes[op])
                if op in (0x88,0x96):
                    yield from array(u(apt,at),u(apt,at+4),4)
                at+=aligned_sizes[op]
            else:
                size=packed_sizes.get(op,0);span(at,size);at+=size
    span(root,48)
    imported={u(apt,at+8) for at in array(u(apt,root+40),u(apt,root+44),16)}
    for char_id,char_slot in enumerate(array(u(apt,root+20),u(apt,root+24),4)):
        if char_id in imported:continue
        char=u(apt,char_slot)
        if not char:continue
        span(char,4)
        if u(apt,char) not in (5,9):continue
        span(char,16)
        for frame in array(u(apt,char+8),u(apt,char+12),8):
            for control_slot in array(u(apt,frame),u(apt,frame+4),4):
                control=u(apt,control_slot);span(control,4);kind=u(apt,control)
                if kind==1:
                    span(control,8);yield from actions(u(apt,control+4))
                elif kind==8:
                    span(control,12);yield from actions(u(apt,control+8))
                elif kind==3:
                    span(control,64);events=u(apt,control+60)
                    if events:
                        span(events,8)
                        for event in array(u(apt,events),u(apt,events+4),12):
                            yield from actions(u(apt,event+8))

def canonicalize_constants(apt,cons,root):
    """Make first load and subsequent native unload/reload use equal indices.

    Native unload replaces every Push/ConstantPool reference with its traversal
    ordinal (827D5BD4), rather than restoring its serialized index. New root
    actions precede original child actions, so appending cons entries alone
    corrupts both scripts on the next load. Keep one entry per occurrence in
    exactly that order, copying values and retaining their string contents.
    """
    count,table=u(cons,24),u(cons,28)
    if table!=32 or table+count*8>len(cons):raise ValueError('Invalid Apt constant table')
    slots=list(constant_references(apt,root))
    if len(set(slots))!=len(slots):raise ValueError('Apt constant table is shared between traversed actions')
    indices=[u(apt,at) for at in slots]
    if any(index>=count for index in indices):raise ValueError('Invalid Apt constant index')
    entries=bytearray();shift=(len(slots)-count)*8
    for index in indices:
        entry=bytearray(cons[table+index*8:table+(index+1)*8])
        if u(entry,0)==1 and u(entry,4):put(entry,4,u(entry,4)+shift)
        entries.extend(entry)
    strings=cons[table+count*8:];header=bytearray(cons[:table]);put(header,24,len(slots))
    for ordinal,at in enumerate(slots):put(apt,at,ordinal)
    cons[:]=header+entries+strings

def patch_mouse(original):
    chunks=ui_chunks(original);apt=None;cons=None
    for tag,data in chunks:
        if tag==b'apti':apt=bytearray(data)
        elif tag==b'cons':cons=bytearray(data)
    if apt is None or cons is None:raise ValueError('UIX lacks Apt constants')
    # Root Movie has a uniquely typed header and the authored stage dimensions.
    roots=[at for at in range(16,len(apt)-36,4) if u(apt,at)==9 and u(apt,at+4)==0x09876543]
    if len(roots)!=1:raise ValueError('Ambiguous original Apt root movie')
    root=roots[0];name_imported_footers(apt,root);frames=u(apt,root+12)
    count,table=u(apt,frames),u(apt,frames+4)
    controls=[u(apt,table+4*i) for i in range(count)]
    apt.extend(b'\0'*(-len(apt)%4));stream=len(apt);a=MouseActions(apt,cons)
    v=lambda name:lambda:a.variable(name)
    integer=lambda n:lambda:a.integer(n)
    def member(obj,name):return lambda:(obj(),a.member(name))
    def hit(clip):
        a.call(v('_root'),'nativeMouseHit',(clip,v('mx'),v('my')))
    def hit_body():
        a.variable('clip');a.op(0x12);missing=a.jump(0x9d)
        a.variable('clip');a.member('_visible');a.op(0x12);hidden=a.jump(0x9d)
        a.call(v('clip'),'hitTest',(v('mx'),v('my'),integer(0)))
        a.op(0x3e);a.target(missing);a.target(hidden);a.return_int(0)
    a.function_named('nativeMouseHit',('clip','mx','my'),hit_body)
    # Retail Popup names are swapped: select_yes imports SharedLibrary.NoButton
    # (cancel), while select_no imports YesButton (accept). Preserve the actual
    # authored button events rather than interpreting those instance names.
    footer_groups=((7,('BackButton','CancelButton','select_back','select_cancel','select_decline','select_yes')),
        (6,('AcceptButton','DoneButton','SelectButton','ContinueButton','SubmitButton',
            'NextButton','BtnSelect','select_done','select_no','select_accept',
            'select_submit','select_next')),
        (13,('DeleteButton',)),(12,('ChangeDeviceButton',)))
    def footers(receivers):
        for event,names in footer_groups:
            for receiver in receivers:
                for name in names:
                    a.local('clip',member(receiver,name));hit(v('clip'));a.op(0x12);skip=a.jump(0x9d)
                    a.call(v('clip'),'rollOver',discard=True)
                    a.return_int(100+event);a.target(skip)
    def popup_body():
        a.local('popup',lambda:(a.variable('popupScreens'),a.variable('top'),a.op(0x4e)))
        a.variable('popup');a.op(0x12);missing=a.jump(0x9d)
        # Some authored placeholder objects do not expose _visible. Screen
        # ownership comes from its populated stack; actual child hitTest checks
        # visibility throughout the original display tree.
        a.variable('update');a.op(0x4a);a.integer(1);a.op(0x49);a.op(0x12);query_only=a.jump(0x9d)
        footers((v('popup'),member(v('popup'),'target'),v('_root')))
        a.local('i',integer(0));loop=len(apt)
        a.variable('i');a.variable('popup');a.member('totalOptions');a.op(0x48)
        a.op(0x12);done=a.jump(0x9d)
        a.local('clip',lambda:(a.variable('popup'),a.member('target'),a.string('mov_'),a.variable('i'),a.op(0x47),a.op(0x4e)))
        hit(v('clip'));a.op(0x12);next_row=a.jump(0x9d)
        # Popup selection is separate from the ordinary menu button array.
        # selectText owns its colors/pulse; existing accept commits SetSelected.
        a.call(v('popup'),'selectText',(
            lambda:(a.variable('i'),a.variable('popup'),a.member('currentOption'),a.op(0x0b)),
            member(v('popup'),'reg_Color'),member(v('popup'),'sel_Color')),discard=True)
        a.return_int(106)
        a.target(next_row);a.string('i');a.variable('i');a.op(0x50);a.op(0x1d)
        back=a.jump();put(apt,back,(loop-back-4)&0xffffffff)
        a.target(done);a.target(query_only);a.return_int(1)
        a.target(missing);a.return_int(0)
    a.function_named('nativeMousePopup',('mx','my','update'),popup_body)
    def custom_body():
        a.local('cards',v('game_mc'))
        a.variable('cards');a.op(0x12);no_cards=a.jump(0x9d)
        a.variable('cards');a.member('_visible');a.op(0x12);hidden_cards=a.jump(0x9d)
        a.variable('update');a.op(0x4a);a.integer(1);a.op(0x49);a.op(0x12);cards_query=a.jump(0x9d)
        footers((v('cards'),v('_root')))
        # Save/load's platform prompt class authors ACCEPT at index0 and
        # CANCEL at index4, with the other three prompt slots disabled.
        holder=member(v('platform'),'placeHolder_mc')
        for event,index in ((6,0),(7,4)):
            for prefix in ('btn_','txt_'):
                a.local('clip',member(holder,prefix+str(index)))
                hit(v('clip'));a.op(0x12);skip=a.jump(0x9d)
                a.return_int(100+event);a.target(skip)
        a.local('i',integer(0));cards_loop=len(apt)
        a.variable('i');a.variable('cards');a.member('totalSlots');a.op(0x48)
        a.op(0x12);cards_done=a.jump(0x9d)
        a.local('clip',lambda:(a.variable('cards'),a.member('slotObjs'),a.variable('i'),a.op(0x4e)))
        hit(v('clip'));a.op(0x12);next_card=a.jump(0x9d)
        a.variable('i');a.variable('cards');a.member('curSlot');a.op(0x49);same_card=a.jump(0x9d)
        a.call(v('cards'),'move',(lambda:(a.variable('i'),a.variable('cards'),a.member('curSlot'),a.op(0x0b)),),discard=True)
        a.target(same_card);a.return_int(106)
        a.target(next_card);a.string('i');a.variable('i');a.op(0x50);a.op(0x1d)
        back=a.jump();put(apt,back,(cards_loop-back-4)&0xffffffff)
        a.target(cards_done);a.target(cards_query);a.return_int(1)
        a.target(no_cards);a.target(hidden_cards)
        # Language's root locals own the index; menu is only its display list.
        a.variable('menu');a.op(0x12);no_languages=a.jump(0x9d)
        a.variable('numOfLanguages');a.op(0x12);no_count=a.jump(0x9d)
        a.variable('menu');a.member('_visible');a.op(0x12);hidden_languages=a.jump(0x9d)
        a.variable('update');a.op(0x4a);a.integer(1);a.op(0x49);a.op(0x12);languages_query=a.jump(0x9d)
        footers((v('menu'),v('_root')))
        a.local('i',integer(0));languages_loop=len(apt)
        a.variable('i');a.variable('numOfLanguages');a.op(0x48)
        a.op(0x12);languages_done=a.jump(0x9d)
        a.local('clip',lambda:(a.variable('menu'),a.string('btn_'),a.variable('i'),a.op(0x47),a.op(0x4e)))
        hit(v('clip'));a.op(0x12);next_language=a.jump(0x9d)
        a.variable('i');a.variable('currentBtn');a.op(0x49);same_language=a.jump(0x9d)
        a.call(v('_root'),'selectBtn',(lambda:(a.variable('i'),a.variable('currentBtn'),a.op(0x0b)),),discard=True)
        a.target(same_language);a.return_int(106)
        a.target(next_language);a.string('i');a.variable('i');a.op(0x50);a.op(0x1d)
        back=a.jump();put(apt,back,(languages_loop-back-4)&0xffffffff)
        a.target(languages_done);a.target(languages_query);a.return_int(1)
        a.target(no_languages);a.target(no_count);a.target(hidden_languages);a.return_int(0)
    a.function_named('nativeMouseCustom',('mx','my','update'),custom_body)
    def query_body():
        a.local('popupHit',lambda:a.call(v('_root'),'nativeMousePopup',(v('mx'),v('my'),v('update'))))
        a.variable('popupHit');a.op(0x12);no_popup=a.jump(0x9d)
        a.variable('popupHit');a.op(0x3e);a.target(no_popup)
        a.local('customHit',lambda:a.call(v('_root'),'nativeMouseCustom',(v('mx'),v('my'),v('update'))))
        a.variable('customHit');a.op(0x12);no_custom=a.jump(0x9d)
        a.variable('customHit');a.op(0x3e);a.target(no_custom)
        # Retail simpsons_start's title/Press Start screen is the StartScreen
        # movie. Its native Start handler owns the transition, so the click
        # uses the same controller event.
        a.variable('StartScreen');a.op(0x12);no_title=a.jump(0x9d)
        a.variable('StartScreen');a.member('_visible');a.op(0x12);hidden_title=a.jump(0x9d)
        a.variable('update');a.op(0x4a);a.op(0x12);title_query=a.jump(0x9d)
        a.return_int(110);a.target(title_query);a.return_int(1)
        a.target(no_title);a.target(hidden_title)
        a.local('menu',v('ActiveMenu'))
        a.variable('menu');a.op(0x12);missing=a.jump(0x9d)
        a.variable('menu');a.member('_visible');a.op(0x12);hidden=a.jump(0x9d)
        # No hit queries while authored timeline animations block input.
        a.variable('OkayToAcceptUserInput');a.op(0x12);blocked=a.jump(0x9d)
        a.variable('update');a.op(0x4a);a.op(0x12);query_only=a.jump(0x9d)
        # Footers are separate movie clips and are absent from MenuItemButtons.
        # Their original rollOver callback owns popup Yes/No selection.
        a.variable('update');a.op(0x4a);a.integer(2);a.op(0x49);wheel_footers=a.jump(0x9d)
        footers((v('menu'),v('_root')))
        a.target(wheel_footers)
        # Original initializeButtons attaches row i at display depth i+100
        # (retail options 0x5524..0x5560). Native Video labels retain their
        # authored text bounds, which extend below the visible 20-point line.
        # Query visible button wrappers from highest depth to lowest, matching
        # Flash drawing order when those rectangles overlap. initializeButtons
        # copies TextRefs into those wrappers and hides the source TextRefs.
        # Ordinary rows keep the already authored menu traversal.
        a.local('reverseRows',lambda:(a.variable('menu'),a.variable('_root'),a.member('VideoMenu'),a.op(0x49)))
        a.local('hitNames',member(v('menu'),'MenuItemButtons'))
        a.local('i',integer(0))
        a.variable('reverseRows');a.op(0x12);forward_rows=a.jump(0x9d)
        a.string('i');a.variable('hitNames');a.member('length');a.push(7,1);a.op(0x0b);a.op(0x1d)
        a.target(forward_rows);loop=len(apt)
        a.variable('i');a.push(7,0);a.op(0x48);negative_done=a.jump(0x9d)
        a.variable('i');a.variable('menu');a.member('MenuItemButtons');a.member('length');a.op(0x48)
        a.op(0x12);done=a.jump(0x9d)
        a.local('clip',lambda:(a.variable('menu'),a.variable('hitNames'),a.variable('i'),a.op(0x4e),a.op(0x4e)))
        hit(v('clip'));a.op(0x12);next_row=a.jump(0x9d)
        a.local('editRow',v('reverseRows'))
        a.variable('editRow');is_video=a.jump(0x9d)
        a.local('editRow',lambda:(a.variable('i'),a.variable('menu'),a.member('GizmoTypes'),a.member('length'),a.op(0x48)))
        a.target(is_video)
        # Wheel-only queries retain keyboard/controller navigation for normal
        # lists. A setting wheel still targets its actual hovered value row.
        a.variable('editRow');edit_hover=a.jump(0x9d)
        a.variable('update');a.op(0x4a);a.integer(2);a.op(0x49);wheel_hover=a.jump(0x9d)
        a.target(edit_hover)
        a.variable('menu');a.member('currentSelection');a.variable('i');a.op(0x49);same=a.jump(0x9d)
        # Original activateMenuButtons takes/returns a numeric button index.
        # Its remembered InitialSelection string can override that argument;
        # clear it only for this call, then restore it for later screen entry.
        a.local('rememberedSelection',member(v('_root'),'InitialSelection'))
        a.variable('_root');a.string('InitialSelection');a.string('');a.op(0x4f)
        a.variable('menu');a.string('currentSelection')
        a.call(v('_root'),'activateMenuButtons',(v('i'),))
        a.op(0x4f)
        a.variable('_root');a.string('InitialSelection');a.variable('rememberedSelection');a.op(0x4f)
        a.target(same);a.target(wheel_hover)
        a.variable('editRow');gizmo=a.jump(0x9d)
        a.return_int(106);a.target(gizmo)
        # Row labels span both directions. The center is supplied by the host
        # in stage coordinates; clicking left decreases and right increases.
        a.variable('mx');a.op(0x4a);a.variable('center');a.op(0x4a);a.op(0x48);left=a.jump(0x9d)
        a.return_int(103);a.target(left);a.return_int(102)
        a.target(next_row)
        a.variable('reverseRows');a.op(0x12);forward_next=a.jump(0x9d)
        a.string('i');a.variable('i');a.push(7,1);a.op(0x0b);a.op(0x1d)
        reverse_next=a.jump()
        a.target(forward_next);a.string('i');a.variable('i');a.op(0x50);a.op(0x1d)
        a.target(reverse_next)
        back=a.jump();put(apt,back,(loop-back-4)&0xffffffff)
        a.target(negative_done);a.target(done);a.target(blocked);a.target(query_only);a.return_int(1)
        a.target(missing);a.target(hidden);a.return_int(0)
    a.function_named('nativeMouseQuery',('mx','my','update','center'),query_body)
    a.finish()
    action=len(apt);apt.extend(struct.pack('>II',1,stream))
    new_table=len(apt)
    for at in (*controls,action):apt.extend(struct.pack('>I',at))
    put(apt,frames,count+1);put(apt,frames+4,new_table)
    canonicalize_constants(apt,cons,root)
    rebuilt=[(tag,bytes(apt) if tag==b'apti' else bytes(cons) if tag==b'cons' else data) for tag,data in chunks]
    return rebuild_ui(original,rebuilt)
