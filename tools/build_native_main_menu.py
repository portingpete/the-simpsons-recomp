"""Append a native Exit Game row to the owned retail MainMenu Apt movie.

Keep the original pulsing-button layout, selection methods and availability
gates. The host recognizes its menu ID only after the retail Select handler
has accepted input and exported the selected row through SetSafeString.
"""
import hashlib
import struct
from build_native_video_menu import Actions, u, put
from build_native_mouse_menu import ui_chunks, rebuild_ui

FRONTEND_SHA = '973b276f05da7ef7298a0be7f415c26fa1bdbccd52f24f11237c74add0261721'
ROOT, MENU, INIT_CONTROL = 0x39E8, 0x4D98, 0xEF94
ORIGINAL_INIT, ORIGINAL_METHODS_END = 0x1F534, 0x1F59A
ROW_NAMES = ('Continue','Replay_episode','Debug_replay','Cliches','Movies','Options','Extras','ExitGame')
ROW_IDS = ('Continue','Replay_episode','Debug_replay','Cliches','FMVBrowser','Options','Extras','NativeExitGame')
TEXT_NAMES = ('continue','replay_episode','debug_replay','cliches','movies','options','extras','exitgame')
SOURCE_PLACEMENTS = (0xF05C,0xF11C,0xF15C,0xF01C,0xF09C,0xF0DC,0xF19C)
ROW_TOP, ROW_SPACING = -105.0, 36.0
ACTIVATE_CONTROL, ORIGINAL_ACTIVATE, ORIGINAL_ACTIVATE_END = 0xF25C, 0x1F7AC, 0x1F7DF


def patch_main_menu(original):
    if hashlib.sha256(original).hexdigest() != FRONTEND_SHA:
        raise ValueError('Unsupported retail Frontend UIX identity')
    chunks=ui_chunks(original)
    apt=bytearray(next(data for tag,data in chunks if tag==b'apti'))
    cons=bytearray(next(data for tag,data in chunks if tag==b'cons'))
    if u(apt,INIT_CONTROL)!=1 or u(apt,INIT_CONTROL+4)!=ORIGINAL_INIT:
        raise ValueError('MainMenu initializer changed')
    apt.extend(b'\0'*(-len(apt)%4));stream=len(apt)
    apt.extend(apt[ORIGINAL_INIT:ORIGINAL_METHODS_END])
    a=Actions(apt,cons)
    a.array('MenuItemButtons',['btn_'+row for row in ROW_NAMES])
    a.array('TextRefs',['text_'+row for row in TEXT_NAMES])
    a.array('MenuItemIds',ROW_IDS)
    a.array('RemoveItems',[])
    for method,row in (('IsSavedGameAvailable','Continue'),('IsDebugEnabled','Debug_replay'),('AreExtrasAvailable','Extras')):
        a.integer(0);a.variable('_root');a.member('_screen');a.string(method);a.op(0x52)
        available=a.jump(0x9d)
        a.string(row);a.integer(1);a.variable('RemoveItems');a.string('push');a.op(0x52);a.op(0x17)
        a.target(available)
    a.method('_root','initializeButtons');a.assign('currentSelection',0)
    a.integer(0);a.variable('_root');a.string('getInitialSelection');a.op(0x52)
    a.string('');a.op(0x49);a.op(0x12);remembered=a.jump(0x9d)
    a.string('Continue');a.integer(1);a.variable('_root');a.string('setInitialSelection');a.op(0x52);a.op(0x17)
    a.target(remembered);a.finish();put(apt,INIT_CONTROL+4,stream)

    # The shared button activates its own DynamicText timeline on frame two.
    # Publish the explicit caption after that original activation so its
    # imported placeholder/binding cannot replace the native literal.
    apt.extend(b'\0'*(-len(apt)%4));activation_stream=len(apt)
    apt.extend(apt[ORIGINAL_ACTIVATE:ORIGINAL_ACTIVATE_END])
    label=Actions(apt,cons)
    def button():label.variable('btn_ExitGame')
    for property in ('variable_text','text_str'):
        button();label.string(property);label.string('Exit Game' if property=='text_str' else '');label.op(0x4f)
    for property in ('variable','text'):
        button();label.member('DynamicText_mc');label.member('dynamicText')
        label.string(property);label.string('Exit Game' if property=='text' else '');label.op(0x4f)
    label.finish();put(apt,ACTIVATE_CONTROL+4,activation_stream)

    # Clone the authored Extras text, wrapper and transform. Its empty variable
    # binding preserves this literal caption through the localization pass.
    character_count,character_table=u(apt,ROOT+20),u(apt,ROOT+24)
    text_at=len(apt);text=bytearray(apt[0x4D0C:0x4D48])
    put(text,52,text_at+60);put(text,56,text_at+60+len('Exit Game')+1)
    apt.extend(text);apt.extend(b'Exit Game\0\0');apt.extend(b'\0'*(-len(apt)%4))
    place_at=len(apt);place=bytearray(apt[0xEF54:0xEF94]);put(place,12,character_count);apt.extend(place)
    controls_at=len(apt);apt.extend(struct.pack('>I',place_at))
    frames_at=len(apt);apt.extend(struct.pack('>II',1,controls_at))
    sprite_at=len(apt);sprite=bytearray(apt[0x4D48:0x4D5C]);put(sprite,12,frames_at);apt.extend(sprite)
    placement_at=len(apt);placement=bytearray(apt[0xF19C:0xF1DC])
    # Depth 21 is the original imported MenuStroke; preserve every stock depth.
    put(placement,8,25);put(placement,12,character_count+1);put(placement,52,placement_at+64)
    # Keep the authored 48-point font and scale. A compact source grid leaves
    # room for eight choices; shared initializeButtons recenters fewer choices.
    for i,source in enumerate(SOURCE_PLACEMENTS):struct.pack_into('>f',apt,source+36,ROW_TOP+ROW_SPACING*i)
    struct.pack_into('>f',placement,36,ROW_TOP+ROW_SPACING*(len(ROW_NAMES)-1))
    apt.extend(placement);apt.extend(b'text_exitgame\0');apt.extend(b'\0'*(-len(apt)%4))
    new_character_table=len(apt);apt.extend(apt[character_table:character_table+4*character_count])
    apt.extend(struct.pack('>II',text_at,sprite_at))
    put(apt,ROOT+20,character_count+2);put(apt,ROOT+24,new_character_table)
    frames=u(apt,MENU+12);count,table=u(apt,frames),u(apt,frames+4)
    # Instantiate the added TextRef before the initializer reads its text and
    # geometry. Appending it leaves an unresolved Dynamic Text button and a
    # separate visible yellow template on the first active menu frame.
    new_controls=len(apt);apt.extend(struct.pack('>I',placement_at));apt.extend(apt[table:table+4*count])
    put(apt,frames,count+1);put(apt,frames+4,new_controls)
    return rebuild_ui(original,[(tag,bytes(apt) if tag==b'apti' else bytes(cons) if tag==b'cons' else data) for tag,data in chunks])
