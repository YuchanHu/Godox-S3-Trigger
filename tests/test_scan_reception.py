"""Regressions for repeated ID-OFF transmissions staying absent from the list."""
import ctypes as C
import unittest
import numpy as np
import test_scan_pipeline as scan
import test_scan_confidence as confidence
from test_scan_confidence import Confirmation
from test_stage_c import ROOT


class ScanReceptionTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        scan.ScanTests.setUpClass()
        cls.lib=scan.ScanTests.lib

    signal=scan.ScanTests.signal
    packed=scan.ScanTests.packed
    decode=scan.ScanTests.decode
    samples40=confidence.ScanConfidenceTests.samples40
    decode40=confidence.ScanConfidenceTests.decode40

    def header(self,iq,offset):
        ids=(C.c_uint*4)()
        carriers=(C.c_int32*4)()
        diag=(C.c_uint32*5)()
        n=self.lib.godox_verify_iq8_header(iq.ctypes.data_as(C.POINTER(C.c_int8)),
                 C.c_size_t(len(iq)//2),offset,ids,4,diag,carriers)
        return list(ids[:n]),list(carriers[:n])

    def test_truncated_long_frame_verifies_header_on_all_channels(self):
        for ch in range(1,33):
            wifi=self.lib.godox_scan_wifi(self.lib.godox_scan_band(ch))
            for variant in [0,1]:
                offset=self.lib.godox_scan_offset(ch,variant,wifi)
                original=self.samples40(0,offset,long=True)
                for delay in [2000,4000]:
                    iq=np.zeros_like(original)
                    iq[2*delay:]=original[:-2*delay]
                    self.assertEqual(self.decode40(iq,offset)[0],[])
                    ids,carriers=self.header(iq,offset)
                    self.assertEqual(ids,[0],(ch,variant,delay))
                    self.assertEqual(self.lib.godox_scan_locate(wifi,carriers[0]),ch)

    def test_header_cannot_create_candidate_or_confirm_another_id(self):
        state=Confirmation()
        self.assertEqual(self.lib.godox_scan_confirm(C.byref(state),14,0,40000000,100),0)
        self.assertEqual(self.lib.godox_scan_pending(C.byref(state),100),0)
        self.assertEqual(self.lib.godox_scan_confirm(C.byref(state),14,0,16000000,200),-1)
        self.assertEqual(self.lib.godox_scan_confirm(C.byref(state),14,1,40000000,300),0)
        self.assertEqual(self.lib.godox_scan_confirm(C.byref(state),14,0,40000000,10000),1)
        self.assertEqual(self.lib.godox_scan_pending(C.byref(state),11000),0)

    def test_pending_retries_without_new_full_frame_and_serves_multiple_channels(self):
        state=Confirmation()
        for ch in [14,8,32]:
            self.lib.godox_scan_confirm(C.byref(state),ch,0,16000000,100)
        self.assertEqual([self.lib.godox_scan_pending(C.byref(state),ms)
                          for ms in [100,1600,3100,4600,6100,7600]],
                         [14,8,32,14,8,32])
        self.assertEqual(self.lib.godox_scan_pending(C.byref(state),30100),0)

    def test_pending_retry_clock_wrap(self):
        state=Confirmation()
        self.lib.godox_scan_confirm(C.byref(state),14,0,16000000,C.c_uint32(0xfffffff0))
        self.assertEqual(self.lib.godox_scan_pending(C.byref(state),32),14)
        self.assertEqual(self.lib.godox_scan_confirm(C.byref(state),14,0,40000000,2000),1)

    def test_prefix_verification_still_rejects_ch3_alias_as_ch22(self):
        self.assertEqual(self.header(self.samples40(0,-31000000),1000000)[0],[])

    def test_header_rejects_corrupt_repeated_id_and_unknown_frame_kind(self):
        for corrupt in [48,64]:
            raw=(C.c_uint8*12)();bits=(C.c_uint8*97)()
            self.lib.godox_short_frame(0,0x09,raw,bits)
            bits[corrupt]^=1
            words=(C.c_uint32*16128)();gate=C.c_uint32()
            self.assertTrue(self.lib.godox_dsp_generate(bits,81,0,words,C.byref(gate)))
            w=np.ctypeslib.as_array(words).astype(np.int32)
            i=w&1023;q=(w>>10)&1023;i[i>=512]-=1024;q[q>=512]-=1024
            z=(i+1j*q)*.16*np.exp(2j*np.pi*2500000*np.arange(len(w))/40000000)
            self.assertEqual(self.header(self.packed(z.conj()),2500000)[0],[])

    def test_header_rejects_noise_and_unmodulated_carrier(self):
        rng=np.random.default_rng(630)
        for z in [rng.normal(0,20,16128)+1j*rng.normal(0,20,16128),
                  60*np.exp(-2j*np.pi*2500000*np.arange(16128)/40000000)]:
            self.assertEqual(self.header(self.packed(z),2500000)[0],[])

    def test_serial_capture_id_off_decodes_and_locates_ch14(self):
        iq=np.frombuffer((ROOT/'tests/fixtures/rx-v062-ch14-id-off-iq8.bin').read_bytes(),
                         dtype=np.int8)
        ids=(C.c_uint*4)();carriers=(C.c_int32*4)();diag=(C.c_uint32*5)()
        n=self.lib.godox_decode_iq8_carriers(iq.ctypes.data_as(C.POINTER(C.c_int8)),
                C.c_size_t(len(iq)//2),-2500000,ids,4,diag,carriers)
        self.assertEqual(list(ids[:n]),[0])
        self.assertEqual(self.lib.godox_scan_locate(6,carriers[0]),14)

    def test_real_40msps_partial_frame_verifies_at_eight_timing_phases(self):
        original=np.frombuffer((ROOT/'tests/fixtures/rx-v063-ch14-id-off-40m-iq8.bin').read_bytes(),
                               dtype=np.int8)
        for shift in range(0,40,5):
            iq=np.zeros_like(original)
            if shift:iq[:-2*shift]=original[2*shift:]
            else:iq[:]=original
            self.assertEqual(self.decode40(iq,-2500000)[0],[])
            ids,carriers=self.header(iq,-2500000)
            self.assertEqual(ids,[0],shift)
            self.assertEqual(self.lib.godox_scan_locate(6,carriers[0]),14)
