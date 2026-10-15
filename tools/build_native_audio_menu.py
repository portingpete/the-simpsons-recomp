"""Add a native Master volume row to the retail AudioMenu Apt timeline.

The original Voice, Music, Effects and Chatter sliders, the Subtitles selector,
their PopulateAudioSettings/SaveAudioSettings callbacks, fonts, pulsing buttons
and Accept/Cancel footers remain in use. The new first row is a native text row:
Left/Right export an action that SaveAudioSettings hands to the native runtime.
Apply after the Controls patch and before the mouse constant canonicalizer.
"""
import struct
from build_native_video_menu import Actions, u, put
from build_native_mouse_menu import ui_chunks, rebuild_ui

MENU = 0xD4C
INIT_CONTROL = 0x33CC
ORIGINAL_INIT = 0x776C
# Constant pool, move/set functions and the five value getters. The retail
# configuration arrays, initializeButtons and currentSelection follow.
ORIGINAL_METHODS_END = 0x7B8A
MASTER_Y = -127.0
MASTER_DEFAULT = 'Master volume: 100%'
# Original row placements (text, slider, colour reference) and how far each
# moves down so the new top row fits above the unchanged Subtitles row.
ROW_SHIFTS = {0x3854:26.0,0x37D4:26.0,0x3814:26.0,   # Voice
              0x3794:19.5,0x3714:19.5,0x3754:19.5,   # Music
              0x36D4:13.0,0x3654:13.0,0x3694:13.0,   # Effects
              0x3614:6.5,0x3594:6.5,0x35D4:6.5}      # Chatter
VOICE_TEXT, VOICE_SPRITE, VOICE_INNER, VOICE_ITEM = 0xCFC, 0xD38, 0x338C, 0x3854
MASTER_DEPTH = 80


def patch_audio(original):
    chunks = ui_chunks(original)
    apt = bytearray(next(data for tag,data in chunks if tag == b'apti'))
    cons = bytearray(next(data for tag,data in chunks if tag == b'cons'))
    if apt[MENU:MENU+20].hex() != '000000050987654300000002000015f400000000':
        raise ValueError('Unsupported retail AudioMenu identity')
    if u(apt, INIT_CONTROL) != 1 or u(apt, INIT_CONTROL+4) != ORIGINAL_INIT:
        raise ValueError('AudioMenu already patched or its initializer changed')
    if apt[ORIGINAL_METHODS_END:ORIGINAL_METHODS_END+1] != b'\x96':
        raise ValueError('AudioMenu configuration arrays moved')
    for at in ROW_SHIFTS:
        if u(apt, at) != 3:
            raise ValueError('AudioMenu row placement changed')

    apt.extend(b'\0' * (-len(apt)%4))
    stream = len(apt)
    apt.extend(apt[ORIGINAL_INIT:ORIGINAL_METHODS_END])
    a = Actions(apt, cons)
    # Row 0 is the native Master volume; original rows keep row ID zero, so the
    # native callback performs their original gizmo move and save.
    row_ids = [1,0,0,0,0,0]
    def move(right):
        a.string('nativeAction'); a.variable('AudioNativeRowIds'); a.variable('currentSelection'); a.op(0x4e)
        a.integer(2); a.op(0x0c); a.integer(101 if right else 100); a.op(0x0a); a.op(0x1d)
        a.integer(0); a.variable('_root'); a.member('_screen'); a.string('SaveAudioSettings'); a.op(0x52); a.op(0x17)
    a.function('moveLeft', lambda:move(False)); a.function('moveRight', lambda:move(True))
    def getter():
        a.variable('nativeAction'); a.integer(1); a.variable('_level0'); a.member('screen')
        a.string('SetSafeString'); a.op(0x52); a.op(0x17); a.assign('nativeAction', 0)
    a.function('getNativeAction', getter)
    def set_label():
        def clip():a.variable('_root');a.member('AudioMenu')
        # The wrapper exists once initializeButtons has attached it.
        clip();a.member('btn_mastervolume');a.op(0x12);missing=a.jump(0x9d)
        clip();a.member('btn_mastervolume');a.string('variable_text');a.string('');a.op(0x4f)
        clip();a.member('btn_mastervolume');a.string('text_str');a.push(4,1);a.op(0x4f)
        clip();a.member('btn_mastervolume');a.member('DynamicText_mc');a.member('dynamicText');a.string('variable');a.string('');a.op(0x4f)
        clip();a.member('btn_mastervolume');a.member('DynamicText_mc');a.member('dynamicText');a.string('text');a.push(4,1);a.op(0x4f)
        a.target(missing)
        clip();a.member('text_mastervolume');a.member('text_entry');a.string('text');a.push(4,1);a.op(0x4f)
    a.function('set_mastervolume', set_label, parameter=True)
    a.array('AudioNativeRowIds', row_ids)
    a.array('MenuItemButtons', ['btn_mastervolume','btn_Voice','btn_Music','btn_Effects','btn_Chatter','btn_Subtitles'])
    a.array('TextRefs', ['text_mastervolume','text_voice','text_music','text_effects','text_chatter','text_subtitles'])
    a.array('MenuItemIds', ['MasterVolume','Voice','Music','Effects','Chatter','Subtitles'])
    a.array('Gizmos', ['','voiceSlider','musicSlider','effectsSlider','chatterSlider','subtitlesSelector'])
    a.array('ColorRefs', ['','voiceColorRef','musicColorRef','effectsColorRef','chatterColorRef',''])
    a.array('GizmoTypes', ['','slider','slider','slider','slider','selector'])
    a.method('_root','initializeButtons')
    a.assign('currentSelection', 0); a.assign('nativeAction', 0)
    a.finish()

    # Clone the Voice label chain: its edit text without the $FE_Voice
    # localization binding (which would overwrite the native label), its
    # wrapper sprite and the row placement.
    root = 0x5F8; first_id = u(apt, root+20)
    text_id = first_id; sprite_id = first_id+1
    text_at = len(apt); text = bytearray(apt[VOICE_TEXT:VOICE_TEXT+60])
    put(text, 52, text_at+60); put(text, 56, text_at+61+len(MASTER_DEFAULT))
    apt.extend(text); apt.extend(MASTER_DEFAULT.encode('ascii')+b'\0\0'); apt.extend(b'\0'*(-len(apt)%4))
    inner_at = len(apt); inner = bytearray(apt[VOICE_INNER:VOICE_INNER+64]); put(inner, 12, text_id); apt.extend(inner)
    controls_at = len(apt); apt.extend(struct.pack('>I', inner_at))
    frames_at = len(apt); apt.extend(struct.pack('>II', 1, controls_at))
    sprite_at = len(apt); sprite = bytearray(apt[VOICE_SPRITE:VOICE_SPRITE+20]); put(sprite, 12, frames_at); apt.extend(sprite)
    item_at = len(apt); item = bytearray(apt[VOICE_ITEM:VOICE_ITEM+64])
    put(item, 8, MASTER_DEPTH); put(item, 12, sprite_id); put(item, 52, item_at+64)
    struct.pack_into('>f', item, 36, MASTER_Y)
    apt.extend(item); apt.extend(b'text_mastervolume\0'); apt.extend(b'\0'*(-len(apt)%4))
    character_table = len(apt); old_table = u(apt, root+24)
    apt.extend(apt[old_table:old_table+first_id*4])
    apt.extend(struct.pack('>II', text_at, sprite_at))
    put(apt, root+20, first_id+2); put(apt, root+24, character_table)
    for at,dy in ROW_SHIFTS.items():
        struct.pack_into('>f', apt, at+36, struct.unpack_from('>f', apt, at+36)[0]+dy)
    frames = u(apt, MENU+12); count = u(apt, frames); table = u(apt, frames+4)
    controls = [u(apt, table+4*i) for i in range(count)]
    new_table = len(apt)
    for at in controls+[item_at]: apt.extend(struct.pack('>I', at))
    put(apt, frames, count+1); put(apt, frames+4, new_table); put(apt, INIT_CONTROL+4, stream)
    rebuilt = [(tag, bytes(apt) if tag == b'apti' else bytes(cons) if tag == b'cons' else data) for tag,data in chunks]
    return rebuild_ui(original, rebuilt)
