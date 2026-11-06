"""Real pipeline C, Win32 queues/threads in place of FreeRTOS and RF capture."""
import ctypes as C
import time
import unittest
from test_stage_c import ROOT


class Metrics(C.Structure):
    _fields_=[('elapsed_us',C.c_uint32),('variance',C.c_uint32),('peak',C.c_uint32),
              ('clipped',C.c_uint32),('dc_i',C.c_int32),('dc_q',C.c_int32)]


class Sample(C.Structure):
    _fields_=[('iq',C.POINTER(C.c_int8)),('band',C.c_uint),('index',C.c_uint),
              ('captures',C.c_uint32),('errors',C.c_uint32),('listening_us',C.c_uint64),
              ('waiting_us',C.c_uint64),('error',C.c_int),('metrics',Metrics),('wifi',C.c_uint),('focus_ch',C.c_uint),('rate_hz',C.c_uint)]


class PipelineThreadTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.lib=C.CDLL(str(ROOT/'artifacts/rx_pipeline_host.dll'))
        cls.lib.esp_timer_get_time.restype=C.c_int64
        cls.lib.stage_rx_open.argtypes=[C.c_uint,C.c_int64,C.POINTER(C.c_void_p)]
        cls.lib.stage_rx_next.argtypes=[C.c_void_p,C.POINTER(C.POINTER(Sample))]
        cls.lib.stage_rx_recycle.argtypes=[C.c_void_p,C.POINTER(Sample)]
        cls.lib.stage_rx_close.argtypes=[C.c_void_p]
        cls.lib.stage_rx_focus.argtypes=[C.c_void_p,C.c_uint,C.c_uint]

    def setUp(self):
        self.lib.mock_rx_reset(0,0,0,0)
        self.pipeline=C.c_void_p()

    def tearDown(self):
        if self.pipeline.value:
            self.lib.stage_rx_close(self.pipeline)
        until=time.monotonic()+1
        while self.lib.mock_rx_threads() and time.monotonic()<until:time.sleep(.001)
        self.assertEqual(self.lib.mock_rx_threads(),0)
        self.assertEqual(self.lib.mock_rx_queues(),0)

    def open(self,ch=0,ms=1000):
        return self.lib.stage_rx_open(ch,self.lib.esp_timer_get_time()+ms*1000,C.byref(self.pipeline))

    def next(self):
        sample=C.POINTER(Sample)()
        deadline=time.monotonic()+1
        result=0x107
        while result==0x107 and time.monotonic()<deadline:
            result=self.lib.stage_rx_next(self.pipeline,C.byref(sample))
        return result,sample

    def close(self):
        result=self.lib.stage_rx_close(self.pipeline);self.pipeline=C.c_void_p();return result

    def test_full_queue_keeps_owned_sample_immutable_and_stops_cleanly(self):
        self.assertEqual(self.open(),0)
        result,sample=self.next();self.assertEqual(result,0)
        first=C.string_at(sample.contents.iq,32256)
        self.assertEqual((first[0],first[1]),(sample.contents.captures%128,255))
        deadline=time.monotonic()+1
        while self.lib.mock_rx_captures()<2 and time.monotonic()<deadline:time.sleep(.001)
        time.sleep(.04)
        self.assertEqual(self.lib.mock_rx_captures(),2)
        self.assertEqual(C.string_at(sample.contents.iq,32256),first)
        self.lib.stage_rx_recycle(self.pipeline,sample)
        self.assertEqual(self.close(),0)
        self.assertEqual(self.lib.mock_rx_releases(),1)

    def test_psram_allows_four_owned_capture_slots(self):
        self.lib.mock_rx_psram(1)
        self.assertEqual(self.open(),0)
        result,sample=self.next();self.assertEqual(result,0)
        first=C.string_at(sample.contents.iq,32256)
        deadline=time.monotonic()+1
        while self.lib.mock_rx_captures()<4 and time.monotonic()<deadline:time.sleep(.001)
        time.sleep(.04)
        self.assertEqual(self.lib.mock_rx_captures(),4)
        self.assertEqual(C.string_at(sample.contents.iq,32256),first)
        self.lib.stage_rx_recycle(self.pipeline,sample)
        self.assertEqual(self.close(),0)

    def test_all_six_bands_are_visited_and_fixed_channel_stays_in_band(self):
        self.assertEqual(self.open(),0)
        for _ in range(18):
            result,sample=self.next();self.assertEqual(result,0)
            self.lib.stage_rx_recycle(self.pipeline,sample)
        self.close()
        self.assertEqual([self.lib.mock_rx_wifi(i) for i in range(18)],
                         [2]*3+[4]*3+[6]*3+[8]*3+[10]*3+[11]*3)
        self.lib.mock_rx_reset(0,0,0,0)
        self.assertEqual(self.open(ch=14),0)
        for _ in range(5):
            result,sample=self.next();self.assertEqual(result,0)
            self.assertEqual(sample.contents.band,2)
            self.lib.stage_rx_recycle(self.pipeline,sample)
        self.close()
        self.assertEqual([self.lib.mock_rx_wifi(i) for i in range(5)],[6]*5)

    def test_cancel_with_full_queue_and_deadline_release_radio(self):
        self.assertEqual(self.open(),0);time.sleep(.02);self.lib.mock_rx_cancel()
        self.assertEqual(self.close(),0)
        self.assertEqual(self.lib.mock_rx_releases(),1)
        self.lib.mock_rx_reset(0,0,0,0)
        self.assertEqual(self.open(ms=5),0)
        time.sleep(.02)
        while True:
            result,sample=self.next()
            if result==0:self.lib.stage_rx_recycle(self.pipeline,sample)
            elif result==0x105:break
            else:self.assertEqual(result,0x107)
        self.assertEqual(self.close(),0)

    def test_capture_failure_and_short_buffer_stop_after_eight_attempts(self):
        for capture_error,bad_size,expected in [(-1,0,-1),(0,1,0x108)]:
            self.lib.mock_rx_reset(0,capture_error,bad_size,0)
            self.assertEqual(self.open(),0)
            for _ in range(8):
                result,sample=self.next();self.assertEqual(result,0)
                self.assertEqual(sample.contents.error,expected)
                self.lib.stage_rx_recycle(self.pipeline,sample)
            self.assertEqual(self.close(),expected)
            self.assertEqual(self.lib.mock_rx_releases(),1)

    def test_startup_failure_cleans_queue_and_rf(self):
        self.lib.mock_rx_reset(-1,0,0,0)
        self.assertEqual(self.open(),-1);self.assertFalse(self.pipeline.value)
        self.assertEqual(self.lib.mock_rx_releases(),0)
        self.lib.mock_rx_reset(0,0,0,1)
        self.assertEqual(self.open(),0x101);self.assertFalse(self.pipeline.value)
        self.assertEqual(self.lib.mock_rx_releases(),1)

    def test_wifi_memory_pressure_falls_back_to_single_slot(self):
        self.lib.mock_rx_fail_init_once()
        self.assertEqual(self.open(),0)
        result,sample=self.next();self.assertEqual(result,0)
        time.sleep(.04)
        self.assertEqual(self.lib.mock_rx_captures(),1)
        self.lib.stage_rx_recycle(self.pipeline,sample)
        result,sample=self.next();self.assertEqual(result,0)
        self.assertEqual(sample.contents.index,0)
        self.lib.stage_rx_recycle(self.pipeline,sample)
        self.assertEqual(self.close(),0)

    def test_candidate_requests_40msps_verification_at_same_lo(self):
        self.assertEqual(self.open(),0)
        self.lib.stage_rx_focus(self.pipeline,14,6)
        seen=[]
        for _ in range(10):
            result,sample=self.next();self.assertEqual(result,0)
            if sample.contents.focus_ch:
                seen.append((sample.contents.focus_ch,sample.contents.wifi,sample.contents.rate_hz))
            self.lib.stage_rx_recycle(self.pipeline,sample)
        self.assertIn((14,6,40000000),seen)

    def test_request_during_active_verification_survives_cooldown(self):
        self.assertEqual(self.open(ms=3000),0)
        self.lib.stage_rx_focus(self.pipeline,14,6)
        queued=False
        seen_second=False
        for _ in range(180):
            result,sample=self.next();self.assertEqual(result,0)
            if sample.contents.focus_ch==14 and not queued:
                self.lib.stage_rx_focus(self.pipeline,8,4)
                queued=True
            if sample.contents.focus_ch==8:
                self.assertEqual(sample.contents.rate_hz,40000000)
                seen_second=True
            self.lib.stage_rx_recycle(self.pipeline,sample)
            if seen_second:break
        self.assertTrue(queued)
        self.assertTrue(seen_second)
