"""Joint control and receive decoder tests; synthetic RF is not a device capture."""
import ctypes as C
import unittest
import numpy as np
from test_stage_c import ROOT, Ui, Target


@unittest.skipUnless((ROOT/'artifacts/stage_host.dll').exists(), 'native C host library not built')
class JointTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.lib=C.CDLL(str(ROOT/'artifacts/stage_host.dll'))
        cls.lib.godox_decode_bits.restype=C.c_bool
        cls.lib.godox_decode_capture.restype=C.c_bool
        cls.lib.stage_target_add.restype=C.c_bool
        cls.lib.godox_dsp_generate.restype=C.c_bool
        cls.lib.joint_cache_wave.restype=C.POINTER(C.c_uint32)

    def state(self):
        s=Ui();self.lib.stage_core_init(C.byref(s));return s

    def test_frame_validation_id_off_and_full_id_range(self):
        for ident in range(100):
            for command in [0x09,0x11,0x21]:
                raw=(C.c_uint8*10)();bits=(C.c_uint8*81)();out=C.c_uint()
                self.lib.godox_short_frame(ident,command,raw,bits)
                self.assertTrue(self.lib.godox_decode_bits(bits,81,C.byref(out)))
                self.assertEqual(out.value,ident)
                bits[48]^=1
                self.assertFalse(self.lib.godox_decode_bits(bits,81,C.byref(out)))

    def test_user_rf_capture_grid_mixer_and_general_mixer_agree(self):
        # Original LO was omitted from the pasted log, so assert ID and offset,
        # without assigning this real capture to a physical Godox channel.
        payload=(ROOT/'tests/fixtures/rx-user-id-off-iq10.bin').read_bytes()
        samples=(C.c_uint32*(len(payload)//4)).from_buffer_copy(payload)
        self.lib.godox_decode_capture_ids.restype=C.c_uint
        for offset in [2500000,2500001]:
            ids=(C.c_uint*4)();diag=(C.c_uint32*5)()
            found=self.lib.godox_decode_capture_ids(samples,C.c_size_t(len(samples)),offset,ids,4,diag)
            self.assertEqual(list(ids[:found]),[0])
            self.assertGreater(diag[2],0)
        ids=(C.c_uint*4)();diag=(C.c_uint32*5)()
        self.assertEqual(self.lib.godox_decode_capture_ids(samples,C.c_size_t(len(samples)),
                                                          -2500000,ids,4,diag),0)

    def test_receive_iq_matches_real_transmit_dsp_at_native_rx_rate(self):
        rng=np.random.default_rng(404)
        for ident,offset,long_frame,mirror,dc in [(0,2500000,False,False,0),(99,-2000000,False,True,0),
                                               (37,0,True,False,0),(99,500000,True,True,0),
                                               (0,2500000,False,False,120),(99,-2000000,True,True,220)]:
            raw=(C.c_uint8*12)();bits=(C.c_uint8*97)();out=C.c_uint()
            if long_frame:self.lib.godox_property_frame(ident,5,0xbc,0,raw,bits)
            else:self.lib.godox_short_frame(ident,0x09,raw,bits)
            n=97 if long_frame else 81
            words=(C.c_uint32*16128)();gate=C.c_uint32()
            self.assertTrue(self.lib.godox_dsp_generate(bits,n,offset,words,C.byref(gate)))
            packed=np.ctypeslib.as_array(words).astype(np.int32)
            iv=packed&1023;qv=(packed>>10)&1023
            iv[iv>=512]-=1024;qv[qv>=512]-=1024
            x=np.arange(0,16128,2.5)
            z=np.interp(x,np.arange(16128),iv)+1j*np.interp(x,np.arange(16128),qv)
            if mirror:z=z.conj()
            capture=(rng.normal(0,1,16128)+1j*rng.normal(0,1,16128))
            capture[2048:2048+len(z)]+=z*(.1 if dc else .55)
            capture+=dc+1j*dc
            pi=np.clip(np.rint(capture.real),-512,511).astype(np.int32)&1023
            pq=np.clip(np.rint(capture.imag),-512,511).astype(np.int32)&1023
            payload=(pi|(pq<<10)).astype(np.uint32)
            self.assertTrue(self.lib.godox_decode_capture(payload.ctypes.data_as(C.POINTER(C.c_uint32)),
                              C.c_size_t(16128),offset,C.byref(out)),(ident,offset,long_frame,mirror))
            self.assertEqual(out.value,ident)
            self.assertFalse(self.lib.godox_decode_capture(payload.ctypes.data_as(C.POINTER(C.c_uint32)),
                              C.c_size_t(16128),offset-2000000,C.byref(out)), 'Adjacent channel alias must be rejected')
        noise=np.zeros(16128,dtype=np.uint32)
        self.assertFalse(self.lib.godox_decode_capture(noise.ctypes.data_as(C.POINTER(C.c_uint32)),
                           C.c_size_t(16128),0,C.byref(out)))

    def test_rx_diagnostics_reject_zero_buffer_without_inventing_frames(self):
        self.lib.godox_decode_capture_report.restype=C.c_bool
        samples=(C.c_uint32*16128)();diag=(C.c_uint32*5)(1,1,1,1,1);out=C.c_uint()
        self.assertFalse(self.lib.godox_decode_capture_report(samples,C.c_size_t(16128),2500000,C.byref(out),diag))
        self.assertEqual(list(diag),[0,0,0,0,0])

    def test_auto_scan_all_channels_and_two_unknown_ids_per_capture(self):
        # Same scheduling rule as the worker, without a supplied Godox CH or ID.
        self.lib.godox_decode_capture_ids.restype=C.c_uint
        covered=[]
        for lo_channel in range(1,12):
            for ch in range(1,33):
                hz=C.c_uint32();wifi=C.c_uint();offset=C.c_int32()
                self.lib.godox_channel_plan(ch,C.byref(hz),C.byref(wifi),C.byref(offset))
                if wifi.value != lo_channel:continue
                covered.append(ch)
                capture=np.zeros(16128,dtype=np.complex128)
                for pos,ident in [(256,ch%100),(8192,(ch+47)%100)]:
                    raw=(C.c_uint8*10)();bits=(C.c_uint8*81)()
                    self.lib.godox_short_frame(ident,0x09,raw,bits)
                    words=(C.c_uint32*16128)();gate=C.c_uint32()
                    self.assertTrue(self.lib.godox_dsp_generate(bits,81,offset,words,C.byref(gate)))
                    w=np.ctypeslib.as_array(words).astype(np.int32)
                    iv=w&1023;qv=(w>>10)&1023
                    iv[iv>=512]-=1024;qv[qv>=512]-=1024
                    x=np.arange(0,16128,2.5)
                    z=np.interp(x,np.arange(16128),iv)+1j*np.interp(x,np.arange(16128),qv)
                    capture[pos:pos+len(z)]+=z.conj()*.5 # Native S3 dump is I,-Q.
                packed=((np.rint(capture.real).astype(np.int32)&1023)|
                        ((np.rint(capture.imag).astype(np.int32)&1023)<<10)).astype(np.uint32)
                ids=(C.c_uint*4)();diag=(C.c_uint32*5)()
                found=self.lib.godox_decode_capture_ids(packed.ctypes.data_as(C.POINTER(C.c_uint32)),
                    C.c_size_t(len(packed)),offset,ids,4,diag)
                self.assertEqual(set(ids[:found]),{ch%100,(ch+47)%100},ch)
                self.assertEqual(diag[4],32)
                if abs(offset.value) == 1000000:
                    mirrored=self.lib.godox_decode_capture_ids(
                        packed.ctypes.data_as(C.POINTER(C.c_uint32)),
                        C.c_size_t(len(packed)),-offset.value,ids,4,diag)
                    self.assertEqual(mirrored,0,'Spectral mirror must not invent another CH')
        self.assertEqual(sorted(covered),list(range(1,33)))

    def test_multi_id_decoder_capacity_and_noise(self):
        ids=(C.c_uint*4)(111,222,333,444);samples=(C.c_uint32*16128)()
        diag=(C.c_uint32*5)()
        self.assertEqual(self.lib.godox_decode_capture_ids(samples,C.c_size_t(16128),0,ids,0,diag),0)
        self.assertEqual(list(ids),[111,222,333,444])
        rng=np.random.default_rng(912)
        for offset in [-2000000,0,2500000]:
            iv=rng.integers(-512,512,16128);qv=rng.integers(-512,512,16128)
            packed=((iv&1023)|((qv&1023)<<10)).astype(np.uint32)
            self.assertEqual(self.lib.godox_decode_capture_ids(packed.ctypes.data_as(C.POINTER(C.c_uint32)),
                C.c_size_t(16128),offset,ids,4,diag),0)

    def test_shared_lo_snapshot_discovers_different_channels_and_ids(self):
        capture=np.zeros(16128,dtype=np.complex128)
        for pos,ident,offset in [(256,0,-1000000),(8192,99,1000000)]:
            raw=(C.c_uint8*10)();bits=(C.c_uint8*81)()
            self.lib.godox_short_frame(ident,0x09,raw,bits)
            words=(C.c_uint32*16128)();gate=C.c_uint32()
            self.assertTrue(self.lib.godox_dsp_generate(bits,81,offset,words,C.byref(gate)))
            w=np.ctypeslib.as_array(words).astype(np.int32)
            iv=w&1023;qv=(w>>10)&1023
            iv[iv>=512]-=1024;qv[qv>=512]-=1024
            x=np.arange(0,16128,2.5)
            z=np.interp(x,np.arange(16128),iv)-1j*np.interp(x,np.arange(16128),qv)
            capture[pos:pos+len(z)]+=z*.5
        packed=((np.rint(capture.real).astype(np.int32)&1023)|
                ((np.rint(capture.imag).astype(np.int32)&1023)<<10)).astype(np.uint32)
        for offset,expected in [(-1000000,[0]),(1000000,[99]),(2500000,[])]:
            ids=(C.c_uint*4)();diag=(C.c_uint32*5)()
            found=self.lib.godox_decode_capture_ids(packed.ctypes.data_as(C.POINTER(C.c_uint32)),
                C.c_size_t(16128),offset,ids,4,diag)
            self.assertEqual(list(ids[:found]),expected)

    def test_deduplicate_and_capacity_and_hits(self):
        s=self.state()
        self.assertFalse(self.lib.stage_target_add(C.byref(s),0,0))
        self.assertFalse(self.lib.stage_target_add(C.byref(s),1,100))
        for ch in range(1,33):self.assertTrue(self.lib.stage_target_add(C.byref(s),ch,ch%100))
        self.assertFalse(self.lib.stage_target_add(C.byref(s),1,99))
        self.assertFalse(self.lib.stage_target_add(C.byref(s),1,1))
        self.assertEqual((s.target_count,s.targets[0].hits),(32,2))
        s.targets[0].hits=65535
        self.lib.stage_target_add(C.byref(s),1,1)
        self.assertEqual(s.targets[0].hits,65535)

    def test_menu_joint_edit_scan_stop_and_page_actions(self):
        s=self.state();s.page=1
        self.assertEqual(self.lib.stage_core_boot(C.byref(s)),0)
        self.lib.stage_core_touch(C.byref(s),100,135);self.assertEqual(s.page,2)
        self.assertEqual(self.lib.stage_core_boot(C.byref(s)),0) # empty list cannot fire
        self.assertEqual(self.lib.stage_core_touch(C.byref(s),35,43),4) # scan
        self.lib.stage_target_add(C.byref(s),8,0)
        self.assertEqual(self.lib.stage_core_boot(C.byref(s)),2)
        self.lib.stage_core_touch(C.byref(s),40,140);self.assertEqual(s.field,5)
        self.lib.stage_core_touch(C.byref(s),38,78)
        self.assertEqual(self.lib.stage_core_touch(C.byref(s),150,188),1)
        self.assertEqual((s.joint.interval_ms,s.config.interval_ms),(1,1000))
        self.lib.stage_core_touch(C.byref(s),150,43);self.assertEqual(s.page,3)
        s.scanning=True
        self.assertEqual(self.lib.stage_core_touch(C.byref(s),100,189),3)
        self.assertEqual(self.lib.stage_core_boot(C.byref(s)),3)
        s.scanning=False
        self.lib.stage_core_touch(C.byref(s),100,189);self.assertEqual(s.page,2)
        self.lib.stage_core_touch(C.byref(s),50,10);self.assertEqual(s.page,1)

    def test_new_page_buffer_bounds_and_sleep_is_completely_white(self):
        s=self.state();s.joint_active=True;s.target_count=32;s.scanning=True;s.scan_seconds=120
        for i in range(32):s.targets[i].ch=i+1;s.targets[i].id=99;s.targets[i].hits=65535
        for page in [1,2,3]:
            s.page=page
            guarded=(C.c_uint8*5016)(*[0xa5]*5016)
            self.lib.stage_view_render(C.byref(s),C.cast(C.byref(guarded,8),C.POINTER(C.c_uint8)))
            self.assertEqual(bytes(guarded[:8]),b'\xa5'*8)
            self.assertEqual(bytes(guarded[-8:]),b'\xa5'*8)
        s.sleep_requested=True
        frame=(C.c_uint8*5000)();self.lib.stage_view_render(C.byref(s),frame)
        self.assertEqual(bytes(frame),b'\xff'*5000)

    def test_joint_flash_cache_full_capacity_hits_corruption_and_failure(self):
        self.lib.stage_cache_close();self.lib.joint_cache_close();self.lib.mock_flash_reset()
        targets=(Target*32)(*[Target(i+1,i%100,1) for i in range(32)])
        work=(C.c_uint32*16128)()
        self.assertEqual(self.lib.joint_cache_prepare(targets,32,work,None),0)
        self.assertEqual(self.lib.mock_flash_write_calls(),33)
        self.assertEqual(self.lib.mock_flash_erase_calls(),1)
        self.assertTrue(self.lib.joint_cache_wave(31))
        self.assertFalse(self.lib.joint_cache_wave(32))
        targets[0].hits=200 # Reception counters do not invalidate RF cache.
        self.assertEqual(self.lib.joint_cache_prepare(targets,32,work,None),0)
        self.assertEqual(self.lib.mock_flash_write_calls(),33)
        self.lib.mock_joint_corrupt(4096+100)
        self.assertEqual(self.lib.joint_cache_prepare(targets,32,work,None),0)
        self.assertEqual(self.lib.mock_flash_erase_calls(),2)
        self.lib.joint_cache_close();self.assertEqual(self.lib.mock_flash_map_count(),0)
        self.lib.mock_flash_reset();self.lib.mock_flash_fail_write(2)
        self.assertNotEqual(self.lib.joint_cache_prepare(targets,2,work,None),0)
        self.assertFalse(self.lib.joint_cache_wave(0))
        self.lib.mock_flash_fail_write(0)
        self.assertEqual(self.lib.joint_cache_prepare(targets,2,work,None),0)
        self.lib.joint_cache_close()
