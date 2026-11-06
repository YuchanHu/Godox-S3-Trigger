"""Check the accepted layout, touch targets, native font pixels and short intervals.
No preview images are written; existing review PNGs are read as reference fixtures.
"""
import ctypes as C
import unittest
from PIL import Image, ImageDraw, ImageFont
from test_stage_c import ROOT, Ui


@unittest.skipUnless((ROOT/'artifacts/stage_host.dll').exists(), 'native C host library not built')
class StageV040Tests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.lib = C.CDLL(str(ROOT/'artifacts/stage_host.dll'))
        cls.lib.stage_config_valid.restype = C.c_bool
        cls.lib.stage_next_test_deadline.restype = C.c_int64
        cls.lib.stage_next_test_deadline.argtypes = [C.c_int64, C.c_int64, C.c_uint32, C.POINTER(C.c_bool)]

    def state(self):
        s = Ui()
        self.lib.stage_core_init(C.byref(s))
        s.cache_ready = True
        s.battery_valid = True
        s.battery_percent = 78
        return s

    def frame(self, s):
        frame = (C.c_uint8*5000)()
        self.lib.stage_view_render(C.byref(s), frame)
        return bytes(frame)

    def test_home_maximum_power_group_body_matches_accepted_pixels(self):
        # v0.4.1 intentionally changes the shared header and preparation detail.
        for name in ['home', 'home-max', 'power', 'group']:
            s = self.state()
            if name == 'home-max':
                s.config.ch, s.config.group, s.config.id = 32, 5, 99
                s.config.power, s.config.count, s.config.interval_ms = 0, 999, 60000
            elif name == 'power':
                s.field, s.draft, s.digits = 3, 1, 1
            elif name == 'group':
                s.field, s.draft, s.digits = 1, 0, 1
            with self.subTest(page=name):
                expected = Image.open(ROOT/f'tests/fixtures/ui-v040/{name}.png').convert('1').tobytes()
                self.assertEqual(self.frame(s)[24*25:], expected[24*25:])

    def test_home_gutters_and_label_to_value_spacing_are_clear(self):
        im = Image.frombytes('1', (200, 200), self.frame(self.state()))
        self.assertEqual(im.crop((94, 24, 106, 180)).getextrema(), (255, 255))
        for x in [8, 106]:
            for y in [36, 86, 136]:
                self.assertEqual(im.crop((x+2, y+8, x+84, y+14)).getextrema(), (255, 255))

    def test_running_context_entire_line_is_qiuye_16(self):
        for ch, ident, group, group_text in [(8, 0, 0, 'ALL'), (32, 99, 5, 'E')]:
            s = self.state()
            s.mode = 1
            s.run = s.config
            s.run.ch, s.run.id, s.run.group = ch, ident, group
            label = f'CH{ch:02} ID{ident:02}' if ident else f'CH{ch:02} IDOFF'
            label += ' Group ' + group_text
            font = ImageFont.truetype(str(ROOT/'assets/QiuYeYuanTi-16.ttf'), 16)
            mask = Image.new('L', (200, 20))
            ImageDraw.Draw(mask).text((100-round(font.getlength(label))//2, 16), label,
                                      font=font, fill=255, anchor='ls')
            expected = mask.point(lambda v: 0 if v >= 128 else 255, mode='1').tobytes()
            self.assertEqual(self.frame(s)[157*25:177*25], expected)

    def test_interval_boundaries_and_touch_can_save_one_ms(self):
        s = self.state()
        for value in [1, 2, 10, 99, 100, 60000]:
            s.config.interval_ms = value
            self.assertTrue(self.lib.stage_config_valid(C.byref(s.config)))
        for value in [0, 60001, 0xffffffff]:
            s.config.interval_ms = value
            self.assertFalse(self.lib.stage_config_valid(C.byref(s.config)))
        s = self.state()
        self.lib.stage_core_touch(C.byref(s), 150, 145)
        self.assertEqual(s.field, 5)
        self.lib.stage_core_touch(C.byref(s), 38, 78)  # 1 replaces 1000
        self.assertEqual(self.lib.stage_core_touch(C.byref(s), 150, 188), 1)
        self.assertEqual(s.config.interval_ms, 1)

    def test_descriptor_hits_gaps_and_editor_actions_match_layout(self):
        for field in range(6):
            s = self.state()
            self.lib.stage_core_touch(C.byref(s), 20 + field%2*98, 29+field//2*50)
            self.assertEqual(s.field, field)
        for x, y in [(100, 45), (100, 95), (100, 145), (50, 178)]:
            s = self.state()
            self.lib.stage_core_touch(C.byref(s), x, y)
            self.assertEqual(s.field, -1)
        s = self.state()
        s.field, s.draft, s.digits = 0, 9, 1
        self.assertEqual(self.lib.stage_core_touch(C.byref(s), 100, 188), 0)  # button gap
        self.assertEqual(s.field, 0)
        self.lib.stage_core_touch(C.byref(s), 50, 188)
        self.assertEqual(s.field, -1)
        s.mode = 2
        self.lib.stage_core_touch(C.byref(s), 100, 178)  # completion/footer gap
        self.assertEqual(s.mode, 2)
        self.lib.stage_core_touch(C.byref(s), 100, 165)
        self.assertEqual(s.mode, 0)
        s.mode = 1
        self.assertEqual(self.lib.stage_core_touch(C.byref(s), 100, 170), 0)
        self.assertEqual(self.lib.stage_core_touch(C.byref(s), 100, 185), 3)

    def test_every_numeric_key_and_power_option_responds(self):
        for index, value in [(i, i+1) for i in range(9)] + [(10, 0)]:
            s = self.state()
            s.field, s.fresh, s.digits = 5, True, 1
            self.lib.stage_core_touch(C.byref(s), 38+index%3*62, 78+index//3*27)
            self.assertEqual(s.draft, value)
        for index in range(8):
            s = self.state()
            s.field = 3
            self.lib.stage_core_touch(C.byref(s), 50+index%2*96, 70+index//2*28)
            self.assertEqual(s.draft, index)

    def test_short_interval_deadlines_do_not_add_a_second_gap_on_overrun(self):
        for begin, now, interval, expected, overrun in [
            (100000, 100500, 1, 101000, False),
            (100000, 101000, 1, 101000, False),
            (100000, 102500, 1, 102500, True),
            (100000, 300000, 100, 300000, True),
            (4294967295000, 4294967295500, 60000, 4295027295000, False),
        ]:
            limited = C.c_bool(False)
            self.assertEqual(self.lib.stage_next_test_deadline(begin, now, interval, C.byref(limited)), expected)
            self.assertEqual(limited.value, overrun)
