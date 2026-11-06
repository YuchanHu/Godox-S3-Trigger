"""Exercise actual FFT gate and native IQ8 decoder, independent of RTOS scheduling."""
import ctypes as C
import unittest
import numpy as np
from test_stage_c import ROOT


class Candidate(C.Structure):
    _fields_=[('ch',C.c_uint),('offset',C.c_int32),('score',C.c_float)]


class ScanTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.lib=C.CDLL(str(ROOT/'artifacts/stage_host.dll'))
        cls.lib.godox_scan_init()
        cls.lib.godox_scan_offset.restype=C.c_int32
        cls.lib.godox_scan_detect.restype=C.c_uint
        cls.lib.godox_decode_iq8_ids.restype=C.c_uint
        cls.lib.godox_dsp_generate.restype=C.c_bool

    def signal(self, ident, offset, start=2048, long=False, cfo=0):
        raw=(C.c_uint8*12)();bits=(C.c_uint8*97)()
        if long:self.lib.godox_property_frame(ident,5,0xbc,60,raw,bits)
        else:self.lib.godox_short_frame(ident,0x09,raw,bits)
        words=(C.c_uint32*16128)();gate=C.c_uint32()
        self.assertTrue(self.lib.godox_dsp_generate(bits,97 if long else 81,0,words,C.byref(gate)))
        w=np.ctypeslib.as_array(words).astype(np.int32)
        i=w&1023;q=(w>>10)&1023;i[i>=512]-=1024;q[q>=512]-=1024
        at=np.arange(0,16128,2.5)
        z=(np.interp(at,np.arange(16128),i)+1j*np.interp(at,np.arange(16128),q))*.16
        z*=np.exp(2j*np.pi*(offset+cfo)*np.arange(len(z))/16000000)
        out=np.zeros(16128,dtype=complex);out[start:start+len(z)]=z.conj()
        return out

    def packed(self,z):
        out=np.empty(2*len(z),dtype=np.int8)
        out[0::2]=np.clip(np.rint(z.real),-128,127).astype(np.int8)
        out[1::2]=np.clip(np.rint(z.imag),-128,127).astype(np.int8)
        return out

    def candidates(self,iq,band):
        out=(Candidate*12)()
        n=self.lib.godox_scan_detect(iq.ctypes.data_as(C.POINTER(C.c_int8)),
                                    C.c_size_t(len(iq)//2),band,out)
        return list(out[:n])

    def decode(self,iq,offset):
        ids=(C.c_uint*4)();diag=(C.c_uint32*5)()
        n=self.lib.godox_decode_iq8_ids(iq.ctypes.data_as(C.POINTER(C.c_int8)),
                 C.c_size_t(len(iq)//2),offset,ids,4,diag)
        return list(ids[:n])

    def test_all_32_channels_and_both_frequency_variants_pass_fft_and_decode(self):
        for ch in range(1,33):
            band=self.lib.godox_scan_band(ch);wifi=self.lib.godox_scan_wifi(band)
            for variant in [0,1]:
                offset=self.lib.godox_scan_offset(ch,variant,wifi)
                self.assertLessEqual(abs(offset),4750000)
                iq=self.packed(self.signal(ch%100,offset,long=variant==1))
                candidates=self.candidates(iq,band)
                c=next(c for c in candidates if c.ch==ch and c.offset==offset)
                self.assertGreaterEqual(c.score,8,(ch,variant,c.score))
                self.assertIn(ch,{c.ch for c in candidates[:2]},(ch,variant))
                self.assertEqual(self.decode(iq,offset),[ch%100],(ch,variant))

    def test_detector_covers_frame_positions_and_cfo(self):
        for start in [0,96,2000,6000,9000]:
            for cfo in [-100000,100000]:
                iq=self.packed(self.signal(99,2500000,start=start,cfo=cfo))
                # CH32 at LO2462 uses the same +2.5MHz offset as this signal.
                c=next(c for c in self.candidates(iq,5) if c.ch==32)
                self.assertGreaterEqual(c.score,8)
                self.assertEqual(self.decode(iq,2500000),[99])

    def test_two_different_channels_in_shared_capture(self):
        iq=self.packed(self.signal(0,-4000000,start=128)+self.signal(99,4000000,start=8192))
        candidates=self.candidates(iq,0)
        self.assertEqual({c.ch for c in candidates[:2]},{1,6})
        self.assertEqual(self.decode(iq,-4000000),[0])
        self.assertEqual(self.decode(iq,4000000),[99])

    def test_zero_noise_and_invalid_band(self):
        iq=np.zeros(32256,dtype=np.int8)
        self.assertTrue(all(c.score==0 for c in self.candidates(iq,0)))
        self.assertEqual(self.candidates(iq,6),[])
        self.assertEqual(self.decode(iq,1000000),[])

    def test_real_iq_survives_queue_quantization(self):
        w=np.frombuffer((ROOT/'tests/fixtures/rx-user-id-off-iq10.bin').read_bytes(),dtype='<u4')
        iq=np.empty(2*len(w),dtype=np.int8)
        iq[0::2]=((w>>2)&255).astype(np.uint8).view(np.int8)
        iq[1::2]=((w>>12)&255).astype(np.uint8).view(np.int8)
        self.assertEqual(self.decode(iq,2500000),[0])
        self.assertEqual(self.decode(iq,-2500000),[])

    def test_weak_signal_with_dc_and_noise(self):
        rng=np.random.default_rng(1606)
        for band,ch,offset in [(0,1,-4000000),(2,18,4750000),(5,32,2500000)]:
            z=self.signal(99,offset)*.2+45+50j
            z+=rng.normal(0,1,len(z))+1j*rng.normal(0,1,len(z))
            iq=self.packed(z)
            c=next(c for c in self.candidates(iq,band) if c.ch==ch and c.offset==offset)
            self.assertGreaterEqual(c.score,8)
            self.assertEqual(self.decode(iq,offset),[99])
