"""Manual targets use normal touch editing and the shared joint RF cache."""
import ctypes as C
import unittest
from test_stage_c import ROOT, Ui


class ManualTargetTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.lib = C.CDLL(str(ROOT / 'artifacts/stage_host.dll'))
        cls.lib.stage_manual_add.restype = C.c_bool
        cls.lib.stage_target_add.restype = C.c_bool

    def setUp(self):
        self.state = Ui()
        self.lib.stage_core_init(C.byref(self.state))
        self.state.page = 2

    def touch(self, x, y):
        return self.lib.stage_core_touch(C.byref(self.state), x, y)

    def add(self):
        self.assertEqual(self.touch(100, 150), 6)
        return self.lib.stage_manual_add(C.byref(self.state))

    def open_manual(self):
        self.touch(150, 43)
        self.assertEqual(self.state.page, 3)
        self.touch(40, 35)
        self.assertEqual(self.state.page, 4)

    def test_touch_entry_edit_ch_and_id_add_and_return_to_joint(self):
        self.open_manual()
        original = bytes(self.state.config), bytes(self.state.joint)
        self.assertEqual(self.lib.stage_core_boot(C.byref(self.state)), 0)
        self.touch(30, 80)
        self.assertEqual(self.state.field, 0)
        self.touch(160, 80)  # 3
        self.touch(100, 80)  # 2
        self.assertEqual(self.touch(150, 185), 0)
        self.assertEqual(self.state.manual.ch, 32)
        self.touch(150, 80)
        self.assertEqual(self.state.field, 2)
        self.touch(160, 133)  # 9
        self.touch(160, 133)  # 9
        self.touch(150, 185)
        self.assertTrue(self.add())
        self.assertEqual((self.state.target_count, self.state.targets[0].ch,
                          self.state.targets[0].id, self.state.targets[0].hits), (1,32,99,0))
        self.assertEqual(self.state.page, 4)
        self.assertEqual((bytes(self.state.config), bytes(self.state.joint)), original)
        self.touch(100, 189)
        self.assertEqual(self.state.page, 3)
        self.touch(100, 189)
        self.assertEqual(self.state.page, 2)
        self.assertEqual(self.lib.stage_core_boot(C.byref(self.state)), 2)

    def test_multiple_targets_off_duplicate_and_scan_hit_share_one_list(self):
        self.open_manual()
        for ch,ident in [(1,0),(14,99),(14,1)]:
            self.state.manual.ch=ch
            self.state.manual.id=ident
            self.assertTrue(self.add())
        self.assertEqual(self.state.target_count,3)
        self.assertFalse(self.add())
        self.assertEqual((self.state.manual_status,self.state.target_count,
                          self.state.targets[2].hits),(2,3,0))
        self.assertFalse(self.lib.stage_target_add(C.byref(self.state),14,1))
        self.assertEqual(self.state.targets[2].hits,1)

    def test_off_key_and_cancel_keep_staged_value(self):
        self.open_manual()
        self.state.manual.id=99
        self.touch(150,80)
        self.touch(30,160)  # OFF
        self.touch(30,185)  # cancel
        self.assertEqual(self.state.manual.id,99)
        self.touch(150,80)
        self.touch(30,160)
        self.touch(150,185)
        self.assertEqual(self.state.manual.id,0)

    def test_invalid_input_and_capacity_do_not_add(self):
        self.open_manual()
        for ch,ident in [(0,0),(33,0),(1,100)]:
            self.state.manual.ch=ch;self.state.manual.id=ident
            self.assertFalse(self.add())
        for ch in range(1,33):
            self.state.manual.ch=ch;self.state.manual.id=0
            self.assertTrue(self.add())
        self.assertEqual(self.state.list_page,7)
        self.state.manual.id=1
        self.assertFalse(self.add())
        self.assertEqual((self.state.manual_status,self.state.target_count),(3,32))
        self.state.manual.id=0
        self.assertFalse(self.add())
        self.assertEqual(self.state.manual_status,2)

    def test_scanning_cannot_enter_manual_and_render_bounds(self):
        self.state.page=3;self.state.scanning=True
        self.touch(40,35)
        self.assertEqual(self.state.page,3)
        self.state.scanning=False
        self.touch(40,35)
        for status in range(5):
            self.state.manual_status=status
            guarded=(C.c_uint8*5016)(*[0xa5]*5016)
            self.lib.stage_view_render(C.byref(self.state),C.cast(C.byref(guarded,8),C.POINTER(C.c_uint8)))
            self.assertEqual(bytes(guarded[:8]),b'\xa5'*8)
            self.assertEqual(bytes(guarded[-8:]),b'\xa5'*8)

    def test_manual_targets_feed_joint_wave_cache(self):
        self.open_manual()
        self.state.manual.ch=1
        self.assertTrue(self.add())
        self.state.manual.ch=32;self.state.manual.id=99
        self.assertTrue(self.add())
        self.lib.joint_cache_close();self.lib.mock_flash_reset()
        workspace=(C.c_uint32*16128)()
        self.assertEqual(self.lib.joint_cache_prepare(self.state.targets,self.state.target_count,
                                                    workspace,None),0)
        self.assertEqual(self.lib.mock_flash_write_calls(),3)
        self.lib.joint_cache_close()
