"""PWR returns to the main menu before it can request deep sleep."""
import ctypes as C
import unittest
from test_stage_c import ROOT, Ui


class PowerNavigationTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.lib = C.CDLL(str(ROOT / 'artifacts/stage_host.dll'))

    def state(self):
        state = Ui()
        self.lib.stage_core_init(C.byref(state))
        return state

    def test_every_function_page_returns_home_before_sleep(self):
        for page in [0,2,3,4]:
            s=self.state();s.page=page
            self.assertEqual(self.lib.stage_core_power(C.byref(s)),7)
            self.assertFalse(s.sleep_requested)
            self.lib.stage_core_home(C.byref(s))
            self.assertEqual((s.page,s.mode,s.field),(1,0,-1))
            self.assertEqual(self.lib.stage_core_power(C.byref(s)),8)

    def test_running_scanning_preparing_and_finished_pages_return_home(self):
        for mode,preparing,scanning in [(1,False,False),(0,True,False),
                                        (0,False,True),(2,False,False),(3,False,False),(4,False,False)]:
            s=self.state();s.page=2;s.mode=mode;s.preparing=preparing;s.scanning=scanning
            s.joint_active=True;s.legacy=True;s.scan_seconds=120
            self.assertEqual(self.lib.stage_core_power(C.byref(s)),7)
            self.lib.stage_core_home(C.byref(s))
            self.assertEqual((s.preparing,s.scanning,s.joint_active,s.legacy,s.scan_seconds),
                             (False,False,False,False,0))
            self.assertEqual(self.lib.stage_core_boot(C.byref(s)),0)

    def test_exit_editor_keeps_saved_parameters_and_manual_targets(self):
        s=self.state();s.page=4;s.field=2;s.draft=99
        s.manual.ch=14;s.manual.id=0
        self.lib.stage_manual_add(C.byref(s))
        saved=bytes(s.config),bytes(s.joint),bytes(s.manual),bytes(s.targets)
        self.lib.stage_core_home(C.byref(s))
        self.assertEqual(s.field,-1)
        self.assertEqual((bytes(s.config),bytes(s.joint),bytes(s.manual),bytes(s.targets)),saved)
        self.assertEqual(s.target_count,1)

    def test_home_busy_view_returns_home_and_shutdown_is_not_repeated(self):
        s=self.state();s.page=1;s.preparing=True
        self.assertEqual(self.lib.stage_core_power(C.byref(s)),7)
        s.preparing=False
        self.assertEqual(self.lib.stage_core_power(C.byref(s)),8)
        s.sleep_requested=True
        self.assertEqual(self.lib.stage_core_power(C.byref(s)),0)
