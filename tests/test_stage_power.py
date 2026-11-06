"""Execute the production battery hold setup against a GPIO latch/direction model."""
import ctypes as C
import unittest
from test_stage_c import ROOT


@unittest.skipUnless((ROOT/'artifacts/stage_host.dll').exists(), 'native C host library not built')
class PowerTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.lib = C.CDLL(str(ROOT/'artifacts/stage_host.dll'))

    def test_battery_latch_high_before_output_without_touching_other_pins(self):
        self.lib.mock_gpio_reset(0)
        for attempt in range(2):
            self.assertEqual(self.lib.stage_board_power_init(), 0)
            self.assertEqual(self.lib.mock_gpio_level(), 1)
            self.assertEqual(self.lib.mock_gpio_enabled(), 1)
            self.assertEqual(self.lib.mock_gpio_low_pulse(), 0)
            self.assertEqual(self.lib.mock_gpio_unrelated(), 0)
        self.assertEqual(self.lib.mock_gpio_calls(), 4)

    def test_gpio_failures_propagate_and_do_not_enable_a_low_output(self):
        for failure in [1, 2]:
            self.lib.mock_gpio_reset(failure)
            self.assertNotEqual(self.lib.stage_board_power_init(), 0)
            self.assertEqual(self.lib.mock_gpio_calls(), failure)
            self.assertEqual(self.lib.mock_gpio_enabled(), 0)
            self.assertEqual(self.lib.mock_gpio_low_pulse(), 0)
