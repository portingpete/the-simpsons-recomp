"""Qualify the ultrawide frontend art against the retail frontend package."""
import struct
import unittest

from build_native_mouse_menu import ui_chunks
from build_native_ultrawide_art import (BGNDTV_INTRO, CABINET_KEEPOUT, CLOUD_ENDS, EXTENSION_DEPTH, LIMIT,
    PAUSE_SHUTTERS, PAUSE_TIMELINE, PAUSE_TVS, ROOM_COVERS_16_9, ROOM_DEPTH, ROOM_REPLACED, ROOM_SPAN,
    ROOM_ZOOM_SPRITE, SHUTTER_SPRITE, SIGNATURE, STUDIO_OVERLAP, STUDIO_SPRITE, TITLE_SPRITE, VIEW_HALF_16_9,
    characters, f32, find_root, frame_controls, frame_placements, geometry, itxd_texture, living_room_wall,
    matrix_of, outside, patch_cloud_ends, patch_pause, patch_shared_library, patch_start, patch_ultrawide, read_pixels,
    retail_resource, solid_rect, sprite_frames, string_at, studio_wall, transform, u)

FRONTEND = 'frontend/frontend.str'
CHARACTERS = 'simpsons_chars/simpsons_chars_global.str'
# The 640x480 Apt stage shows x -106.67..746.67 at 16:9 and -533.33..1173.33 at 32:9.
STAGE_16_9 = (320-240*16/9, 320+240*16/9)
STAGE_32_9 = (320-240*32/9, 320+240*32/9)


def apt_of(payload):
    chunks = dict(ui_chunks(payload))
    return chunks[b'apti'], chunks.get(b'rubi', b'')


def exports(apt, root):
    table = u(apt, root+52)
    return [(string_at(apt, u(apt, table+8*i)), u(apt, table+8*i+4)) for i in range(u(apt, root+48))]


def imports(apt, root):
    table = u(apt, root+44)
    return {u(apt, at+8): (string_at(apt, u(apt, at)), string_at(apt, u(apt, at+4)))
            for at in range(table, table+16*u(apt, root+40), 16)}


class LivingRoomWall(unittest.TestCase):
    def test_wall_is_the_opening_room(self):
        layers = living_room_wall()
        self.assertEqual(len(layers), 11)
        colors = [color for color, _ in layers]
        self.assertEqual(colors[:3], [0x4FAA7AFF, 0xFAC3D3FF, 0xBFB6FCFF])  # Floor, wall, window glass.
        self.assertEqual(set(colors[3:]), {0x000000FF})

        # BgndTV sits at the stage centre at scale 0.67 in every menu: the trim
        # lines land where the opening's last frame draws them.
        def stage_y(rect): return 240+0.67*rect[1], 240+0.67*rect[3]
        window_sill, wall_line = layers[6][1], layers[4][1]
        for got, want in zip(stage_y(window_sill), (31.8, 37.8)): self.assertAlmostEqual(got, want, delta=0.2)
        for got, want in zip(stage_y(wall_line), (441.7, 447.7)): self.assertAlmostEqual(got, want, delta=0.2)
        for _, rect in layers:
            self.assertTrue(all(-LIMIT <= v <= LIMIT for v in rect))
            self.assertLess(rect[0], rect[2]); self.assertLess(rect[1], rect[3])

    def test_wall_covers_32_9_menus(self):
        """The wall fills both sides of the visible stage at 32:9."""
        wall = living_room_wall()[1][1]
        left, right = (-533.33-320)/0.67, (1173.33-320)/0.67
        self.assertLess(wall[0], left); self.assertGreater(wall[2], right)
        self.assertLess(wall[1], (0-240)/0.67); self.assertGreater(wall[3], (480-240)/0.67)

    def test_wall_never_covers_the_screen(self):
        pieces = [piece for _, rect in living_room_wall() for piece in outside(rect, CABINET_KEEPOUT)]
        x0, y0, x1, y1 = CABINET_KEEPOUT
        for piece in pieces:
            self.assertTrue(piece[2] <= x0 or piece[0] >= x1 or piece[3] <= y0 or piece[1] >= y1, piece)

    def test_keepout_matches_the_cabinet_texture(self):
        """Keep-out edges sit on the opaque cabinet; its screen opening is well inside."""
        payload = retail_resource(FRONTEND, 'frontend_split3.itxd')
        texture = itxd_texture(payload, '9_SharedLibrary')
        row = read_pixels(payload, texture, 0, 512, 2048, 516)[0]
        opaque = [x for x, pixel in enumerate(row) if pixel[3] >= 128]
        self.assertEqual((opaque[0], opaque[-1]), (380, 1840))
        hole = [x for x in range(opaque[0], opaque[-1]) if row[x][3] < 128]
        x0, y0, x1, _ = (v+off for v, off in zip(CABINET_KEEPOUT, (1024, 512, 1024, 512)))
        self.assertTrue(row[int(x0)][3] == 255 and row[int(x1)][3] == 255)
        self.assertGreater(hole[0]-x0, 100); self.assertGreater(x1-hole[-1], 100)
        top = read_pixels(payload, texture, 400, 24, 1824, 28)[2]
        self.assertTrue(all(pixel[3] == 255 for pixel in top))  # Keep-out top edge (y0 = 26).


def swf_resources(package):
    from build_native_video_menu import ROOT, decode_entry, resource_chunks, stoc_entries
    data = (ROOT/'Simpsons Game, The (USA)'/package).read_bytes()
    for entry in stoc_entries(data)['entries']:
        decoded, _ = decode_entry(data, entry)
        for chunk in resource_chunks(decoded):
            name = chunk.get('name') or ''
            if name.endswith('.swf') and 'payload_decoded_offset' in chunk:
                at = chunk['payload_decoded_offset']
                yield name, bytes(decoded[at:at+chunk['payload_size']])


def cabinet_framings(payload, movie):
    """Every stage matrix SharedLibrary.BgndTV is drawn with, over all frames of every timeline."""
    apt, _ = apt_of(payload); root = find_root(apt); chars = characters(apt, root)
    targets = {cid for cid, name in imports(apt, root).items() if name == ('SharedLibrary', 'BgndTV')}
    if movie == 'SharedLibrary.swf': targets |= {cid for name, cid in exports(apt, root) if name == 'BgndTV'}
    placed = {}  # timeline -> {child: {matrix}}
    for timeline in [root]+[at for at in chars if at and u(apt, at) == 5]:
        held, children = {}, placed.setdefault(timeline, {})
        for entry in sprite_frames(apt, timeline):
            for at in frame_controls(apt, entry):
                if u(apt, at) == 4: held.pop(u(apt, at+4), None)
                if u(apt, at) != 3: continue
                flags, depth = u(apt, at+4), u(apt, at+8)
                if flags & 2: held[depth] = u(apt, at+12)
                if depth in held and flags & 6:
                    matrix = matrix_of(apt, at) if flags & 4 else (1, 0, 0, 1, 0, 0)
                    children.setdefault(held[depth], set()).add(matrix)

    def compose(p, c):
        a, b, cc, d, tx, ty = p
        return (a*c[0]+cc*c[1], b*c[0]+d*c[1], a*c[2]+cc*c[3], b*c[2]+d*c[3], a*c[4]+cc*c[5]+tx, b*c[4]+d*c[5]+ty)

    def walk(timeline, matrix, depth=0):
        for child, matrices in placed.get(timeline, {}).items():
            for m in matrices:
                if child in targets: yield compose(matrix, m)
                elif depth < 8 and child < len(chars) and chars[child] in placed:
                    yield from walk(chars[child], compose(matrix, m), depth+1)
    return list(walk(root, (1, 0, 0, 1, 0, 0)))


class CabinetUses(unittest.TestCase):
    def test_walled_cabinet_only_at_the_menu_framing(self):
        """The living-room wall stays outside 16:9 only with the cabinet at 0.67 on the stage centre.

        The pause menu zooms its TVs, so it uses the plain cabinet; nothing
        else may draw the walled one at another scale or position.
        """
        menu = (0.67, 0, 0, 0.67, 320, 240)
        uses = 0
        for package in (FRONTEND, CHARACTERS):
            for name, payload in swf_resources(package):
                for framing in cabinet_framings(patch_ultrawide(name, payload), name):
                    uses += 1
                    self.assertTrue(all(abs(got-want) < 1e-4 for got, want in zip(framing, menu)), (package, name, framing))
        self.assertGreater(uses, 10)


class CabinetPatch(unittest.TestCase):
    def test_menus_get_the_wall_and_intro_the_original(self):
        original = retail_resource(FRONTEND, 'SharedLibrary.swf')
        old, _ = apt_of(original); new, rubi = apt_of(patch_shared_library(original))
        root = find_root(old); self.assertEqual(find_root(new), root)
        old_chars, new_chars = characters(old, root), characters(new, root)
        tv = {cid for name, cid in exports(old, root) if name == 'BgndTV'}.pop()
        self.assertEqual(old_chars, new_chars[:len(old_chars)])
        # Original records are untouched apart from BgndTV's frame list and the root tables.
        changed = {at for at in range(len(old)) if old[at] != new[at]}
        allowed = set(range(old_chars[tv]+12, old_chars[tv]+16)) | set(range(root+20, root+28)) | set(range(root+48, root+56))
        self.assertLessEqual(changed, allowed)
        names = dict((name, cid) for name, cid in exports(new, root))
        intro = new_chars[names[BGNDTV_INTRO]]
        self.assertEqual(new[intro:intro+20], old[old_chars[tv]:old_chars[tv]+20])
        layers = frame_placements(new, new_chars[tv])
        pieces = [piece for _, rect in living_room_wall() for piece in outside(rect, CABINET_KEEPOUT)]
        self.assertEqual(sorted(layers), list(range(1, len(pieces)+2)))
        # The cabinet is the original placement, drawn last.
        cabinet, retail = layers[len(pieces)+1], frame_placements(old, old_chars[tv])[1]
        self.assertEqual(new[cabinet:cabinet+8]+new[cabinet+12:cabinet+64], old[retail:retail+8]+old[retail+12:retail+64])
        shapes = geometry(rubi)
        for depth in range(1, len(pieces)+1):
            cid = u(new, layers[depth]+12)
            self.assertEqual(u(new, new_chars[cid]), 1); self.assertEqual(u(new, new_chars[cid]+4), SIGNATURE)
            record = shapes[u(new, new_chars[cid]+24)]
            self.assertEqual((u(record, 4), u(record, 12)), (0x40, 2))
            self.assertEqual(matrix_of(new, layers[depth]), (1, 0, 0, 1, 0, 0))

    def test_opening_imports_the_unchanged_cabinet(self):
        original = retail_resource(FRONTEND, 'simpsons_start.swf')
        old, _ = apt_of(original); new, _ = apt_of(patch_start(original))
        root = find_root(old)
        before, after = imports(old, root), imports(new, root)
        self.assertEqual({cid: name for cid, name in after.items() if name[1] != BGNDTV_INTRO},
                         {cid: name for cid, name in before.items() if name[1] != 'BgndTV'})
        self.assertEqual([cid for cid, name in after.items() if name == ('SharedLibrary', BGNDTV_INTRO)],
                         [cid for cid, name in before.items() if name == ('SharedLibrary', 'BgndTV')])


class RoomZoom(unittest.TestCase):
    def test_zoom_is_widened_only_where_16_9_is_covered(self):
        original = retail_resource(FRONTEND, 'simpsons_start.swf')
        old, rubi = apt_of(original); new, _ = apt_of(patch_start(original))
        root = find_root(new); chars = characters(new, root)
        # The original room box: the narrowest of its floor and wall spans.
        shapes, room = geometry(rubi), characters(old, root)[ROOM_ZOOM_SPRITE]
        spans = []
        for depth in (1, 3):
            place = frame_placements(old, room)[depth]
            (inner,) = frame_placements(old, characters(old, root)[u(old, place+12)]).values()
            _, rect = solid_rect(shapes[u(old, characters(old, root)[u(old, inner+12)]+24)])
            spans.append(transform(matrix_of(old, place), rect))
        box = (max(r[0] for r in spans), min(r[2] for r in spans))
        entries = sprite_frames(new, chars[TITLE_SPRITE])
        placed, removed = {}, []
        for frame, entry in enumerate(entries):
            for at in frame_controls(new, entry):
                if u(new, at) == 3 and u(new, at+8) == EXTENSION_DEPTH: placed[frame] = at
                if u(new, at) == 4 and u(new, at+4) == EXTENSION_DEPTH: removed.append(frame)
        self.assertEqual(sorted(placed), list(range(ROOM_COVERS_16_9, ROOM_REPLACED)))
        self.assertEqual(removed, [ROOM_REPLACED])
        self.assertEqual(u(new, placed[ROOM_COVERS_16_9]+12), len(chars)-1)
        for frame, at in placed.items():
            (zoom,) = [c for c in frame_controls(new, entries[frame]) if u(new, c) == 3 and u(new, c+8) == ROOM_DEPTH]
            self.assertEqual(matrix_of(new, at), matrix_of(new, zoom))
            a, _, _, _, tx, _ = matrix_of(new, at)
            # The room covers 16:9, hiding the wider copy beneath it there...
            self.assertLessEqual(320+tx+a*box[0], STAGE_16_9[0]); self.assertGreaterEqual(320+tx+a*box[1], STAGE_16_9[1])
            # ...and the copy covers 32:9.
            self.assertLessEqual(320+tx+a*ROOM_SPAN[0], STAGE_32_9[0]); self.assertGreaterEqual(320+tx+a*ROOM_SPAN[1], STAGE_32_9[1])


class CloudEnds(unittest.TestCase):
    def test_painted_columns_never_reach_the_16_9_view(self):
        """Every title frame keeps the painted columns outside the 16:9 stage."""
        start, _ = apt_of(retail_resource(FRONTEND, 'simpsons_start.swf'))
        assets, rubi = apt_of(retail_resource(FRONTEND, 'frontend_assets.swf'))
        sroot, aroot = find_root(start), find_root(assets)
        shapes = geometry(rubi); achars = characters(assets, aroot)
        exported = {}
        for name, cid in exports(assets, aroot): exported.setdefault(name, cid)
        clouds = {}  # simpsons_start character -> (texture, texel matrix, rect)
        for cid, (movie, name) in imports(start, sroot).items():
            if movie != '..\\frontend_assets' or not name.startswith('Cloud_'): continue
            (place,) = frame_placements(assets, achars[exported[name]]).values()
            record = shapes[u(assets, achars[u(assets, place+12)]+24)]
            texmatrix = struct.unpack_from('>6f', record, 20)
            clouds[cid] = (u(record, 44), texmatrix)
        ends = {int(texture.split('_')[0]): (side, column) for _, texture, side, column in CLOUD_ENDS}
        title = characters(start, sroot)[82]
        count, frames = u(start, title+8), u(start, title+12)
        state, seen = {}, set()
        for frame in range(count):
            controls, table = u(start, frames+8*frame), u(start, frames+8*frame+4)
            for i in range(controls):
                at = u(start, table+4*i)
                if u(start, at) != 3: continue
                flags, depth = u(start, at+4), u(start, at+8)
                current = dict(state.get(depth, {}))
                if flags & 2: current['char'] = u(start, at+12)
                if flags & 4: current['matrix'] = matrix_of(start, at)
                state[depth] = current
            for current in state.values():
                cloud = clouds.get(current.get('char'))
                if not cloud or 'matrix' not in current or cloud[0] not in ends: continue
                (texture, texmatrix), (side, column) = cloud, ends[cloud[0]]
                a, b, c, d, tx, ty = current['matrix']
                self.assertTrue(abs(b) < 1e-6 and abs(c) < 1e-6 and a > 0)
                # A right end lies right of `column`, so that column must be past
                # the stage's right edge (a left end's, past its left edge).
                x = 320+tx+a*(column-texmatrix[2])
                self.assertTrue(x > STAGE_16_9[1] if side == 'R' else x < STAGE_16_9[0], (frame, texture, x))
                seen.add(texture)
        self.assertEqual(seen, set(ends))

    def test_cloud_ends_only_touch_their_columns(self):
        for dictionary, texture, side, column in CLOUD_ENDS:
            with self.subTest(texture=texture):
                original = retail_resource(FRONTEND, dictionary)
                painted = patch_cloud_ends(dictionary, original)
                self.assertEqual(len(painted), len(original))
                info = itxd_texture(original, texture)
                w, h = info['width'], info['height']
                keep = (0, column) if side == 'R' else (column, w)
                end = (column, w) if side == 'R' else (0, column)
                for x0, x1 in ((keep[0], min(keep[1], keep[0]+256)), (max(keep[0], keep[1]-256), keep[1])):
                    self.assertEqual(read_pixels(original, info, x0, 0, x1, h), read_pixels(painted, info, x0, 0, x1, h))
                # The cloud continues into a painted end that closes before the
                # texture edge (the retail cut ran up to it or stopped short).
                after = read_pixels(painted, info, end[0], 0, end[1], h)
                self.assertGreater(sum(p[3] == 255 for row in after for p in row), 1000)
                self.assertTrue(all((row[-1] if side == 'R' else row[0])[3] == 0 for row in after))


class PauseTransition(unittest.TestCase):
    """The pause timeline is centred on the stage: 16:9 shows +-426.67 around it, 32:9 +-853.33."""
    HALF_32_9 = 240*32/9

    @classmethod
    def setUpClass(cls):
        cls.original = retail_resource(CHARACTERS, 'pause.swf')
        cls.old, cls.rubi = apt_of(cls.original)
        cls.new, cls.new_rubi = apt_of(patch_pause(cls.original))
        cls.root = find_root(cls.old); cls.chars = characters(cls.old, cls.root)

    def placements(self, apt, sprite):
        return [at for entry in sprite_frames(apt, sprite) for at in frame_controls(apt, entry)
                if u(apt, at) == 3 and u(apt, at+4) & 2]

    def test_only_shutters_tvs_and_tables_change(self):
        old, new, chars, root = self.old, self.new, self.chars, self.root
        self.assertEqual(find_root(new), root)
        self.assertEqual(characters(new, root)[:len(chars)], chars)
        allowed = set(range(root+20, root+28)) | set(range(root+40, root+48))
        allowed |= {at+i for entry in sprite_frames(old, chars[STUDIO_SPRITE]) for at in (entry, entry+4) for i in range(4)}
        for at in self.placements(old, root)+[at for sprite in PAUSE_TVS for at in self.placements(old, chars[sprite])]:
            allowed |= set(range(at+12, at+16))
        for entry in sprite_frames(old, chars[PAUSE_TIMELINE]):
            for at in frame_controls(old, entry):
                if u(old, at) == 3 and u(old, at+8) in PAUSE_SHUTTERS:
                    allowed |= set(range(at+24, at+28)) | set(range(at+32, at+36))
        changed = {at for at in range(len(old)) if old[at] != new[at]}
        self.assertLessEqual(changed, allowed)
        self.assertEqual(self.new_rubi[:len(self.rubi)], self.rubi)

    def test_vertical_shutters_reach_past_32_9_and_keep_16_9(self):
        old, new = self.old, self.new
        (bar,) = frame_placements(old, self.chars[SHUTTER_SPRITE]).values()
        bar = self.chars[u(old, bar+12)]
        y0, y1 = f32(old, bar+12), f32(old, bar+20)

        def ends(matrix): return sorted((matrix[2]*y0+matrix[4], matrix[2]*y1+matrix[4]))

        def in_16_9(e): return max(e[0], -VIEW_HALF_16_9), min(e[1], VIEW_HALF_16_9)
        widened = 0
        for entry in sprite_frames(old, self.chars[PAUSE_TIMELINE]):
            for at in frame_controls(old, entry):
                if u(old, at) != 3 or matrix_of(old, at) == matrix_of(new, at): continue
                widened += 1
                self.assertIn(u(old, at+8), PAUSE_SHUTTERS)
                before, after = ends(matrix_of(old, at)), ends(matrix_of(new, at))
                for got, want in zip(in_16_9(after), in_16_9(before)): self.assertAlmostEqual(got, want, places=3)
                self.assertTrue(after[0] <= -self.HALF_32_9 or after[1] >= self.HALF_32_9, after)
        self.assertEqual(widened, 36)  # Both bars over nine frames as the menu opens and as it closes.

    def test_studio_wall_continues_the_door_frame(self):
        color, rects = studio_wall(self.old, self.chars, geometry(self.rubi))
        self.assertEqual(color, 0x37486BFF)  # The panels' blue, BC2-dithered between 36476B and 39496B.
        scales = {matrix_of(self.old, at) for at in self.placements(self.old, self.chars[PAUSE_TIMELINE])
                  if u(self.old, at+12) == STUDIO_SPRITE}
        self.assertEqual(len(scales), 1)
        a, _, _, d, tx, ty = scales.pop()
        self.assertEqual((a, tx, ty), (d, 0, 0))
        left, right = rects
        # Past the panels' opaque edges the wall is outside the 16:9 view...
        self.assertLessEqual(a*(left[2]-STUDIO_OVERLAP), -VIEW_HALF_16_9)
        self.assertGreaterEqual(a*(right[0]+STUDIO_OVERLAP), VIEW_HALF_16_9)
        # ...and it reaches past the 32:9 view, as tall as the studio art.
        self.assertLessEqual(a*left[0], -self.HALF_32_9); self.assertGreaterEqual(a*right[2], self.HALF_32_9)
        for rect in rects:
            self.assertLessEqual(240+a*rect[1], 0); self.assertGreaterEqual(240+a*rect[3], 479)
        layers = frame_placements(self.new, characters(self.new, self.root)[STUDIO_SPRITE])
        self.assertEqual(sorted(layers), [1, 2, 5])  # Beneath the studio art.

    def test_tvs_are_plain_cabinets_in_the_studio_wall(self):
        new, root = self.new, self.root
        chars = characters(new, root); names = imports(new, root)
        (intro,) = [cid for cid, name in names.items() if name == ('SharedLibrary', BGNDTV_INTRO)]
        (walled,) = [cid for cid, name in names.items() if name == ('SharedLibrary', 'BgndTV')]
        for sprite in PAUSE_TVS:
            placed = [u(new, at+12) for at in self.placements(new, chars[sprite])]
            self.assertIn(intro, placed); self.assertNotIn(walled, placed)
        (menu,) = [u(new, at+12) for at in self.placements(new, root) if u(new, at+12) >= len(self.chars)]
        layers = frame_placements(new, chars[menu])
        pieces = outside((-LIMIT, -LIMIT, LIMIT, LIMIT), CABINET_KEEPOUT)
        self.assertEqual(sorted(layers), list(range(1, len(pieces)+2)))
        self.assertEqual(u(new, layers[len(pieces)+1]+12), intro)
        shapes = geometry(self.new_rubi)
        color, _ = studio_wall(self.old, self.chars, geometry(self.rubi))
        for depth, piece in enumerate(pieces, start=1):
            shape = chars[u(new, layers[depth]+12)]
            self.assertEqual(solid_rect(shapes[u(new, shape+24)]), (color, piece))
        self.assertFalse([sprite for sprite in range(len(chars)) if names.get(sprite) is None and chars[sprite]
                          and u(new, chars[sprite]) in (5, 9) and walled in
                          [u(new, at+12) for at in self.placements(new, chars[sprite])]])


if __name__ == '__main__':
    unittest.main()
