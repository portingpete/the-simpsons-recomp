"""Add keyboard rebinding to the retail ControlsMenu Apt timeline.

The original pulsing buttons, imported selectors, font, Accept/Cancel movies,
input dispatcher, and original gamepad preference callbacks remain in use.
Apply this after the video patch and before the mouse constant canonicalizer.
"""
import struct
from build_native_video_menu import Actions, u, put
from build_native_mouse_menu import ui_chunks, rebuild_ui

MENU = 0xF40
INIT_CONTROL = 0x3BAC
ORIGINAL_INIT = 0x80CC
ORIGINAL_METHODS_END = 0x836A
NATIVE_NAMES = ('ctlpage', *('ctlrow'+str(i) for i in range(8)), 'ctlaccept')
# Leave room below the authored title stroke without pushing the ten-row
# keyboard page into the original Accept/Cancel footer text rectangles.
ROW_TOP, ROW_SPACING, ROW_FONT_SIZE = -98.0, 26.0, 22.0
MOUSE_ROW_SPACING = 24.0
MOUSE_ACCEPT_OFFSET = 12
STOCK_ROW_Y = (16.0, 54.0, 92.0)
KEYBOARD_DEFAULTS = ('Keyboard 1/2 - Primary (Left/Right); Enter assigns',
    'Move forward [1]: W / Unbound', 'Move backward [1]: S / Unbound',
    'Move left [1]: A / Unbound', 'Move right [1]: D / Unbound',
    'Jump [1]: Space / Enter', 'Attack [1]: J / Mouse 1',
    'Special [1]: K / Mouse 2', 'Action [1]: E / Unbound', 'Accept changes')


def patch_controls(original):
    chunks = ui_chunks(original)
    apt = bytearray(next(data for tag,data in chunks if tag == b'apti'))
    cons = bytearray(next(data for tag,data in chunks if tag == b'cons'))
    if apt[MENU:MENU+20].hex() != '0000000509876543000000020000170800000000':
        raise ValueError('Unsupported retail ControlsMenu identity')
    if u(apt, INIT_CONTROL) != 1 or u(apt, INIT_CONTROL+4) != ORIGINAL_INIT:
        raise ValueError('ControlsMenu already patched or its initializer changed')

    # Copy all retail selector getter/setter functions and their constant pool.
    # Replacing the configuration tail makes the additional rows ordinary Apt
    # members before the original shared initializeButtons call executes.
    apt.extend(b'\0' * (-len(apt)%4))
    stream = len(apt)
    apt.extend(apt[ORIGINAL_INIT:ORIGINAL_METHODS_END])
    appended = len(apt)
    a = Actions(apt, cons)
    def move(right):
        a.string('nativeAction'); a.variable('currentSelection'); a.integer(2); a.op(0x0c)
        a.integer(101 if right else 100); a.op(0x0a); a.op(0x1d)
        a.integer(0); a.variable('_root'); a.member('_screen'); a.string('SaveControlsSettings'); a.op(0x52); a.op(0x17)
    a.function('moveLeft', lambda:move(False)); a.function('moveRight', lambda:move(True))
    def export(value):
        value(); a.integer(1); a.variable('_level0'); a.member('screen')
        a.string('SetSafeString'); a.op(0x52); a.op(0x17)
    def getter():
        export(lambda:a.variable('nativeAction')); a.assign('nativeAction', 0)
    a.function('getNativeAction', getter)
    # nativeAccept is only set by an actual mouse hit on the authored Accept
    # footer. A selected row exports 200+index; the explicit accept row exports
    # 209 (208 on the shorter mouse/controller page).
    def selection():
        export(lambda:(a.variable('nativeAccept'), a.integer(1000), a.op(0x0c),
                       a.variable('currentSelection'), a.op(0x0a), a.integer(200), a.op(0x0a)))
        a.assign('nativeAccept', 0)
    a.function('getNativeSelection', selection)
    a.function('nativeFooterAccept', lambda:a.assign('nativeAccept', 1))
    def toggle_controller():
        # Use the same original selector paths as Left/Right. Their animation
        # publishes currentState, which the unchanged gamepad getters consume.
        a.variable('_root');a.member('ControlsMenu');a.variable('Gizmos');a.variable('currentSelection');a.op(0x4e);a.op(0x4e);a.member('currentState')
        a.op(0x12);turn_on=a.jump(0x9d)
        a.method('_root','gizmoMoveRight');done=a.jump()
        a.target(turn_on);a.method('_root','gizmoMoveLeft');a.target(done)
    a.function('nativeControllerToggle',toggle_controller)
    def set_label(row):
        for member in ('variable_text',):
            a.variable('_root'); a.member('ControlsMenu'); a.member('btn_'+row); a.string(member); a.string(''); a.op(0x4f)
        a.variable('_root'); a.member('ControlsMenu'); a.member('btn_'+row); a.string('text_str'); a.push(4,1); a.op(0x4f)
        a.variable('_root'); a.member('ControlsMenu'); a.member('btn_'+row); a.member('DynamicText_mc'); a.member('dynamicText'); a.string('variable'); a.string(''); a.op(0x4f)
        a.variable('_root'); a.member('ControlsMenu'); a.member('btn_'+row); a.member('DynamicText_mc'); a.member('dynamicText'); a.string('text'); a.push(4,1); a.op(0x4f)
        a.variable('_root'); a.member('ControlsMenu'); a.member('text_'+row); a.member('text_entry'); a.string('text'); a.push(4,1); a.op(0x4f)
    for row in NATIVE_NAMES:a.function('set_'+row, lambda row=row:set_label(row), parameter=True)

    native_clips = ['btn_'+row for row in NATIVE_NAMES]
    stock_buttons = ['btn_Inverty','btn_Invertx','btn_Rumble']
    stock_gizmos = ['invertYSelector','invertXSelector','rumbleSelector']
    stock_texts = ['text_inverty','text_invertx','text_rumble']
    source_texts = ['text_'+row for row in NATIVE_NAMES]+stock_texts
    # Both page configurations create their visible button wrappers through the
    # original attachMovie path. Hidden old wrappers cannot intercept mouse hits.
    def visibility(name, visible):
        a.variable('_root'); a.member('ControlsMenu'); a.member(name); a.string('_visible'); a.push(5,int(visible)); a.op(0x4f)
    def stock_label(button, source):
        def value():
            a.variable('_root');a.member('ControlsMenu');a.member(source);a.member('text_entry');a.member('text')
        # Reattaching a stock button on the paged screen must refresh its actual
        # imported DynamicText member, as the native rows do. The shared button
        # timeline can otherwise retain its large authored placeholder text.
        a.variable('_root');a.member('ControlsMenu');a.member(button);a.string('variable_text');a.string('');a.op(0x4f)
        a.variable('_root');a.member('ControlsMenu');a.member(button);a.string('text_str');value();a.op(0x4f)
        a.variable('_root');a.member('ControlsMenu');a.member(button);a.member('DynamicText_mc');a.member('dynamicText');a.string('variable');a.string('');a.op(0x4f)
        a.variable('_root');a.member('ControlsMenu');a.member(button);a.member('DynamicText_mc');a.member('dynamicText');a.string('text');value();a.op(0x4f)
    def configure(mouse, activate=True):
        if activate:
            # The retail screen lifecycle removes attached pulsingButton
            # movies before rebuilding its rows. A page change shares the
            # screen, so perform that same teardown before reusing depths and
            # changing instance names on the mouse/controller page.
            a.method('_root','destroyActiveMenuButtons')
        for clip in native_clips+stock_buttons+stock_gizmos+source_texts:
            visibility(clip, False)
        a.assign('nativeMousePageActive',int(mouse))
        # Native mouse captions fit above the larger authored controller
        # selectors. Restore the keyboard pitch whenever its page is rebuilt.
        for i,row in enumerate(NATIVE_NAMES[:-1]):
            a.variable('_root');a.member('ControlsMenu');a.member('text_'+row);a.string('_y')
            y=int(ROW_TOP+(MOUSE_ROW_SPACING if mouse else ROW_SPACING)*i)
            if y<0:a.integer(0);a.integer(-y);a.op(0x0b)
            else:a.integer(y)
            a.op(0x4f)
        # The controller selectors need wider spacing than keyboard captions.
        # Give that page's last row the same extra room, restoring its authored
        # native position when either keyboard page is rebuilt.
        a.variable('_root');a.member('ControlsMenu');a.member('text_ctlaccept');a.string('_y')
        a.integer(int(ROW_TOP+ROW_SPACING*(len(NATIVE_NAMES)-1))+(MOUSE_ACCEPT_OFFSET if mouse else 0));a.op(0x4f)
        rows = ['ctlpage', *('ctlrow'+str(i) for i in range(4 if mouse else 8))]
        buttons = ['btn_'+row for row in rows]
        texts = ['text_'+row for row in rows]
        ids = ['NativeControls'+str(i) for i in range(len(rows))]
        if mouse:
            buttons += stock_buttons; texts += ['text_inverty','text_invertx','text_rumble']
            ids += ['InvertY','InvertX','Rumble']
        buttons += ['btn_ctlaccept']; texts += ['text_ctlaccept']; ids += ['NativeControlsAccept']
        a.array('MenuItemButtons',buttons); a.array('TextRefs',texts); a.array('MenuItemIds',ids)
        # Empty gizmo types are ignored by the original selector animator.
        a.array('Gizmos',(['']*5+stock_gizmos+['']) if mouse else [])
        a.array('GizmoTypes',(['']*5+['selector']*3+['']) if mouse else [])
        a.array('ColorRefs', [])
        for clip in stock_gizmos:visibility(clip, mouse)
        a.method('_root','initializeButtons'); a.assign('currentSelection',0)
        if activate:
            a.method('_root','activateGizmos')
            # InitialSelection belongs to other screen transitions as well.
            a.string('nativeRememberedSelection'); a.variable('_root'); a.member('InitialSelection'); a.op(0x1d)
            a.variable('_root'); a.string('InitialSelection'); a.string(''); a.op(0x4f)
            a.integer(0); a.integer(1); a.variable('_root'); a.string('activateMenuButtons'); a.op(0x52); a.op(0x17)
            a.variable('_root'); a.string('InitialSelection'); a.variable('nativeRememberedSelection'); a.op(0x4f)
        if mouse:
            for button,source in zip(stock_buttons,stock_texts):stock_label(button,source)
        # initialize/activate routines may change display-list visibility. Keep
        # source templates hidden after every layout and selection animation.
        for clip in source_texts:visibility(clip,False)
    a.function('nativeKeyboardPage',lambda:configure(False))
    a.function('nativeMousePage',lambda:configure(True))
    a.assign('nativeAction',0); a.assign('nativeAccept',0); configure(False, False)
    a.finish()

    # Clone only the retail dynamic-text/font and wrapper placement; character
    # IDs are allocated after whichever video rows were already appended.
    root=0x5F8; first_id=u(apt,root+20); characters=[]; placements=[]
    for i,(row,label) in enumerate(zip(NATIVE_NAMES,KEYBOARD_DEFAULTS)):
        text_id=first_id+i*2; text_at=len(apt); text=bytearray(apt[0xDB0:0xDEC])
        struct.pack_into('>f',text,36,ROW_FONT_SIZE)
        put(text,52,text_at+60);put(text,56,text_at+61+len(label))
        apt.extend(text);apt.extend(label.encode('ascii')+b'\0\0');apt.extend(b'\0'*(-len(apt)%4));characters.append(text_at)
        place_at=len(apt);place=bytearray(apt[0x38DC:0x391C]);put(place,12,text_id);apt.extend(place)
        controls_at=len(apt);apt.extend(struct.pack('>I',place_at))
        frames_at=len(apt);apt.extend(struct.pack('>II',1,controls_at))
        sprite_at=len(apt);sprite=bytearray(apt[0xDEC:0xE00]);put(sprite,12,frames_at);apt.extend(sprite);characters.append(sprite_at)
        at=len(apt);item=bytearray(apt[0x3AA4:0x3AE4]);put(item,8,40+i);put(item,12,text_id+1);put(item,52,at+64)
        struct.pack_into('>f',item,36,ROW_TOP+ROW_SPACING*i)
        apt.extend(item);apt.extend(('text_'+row).encode('ascii')+b'\0');apt.extend(b'\0'*(-len(apt)%4));placements.append(at)
    character_table=len(apt);old_table=u(apt,root+24)
    apt.extend(apt[old_table:old_table+first_id*4])
    for at in characters:apt.extend(struct.pack('>I',at))
    put(apt,root+20,first_id+len(characters));put(apt,root+24,character_table)
    # Controller rows occupy the last three mouse-page slots. Their original
    # selectors and localization stay intact; only the authored row Y changes.
    for text_at,y in zip((0x3DB4,0x3E74,0x3CB4),STOCK_ROW_Y):
        struct.pack_into('>f',apt,text_at+36,y)
    for gizmo_at,y in zip((0x3E34,0x3DF4,0x3EB4),STOCK_ROW_Y):
        struct.pack_into('>f',apt,gizmo_at+36,y+10)
    # Native labels use the same small point size, including original gamepad
    # text. SharedLibrary selector geometry and callbacks remain untouched.
    for character in (0xE50,0xEA0,0xEF0):struct.pack_into('>f',apt,character+36,ROW_FONT_SIZE)
    # Decorative authored strokes are fixed to the old three-row layout.
    # Move them below the page rather than displaying separators across rows.
    for at in (0x3CF4,0x3D34,0x3D74):struct.pack_into('>f',apt,at+36,1000.0)
    frames=u(apt,MENU+12);count=u(apt,frames);table=u(apt,frames+4)
    old_controls=[u(apt,table+4*i) for i in range(count)]
    new_table=len(apt)
    for at in old_controls+placements:apt.extend(struct.pack('>I',at))
    put(apt,frames,count+len(placements));put(apt,frames+4,new_table);put(apt,INIT_CONTROL+4,stream)
    rebuilt=[(tag,bytes(apt) if tag==b'apti' else bytes(cons) if tag==b'cons' else data) for tag,data in chunks]
    return rebuild_ui(original,rebuilt)
