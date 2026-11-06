import ctypes as C
import unittest
from test_stage_c import ROOT, Ui, Target


class WaveWorkspaceTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.lib=C.CDLL(str(ROOT/'artifacts/stage_host.dll'))
        cls.lib.stage_wave_workspace.restype=C.POINTER(C.c_uint32)
        cls.lib.joint_cache_wave.restype=C.POINTER(C.c_uint32)

    def test_single_cache_build_with_no_large_contiguous_heap_block(self):
        self.lib.stage_cache_close();self.lib.joint_cache_close();self.lib.mock_flash_reset()
        s=Ui();self.lib.stage_core_init(C.byref(s));s.config.group=1
        self.lib.mock_wave_heap_limit(1)
        try:
            self.assertEqual(self.lib.stage_cache_build(C.byref(s.config),None),0)
            self.assertEqual(self.lib.stage_cache_open(C.byref(s.config)),0)
        finally:
            self.lib.mock_wave_heap_limit(0);self.lib.stage_cache_close()

    def test_joint_uses_same_reusable_workspace_and_keeps_cached_waves(self):
        self.lib.stage_cache_close();self.lib.joint_cache_close();self.lib.mock_flash_reset()
        targets=(Target*2)(Target(11,0,1),Target(14,99,1))
        work=self.lib.stage_wave_workspace()
        self.assertEqual(C.addressof(work.contents),C.addressof(self.lib.stage_wave_workspace().contents))
        self.assertEqual(self.lib.joint_cache_prepare(targets,2,work,None),0)
        before=C.string_at(self.lib.joint_cache_wave(0),64512)
        C.memset(work,0,64512)
        self.assertEqual(C.string_at(self.lib.joint_cache_wave(0),64512),before)
        self.lib.joint_cache_close()

    def test_joint_failure_uses_execution_error_view(self):
        s=Ui();self.lib.stage_core_init(C.byref(s))
        s.page=2;s.joint_active=True;s.mode=4;s.result=0x101;s.run=s.config
        frame=(C.c_uint8*5000)();other=(C.c_uint8*5000)()
        self.lib.stage_view_render(C.byref(s),frame)
        s.cache_ready=True;self.lib.stage_view_render(C.byref(s),other)
        self.assertEqual(bytes(frame[:153*25]),bytes(other[:153*25]))

    def test_preparation_failure_displays_different_error_codes(self):
        s=Ui();self.lib.stage_core_init(C.byref(s));s.mode=4;s.result=0x101
        frame=(C.c_uint8*5000)();other=(C.c_uint8*5000)()
        self.lib.stage_view_render(C.byref(s),frame)
        s.result=0x103;self.lib.stage_view_render(C.byref(s),other)
        self.assertNotEqual(bytes(frame),bytes(other))
