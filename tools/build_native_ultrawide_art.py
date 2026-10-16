"""Ultrawide frontend art: let the frontend scenes continue past the 16:9 stage.

On wide outputs the renderer draws frontend Apt UI past the 16:9 stage at the
stage's own scale (EngineDriver::setUiDrawing overscan; nothing is stretched).
Most retail art already continues there: the title clouds and the opening
living room were authored wider than the stage. This fills what 16:9 never
needed:

* SharedLibrary.BgndTV, the TV cabinet every frontend menu sits in, gains the
  living-room wall around it. Its rectangles, colors and trim lines are the
  opening scene's own (the final living-room sprite in simpsons_start), mapped
  into BgndTV's local space, so the cut from the opening zoom to the menus is
  seamless. At 16:9 the cabinet covers all of it.
* The opening scene draws its own room, so it imports an unchanged copy of the
  cabinet (BgndTV_intro).
* Four title clouds whose textures were cut where the 16:9 stage ended get
  painted ends (paint_cloud_ends).
* The pause menu's transition: its vertical shutters reach past the widest
  view, and its TV studio's door frame continues as wall (patch_pause).

Apt layout notes (Xbox 360, big-endian, offsets relative to the apti chunk):
root movie +8/+12 frames, +20/+24 characters, +40/+44 imports (16 bytes:
movie, name, character), +48/+52 exports (8 bytes: name, character). Sprites
are 20 bytes (type 5, signature, frame count, frames). PlaceObject records are
64 bytes (+8 depth, +12 character, +16 a b c d, +32 tx ty). Shapes are 28 bytes
(type 1, signature, bounds, geometry id). The rubi chunk holds the shape
geometry: [id][size] then 0x38-byte parts [0x38][kind][RGBA][texture matrix
x6][texture][0][count, x0, y0, x1, y1, 0 as int16]; kind 2 is a solid color and
a count of -2 stores a rectangle in 1/16 units.
"""
from functools import lru_cache
import hashlib
import math
import struct

from build_native_mouse_menu import ui_chunks, rebuild_ui
from build_native_video_menu import ROOT, u, put

SIGNATURE = 0x09876543
BGNDTV_INTRO = 'BgndTV_intro'
# The opening's final living-room sprite and its cabinet placement.
ROOM_SPRITE, ROOM_TV_DEPTH = 81, 66
# The title timeline zooms the living room (sprite 77 at depth 3) in from
# frame 647; from 650 it covers the 16:9 view, and at 694 the final room
# (sprite 81) replaces it. Its floor and wall sprites, and the trim lines at
# depths 6 and 8, are widened to ROOM_SPAN (the 32:9 view needs -3250..4621).
TITLE_SPRITE, ROOM_ZOOM_SPRITE, ROOM_DEPTH, EXTENSION_DEPTH = 82, 77, 3, 2
ROOM_COVERS_16_9, ROOM_REPLACED = 650, 694
ROOM_FLOOR_SPRITE, ROOM_WALL_SPRITE = 62, 64
ROOM_EXTENDED = (1, 3, 6, 8)
ROOM_SPAN = (-3400.0, 4800.0)
# Scene layers in drawing order: floor, wall, window glass, then the 6 pt trim
# lines. Lines are the 104x24 strip of frontend_assets "Final frame-line- 6 pt"
# whose opaque black texels are rows 11..17.
ROOM_FILLS = (1, 3, 5)
ROOM_LINES = (6, 8, 10, 12, 14, 16, 18, 20)
LINE_STRIP, LINE_CORE = (0.0, 0.0, 104.0, 24.0), (11.0, 17.0)
LINE_IMPORT = ('..\\frontend_assets', 'Final frame-line- 6 pt')
BLACK = 0x000000FF
# 1/16-unit rectangles hold +-2047 BgndTV units: 32:9 needs +-1273.
LIMIT = 2040.0
# The menus draw their screen content behind the cabinet, through its
# transparent screen. BgndTV maps local (x, y) to texel (x+1024, y+512) of
# 9_SharedLibrary, whose opaque cabinet spans texels x 380..1840 from y 6 (18
# at the rounded corners) to the bottom of the quad. The wall stays outside,
# reaching 20 units under the cabinet's border, far from the screen opening.
CABINET_KEEPOUT = (380-1024+20, 6-512+20, 1841-1024-20, 512.0)


def f32(data, at): return struct.unpack_from('>f', data, at)[0]


def find_root(apt):
    roots = [at for at in range(16, len(apt)-36, 4) if u(apt, at) == 9 and u(apt, at+4) == SIGNATURE]
    if len(roots) != 1: raise ValueError('Ambiguous original Apt root movie')
    return roots[0]


def string_at(apt, at):
    end = apt.index(b'\0', at)
    return apt[at:end].decode('latin1')


def characters(apt, root):
    return [u(apt, u(apt, root+24)+4*i) for i in range(u(apt, root+20))]


def frame_placements(apt, sprite, frame=0):
    count, frames = u(apt, sprite+8), u(apt, sprite+12)
    if frame >= count: raise ValueError('Missing Apt frame')
    controls, table = u(apt, frames+8*frame), u(apt, frames+8*frame+4)
    out = {}
    for i in range(controls):
        at = u(apt, table+4*i)
        if u(apt, at) == 3: out[u(apt, at+8)] = at
    return out


def geometry(rubi):
    out, at = {}, 0
    while at < len(rubi):
        gid, size = u(rubi, at), u(rubi, at+4)
        if size < 8 or at+size > len(rubi) or gid in out: raise ValueError('Invalid Apt geometry chunk')
        out[gid] = rubi[at:at+size]; at += size
    return out


def solid_rect(record):
    """(color, (x0, y0, x1, y1)) of a one-part solid rectangle geometry record."""
    if u(record, 4) != 0x40 or u(record, 8) != 0x38 or u(record, 12) != 2 or any(record[20:52]):
        raise ValueError('Living-room fill is not a solid rectangle')
    count, *rect, tail = struct.unpack_from('>6h', record, 52)
    if count not in (2, -2) or tail: raise ValueError('Unsupported Apt rectangle encoding')
    scale = 16 if count < 0 else 32
    return u(record, 16), tuple(v/scale for v in rect)


def transform(matrix, rect):
    a, b, c, d, tx, ty = matrix
    if not (abs(b) < 1e-6 and abs(c) < 1e-6 or abs(a) < 1e-6 and abs(d) < 1e-6):
        raise ValueError('Living-room layer is not axis-aligned')
    xs, ys = [], []
    for x in (rect[0], rect[2]):
        for y in (rect[1], rect[3]):
            xs.append(a*x+c*y+tx); ys.append(b*x+d*y+ty)
    return min(xs), min(ys), max(xs), max(ys)


def matrix_of(apt, place):
    return tuple(f32(apt, place+16+4*i) for i in range(6))


@lru_cache(maxsize=None)
def living_room_wall():
    """The opening scene's wall layers as (RGBA, rect) in BgndTV local units."""
    payload = retail_resource('frontend/frontend.str', 'simpsons_start.swf')
    chunks = dict(ui_chunks(payload)); apt, shapes = chunks[b'apti'], geometry(chunks[b'rubi'])
    root = find_root(apt); chars = characters(apt, root)
    imports = {u(apt, at+8): (string_at(apt, u(apt, at)), string_at(apt, u(apt, at+4)))
               for at in range(u(apt, root+44), u(apt, root+44)+16*u(apt, root+40), 16)}
    room = chars[ROOM_SPRITE]
    if u(apt, room) != 5: raise ValueError('Opening living room sprite moved')
    places = frame_placements(apt, room)
    tv = places[ROOM_TV_DEPTH]
    if imports.get(u(apt, tv+12)) != ('SharedLibrary', 'BgndTV'): raise ValueError('Opening cabinet placement moved')
    a, b, c, d, tx, ty = matrix_of(apt, tv)
    if b or c or a <= 0 or abs(a-d) > 1e-6: raise ValueError('Opening cabinet is not uniformly scaled')

    def local(rect):
        x0, y0, x1, y1 = ((rect[0]-tx)/a, (rect[1]-ty)/a, (rect[2]-tx)/a, (rect[3]-ty)/a)
        return (max(x0, -LIMIT), max(y0, -LIMIT), min(x1, LIMIT), min(y1, LIMIT))

    def fill(cid):
        if u(apt, chars[cid]) == 1:
            return solid_rect(shapes[u(apt, chars[cid]+24)]), (1, 0, 0, 1, 0, 0)
        inner = frame_placements(apt, chars[cid])
        if len(inner) != 1: raise ValueError('Living-room fill sprite has several layers')
        child = next(iter(inner.values()))
        if matrix_of(apt, child) != (1, 0, 0, 1, 0, 0): raise ValueError('Living-room fill is offset')
        return fill(u(apt, child+12))

    layers = []
    for depth in ROOM_FILLS:
        place = places[depth]
        (color, rect), _ = fill(u(apt, place+12))
        layers.append((color, local(transform(matrix_of(apt, place), rect))))
    for depth in ROOM_LINES:
        place = places[depth]
        if imports.get(u(apt, place+12)) != LINE_IMPORT: raise ValueError('Living-room trim line moved')
        core = (LINE_STRIP[0], LINE_CORE[0], LINE_STRIP[2], LINE_CORE[1])
        layers.append((BLACK, local(transform(matrix_of(apt, place), core))))
    return tuple(layers)


@lru_cache(maxsize=None)
def retail_resource(package, name):
    from build_native_video_menu import stoc_entries, decode_entry, resource_chunks
    data = (ROOT/'Simpsons Game, The (USA)'/package).read_bytes()
    for entry in stoc_entries(data)['entries']:
        decoded, _ = decode_entry(data, entry)
        for chunk in resource_chunks(decoded):
            if chunk.get('name') == name:
                at = chunk['payload_decoded_offset']
                return bytes(decoded[at:at+chunk['payload_size']])
    raise ValueError(f'Retail {name} missing from {package}')


def align(apt):
    apt.extend(b'\0'*(-len(apt) % 4))
    return len(apt)


def outside(rect, hole):
    """rect minus hole, as up to four rectangles."""
    x0, y0, x1, y1 = rect; hx0, hy0, hx1, hy1 = hole
    pieces = [(x0, y0, min(x1, hx0), y1), (max(x0, hx1), y0, x1, y1),
              (max(x0, hx0), y0, min(x1, hx1), min(y1, hy0)), (max(x0, hx0), max(y0, hy1), min(x1, hx1), y1)]
    return [p for p in pieces if p[2]-p[0] > 0.01 and p[3]-p[1] > 0.01]


def rect_geometry(gid, color, rect):
    words = [round(v*16) for v in rect]
    if any(not -32768 <= v <= 32767 for v in words) or words[0] >= words[2] or words[1] >= words[3]:
        raise ValueError('Wall rectangle outside the 1/16-unit range')
    return struct.pack('>5I', gid, 0x40, 0x38, 2, color)+bytes(32)+struct.pack('>6h', -2, *words, 0)


def patch_shared_library(original):
    """Give BgndTV the living-room wall; keep the unchanged cabinet as BgndTV_intro."""
    chunks = ui_chunks(original)
    apt = bytearray(next(data for tag, data in chunks if tag == b'apti'))
    rubi = bytearray(next(data for tag, data in chunks if tag == b'rubi'))
    root = find_root(apt); size = len(apt)
    char_count, char_table = u(apt, root+20), u(apt, root+24)
    export_count, export_table = u(apt, root+48), u(apt, root+52)
    exports = [(string_at(apt, u(apt, export_table+8*i)), u(apt, export_table+8*i+4)) for i in range(export_count)]
    # The retail table repeats identical (name, character) pairs.
    tv_ids = {cid for name, cid in exports if name == 'BgndTV'}
    if len(tv_ids) != 1 or any(name == BGNDTV_INTRO for name, _ in exports):
        raise ValueError('Unexpected SharedLibrary BgndTV exports')
    tv = u(apt, char_table+4*tv_ids.pop())
    if u(apt, tv) != 5 or u(apt, tv+8) != 1: raise ValueError('BgndTV is not a one-frame sprite')
    cabinet = frame_placements(apt, tv)
    if list(cabinet) != [1]: raise ValueError('BgndTV no longer holds one cabinet layer')
    cabinet = cabinet[1]

    # One solid shape per wall piece, below a copy of the cabinet placement.
    layers = [(color, piece) for color, rect in living_room_wall() for piece in outside(rect, CABINET_KEEPOUT)]
    first = char_count; ids = list(range(first, first+len(layers)))
    shapes, placements = [], []
    for depth, (gid, (color, rect)) in enumerate(zip(ids, layers), start=1):
        rubi.extend(rect_geometry(gid, color, rect))
        at = align(apt); shapes.append(at)
        apt.extend(struct.pack('>2I4fI', 1, SIGNATURE, *rect, gid))
        at = align(apt); placements.append(at)
        place = bytearray(apt[cabinet:cabinet+64]); put(place, 8, depth); put(place, 12, gid)
        struct.pack_into('>6f', place, 16, 1, 0, 0, 1, 0, 0); apt.extend(place)
    at = align(apt); placements.append(at)
    place = bytearray(apt[cabinet:cabinet+64]); put(place, 8, len(layers)+1); apt.extend(place)
    controls = align(apt); apt.extend(struct.pack(f'>{len(placements)}I', *placements))
    frames = align(apt); apt.extend(struct.pack('>II', len(placements), controls))

    # The intro copy keeps the original frames; no frame data is shared, since
    # the loader relocates each sprite's structures in place.
    intro = align(apt); apt.extend(apt[tv:tv+20])
    put(apt, tv+12, frames)
    intro_id = first+len(layers)
    table = align(apt); apt.extend(apt[char_table:char_table+4*char_count])
    apt.extend(struct.pack(f'>{len(shapes)+1}I', *shapes, intro))
    put(apt, root+20, intro_id+1); put(apt, root+24, table)
    name = align(apt); apt.extend(BGNDTV_INTRO.encode('ascii')+b'\0')
    table = align(apt); apt.extend(apt[export_table:export_table+8*export_count])
    apt.extend(struct.pack('>II', name, intro_id))
    put(apt, root+48, export_count+1); put(apt, root+52, table)
    align(apt)
    if len(apt) <= size: raise AssertionError('SharedLibrary patch appended nothing')
    return rebuild_ui(original, [(tag, bytes(apt) if tag == b'apti' else bytes(rubi) if tag == b'rubi' else data)
                                 for tag, data in chunks])


def sprite_frames(apt, sprite):
    """[(control count, control table)] frame entries, by frame-table address."""
    frames = u(apt, sprite+12)
    return [frames+8*i for i in range(u(apt, sprite+8))]


def frame_controls(apt, entry):
    return [u(apt, u(apt, entry+4)+4*i) for i in range(u(apt, entry))]


def append_controls(apt, entry, records):
    """Append control records to a frame (the control table is copied)."""
    controls = frame_controls(apt, entry)
    for record in records:
        controls.append(align(apt)); apt.extend(record)
    table = align(apt); apt.extend(struct.pack(f'>{len(controls)}I', *controls))
    put(apt, entry, len(controls)); put(apt, entry+4, table)


def room_extension(apt, chars, shapes):
    """A sprite of the room's floor, wall and horizontal trim lines, widened."""
    room = chars[ROOM_ZOOM_SPRITE]
    places = frame_placements(apt, room)
    records = []
    for depth in ROOM_EXTENDED:
        place = places[depth]; child = u(apt, place+12)
        if child in (ROOM_FLOOR_SPRITE, ROOM_WALL_SPRITE):
            (inner,) = frame_placements(apt, chars[child]).values()
            _, (x0, _, x1, _) = solid_rect(shapes[u(apt, chars[u(apt, inner+12)]+24)])
        else:
            x0, _, x1, _ = LINE_STRIP
        a, b, c, d, tx, ty = matrix_of(apt, place)
        if b or c or a <= 0: raise ValueError('Room layer is not a horizontal span')
        record = bytearray(apt[place:place+64])
        scale = (ROOM_SPAN[1]-ROOM_SPAN[0])/(x1-x0)
        struct.pack_into('>2f', record, 16, scale, 0.0); struct.pack_into('>f', record, 32, ROOM_SPAN[0]-scale*x0)
        records.append(bytes(record))
    controls = []
    for record in records:
        controls.append(align(apt)); apt.extend(record)
    table = align(apt); apt.extend(struct.pack(f'>{len(controls)}I', *controls))
    frames = align(apt); apt.extend(struct.pack('>II', len(controls), table))
    sprite = align(apt); apt.extend(apt[room:room+20]); put(apt, sprite+8, 1); put(apt, sprite+12, frames)
    return sprite


def patch_start(original):
    """The opening imports the unchanged cabinet, and its room zoom is widened.

    The living room flies in as a box. From frame ROOM_COVERS_16_9 the box
    covers the 16:9 view; on wider outputs its edges would still show, so a
    wider copy of its floor, wall and trim lines follows it one depth below
    until the final room replaces it. 16:9 frames are unchanged.
    """
    chunks = ui_chunks(original)
    apt = bytearray(next(data for tag, data in chunks if tag == b'apti'))
    shapes = geometry(next(data for tag, data in chunks if tag == b'rubi'))
    root = find_root(apt)
    imports = [at for at in range(u(apt, root+44), u(apt, root+44)+16*u(apt, root+40), 16)
               if (string_at(apt, u(apt, at)), string_at(apt, u(apt, at+4))) == ('SharedLibrary', 'BgndTV')]
    if len(imports) != 1: raise ValueError('Opening BgndTV import moved')
    name = align(apt); apt.extend(BGNDTV_INTRO.encode('ascii')+b'\0'); align(apt)
    put(apt, imports[0]+4, name)

    chars = characters(apt, root); count = len(chars)
    timeline = chars[TITLE_SPRITE]
    if u(apt, timeline) != 5: raise ValueError('Opening timeline sprite moved')
    entries = sprite_frames(apt, timeline)
    zoom = {}
    for frame in range(ROOM_COVERS_16_9, ROOM_REPLACED):
        moves = [at for at in frame_controls(apt, entries[frame]) if u(apt, at) == 3 and u(apt, at+8) == ROOM_DEPTH]
        if len(moves) != 1 or u(apt, moves[0]+4) != 5: raise ValueError('Opening room zoom moved')
        zoom[frame] = moves[0]
    if any(u(apt, at) == 3 and u(apt, at+8) == EXTENSION_DEPTH
           for entry in entries for at in frame_controls(apt, entry)):
        raise ValueError('Opening depth for the room extension is in use')
    extension = room_extension(apt, chars, shapes)
    for frame, move in zoom.items():
        record = bytearray(apt[move:move+64]); put(record, 8, EXTENSION_DEPTH)
        if frame == ROOM_COVERS_16_9: put(record, 4, 6); put(record, 12, count)  # Place, then move.
        append_controls(apt, entries[frame], [bytes(record)])
    append_controls(apt, entries[ROOM_REPLACED], [struct.pack('>II', 4, EXTENSION_DEPTH)])
    table = align(apt); apt.extend(apt[u(apt, root+24):u(apt, root+24)+4*count]); apt.extend(struct.pack('>I', extension))
    put(apt, root+20, count+1); put(apt, root+24, table)
    align(apt)
    return rebuild_ui(original, [(tag, bytes(apt) if tag == b'apti' else data) for tag, data in chunks])


# The pause menu plays over the paused game. Its PauseMenu timeline (sprite 73,
# centered on the stage) first closes shutters: horizontal bars at depths 3 and
# 5 span the canvas, and the renderer spans them across wide outputs like the
# full-screen fills (isFullWidthUiBand). Vertical bars at depths 7 and 9 (sprite
# 7, a 60-unit thick bar turned on its side) then close the slit between them
# from the 16:9 edges inward; their outer ends move out to SHUTTER_REACH, past
# the 32:9 view (+-853), and their inner ends keep their motion. The timeline
# then opens on a TV studio (sprite 10 at 0.67, frames 22..128) that ends where
# 16:9 does, at the flat outer panels of its blue door frame. Wider outputs see
# them continue as wall out to STUDIO_SPAN, one depth below the studio art.
# The studio's TV (sprites 40, 41, 46 and 72) zooms from full screen into the
# studio and back into the menu; zoomed out, BgndTV's living-room wall would
# enter the 16:9 view, so these import the unchanged cabinet and the studio
# surrounds them. The root's own static BgndTV (another screen) keeps the wall.
PAUSE_TIMELINE, PAUSE_SHUTTERS, SHUTTER_SPRITE = 73, (7, 9), 7
PAUSE_TVS = (40, 41, 46, 72)
SHUTTER_REACH = 1000.0
PAUSE_CENTER = (320.0, 240.0)  # Where the root places the timeline: the stage center.
VIEW_HALF_16_9 = 1280.0/3     # Half the 16:9 view, in stage units.
STUDIO_SPRITE, STUDIO_SHAPE, STUDIO_DEPTH, STUDIO_WALL_DEPTHS = 10, 9, 5, (1, 2)
STUDIO_TEXTURE = ('simpsons_chars/simpsons_chars_global.str', 'simpsons_chars_global_split3.itxd', '8_pause')
STUDIO_SPAN = 1400.0  # The 32:9 view needs +-1274 at the studio's 0.67 scale.
STUDIO_OVERLAP = 4.0  # Units the wall reaches under the panels' filtered edge.
PANEL_COLUMNS = 8     # Texels of each panel row sampled for its color.
PANEL_TOLERANCE = 8   # Per-channel spread of the panels' BC2-dithered color.
PANEL_FLAT = 0.95     # Share of samples within it (the floor line meets a corner).


def textured_parts(record):
    """[(texture matrix, [(x, y)])] of a shape's textured parts.

    A part's geometry is a run of int16 groups [count, x, y, ...] ended by 0 or
    the part's end: a count of +-2 holds a rectangle's two corners, larger
    counts a triangle strip; negative counts store 1/16 units, positive 1/32.
    The texture matrix maps local (x, y) to texels: (a x + b y + tx, c x + d y + ty).
    """
    out, at = [], 8
    while at < len(record):
        size, kind = u(record, at), u(record, at+4)
        if size < 0x38 or at+size > len(record): raise ValueError('Invalid Apt shape part')
        words, points, i = struct.unpack_from(f'>{(size-44)//2}h', record, at+44), [], 0
        while i < len(words) and words[i]:
            count = abs(words[i]); scale = 16 if words[i] < 0 else 32
            group = words[i+1:i+1+2*count]
            if len(group) != 2*count: raise ValueError('Truncated Apt shape part')
            points += [(group[j]/scale, group[j+1]/scale) for j in range(0, 2*count, 2)]
            if count == 2: points += [(points[-2][0], points[-1][1]), (points[-1][0], points[-2][1])]
            i += 1+2*count
        if kind == 1: out.append((struct.unpack_from('>6f', record, at+12), points))
        at += size
    return out


def studio_wall(apt, chars, shapes):
    """(RGBA, [left, right]) wall rectangles beside the studio, in sprite 10 units.

    The door frame's outer panels are the studio shape's outermost textured
    parts. Their color, and the local x of their opaque edge and y of their
    opaque rows, are read from the studio texture.
    """
    places = frame_placements(apt, chars[STUDIO_SPRITE])
    if list(places) != [STUDIO_DEPTH] or u(apt, places[STUDIO_DEPTH]+12) != STUDIO_SHAPE or \
            matrix_of(apt, places[STUDIO_DEPTH]) != (1, 0, 0, 1, 0, 0):
        raise ValueError('Pause studio sprite moved')
    parts = textured_parts(shapes[u(apt, chars[STUDIO_SHAPE]+24)])
    package, dictionary, name = STUDIO_TEXTURE
    payload = retail_resource(package, dictionary); texture = itxd_texture(payload, name)
    samples, rects = [], []
    for side in (-1, 1):
        reach = max(side*x for _, points in parts for x, _ in points)
        edges, extent = [], []
        for (a, b, tx, c, d, ty), points in parts:
            outer = [y for x, y in points if side*x >= reach-1]  # The part's outer edge.
            if not outer: continue
            if b or c or a <= 0 or d <= 0: raise ValueError('Studio panel texture is not axis-aligned')
            y0, y1 = min(outer), max(outer); xs = [x for x, _ in points]
            v0, v1 = math.floor(d*y0+ty), math.ceil(d*y1+ty)
            wx0, wx1 = int(a*min(xs)+tx)//4*4, -(-int(a*max(xs)+tx+1)//4)*4
            rows = read_pixels(payload, texture, wx0, v0//4*4, wx1, -(-v1//4)*4)[v0 % 4:v0 % 4+v1-v0]
            for v, row in enumerate(rows, start=v0):
                columns = range(len(row)) if side < 0 else range(len(row)-1, -1, -1)
                first = next((i for i in columns if row[i][3] == 255), None)
                if first is None: continue  # Rows past the art's opaque edge.
                panel = row[first:first+PANEL_COLUMNS] if side < 0 else row[first-PANEL_COLUMNS+1:first+1]
                samples += [tuple(p) for p in panel]
                edges.append((wx0+first+(side > 0)-tx)/a)
                extent += [max(y0, (v-ty)/d), min(y1, (v+1-ty)/d)]
        if not edges or max(edges)-min(edges) > 0.5: raise ValueError('Studio door frame edge is not straight')
        edge, top, bottom = (min if side < 0 else max)(edges), min(extent), max(extent)
        rects.append((-STUDIO_SPAN, top, edge+STUDIO_OVERLAP, bottom) if side < 0 else
                     (edge-STUDIO_OVERLAP, top, STUDIO_SPAN, bottom))
    mode = max(set(samples), key=samples.count)
    flat = [p for p in samples if p[3] == 255 and max(abs(p[i]-mode[i]) for i in range(3)) <= PANEL_TOLERANCE]
    if len(flat) < PANEL_FLAT*len(samples): raise ValueError('Studio door frame panels are not one flat color')
    r, g, b = (round(sum(p[i] for p in flat)/len(flat)) for i in range(3))
    return (r << 24 | g << 16 | b << 8 | 255), rects


def widen_shutters(apt, root, chars):
    """Move the vertical shutters' outer ends out to SHUTTER_REACH."""
    if [matrix_of(apt, at) for at in frame_placements(apt, root).values() if u(apt, at+12) == PAUSE_TIMELINE] != \
            [(1, 0, 0, 1, *PAUSE_CENTER)]:
        raise ValueError('Pause timeline is not placed at the stage center')
    (bar,) = frame_placements(apt, chars[SHUTTER_SPRITE]).values()
    if matrix_of(apt, bar) != (1, 0, 0, 1, 0, 0): raise ValueError('Pause shutter bar is offset')
    bar = chars[u(apt, bar+12)]
    _, y0, _, y1 = (f32(apt, bar+8+4*i) for i in range(4))
    # Studio props reuse the shutters' depths: follow what each depth holds.
    records, held = set(), {}
    for entry in sprite_frames(apt, chars[PAUSE_TIMELINE]):
        for at in frame_controls(apt, entry):
            if u(apt, at) == 4: held.pop(u(apt, at+4), None)
            if u(apt, at) != 3: continue
            depth, flags = u(apt, at+8), u(apt, at+4)
            if flags & 2: held[depth] = u(apt, at+12)
            if depth in PAUSE_SHUTTERS and flags & 4 and held.get(depth) == SHUTTER_SPRITE: records.add(at)
    for at in records:
        a, b, c, d, tx, ty = matrix_of(apt, at)
        if a or d: raise ValueError('Pause shutter is not a vertical bar')
        ends = (c*y0+tx, c*y1+tx)  # x of the bar's two long edges
        outer = max((0, 1), key=lambda i: abs(ends[i]))
        if abs(ends[outer]) < VIEW_HALF_16_9: raise ValueError('Pause shutter ends inside the 16:9 view')
        target = list(ends); target[outer] = SHUTTER_REACH if ends[outer] > 0 else -SHUTTER_REACH
        c = (target[1]-target[0])/(y1-y0); tx = target[0]-c*y0
        struct.pack_into('>f', apt, at+24, c); struct.pack_into('>f', apt, at+32, tx)
    if not records: raise ValueError('Pause shutters missing')


def import_intro_cabinet(apt, root, chars, intro):
    """Import BgndTV_intro as character `intro` and place it in the pause TVs.

    Returns the root's own BgndTV placement, the menu TV.
    """
    imports = [u(apt, root+44)+16*i for i in range(u(apt, root+40))]
    tv = [at for at in imports if (string_at(apt, u(apt, at)), string_at(apt, u(apt, at+4))) == ('SharedLibrary', 'BgndTV')]
    if len(tv) != 1: raise ValueError('Pause BgndTV import moved')
    cabinet = u(apt, tv[0]+8)

    def placements(sprite):
        places = [at for entry in sprite_frames(apt, sprite) for at in frame_controls(apt, entry)
                  if u(apt, at) == 3 and u(apt, at+4) & 2 and u(apt, at+12) == cabinet]
        if len(places) != 1: raise ValueError('Pause TV cabinet moved')
        return places[0]
    for sprite in PAUSE_TVS: put(apt, placements(chars[sprite])+12, intro)
    name = align(apt); apt.extend(BGNDTV_INTRO.encode('ascii')+b'\0')
    table = align(apt); apt.extend(apt[imports[0]:imports[0]+16*len(imports)])
    record = bytearray(apt[tv[0]:tv[0]+16]); put(record, 4, name); put(record, 8, intro); apt.extend(record)
    put(apt, root+40, len(imports)+1); put(apt, root+44, table)
    return placements(root)


def patch_pause(original):
    """Widen the pause transition's shutters and set its TVs in the studio's wall."""
    chunks = ui_chunks(original)
    apt = bytearray(next(data for tag, data in chunks if tag == b'apti'))
    rubi = bytearray(next(data for tag, data in chunks if tag == b'rubi'))
    root = find_root(apt); chars = characters(apt, root); count = len(chars)
    widen_shutters(apt, root, chars)
    ids = iter(range(count, 1 << 15)); intro = next(ids)
    entries = {intro: 0}  # Imports keep an empty character entry.
    menu_tv = import_intro_cabinet(apt, root, chars, intro)
    color, rects = studio_wall(apt, chars, geometry(bytes(rubi)))
    studio = chars[STUDIO_SPRITE]
    template = apt[frame_placements(apt, studio)[STUDIO_DEPTH]:][:64]

    def shape(rect):
        gid = next(ids); rubi.extend(rect_geometry(gid, color, rect))
        entries[gid] = align(apt); apt.extend(struct.pack('>2I4fI', 1, SIGNATURE, *rect, gid))
        return gid

    def place(depth, char):
        record = bytearray(template); put(record, 8, depth); put(record, 12, char)
        return bytes(record)
    (entry,) = sprite_frames(apt, studio)
    append_controls(apt, entry, [place(depth, shape(rect)) for depth, rect in zip(STUDIO_WALL_DEPTHS, rects)])
    # The menu TV the transition ends on: the plain cabinet, surrounded by the
    # studio's wall in BgndTV's local space, outside the cabinet as in BgndTV.
    pieces = outside((-LIMIT, -LIMIT, LIMIT, LIMIT), CABINET_KEEPOUT)
    controls = [place(depth, shape(piece)) for depth, piece in enumerate(pieces, start=1)]
    controls.append(place(len(pieces)+1, intro))
    addresses = []
    for record in controls:
        addresses.append(align(apt)); apt.extend(record)
    table = align(apt); apt.extend(struct.pack(f'>{len(addresses)}I', *addresses))
    frames = align(apt); apt.extend(struct.pack('>II', len(addresses), table))
    tv = next(ids); entries[tv] = align(apt); apt.extend(apt[studio:studio+20])
    put(apt, entries[tv]+8, 1); put(apt, entries[tv]+12, frames)
    put(apt, menu_tv+12, tv)
    table = align(apt); apt.extend(apt[u(apt, root+24):u(apt, root+24)+4*count])
    apt.extend(struct.pack(f'>{len(entries)}I', *(entries[i] for i in sorted(entries))))
    put(apt, root+20, count+len(entries)); put(apt, root+24, table)
    align(apt)
    return rebuild_ui(original, [(tag, bytes(apt) if tag == b'apti' else bytes(rubi) if tag == b'rubi' else data)
                                 for tag, data in chunks])


# Title clouds whose textures were cut where the 16:9 stage ended:
# (dictionary, texture, cut side, first untouched column). A right end is
# painted from that column to the texture edge, a left one from the edge up
# to it. Columns past it stay bit-identical, and tests check that no title
# frame ever shows the painted columns inside the 16:9 stage.
CLOUD_ENDS = (
    ('frontend_split2.itxd', '7_frontend_assets', 'R', 1812),   # Cloud_08, cut at 1820
    ('frontend_split7.itxd', '11_frontend_assets', 'R', 1868),  # Cloud_04, cut at 1947
    ('frontend_split8.itxd', '6_frontend_assets', 'R', 1952),   # Cloud_09, ran into the edge
    ('frontend_split9.itxd', '12_frontend_assets', 'L', 256),   # Cloud_03, cut at 244
)
# Billows of a cloud end as (x offset, radius) in cloud heights; the vertical
# placement is solved so the top and bottom billows meet the cloud's edges.
BILLOWS = {'top': (0.02, 0.38), 'bottom': (0.02, 0.30), 'upper': (0.36, 0.22),
           'end': (0.40, 0.30), 'body': (0.15, 0.45)}
RIM_DEPTH = 40      # Rows of edge shading measured from the cloud's own edges.
PROFILE_SPAN = 96   # Columns before the seam that the edge shading is measured over.
EDGE_SOFTNESS = 2.0  # Texels per half alpha ramp, as the retail edges.
SEAM = 24           # Columns blending the original colors into the painted end.
MIRROR = 0.40       # Mirrored body depth, in cloud heights.
SMOOTH = 12         # Columns averaged into each source column's extent.
SHADE_BLEND = 0.12  # Smooth-union radius of the shading, in cloud heights.
OUTLINE = 8         # Texels inside the outline that keep its exact distance.
TEXTURE_MARGIN = 4  # Transparent texels kept at the texture edge.


def itxd_texture(payload, name):
    """Tiled, single-level BC2 texture record of an EARS ITXD."""
    at, seen = u(payload, 0x18), set()
    while at != 0x18 and at not in seen:
        seen.add(at)
        if payload[at+8:payload.index(b'\0', at+8)].decode('ascii') == name:
            a, b, c, _, e, _ = (u(payload, at+0xe0+4*i) for i in range(6))
            # 0x53: BC2 (format 19) with 8-in-16 endian swap and no base offset.
            if b != 0x53 or not a >> 31 or (e >> 6) & 15:
                raise ValueError(f'{name} is not a tiled one-level BC2 texture')
            width, height = (c & 8191)+1, ((c >> 13) & 8191)+1
            return dict(width=width, height=height, pitch=((a >> 22) & 511)*8, offset=u(payload, at+0xb8))
        at = u(payload, at)
    raise ValueError(f'{name} missing from its texture dictionary')


def block_at(texture, bx, by):
    from decode_itxd import tiled_offset_2d
    return texture['offset']+tiled_offset_2d(bx, by, texture['pitch'], 16)


def read_pixels(payload, texture, x0, y0, x1, y1):
    """RGBA rows of a block-aligned window."""
    from decode_itxd import decode_bc_block
    rows = [[None]*(x1-x0) for _ in range(y1-y0)]
    for by in range(y0//4, y1//4):
        for bx in range(x0//4, x1//4):
            at = block_at(texture, bx, by)
            block = bytes(payload[at+(i ^ 1)] for i in range(16))  # 8in16
            for i, pixel in enumerate(decode_bc_block(block, 19)):
                rows[by*4+i//4-y0][bx*4+i % 4-x0] = list(pixel)
    return rows


def encode_bc2(pixels):
    """16 RGBA pixels -> little-endian BC2 block (4-color, explicit alpha)."""
    alpha = sum(min(15, (p[3]*15+127)//255) << 4*i for i, p in enumerate(pixels))
    mean = [sum(p[c] for p in pixels)/16 for c in range(3)]
    cov = [[sum((p[i]-mean[i])*(p[j]-mean[j]) for p in pixels) for j in range(3)] for i in range(3)]
    axis = [1.0, 1.0, 1.0]
    for _ in range(8):
        axis = [sum(cov[i][j]*axis[j] for j in range(3)) for i in range(3)]
        norm = max(abs(v) for v in axis)
        if norm < 1e-9: axis = [1.0, 1.0, 1.0]; break
        axis = [v/norm for v in axis]
    proj = [sum((p[c]-mean[c])*axis[c] for c in range(3)) for p in pixels]
    lo, hi = min(proj), max(proj)

    def q565(t):
        r, g, b = (min(255, max(0, round(mean[c]+axis[c]*t))) for c in range(3))
        return ((r*31+127)//255) << 11 | ((g*63+127)//255) << 5 | (b*31+127)//255

    def rgb(v):
        r, g, b = v >> 11, (v >> 5) & 63, v & 31
        return ((r << 3) | (r >> 2), (g << 2) | (g >> 4), (b << 3) | (b >> 2))
    c0, c1 = q565(hi), q565(lo)
    e0, e1 = rgb(c0), rgb(c1)
    palette = [e0, e1, tuple((2*e0[i]+e1[i])//3 for i in range(3)), tuple((e0[i]+2*e1[i])//3 for i in range(3))]
    indices = 0
    for i, p in enumerate(pixels):
        best = min(range(4), key=lambda k: sum((p[c]-palette[k][c])**2 for c in range(3)))
        indices |= best << 2*i
    return alpha.to_bytes(8, 'little')+struct.pack('<HHI', c0, c1, indices)


def write_pixels(payload, texture, x0, y0, rows):
    for by in range(len(rows)//4):
        for bx in range(len(rows[0])//4):
            pixels = [rows[by*4+i//4][bx*4+i % 4] for i in range(16)]
            block = encode_bc2(pixels)
            at = block_at(texture, x0//4+bx, y0//4+by)
            payload[at:at+16] = bytes(block[i ^ 1] for i in range(16))


def cloud_extent(rows, x):
    inside = [y for y in range(len(rows)) if rows[y][x][3] >= 128]
    return (inside[0], inside[-1]) if inside else None


def paint_cloud_end(rows, start, seed):
    """Paint a billowed right end into `rows` from column `start` on.

    The end is a union of billows meeting the cloud's own top and bottom
    edges at `start`. Its body is the cloud mirrored across the seam and
    remapped into the new outline; its outline shading is the cloud's own,
    measured beside the seam, blended from the top profile to the bottom one
    down the cloud's height. Returns the rows and the block-aligned row range
    that changed.
    """
    h, w = len(rows), len(rows[0])
    extents = [cloud_extent(rows, x) for x in range(start)]
    top, bottom = extents[start-1]
    height = bottom-top
    profile = {}
    for side in ('top', 'bottom'):
        sums = [[0.0]*3 for _ in range(RIM_DEPTH)]; count = 0
        for x in range(max(0, start-PROFILE_SPAN), start-2):
            if not extents[x] or extents[x][1]-extents[x][0] < 2*RIM_DEPTH: continue
            edge, step = (extents[x][0], 1) if side == 'top' else (extents[x][1], -1)
            for d in range(RIM_DEPTH):
                for c in range(3): sums[d][c] += rows[edge+step*d][x][c]
            count += 1
        if not count: raise ValueError('Cloud has no measurable edges')
        ref = [sums[RIM_DEPTH-1][c]/count or 1.0 for c in range(3)]
        profile[side] = [[sums[d][c]/count/ref[c] for c in range(3)] for d in range(RIM_DEPTH)]
    jitter = hashlib.sha256(seed.encode()).digest()
    squeeze = min(1.0, (w-TEXTURE_MARGIN-start)/(0.70*height))
    centre = (top+bottom)/2
    shapes = []
    for i, (name, (offset, radius)) in enumerate(BILLOWS.items()):
        radius *= height*(0.94+jitter[i]/255*0.12)
        if name == 'top': y = top+radius*(1-(offset*height/radius)**2)**0.5
        elif name == 'bottom': y = bottom-radius*(1-(offset*height/radius)**2)**0.5
        elif name == 'upper': y = centre-0.16*height
        elif name == 'end': y = centre+(0.03+jitter[8]/255*0.06)*height
        else: y = centre
        shapes.append((start+offset*height*squeeze, y, radius))
    if max(x+r*squeeze for x, _, r in shapes) > w-TEXTURE_MARGIN+0.5:
        raise ValueError('Cloud end does not fit its texture')
    # The old cut's own dark edge must not be blended into the seam.
    body = 0
    while start+body < w and rows[round(centre)][start+body][3] == 255: body += 1
    seam = max(1, min(SEAM, body-6))

    blend = SHADE_BLEND*height

    def depth(x, y):
        """Distance inside the billows (texels): their union for the outline,
        and a smooth union for the shading, which has no creases inside."""
        hard = soft = None
        for cx, cy, r in shapes:
            dx, dy = (x-cx)/squeeze, y-cy
            dist = (dx*dx+dy*dy)**0.5
            value = (r-dist)*(squeeze*abs(dx)+abs(dy))/(abs(dx)+abs(dy)+1e-6)
            if hard is None: hard = soft = value; continue
            hard = max(hard, value)
            k = max(blend-abs(soft-value), 0.0)/blend
            soft = max(soft, value)+k*k*blend/4
        # The outline keeps its exact distance; only the inside is smoothed.
        mix = min(1.0, max(0.0, (hard-OUTLINE)/(2*OUTLINE)))
        return hard, hard+(soft-hard)*mix*mix*(3-2*mix)
    old = [y for y in range(h) if any(rows[y][x][3] for x in range(start, w))]
    lo = min([int(min(cy-r for _, cy, r in shapes))]+old[:1])-4
    hi = max([int(max(cy+r for _, cy, r in shapes))+1]+old[-1:])+4
    lo, hi = max(0, lo//4*4), min(h, (hi+3)//4*4)
    field = {(x, y): depth(x+0.5, y+0.5) for x in range(start, w) for y in range(lo, hi)}
    spans = {}
    for x in range(start, w):
        inside = [y for y in range(lo, hi) if field[x, y][0] >= 0]
        spans[x] = (inside[0], inside[-1]) if inside else None
    # Notches between billows would make the body remap jump; follow
    # smoothed spans (each column still clips to its own outline).
    smoothed = {}
    for x in range(start, w):
        near = [spans[i] for i in range(max(start, x-SMOOTH), min(w, x+SMOOTH+1)) if spans[i]]
        if spans[x]: smoothed[x] = (sum(v[0] for v in near)/len(near), sum(v[1] for v in near)/len(near))

    def sample(x, y):
        """Body color, smoothed across a few columns against streaks."""
        y0 = min(max(int(y), 0), h-2); t = min(max(y-y0, 0.0), 1.0)
        near = [i for i in range(x-2, x+3) if 0 <= i < start and rows[y0][i][3] == 255 and rows[y0+1][i][3] == 255] or [x]
        return [sum(rows[y0][i][c]*(1-t)+rows[y0+1][i][c]*t for i in near)/len(near) for c in range(3)]
    # The body is mirrored back and forth within MIRROR heights of the seam,
    # and remapped along smoothed column extents (the edges are scalloped).
    period = max(8, round(MIRROR*height))
    smooth = {}
    for x in range(max(0, start-period-SMOOTH), start):
        near = [extents[i] for i in range(max(0, x-SMOOTH), min(start, x+SMOOTH+1)) if extents[i]]
        smooth[x] = (sum(e[0] for e in near)/len(near), sum(e[1] for e in near)/len(near))
    out = [row[:] for row in rows]
    for x in range(start, w):
        k = (x-start) % (2*period)
        source = start-1-(k if k < period else 2*period-1-k)
        st, sb = smooth[source]
        real = extents[source]
        span = smoothed.get(x)
        for y in range(lo, hi):
            edge, d = field[x, y]
            alpha = min(1.0, max(0.0, 0.5+edge/EDGE_SOFTNESS))
            if not alpha or not span:
                out[y][x] = [0, 0, 0, 0]
                continue
            nt, nb = span
            # Proportional down the column (identity at the seam), kept off the
            # source's own edge shading, which `rim` re-applies.
            ms = min(RIM_DEPTH, (sb-st)/2)
            sy = st+(y-nt)*(sb-st)/max(1, nb-nt)
            sy = min(max(sy, st+ms, real[0]+ms/2), sb-ms, real[1]-ms/2)
            base = sample(source, sy)
            up = min(1.0, max(0.0, (bottom-y)/height))  # Lit from above, as the cloud.
            k = min(RIM_DEPTH-1.001, max(0.0, d)); i = int(k); f = k-i
            rim = [up*(profile['top'][i][c]*(1-f)+profile['top'][i+1][c]*f) +
                   (1-up)*(profile['bottom'][i][c]*(1-f)+profile['bottom'][i+1][c]*f) for c in range(3)]
            pixel = [base[c]*rim[c] for c in range(3)]
            if x < start+seam and rows[y][x][3]:
                mix = (x-start+0.5)/seam
                pixel = [rows[y][x][c]*(1-mix)+pixel[c]*mix for c in range(3)]
            out[y][x] = [min(255, max(0, round(v))) for v in pixel]+[round(alpha*255)]
    return out, lo, hi


def patch_cloud_ends(name, original):
    payload = bytearray(original)
    for dictionary, texture_name, side, column in CLOUD_ENDS:
        if dictionary != name: continue
        texture = itxd_texture(payload, texture_name)
        w, h = texture['width'], texture['height']
        if column % 4 or not 0 < column < w: raise ValueError('Cloud end column is not block-aligned')
        # Mirror sources and edge measurements come from up to 320 columns of body.
        x0, x1 = (max(0, column-320), w) if side == 'R' else (0, min(w, column+320))
        rows = read_pixels(payload, texture, x0, 0, x1, h)
        if side == 'L': rows = [row[::-1] for row in rows]
        start = column-x0 if side == 'R' else x1-column
        rows, lo, hi = paint_cloud_end(rows, start, texture_name)
        if side == 'L': rows = [row[::-1] for row in rows]
        first = column-x0 if side == 'R' else 0
        last = len(rows[0]) if side == 'R' else column-x0
        write_pixels(payload, texture, x0+first, lo, [row[first:last] for row in rows[lo:hi]])
    return bytes(payload)


PATCHES = {'SharedLibrary.swf': patch_shared_library, 'simpsons_start.swf': patch_start, 'pause.swf': patch_pause,
           **{dictionary: lambda payload, name=dictionary: patch_cloud_ends(name, payload)
              for dictionary, *_ in CLOUD_ENDS}}
ULTRAWIDE_RESOURCES = frozenset(PATCHES)


def patch_ultrawide(name, payload):
    return PATCHES[name](payload) if name in PATCHES else payload


if __name__ == '__main__':
    for color, rect in living_room_wall():
        print(f'{color:08X}', ' '.join(f'{v:8.2f}' for v in rect))
