"""Exercise the production EPD command sequencer against an SSD1681 RAM model.

Models address counters, reset defaults, reference/target banks and BUSY exclusion.
It does not model physical ink waveforms, temperature, or ghosting.
"""
import ctypes as C
import unittest

from test_stage_c import ROOT, Ui, Rect

TRANSFER = C.CFUNCTYPE(C.c_int, C.c_void_p, C.c_bool, C.POINTER(C.c_uint8), C.c_size_t)
CONTROL = C.CFUNCTYPE(C.c_int, C.c_void_p)


class Io(C.Structure):
    _fields_ = [('transfer', TRANSFER), ('reset', CONTROL), ('wait_idle', CONTROL), ('ctx', C.c_void_p)]


class Controller:
    def __init__(self):
        self.panel = bytearray([255] * 5000)
        self.events = []
        self.errors = []
        self.busy = False
        self.fail_wait = False
        self.fail_transfer = False
        self.defaults()
        # Keep callbacks alive for the lifetime of the C call.
        self.callbacks = (TRANSFER(self.transfer), CONTROL(self.reset), CONTROL(self.wait))
        self.io = Io(*self.callbacks, None)

    def defaults(self):
        self.mode = 3  # SSD1681 0x11 POR: X increment, Y increment
        self.xs, self.xe, self.ys, self.ye = 0, 24, 0, 199
        self.x, self.y = 0, 0
        self.flags, self.command = 0, None
        self.banks = {0x24: bytearray([0xA5] * 5000), 0x26: bytearray([0x5A] * 5000)}

    def reset(self, _):
        if self.busy:
            self.errors.append('reset during BUSY')
        self.events.append(('reset',))
        # Intentionally lose RAM on reset to require explicit baseline seeding.
        self.defaults()
        return 0

    def wait(self, _):
        self.events.append(('wait',))
        if self.fail_wait:
            return 0x107
        self.busy = False
        return 0

    def transfer(self, _, data, ptr, size):
        if self.fail_transfer:
            return -1
        if self.busy:
            self.errors.append('SPI write during BUSY')
        payload = C.string_at(ptr, size)
        if not data:
            self.command = payload[0]
            if self.command == 0x12:
                self.defaults()
                self.busy = True
            elif self.command == 0x20:
                if self.flags in (0xC7, 0xCF):
                    self.events.append(('refresh', self.flags, self.xs, self.xe, self.ys, self.ye))
                    if self.flags == 0xCF and self.banks[0x26] != self.panel:
                        self.errors.append('reference differs from displayed image at activation')
                    self.panel[:] = self.banks[0x24]
                self.busy = True
            return 0
        cmd = self.command
        if cmd in (0x24, 0x26):
            self.events.append(('ram', cmd, len(payload)))
            for value in payload:
                if not (0 <= self.x < 25 and 0 <= self.y < 200):
                    self.errors.append('RAM cursor outside screen')
                    break
                self.banks[cmd][(199 - self.y) * 25 + self.x] = value
                self.x += 1 if self.mode & 1 else -1
                if self.x > self.xe or self.x < self.xs:
                    self.x = self.xs if self.mode & 1 else self.xe
                    self.y += 1 if self.mode & 2 else -1
                    if self.y > max(self.ys, self.ye) or self.y < min(self.ys, self.ye):
                        self.y = self.ys
        elif cmd == 0x11:
            self.mode = payload[0]
        elif cmd == 0x44:
            self.xs, self.xe = payload
        elif cmd == 0x45:
            self.ys = int.from_bytes(payload[:2], 'little')
            self.ye = int.from_bytes(payload[2:], 'little')
        elif cmd == 0x4E:
            self.x = payload[0]
        elif cmd == 0x4F:
            self.y = int.from_bytes(payload, 'little')
        elif cmd == 0x22:
            self.flags = payload[0]
        elif cmd == 0x37:
            if payload[5] & 0x40:
                self.errors.append('automatic ping-pong mixed with explicit bank ownership')
        return 0


@unittest.skipUnless((ROOT / 'artifacts/stage_host.dll').exists(), 'native C host library not built')
class EpdTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.lib = C.CDLL(str(ROOT / 'artifacts/stage_host.dll'))
        cls.lib.stage_epd_refresh.restype = C.c_int
        cls.lib.stage_frame_diff.restype = C.c_bool

    def setUp(self):
        self.controller = Controller()
        self.old = (C.c_uint8 * 5000)(*[255] * 5000)
        self.scratch = (C.c_uint8 * 5000)()

    def update(self, values, full=False):
        new = (C.c_uint8 * 5000).from_buffer_copy(bytes(values))
        rect = Rect(0, 0, 200, 200)
        if not full:
            self.assertTrue(self.lib.stage_frame_diff(self.old, new, C.byref(rect)))
        result = self.lib.stage_epd_refresh(C.byref(self.controller.io), full, self.old,
                                             new, self.scratch, C.byref(rect))
        if result == 0:
            self.old = new
            self.assertEqual(self.controller.panel, bytes(new))
            for bank in self.controller.banks.values():
                self.assertEqual(bank, bytes(new))
            self.assertEqual(self.controller.errors, [])
            self.assertEqual((self.controller.xs, self.controller.xe,
                              self.controller.ys, self.controller.ye), (0, 24, 199, 0))
        return result

    def test_full_reset_reseeds_both_banks_and_restores_direction(self):
        image = bytes((i * 17 + i // 25) % 256 for i in range(5000))
        self.assertEqual(self.update(image, full=True), 0)
        self.assertEqual(self.controller.mode, 1)
        self.assertEqual(sum(e[0] == 'reset' for e in self.controller.events), 2)
        last_reset = max(i for i, e in enumerate(self.controller.events) if e[0] == 'reset')
        writes = [e for e in self.controller.events[last_reset:] if e[0] == 'ram']
        self.assertEqual(writes, [('ram', 0x24, 5000), ('ram', 0x26, 5000)])

    def test_nonoverlapping_edge_updates_keep_reference_synchronized(self):
        self.update(bytes(self.old), full=True)
        for y, x, height, width in [(0, 0, 1, 1), (31, 2, 44, 12),
                                     (120, 14, 45, 11), (199, 24, 1, 1)]:
            image = bytearray(self.old)
            for row in range(y, y + height):
                for col in range(x, x + width):
                    image[row * 25 + col] ^= (row + col) % 255 + 1
            start = len(self.controller.events)
            self.assertEqual(self.update(image), 0)
            events = self.controller.events[start:]
            writes = [e for e in events if e[0] == 'ram']
            self.assertEqual(writes, [('ram', 0x26, height * width),
                                      ('ram', 0x24, height * width),
                                      ('ram', 0x26, height * width)])
            self.assertIn(('refresh', 0xCF, 0, 24, 199, 0), events)

    def test_actual_ui_ready_running_done_stop_and_modal_transitions(self):
        s = Ui()
        self.lib.stage_core_init(C.byref(s))
        s.cache_ready = True
        s.run = s.config
        sequence = [(0, 0, False, -1), (1, 0, False, -1), (1, 1, False, -1),
                    (1, 10, False, -1), (2, 10, False, -1), (1, 0, False, -1),
                    (3, 1, False, -1), (0, 0, False, 0), (0, 0, True, -1),
                    (0, 0, False, -1), (1, 0, False, -1), (4, 2, False, -1)]
        for index, (mode, sent, preparing, field) in enumerate(sequence):
            s.mode, s.sent, s.preparing, s.field = mode, sent, preparing, field
            s.prep_done, s.prep_total = 3, 9
            image = (C.c_uint8 * 5000)()
            self.lib.stage_view_render(C.byref(s), image)
            with self.subTest(mode=mode, sent=sent, preparing=preparing):
                self.assertEqual(self.update(image, full=index == 0), 0)

    def test_busy_timeout_aborts_without_committing_reference_and_full_recovers(self):
        self.update(bytes(self.old), full=True)
        image = bytearray(self.old)
        image[50 * 25 + 5] = 0
        before = bytes(self.old)
        self.controller.fail_wait = True
        self.assertEqual(self.update(image), 0x107)
        self.assertEqual(bytes(self.old), before)
        self.assertEqual(self.controller.banks[0x26], before)
        self.controller.fail_wait = False
        self.controller.busy = False  # timed-out hardware has eventually gone idle
        self.assertEqual(self.update(image, full=True), 0)

    def test_spi_error_propagates_without_advancing_host_frame(self):
        self.update(bytes(self.old), full=True)
        image = bytearray(self.old)
        image[-1] = 0
        before = bytes(self.old)
        self.controller.fail_transfer = True
        self.assertEqual(self.update(image), -1)
        self.assertEqual(bytes(self.old), before)
        self.controller.fail_transfer = False
        self.assertEqual(self.update(image, full=True), 0)
