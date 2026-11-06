import ctypes as C
import unittest
import numpy as np
import test_scan_pipeline as scan
from test_stage_c import ROOT


class Vote(C.Structure):
    _fields_=[('last_ms',C.c_uint32),('request_ms',C.c_uint32),('rate_mask',C.c_uint16),
              ('ch',C.c_uint8),('id',C.c_uint8)]


class Confirmation(C.Structure):
    _fields_=[('votes',Vote*32)]


class ScanConfidenceTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        scan.ScanTests.setUpClass();cls.lib=scan.ScanTests.lib
    signal=scan.ScanTests.signal
    packed=scan.ScanTests.packed
    decode=scan.ScanTests.decode

    def test_all_channels_relocate_at_assigned_lo(self):
        for ch in range(1,33):
            wifi=self.lib.godox_scan_wifi(self.lib.godox_scan_band(ch))
            for receiver in [wifi]:
                for variant in [0,1]:
                    offset=self.lib.godox_scan_offset(ch,variant,receiver)
                    self.assertLessEqual(abs(offset),5000000)
                    self.assertEqual(self.lib.godox_scan_locate(receiver,-offset),ch)


    def test_alias_does_not_get_nearest_inband_candidate_label(self):
        # True CH3 at2416MHz folds from -11MHz to +5MHz around LO2427MHz.
        # CH12 alternate frequency2431.75MHz is close, but it is another CH.
        self.assertEqual(self.lib.godox_scan_locate(4,-5000000),0)
        self.assertNotEqual(self.lib.godox_scan_locate(4,-5000000),12)
        self.assertEqual(self.lib.godox_scan_locate(4,-4875000),0)

    def test_single_rate_never_confirms_and_second_rate_confirms(self):
        state=Confirmation()
        self.assertEqual(self.lib.godox_scan_confirm(C.byref(state),14,0,16000000,100),-1)
        for ms in [110,200,300]:
            self.assertEqual(self.lib.godox_scan_confirm(C.byref(state),14,0,16000000,ms),0)
        self.assertEqual(self.lib.godox_scan_confirm(C.byref(state),14,0,16000000,900),0)
        self.assertEqual(self.lib.godox_scan_confirm(C.byref(state),14,0,40000000,1000),1)
        self.assertEqual(self.lib.godox_scan_confirm(C.byref(state),14,0,16000000,1100),1)
        self.assertEqual(self.lib.godox_scan_confirm(C.byref(state),14,99,40000000,1150),0)
        self.assertEqual(self.lib.godox_scan_confirm(C.byref(state),14,0,40000000,5000),1)
        self.assertEqual(self.lib.godox_scan_confirm(C.byref(state),14,0,40000000,35001),0)

    def test_real_waveform_reports_measured_preamble_frequency(self):
        w=np.frombuffer((ROOT/'tests/fixtures/rx-user-id-off-iq10.bin').read_bytes(),dtype='<u4')
        iq=np.empty(2*len(w),dtype=np.int8)
        iq[0::2]=((w>>2)&255).astype(np.uint8).view(np.int8)
        iq[1::2]=((w>>12)&255).astype(np.uint8).view(np.int8)
        ids=(C.c_uint*4)();carriers=(C.c_int32*4)();diag=(C.c_uint32*5)()
        n=self.lib.godox_decode_iq8_carriers(iq.ctypes.data_as(C.POINTER(C.c_int8)),
               C.c_size_t(len(w)),2500000,ids,4,diag,carriers)
        self.assertEqual(list(ids[:n]),[0]);self.assertLess(abs(carriers[0]+2500000),100000)

    def test_half_symbol_quiet_periods_remain_decodable(self):
        for phase in [8,16,24,32,40,48]:
            signal=self.signal(99,2500000)
            signal*=((np.arange(len(signal))+phase)%64)<32
            self.assertEqual(self.decode(self.packed(signal),2500000),[99])

    def samples40(self,ident,offset,long=False):
        raw=(C.c_uint8*12)();bits=(C.c_uint8*97)();words=(C.c_uint32*16128)();gate=C.c_uint32()
        if long:self.lib.godox_property_frame(ident,1,0xbc,60,raw,bits)
        else:self.lib.godox_short_frame(ident,0x09,raw,bits)
        self.assertTrue(self.lib.godox_dsp_generate(bits,97 if long else 81,0,words,C.byref(gate)))
        w=np.ctypeslib.as_array(words).astype(np.int32)
        i=w&1023;q=(w>>10)&1023;i[i>=512]-=1024;q[q>=512]-=1024
        z=(i+1j*q)*.16*np.exp(2j*np.pi*offset*np.arange(len(w))/40000000)
        return self.packed(z.conj())

    def decode40(self,iq,offset):
        ids=(C.c_uint*4)();carriers=(C.c_int32*4)();diag=(C.c_uint32*5)()
        n=self.lib.godox_decode_iq8_rate(iq.ctypes.data_as(C.POINTER(C.c_int8)),C.c_size_t(len(iq)//2),
                                      40000000,offset,ids,4,diag,carriers)
        return list(ids[:n]),list(carriers[:n])

    def test_40msps_verifies_all_32_channels_and_both_frequency_tables(self):
        for ch in range(1,33):
            wifi=self.lib.godox_scan_wifi(self.lib.godox_scan_band(ch))
            for variant in [0,1]:
                offset=self.lib.godox_scan_offset(ch,variant,wifi)
                ids,carriers=self.decode40(self.samples40(ch,offset,long=variant==1),offset)
                self.assertEqual(ids,[ch],(ch,variant))
                self.assertEqual(self.lib.godox_scan_locate(wifi,carriers[0]),ch)

    def test_16msps_alias_of_ch3_cannot_verify_as_ch22_at_40msps(self):
        # LO2447: CH3 at2416 folds from -31MHz to +1MHz at16MS/s.
        # Its ID genuinely decodes as if CH22 (+1MHz), exposing the old bug.
        iq16=self.packed(self.signal(0,-31000000))
        self.assertEqual(self.decode(iq16,1000000),[0])
        self.assertEqual(self.lib.godox_scan_locate(8,-1000000),22)
        ids,_=self.decode40(self.samples40(0,-31000000),1000000)
        self.assertEqual(ids,[])

    def test_mixed_real_and_return_to_zero_frames_keep_both_ids(self):
        w=np.frombuffer((ROOT/'tests/fixtures/rx-user-id-off-iq10.bin').read_bytes(),dtype='<u4')
        i=((w>>2)&255).astype(np.uint8).view(np.int8)
        q=((w>>12)&255).astype(np.uint8).view(np.int8)
        signal=self.signal(99,2500000,start=8192)
        signal*=((np.arange(len(signal))+32)%64)<32
        signal[:len(w)]+=i.astype(float)+1j*q.astype(float)
        self.assertEqual(set(self.decode(self.packed(signal),2500000)),{0,99})
