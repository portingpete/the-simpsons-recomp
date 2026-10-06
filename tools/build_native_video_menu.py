"""Extend the original Options APT timeline with native video rows.

Produces a separate frontend package; never writes to the retail assets.
The existing AVM, font, button animation and screen navigation render the rows.
"""
from pathlib import Path
import argparse, hashlib, struct
from inspect_assets import stoc_entries, decode_entry, resource_chunks, align, padded_string

ROOT = Path(__file__).resolve().parents[1]
OPTIONS_SHA = '1d0f8d2fc3a72b2f676932976eae422c2aa10dfabd6226607f64671b6f7725ad'
PACKAGES = ('frontend/frontend.str', 'simpsons_chars/simpsons_chars_global.str')
NATIVE_ROWS = ('resolution','windowsize','windowmode','vsync','framecap','filtering','antialiasing',
               'fov','renderscale','bloom','depthoffield','motionblur','atmosphericfog','colorgrading','cinematicbars')
ROW_IDS = ('Resolution','WindowSize','WindowMode','VSync','FrameRate','Filtering','Antialiasing',
           'FieldOfView','RenderScale','Bloom','DepthOfField','MotionBlur','AtmosphericFog','ColorGrading','CinematicBars')
VIDEO_PAGES = (('page','brightness',*NATIVE_ROWS[:7]),('page',*NATIVE_ROWS[7:]))
PAGE_ROW_ID = len(NATIVE_ROWS)+1
ROW_FONT_SIZE, ROW_TOP, ROW_SPACING, FIRST_ROW_GAP = 20.0, -115.0, 24.0, 45.0

def row_y(row):
    if row=='page':return ROW_TOP
    if row=='brightness':return ROW_TOP+35
    page=0 if row in NATIVE_ROWS[:7] else 1
    index=VIDEO_PAGES[page].index(row)
    return ROW_TOP+35+(FIRST_ROW_GAP if page==0 else 0)+ROW_SPACING*(index-(2 if page==0 else 1))

def u(data, at): return struct.unpack_from('>I', data, at)[0]
def put(data, at, value): struct.pack_into('>I', data, at, value)

class Actions:
    def __init__(self, apt, cons):
        self.apt = apt
        self.cons = cons
        self.strings = []
        self.pools = []
        self.params = []
        self.values = []
        self.original_count = u(cons,24)
    def aligned(self):
        self.apt.extend(b'\0' * (-len(self.apt) % 4))
    def op(self, value): self.apt.append(value)
    def string(self, value):
        self.push(1,value)
    def push(self, kind, value):
        entry=(kind,value)
        # The Xbox relocation pass consumes each constant slot once. Every
        # Push needs its own entry, including repeated strings and integers.
        self.values.append(entry)
        self.op(0x96); self.aligned(); self.apt.extend(struct.pack('>I',1))
        self.pools.append((len(self.apt),self.original_count+len(self.values)-1))
        self.apt.extend(b'\0'*4)
    def integer(self, value):
        self.push(7,value)
    def variable(self, value): self.string(value); self.op(0x1c)
    def member(self, value): self.string(value); self.op(0x4e)
    def assign(self, name, value):
        self.string(name); self.integer(value); self.op(0x1d)
    def array(self, name, values):
        self.string(name)
        for value in reversed(values):
            self.string(value) if isinstance(value,str) else self.integer(value)
        self.integer(len(values)); self.string('Array'); self.op(0x40); self.op(0x1d)
    def jump(self,op=0x99):
        self.op(op);self.aligned();at=len(self.apt);self.apt.extend(b'\0'*4);return at
    def target(self,at):put(self.apt,at,(len(self.apt)-at-4)&0xffffffff)
    def method(self, receiver, method, count=0):
        self.integer(count); self.variable(receiver); self.string(method); self.op(0x52); self.op(0x17)
    def function(self, name, body, parameter=False):
        self.op(0x8e); self.aligned()
        self.strings.append((len(self.apt), name))
        if parameter:self.params.append(len(self.apt)+12)
        self.apt.extend(struct.pack('>7I', 0, int(parameter), 0x0002002a if parameter else 0x0002006a, 0, 0, 0x98765432, 0x12345678))
        size_at=len(self.apt)-12; begin=len(self.apt)
        body()
        put(self.apt, size_at, len(self.apt)-begin)
    def finish(self):
        self.op(0); self.aligned()
        for at,index in self.pools:
            put(self.apt,at,len(self.apt)); self.apt.extend(struct.pack('>I',index))
        for at in self.params:
            put(self.apt,at,len(self.apt));self.apt.extend(struct.pack('>I',1))
            self.strings.append((len(self.apt),'label'));self.apt.extend(b'\0'*4)
        for at, value in self.strings:
            put(self.apt, at, len(self.apt))
            self.apt.extend(value.encode('ascii')+b'\0'); self.aligned()
        table=u(self.cons,28);end=table+self.original_count*8
        entries=bytearray(self.cons[table:end]);old_strings=self.cons[end:]
        extra=bytearray();shift=len(self.values)*8
        for i in range(self.original_count):
            if u(entries,i*8)==1:put(entries,i*8+4,u(entries,i*8+4)+shift)
        for kind,value in self.values:
            if kind==1:
                pointer=32+(self.original_count+len(self.values))*8+len(old_strings)+len(extra)
                extra.extend(value.encode('ascii')+b'\0');extra.extend(b'\0'*(-len(extra)%4));value=pointer
            entries.extend(struct.pack('>II',kind,value))
        header=bytearray(self.cons[:32]);put(header,24,self.original_count+len(self.values));put(header,28,32)
        self.cons[:]=header+entries+old_strings+extra

def patch_options(original):
    if hashlib.sha256(original).hexdigest() != OPTIONS_SHA:
        raise ValueError('Unsupported Options UIX identity')
    apt=bytearray(original[0x20:0xc4d2])
    cons=bytearray(original[0xc4f0:0x129b1])
    # Byte-pinned original VideoMenu: sprite 65, two frames. Preserve all other
    # sprites, dictionaries, audio/controls menus and imports.
    if apt[0xe00:0xe14].hex() != '0000000509876543000000020000164400000000':
        raise ValueError('Original VideoMenu layout changed')
    frames=u(apt,0xe0c); count=u(apt,frames); table=u(apt,frames+4)
    old_controls=[u(apt,table+4*i) for i in range(count)]
    # Retail Xbox Options unconditionally removes Video. Skip only that push;
    # keep the original frontend/pause Credits policy and menu initialization.
    apt.extend(b'\0'*(-len(apt)%4)); options_stream=len(apt)
    apt.extend(apt[0x74c0:0x7586]); apt.extend(apt[0x759e:0x7612])
    put(apt,0x23c8,options_stream)
    # Keep original method definitions; rebuild its configuration arrays with
    # all rows before original initializeButtons executes.
    apt.extend(b'\0'*(-len(apt)%4)); stream=len(apt)
    apt.extend(apt[0x7da8:0x7f3e])
    a=Actions(apt,cons)
    def move(right):
        a.string('nativeAction'); a.variable('NativeRowIds'); a.variable('currentSelection'); a.op(0x4e); a.integer(2); a.op(0x0c)
        a.integer(101 if right else 100); a.op(0x0a); a.op(0x1d)
        a.integer(0); a.variable('_root'); a.member('_screen'); a.string('SaveVideoSettings'); a.op(0x52); a.op(0x17)
    a.function('moveLeft',lambda:move(False)); a.function('moveRight',lambda:move(True))
    def getter():
        a.variable('nativeAction'); a.integer(1); a.variable('_level0'); a.member('screen')
        a.string('SetSafeString'); a.op(0x52); a.op(0x17); a.assign('nativeAction',0)
    a.function('getNativeAction',getter)
    rows=['brightness',*NATIVE_ROWS,'page']
    def video_clip():a.variable('_root');a.member('VideoMenu')
    def video_method(name):
        a.integer(0);video_clip();a.string(name);a.op(0x52);a.op(0x17)
    def clip_property(name,property,value):
        video_clip();a.member(name);a.string(property);a.integer(value);a.op(0x4f)
    def set_label(row):
        # Inactive pages have only their source text; initializeButtons creates
        # the selected page's wrappers at the original display depths.
        video_clip();a.member('btn_'+row);a.op(0x12);missing=a.jump(0x9d)
        a.variable('_root');a.member('VideoMenu');a.member('btn_'+row);a.string('variable_text');a.string('');a.op(0x4f)
        a.variable('_root');a.member('VideoMenu');a.member('btn_'+row);a.string('text_str');a.push(4,1);a.op(0x4f)
        a.variable('_root');a.member('VideoMenu');a.member('btn_'+row);a.member('DynamicText_mc');a.member('dynamicText');a.string('variable');a.string('');a.op(0x4f)
        a.variable('_root');a.member('VideoMenu');a.member('btn_'+row);a.member('DynamicText_mc');a.member('dynamicText');a.string('text');a.push(4,1);a.op(0x4f)
        a.target(missing)
        a.variable('_root');a.member('VideoMenu');a.member('text_'+row);a.member('text_entry');a.string('text');a.push(4,1);a.op(0x4f)
    for row in rows[1:]:a.function('set_'+row,lambda row=row:set_label(row),parameter=True)
    def layout_page():
        for row in rows:
            video_clip();a.member('btn_'+row);a.op(0x12);missing=a.jump(0x9d)
            clip_property('btn_'+row,'_visible',0);a.target(missing)
            clip_property('text_'+row,'_visible',0)
        a.variable('nativePage');effects=a.jump(0x9d)
        def configure(page):
            names=VIDEO_PAGES[page]
            a.array('MenuItemButtons',['btn_'+row for row in names])
            a.array('TextRefs',['text_'+row for row in names])
            a.array('MenuItemIds',['NativeVideoPage' if row=='page' else 'Brightness' if row=='brightness' else ROW_IDS[NATIVE_ROWS.index(row)] for row in names])
            a.array('NativeRowIds',[PAGE_ROW_ID if row=='page' else 0 if row=='brightness' else NATIVE_ROWS.index(row)+1 for row in names])
            a.array('Gizmos',['','brightnessSlider'] if page==0 else [])
            a.array('ColorRefs',['','brightnessColorRef'] if page==0 else [])
            a.array('GizmoTypes',['','slider'] if page==0 else [])
            clip_property('brightnessSlider','_visible',int(page==0))
            # The color source remains an authored clip; it is only hidden
            # on the page that has no brightness slider.
            clip_property('brightnessColorRef','_visible',int(page==0))
            a.string('nativePageLabel');a.string('Video page: '+('1/2 Display' if page==0 else '2/2 Effects')+' (Left/Right)');a.op(0x1d)
        configure(0);done=a.jump();a.target(effects);configure(1);a.target(done)
        a.method('_root','initializeButtons')
        a.variable('nativePageLabel');a.integer(1);video_clip();a.string('set_page');a.op(0x52);a.op(0x17)
    a.function('layoutNativePage',layout_page)
    def next_page():
        a.string('nativePage');a.integer(1);a.variable('nativePage');a.op(0x0b);a.op(0x1d)
        video_method('layoutNativePage')
        a.method('_root','activateGizmos')
        a.string('rememberedSelection');a.variable('_root');a.member('InitialSelection');a.op(0x1d)
        a.variable('_root');a.string('InitialSelection');a.string('');a.op(0x4f)
        a.string('currentSelection');a.integer(0);a.integer(1);a.variable('_root');a.string('activateMenuButtons');a.op(0x52);a.op(0x1d)
        a.variable('_root');a.string('InitialSelection');a.variable('rememberedSelection');a.op(0x4f)
    a.function('nextPage',next_page)
    a.assign('nativeAction',0);a.assign('nativePage',0);video_method('layoutNativePage');a.assign('currentSelection',0)
    a.finish()
    # Original text movie, font and transform. Separate depths and names make
    # these normal display-list members of VideoMenu, not a drawing overlay.
    additions=[];characters=[]
    defaults=['Render resolution: 1280x720 (restart)','Window size: 1280 x 720','Display mode: Windowed','VSync: Off','Frame limit: 120 FPS','Texture filtering: Original (restart)','Antialiasing: Original (restart)',
              'FOV (16:9): Original','Render scale: 100% (restart)','Bloom: On','Depth of field: On','Motion blur: On',
              'Atmospheric fog: On','Color grading: On','Cinematic bars: On','Video page: 1/2 Display (Left/Right)']
    assert len(defaults)==len(NATIVE_ROWS)+1==len(ROW_IDS)+1
    for i,row in enumerate(rows[1:],1):
        # Give each row its own text character. The retail character is bound
        # to the localization variable $FE_Brightness, which would overwrite
        # every clone even after setting the button's text member.
        text_id=92+(i-1)*2;text_at=len(apt);text=bytearray(apt[0xdb0:0xdec])
        struct.pack_into('>f',text,36,ROW_FONT_SIZE)
        put(text,52,text_at+60);put(text,56,text_at+60+len(defaults[i-1])+1)
        apt.extend(text);apt.extend(defaults[i-1].encode()+b'\0\0');apt.extend(b'\0'*(-len(apt)%4));characters.append(text_at)
        place_at=len(apt);place=bytearray(apt[0x38dc:0x391c]);put(place,12,text_id);apt.extend(place)
        controls_at=len(apt);apt.extend(struct.pack('>I',place_at))
        frames_at=len(apt);apt.extend(struct.pack('>II',1,controls_at))
        sprite_at=len(apt);sprite=bytearray(apt[0xdec:0xe00]);put(sprite,12,frames_at);apt.extend(sprite);characters.append(sprite_at)
        at=len(apt); item=bytearray(apt[0x3aa4:0x3ae4])
        put(item,8,30+i);put(item,12,text_id+1); put(item,52,at+64)
        struct.pack_into('>f',item,36,row_y(row))
        apt.extend(item); apt.extend(('text_'+row).encode()+b'\0'); apt.extend(b'\0'*(-len(apt)%4)); additions.append(at)
    character_table=len(apt);old_character_table=u(apt,0x5f8+24)
    apt.extend(apt[old_character_table:old_character_table+92*4])
    for at in characters:apt.extend(struct.pack('>I',at))
    put(apt,0x5f8+20,92+len(characters));put(apt,0x5f8+24,character_table)
    # Brightness follows the page row on Display. Its original slider and
    # reference move with the label; the original button layout stays in use.
    for at,y in ((0x3a24,row_y('brightness')+10),(0x3a64,row_y('brightness')+10),(0x3aa4,row_y('brightness'))): struct.pack_into('>f',apt,at+36,y)
    struct.pack_into('>f',apt,0xdb0+36,ROW_FONT_SIZE)
    put(apt,old_controls[0]+4,stream)
    new_table=len(apt)
    for at in old_controls+additions: apt.extend(struct.pack('>I',at))
    put(apt,frames,count+len(additions)); put(apt,frames+4,new_table)
    payload=bytearray(original[:0x18])
    payload.extend(b'apti'+struct.pack('>I',len(apt)+8)+apt)
    payload.extend(b'alig'+struct.pack('>I',8+(-len(payload)-8)%32)+b'\0'*((-len(payload)-8)%32))
    payload.extend(b'cons'+struct.pack('>I',len(cons)+8)+cons)
    padding=(-len(payload)-8)%32
    payload.extend(b'alig'+struct.pack('>I',padding+8)+b'\0'*padding)
    payload.extend(original[0x129c8:])
    put(payload,4,len(payload))
    return bytes(payload)

def build(source):
    # Import lazily: the mouse assembler shares the byte/constant writer above.
    from build_native_mouse_menu import MENU_NAMES, patch_mouse
    from build_native_control_menu import patch_controls
    from build_native_main_menu import patch_main_menu
    data=source.read_bytes(); entries=stoc_entries(data)['entries']; output=bytearray(data[:entries[0]['file_offset']])
    found=False
    for e in entries:
        decoded,_=decode_entry(data,e)
        modified=False
        # Several UIX resources can share one decoded entry. Patch from the
        # back so each original descriptor/payload offset stays valid.
        for c in reversed(list(resource_chunks(decoded))):
            if c.get('name') in MENU_NAMES:
                at=c['payload_decoded_offset']; size=c['payload_size']; original=decoded[at:at+size]
                if c['name']=='options.swf':original=patch_controls(patch_options(original))
                elif c['name']=='frontend.swf':original=patch_main_menu(original)
                patch=patch_mouse(original)
                end=at+size; new=bytearray(decoded[:at]+patch+b'\0'*(-len(patch)%4)+decoded[align(end,4):])
                desc=c['decoded_offset']+16
                _,pos=padded_string(decoded,desc)
                pos+=16
                for _ in range(3): _,pos=padded_string(decoded,pos)
                put(new,pos+4,len(patch))
                struct.pack_into('<I',new,c['decoded_offset']+4,4+c['descriptor_size']+align(len(patch),4))
                decoded=bytes(new); found=modified=True
        if modified:
            # Raw SToc rows have allocation = stored extent, and no in-place
            # RefPack offset. Preserve every unrelated entry's encoded bytes.
            stored=align(len(decoded),2048); table=e['table_offset']
            put(output,table+4,0x0eac15c8); put(output,table+8,len(decoded))
            put(output,table+12,stored); put(output,table+16,stored); put(output,table+20,0)
            output.extend(decoded); output.extend(b'\0'*(stored-len(decoded)))
        else:
            output.extend(data[e['file_offset']:e['file_offset']+e['stored_size']])
    if not found: raise ValueError('Original Options resource missing')
    stoc_entries(output)
    return bytes(output)

def main():
    p=argparse.ArgumentParser(); p.add_argument('--output-root',type=Path,required=True); args=p.parse_args()
    for package in PACKAGES:
        output=build(ROOT/'Simpsons Game, The (USA)'/package)
        target=args.output_root/package
        target.parent.mkdir(parents=True,exist_ok=True)
        if not target.exists() or target.read_bytes()!=output: target.write_bytes(output)
        print('Native Video menu package:',target)
if __name__=='__main__':main()
