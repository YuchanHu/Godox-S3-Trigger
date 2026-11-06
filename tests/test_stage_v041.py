"""Verify live preparation phases, compact battery alignment and renamed title.
Only inspect binary frame buffers; no images are exported.
"""
import ctypes as C
import unittest
from PIL import Image, ImageDraw, ImageFont
from test_stage_c import ROOT, Ui

PROGRESS = C.CFUNCTYPE(C.c_int, C.c_int, C.c_uint, C.c_uint, C.c_int, C.c_uint)


@unittest.skipUnless((ROOT/'artifacts/stage_host.dll').exists(), 'native C host library not built')
class StageV041Tests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.lib = C.CDLL(str(ROOT/'artifacts/stage_host.dll'))
        cls.font = ImageFont.truetype(str(ROOT/'assets/QiuYeYuanTi-16.ttf'), 16)

    def state(self):
        s = Ui()
        self.lib.stage_core_init(C.byref(s))
        s.cache_ready = True
        return s

    def image(self, s):
        frame = (C.c_uint8*5000)()
        self.lib.stage_view_render(C.byref(s), frame)
        return Image.frombytes('1', (200, 200), bytes(frame))

    def test_battery_icon_and_ink_are_centered_with_four_pixel_gap(self):
        for percent in [0, 1, 9, 10, 78, 99, 100, None]:
            s = self.state()
            s.battery_valid = percent is not None
            s.battery_percent = percent or 0
            header = self.image(s).crop((100, 0, 200, 24)).convert('L')
            pixels = header.load()
            columns = [any(pixels[x, y] for y in range(24)) for x in range(100)]
            left = columns.index(True)
            icon = header.crop((left, 0, left+20, 24))
            self.assertEqual(icon.getbbox(), (0, 7, 20, 17))
            remaining = header.crop((left+20, 0, 100, 24)).getbbox()
            self.assertEqual(remaining[0], 4)
            self.assertLessEqual(abs((remaining[1]+remaining[3])/2 - 12), .5)

    def test_home_and_preparing_titles_are_trigger_control_qiu16(self):
        expected = Image.new('L', (64, 24))
        ImageDraw.Draw(expected).text((0, 18), '引闪控制', font=self.font, fill=255, anchor='ls')
        expected = expected.point(lambda v: 255 if v >= 128 else 0, mode='1')
        for preparing in [False, True]:
            s = self.state()
            s.preparing = preparing
            self.assertEqual(self.image(s).crop((8, 0, 72, 24)).tobytes(), expected.tobytes())

    def test_detail_matches_current_stage_and_wave(self):
        cases = [(0, 0, 0, '准备重建波形'), (1, 0, 0, '正在检查已有缓存'),
                 (2, 0, 0, '正在擦除旧缓存'), (5, 0, 0, '正在保存缓存索引')]
        for phase, action in [(1, '校验'), (3, '计算'), (4, '保存'), (6, '校验')]:
            for wave, group, target in [(1, 0, '准备'), (2, 0, '模式'), (3, 1, '功率A'),
                                         (3, 5, '功率E'), (4, 0, '引闪'), (5, 0, '收尾')]:
                cases.append((phase, wave, group, action+target+'波形'))
        for phase, wave, group, label in cases:
            s = self.state()
            s.preparing, s.prep_phase, s.prep_wave, s.prep_group = True, phase, wave, group
            mask = Image.new('L', (200, 20))
            text_width = round(self.font.getlength(label))
            self.assertLessEqual(text_width, 160)
            ImageDraw.Draw(mask).text((100-text_width//2, 16), label,
                                      font=self.font, fill=255, anchor='ls')
            expected = mask.point(lambda v: 0 if v >= 128 else 255, mode='1')
            actual = self.image(s).crop((16, 128, 184, 148))
            self.assertEqual(actual.tobytes(), expected.crop((16, 0, 184, 20)).tobytes(), label)

    def test_build_reports_real_operations_and_only_finishes_after_readback(self):
        s = self.state()
        self.lib.stage_cache_close()
        self.lib.mock_flash_reset()
        events = []
        def capture(phase, done, total, wave, group):
            events.append((phase, done, total, wave, group,
                           self.lib.mock_flash_erase_calls(), self.lib.mock_flash_write_calls()))
            return True
        callback = PROGRESS(capture)
        self.assertEqual(self.lib.stage_cache_build(C.byref(s.config), callback), 0)
        self.assertEqual(events[0], (2, 0, 29, 0, 0, 0, 0))
        self.assertEqual([e[1] for e in events], sorted(e[1] for e in events))
        self.assertTrue(all(e[2] == 29 for e in events))
        for phase in [3, 4]:
            self.assertEqual(len([e for e in events if e[0] == phase]), 9)
        self.assertEqual([e[4] for e in events if e[0] == 3 and e[3] == 3], [1, 2, 3, 4, 5])
        self.assertEqual([e for e in events if e[0] == 5], [(5, 19, 29, 0, 0, 1, 9)])
        self.assertEqual(events[-1], (6, 29, 29, 0, 0, 1, 10))
        self.assertTrue(all(e[1] < 29 for e in events[:-1]))
        # Cache hit performs only validation, with no new erases/writes.
        events.clear()
        self.assertEqual(self.lib.stage_cache_open_report(C.byref(s.config), callback), 0)
        self.assertTrue(all(e[0] == 1 and e[2] == 9 and e[5:] == (1, 10) for e in events))
        self.assertEqual(events[-1][1], 9)
        self.lib.stage_cache_close()

    def test_write_failure_never_reports_later_stages_or_completion(self):
        s = self.state()
        s.config.group = 1
        self.lib.stage_cache_close()
        self.lib.mock_flash_reset()
        self.lib.mock_flash_fail_write(2)
        events = []
        callback = PROGRESS(lambda p,d,t,w,g: events.append((p,d,t,w,g)) or True)
        self.assertNotEqual(self.lib.stage_cache_build(C.byref(s.config), callback), 0)
        self.assertTrue(all(p not in (5, 6) and d < t for p,d,t,w,g in events))
        self.assertEqual(events[-1][:3], (4, 4, 17))
        self.assertEqual(self.lib.mock_flash_map_count(), 0)
        self.lib.mock_flash_fail_write(0)

    def test_stop_before_erase_and_during_readback_releases_mapping(self):
        s = self.state()
        s.config.group = 1
        self.lib.stage_cache_close()
        self.lib.mock_flash_reset()
        stop = PROGRESS(lambda p,d,t,w,g: False)
        self.assertNotEqual(self.lib.stage_cache_build(C.byref(s.config), stop), 0)
        self.assertEqual(self.lib.mock_flash_erase_calls(), 0)
        self.assertEqual(self.lib.mock_flash_write_calls(), 0)
        stop = PROGRESS(lambda p,d,t,w,g: not (p == 6 and w == 3))
        self.assertNotEqual(self.lib.stage_cache_build(C.byref(s.config), stop), 0)
        self.assertEqual(self.lib.mock_flash_map_count(), 0)
        # Completed data on flash may be valid, but cancellation never publishes a live mapping.
        self.assertEqual(self.lib.stage_cache_open(C.byref(s.config)), 0)
        self.lib.stage_cache_close()
