"""Execute firmware C protocol, DSP and touch model using a native host DLL.

Build with tools/test_stage_host.ps1. Skips explicitly when host compiler is absent.
"""
import ctypes as C
import importlib.util
from pathlib import Path
import sys
import unittest
import numpy as np
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
from godox_model import frame,waveform,unpack,FS


class Config(C.Structure):
    _fields_=[('interval_ms',C.c_uint32),('count',C.c_uint16),('ch',C.c_uint8),
              ('group',C.c_uint8),('id',C.c_uint8),('power',C.c_uint8)]
class Target(C.Structure):
    _fields_=[('ch',C.c_uint8),('id',C.c_uint8),('hits',C.c_uint16)]


class Ui(C.Structure):
    _fields_=[('config',Config),('run',Config),('mode',C.c_int),('field',C.c_int),
              ('draft',C.c_uint32),('digits',C.c_uint8),('fresh',C.c_bool),
              ('sent',C.c_uint16),('result',C.c_int),('timing_limited',C.c_bool),('legacy',C.c_bool),
              ('preparing',C.c_bool),('cache_ready',C.c_bool),('prep_done',C.c_uint8),('prep_total',C.c_uint8),
              ('battery_valid',C.c_bool),('battery_mv',C.c_int16),('battery_percent',C.c_int16),
              ('first_test_delay_ms',C.c_uint32),('prep_generation',C.c_uint32),
              ('prep_phase',C.c_uint8),('prep_wave',C.c_uint8),('prep_group',C.c_uint8),
              ('page',C.c_int),('joint',Config),('targets',Target*32),
              ('target_count',C.c_uint8),('list_page',C.c_uint8),('scan_ch',C.c_uint8),
              ('scanning',C.c_bool),('sleep_requested',C.c_bool),('joint_active',C.c_bool),
              ('scan_seconds',C.c_uint16),('scan_captures',C.c_uint32),('scan_errors',C.c_uint32),('persist_error',C.c_uint32),
              ('manual',Config),('manual_status',C.c_uint8)]


class CacheEntry(C.Structure):
    _fields_=[('offset',C.c_uint32),('window_us',C.c_uint32),('type',C.c_uint8),('group',C.c_uint8),('key',C.c_uint8),('value',C.c_uint8)]


class CacheHeader(C.Structure):
    _fields_=[('magic',C.c_uint32),('version',C.c_uint32),('ch',C.c_uint8),('group',C.c_uint8),('id',C.c_uint8),('power',C.c_uint8),
              ('count',C.c_uint32),('data_hash',C.c_uint32),('header_hash',C.c_uint32),('entries',CacheEntry*9)]


class Rect(C.Structure):
    _fields_=[('x',C.c_int),('y',C.c_int),('w',C.c_int),('h',C.c_int)]


@unittest.skipUnless((ROOT/'artifacts/stage_host.dll').exists(),'native C host library not built')
class StageCTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.lib=C.CDLL(str(ROOT/'artifacts/stage_host.dll'))
        cls.lib.godox_dsp_generate.restype=C.c_bool
        cls.lib.godox_property_frame.restype=C.c_bool
        cls.lib.godox_channel_plan.restype=C.c_bool
        cls.lib.stage_draft_valid.restype=C.c_bool
        cls.lib.stage_config_valid.restype=C.c_bool
        cls.lib.stage_cache_plan.restype=C.c_bool
        cls.lib.stage_cache_same_rf.restype=C.c_bool
        cls.lib.stage_cache_header_valid.restype=C.c_bool
        cls.lib.stage_cache_compile.restype=C.c_bool
        cls.lib.stage_cache_hash.restype=C.c_uint32
        cls.lib.stage_frame_diff.restype=C.c_bool
        cls.lib.stage_frame_pack.restype=C.c_size_t

    def generate(self,bits,offset):
        arr=(C.c_uint8*len(bits))(*bits);out=(C.c_uint32*16128)();window=C.c_uint32()
        self.assertTrue(self.lib.godox_dsp_generate(arr,C.c_size_t(len(bits)),offset,out,C.byref(window)))
        return np.ctypeslib.as_array(out).copy(),window.value

    def test_c_short_dsp_matches_python(self):
        for command in [0x11,0x09,0x21]:
            words,gate=self.generate(frame(command)[1],2500000)
            self.assertEqual(gate,380)
            a,b=unpack(words),unpack(waveform(command))
            self.assertLessEqual(np.max(np.abs(a.real-b.real)),1)
            self.assertLessEqual(np.max(np.abs(a.imag-b.imag)),1)

    def test_c_property_matches_reference(self):
        path=ROOT/'third_party/godox-flash-radio/transmitter.py'
        if not path.exists():self.skipTest('reference checkout absent')
        spec=importlib.util.spec_from_file_location('reference_stage',path)
        ref=importlib.util.module_from_spec(spec);spec.loader.exec_module(ref)
        for ident in [0,1,37,99]:
            for group in range(6):
                for key,value in [(0xb1,1),(0xbc,60),(0xbc,0),(0xbc,70)]:
                    raw=(C.c_uint8*12)();bits=(C.c_uint8*97)()
                    self.assertTrue(self.lib.godox_property_frame(ident,group,key,value,raw,bits))
                    captured=[];obj=ref.Command(ident);obj.rf_burst=captured.append
                    obj.set_property(None if group==0 else 9+group,key,value)
                    self.assertEqual(list(bits),captured[0])
                    self.assertEqual(bytes(raw),np.packbits(captured[0][:96]).tobytes())

    def test_long_frame_all_97_bits_recover_after_crop(self):
        for ident,group,offset in [(0,1,2500000),(37,3,-2000000),(99,5,0)]:
            raw=(C.c_uint8*12)();bits=(C.c_uint8*97)()
            self.lib.godox_property_frame(ident,group,0xbc,60,raw,bits)
            words,gate=self.generate(list(bits),offset)
            self.assertEqual(gate,398)
            self.assertTrue(np.all(words[:80]==0));self.assertTrue(np.all(words[15880:]==0))
            z=unpack(words)[80:15880];z*=np.exp(-2j*np.pi*offset*np.arange(len(z))/FS)
            freq=np.angle(z[1:]*z[:-1].conj())*FS/(2*np.pi)
            centres=(np.arange(97)*8+15)*20-160
            recovered=[int(np.mean(freq[c-15:c+15])<0) for c in centres]
            self.assertEqual(recovered,list(bits))
            self.assertLess(15880/40,gate);self.assertLess(gate,16128/40)
            self.assertTrue(np.all(words>>20==0))

    def test_channel_table(self):
        expected=[2413,2414.5,2416,2418,2419.5,2421,2423,2424.5,2426,2428,2429.5,
                  2431,2433,2434.5,2436,2438,2439.5,2441,2443,2444.5,2446,2448,
                  2449.5,2451,2453,2454.5,2456,2458,2459.5,2461,2463,2464.5]
        for ch,mhz in enumerate(expected,1):
            hz=C.c_uint32();wifi=C.c_uint();offset=C.c_int32()
            self.assertTrue(self.lib.godox_channel_plan(ch,C.byref(hz),C.byref(wifi),C.byref(offset)))
            self.assertEqual(hz.value,int(mhz*1e6));self.assertLessEqual(abs(offset.value),2500000)
            self.assertEqual(2412000000+(wifi.value-1)*5000000+offset.value,hz.value)
            if ch==8:self.assertEqual((wifi.value,offset.value),(3,2500000))

    def test_touch_edit_save_cancel_and_boot(self):
        s=Ui();self.lib.stage_core_init(C.byref(s))
        self.assertEqual((s.mode,s.field,s.config.ch,s.config.count,s.config.interval_ms),(0,-1,8,10,1000))
        self.lib.stage_core_touch(C.byref(s),20,45);self.assertEqual(s.field,0)
        self.assertEqual(self.lib.stage_core_boot(C.byref(s)),0)
        self.lib.stage_core_touch(C.byref(s),150,80) # 3, replaces 8
        self.lib.stage_core_touch(C.byref(s),150,80) # 33 invalid
        self.assertFalse(self.lib.stage_draft_valid(C.byref(s)))
        self.assertEqual(self.lib.stage_core_touch(C.byref(s),150,185),0)
        self.lib.stage_core_touch(C.byref(s),150,155) # backspace ->3
        self.assertEqual(self.lib.stage_core_touch(C.byref(s),150,185),1)
        self.assertEqual(s.config.ch,3);self.assertEqual(s.field,-1)
        self.assertEqual(self.lib.stage_core_boot(C.byref(s)),2)
        s.mode=1;self.lib.stage_core_touch(C.byref(s),20,45);self.assertEqual(s.field,-1)
        self.assertEqual(self.lib.stage_core_boot(C.byref(s)),3)
        self.assertEqual(self.lib.stage_core_touch(C.byref(s),100,180),3)

    def test_id_off_and_invalid_config(self):
        s=Ui();self.lib.stage_core_init(C.byref(s));s.config.id=57
        self.lib.stage_core_touch(C.byref(s),20,90) # ID card
        self.lib.stage_core_touch(C.byref(s),20,155) # OFF key
        self.assertEqual(self.lib.stage_core_touch(C.byref(s),150,185),1)
        self.assertEqual(s.config.id,0)
        s.config.interval_ms=0;self.assertFalse(self.lib.stage_config_valid(C.byref(s.config)))
        self.assertEqual(self.lib.stage_core_boot(C.byref(s)),0)

    def test_backspace_of_existing_value_and_option_gap(self):
        s=Ui();self.lib.stage_core_init(C.byref(s));s.config.id=57
        self.lib.stage_core_touch(C.byref(s),20,90)
        self.lib.stage_core_touch(C.byref(s),150,155)
        self.assertEqual((s.draft,s.digits),(5,1))
        self.lib.stage_core_touch(C.byref(s),20,185)
        self.lib.stage_core_touch(C.byref(s),120,45)
        self.lib.stage_core_touch(C.byref(s),100,60) # gap between ALL and A
        self.assertEqual(s.draft,0)

    def test_preparation_modal_blocks_boot_and_touch(self):
        s=Ui();self.lib.stage_core_init(C.byref(s));s.preparing=True
        self.assertEqual(self.lib.stage_core_boot(C.byref(s)),0)
        self.lib.stage_core_touch(C.byref(s),20,45)
        self.assertEqual(s.field,-1)
        s.preparing=False;self.lib.stage_core_touch(C.byref(s),20,45)
        self.assertEqual(s.field,0)

    def test_battery_mapping_and_unknown(self):
        for mv,pct in [(0,-1),(2499,-1),(2800,0),(3000,0),(3560,50),(4120,100),(4300,100),(4501,-1)]:
            self.assertEqual(self.lib.stage_battery_percent(mv),pct)

    def test_cache_plan_checksum_key_and_capacity(self):
        s=Ui();self.lib.stage_core_init(C.byref(s));h=CacheHeader()
        self.assertTrue(self.lib.stage_cache_plan(C.byref(s.config),C.byref(h)))
        self.assertEqual(h.count,9)
        self.assertEqual(h.entries[h.count-2].type,0x09)
        self.assertEqual([h.entries[i].group for i in range(2,7)],list(range(1,6)))
        self.assertLessEqual(h.entries[h.count-1].offset+16128*4,0xA0000)
        self.lib.stage_cache_seal(C.byref(h),123456)
        self.assertTrue(self.lib.stage_cache_header_valid(C.byref(h),C.byref(s.config)))
        other=Config.from_buffer_copy(s.config);other.count=33;other.interval_ms=500
        self.assertTrue(self.lib.stage_cache_same_rf(C.byref(s.config),C.byref(other)))
        self.assertTrue(self.lib.stage_cache_header_valid(C.byref(h),C.byref(other)))
        other.id=1;self.assertFalse(self.lib.stage_cache_header_valid(C.byref(h),C.byref(other)))
        h.entries[0].offset+=4;self.assertFalse(self.lib.stage_cache_header_valid(C.byref(h),C.byref(s.config)))
        self.assertEqual(self.lib.stage_cache_hash(2166136261,b'hello',C.c_size_t(5)),0x4f9f2cab)

    def test_cached_waves_are_actual_dsp_output(self):
        s=Ui();self.lib.stage_core_init(C.byref(s));s.config.group=3;h=CacheHeader()
        self.lib.stage_cache_plan(C.byref(s.config),C.byref(h));self.assertEqual(h.count,5)
        for n in range(h.count):
            words=(C.c_uint32*16128)();self.assertTrue(self.lib.stage_cache_compile(C.byref(s.config),C.byref(h),n,words))
            self.assertTrue(np.all(np.ctypeslib.as_array(words)>>20==0))
        self.assertFalse(self.lib.stage_cache_compile(C.byref(s.config),C.byref(h),5,words))

    def test_dirty_rectangle_and_pack(self):
        old=(C.c_uint8*5000)(*[255]*5000);new=(C.c_uint8*5000)(*[255]*5000);rect=Rect()
        self.assertFalse(self.lib.stage_frame_diff(old,new,C.byref(rect)))
        new[48*25+3]=0
        self.assertTrue(self.lib.stage_frame_diff(old,new,C.byref(rect)))
        self.assertEqual((rect.x,rect.y,rect.w,rect.h),(24,48,8,1))
        out=(C.c_uint8*5000)();self.assertEqual(self.lib.stage_frame_pack(new,C.byref(rect),out),1);self.assertEqual(out[0],0)
        new[199*25+24]=0
        self.lib.stage_frame_diff(old,new,C.byref(rect))
        self.assertEqual((rect.x,rect.y,rect.w,rect.h),(24,48,176,152))
        count=self.lib.stage_frame_pack(new,C.byref(rect),out)
        expected=b''.join(bytes(new[y*25+3:y*25+25]) for y in range(48,200))
        self.assertEqual(bytes(out[:count]),expected)
        rect.x=1;self.assertEqual(self.lib.stage_frame_pack(new,C.byref(rect),out),0)

    def test_flash_cache_hit_does_not_erase_or_recalculate(self):
        s=Ui();self.lib.stage_core_init(C.byref(s));s.config.group=1
        self.lib.stage_cache_close();self.lib.mock_flash_reset()
        self.assertEqual(self.lib.stage_cache_build(C.byref(s.config),None),0)
        self.assertEqual(self.lib.mock_flash_erase_calls(),1)
        self.assertEqual(self.lib.mock_flash_write_calls(),6) # five waves, then valid header
        s.config.count=50;s.config.interval_ms=2000
        self.assertEqual(self.lib.stage_cache_open(C.byref(s.config)),0)
        self.assertEqual(self.lib.mock_flash_erase_calls(),1)
        self.assertEqual(self.lib.mock_flash_write_calls(),6)
        self.assertEqual(self.lib.mock_flash_map_count(),1)
        self.lib.stage_cache_close();self.assertEqual(self.lib.mock_flash_map_count(),0)

    def test_flash_cache_header_write_failure_and_repair(self):
        s=Ui();self.lib.stage_core_init(C.byref(s));s.config.group=1
        self.lib.stage_cache_close();self.lib.mock_flash_reset();self.lib.mock_flash_fail_write(6)
        self.assertNotEqual(self.lib.stage_cache_build(C.byref(s.config),None),0)
        self.assertNotEqual(self.lib.stage_cache_open(C.byref(s.config)),0)
        self.assertEqual(self.lib.mock_flash_map_count(),0)
        self.lib.mock_flash_fail_write(0)
        self.assertEqual(self.lib.stage_cache_build(C.byref(s.config),None),0)
        self.lib.stage_cache_close();self.lib.mock_flash_corrupt(4096+100)
        self.assertNotEqual(self.lib.stage_cache_open(C.byref(s.config)),0)
        self.assertEqual(self.lib.mock_flash_map_count(),0)

    def test_flash_cache_cancel_never_publishes_partial_header(self):
        s=Ui();self.lib.stage_core_init(C.byref(s));s.config.group=1
        self.lib.stage_cache_close();self.lib.mock_flash_reset()
        callback=C.CFUNCTYPE(C.c_int,C.c_int,C.c_uint,C.c_uint,C.c_int,C.c_uint)(
            lambda phase,done,total,wave,group:done<2)
        self.assertNotEqual(self.lib.stage_cache_build(C.byref(s.config),callback),0)
        self.assertNotEqual(self.lib.stage_cache_open(C.byref(s.config)),0)
        self.assertEqual(self.lib.mock_flash_map_count(),0)

    def test_footer_gap_and_battery_only_header_dirty(self):
        s=Ui();self.lib.stage_core_init(C.byref(s));s.cache_ready=True;s.run=s.config;s.sent=10;s.mode=2
        out=(C.c_uint8*5000)();self.lib.stage_view_render(C.byref(s),out)
        self.assertEqual(bytes(out[177*25:180*25]),b'\xff'*(3*25)) # gap between return and BOOT footer
        s.mode=0;s.battery_valid=False
        old=(C.c_uint8*5000)();self.lib.stage_view_render(C.byref(s),old)
        s.battery_valid=True;s.battery_percent=100;s.battery_mv=4200
        new=(C.c_uint8*5000)();self.lib.stage_view_render(C.byref(s),new)
        rect=Rect();self.assertTrue(self.lib.stage_frame_diff(old,new,C.byref(rect)))
        self.assertLessEqual(rect.y+rect.h,24);self.assertGreaterEqual(rect.x,128)

    def test_one_parameter_change_is_a_card_partial_update(self):
        s=Ui();self.lib.stage_core_init(C.byref(s));s.cache_ready=True
        old=(C.c_uint8*5000)();new=(C.c_uint8*5000)()
        self.lib.stage_view_render(C.byref(s),old);s.config.interval_ms=1001
        self.lib.stage_view_render(C.byref(s),new);rect=Rect()
        self.assertTrue(self.lib.stage_frame_diff(old,new,C.byref(rect)))
        self.assertGreaterEqual(rect.x,104);self.assertGreaterEqual(rect.y,150)
        self.assertLessEqual(rect.y+rect.h,170);self.assertLessEqual(rect.w*rect.h//8,86*40//8+40)

    def test_invalid_dsp_and_protocol_inputs(self):
        bits=(C.c_uint8*97)(*[0]*97);out=(C.c_uint32*16128)();gate=C.c_uint32()
        self.assertFalse(self.lib.godox_dsp_generate(bits,C.c_size_t(96),0,out,C.byref(gate)))
        self.assertFalse(self.lib.godox_dsp_generate(bits,C.c_size_t(97),3000000,out,C.byref(gate)))
        bits[5]=2
        self.assertFalse(self.lib.godox_dsp_generate(bits,C.c_size_t(97),0,out,C.byref(gate)))
        raw=(C.c_uint8*12)();bits=(C.c_uint8*97)()
        self.assertFalse(self.lib.godox_property_frame(100,1,0xbc,60,raw,bits))
        self.assertFalse(self.lib.godox_property_frame(0,6,0xbc,60,raw,bits))

    def test_actual_c_short_frame_all_ids(self):
        self.lib.godox_short_frame.restype=C.c_bool
        for ident in range(100):
            for cmd in [0x11,0x09,0x21]:
                raw=(C.c_uint8*10)();bits=(C.c_uint8*81)()
                self.assertTrue(self.lib.godox_short_frame(ident,cmd,raw,bits))
                expected,expected_bits=frame(cmd,ident)
                self.assertEqual(bytes(raw),expected);self.assertEqual(list(bits),expected_bits.tolist())

    def test_actual_framebuffer_bounds(self):
        s=Ui();self.lib.stage_core_init(C.byref(s))
        for name,mode,field in [('ready',0,-1),('edit-interval',0,5),('running',1,-1),('done',2,-1),('group',0,1),('preparing',0,-1)]:
            self.lib.stage_core_init(C.byref(s));s.mode=mode;s.field=field;s.draft=1000 if field==5 else 0;s.digits=1;s.run=s.config;s.sent=10 if mode==2 else 3
            s.cache_ready=True;s.battery_valid=True;s.battery_mv=3874;s.battery_percent=78
            if name=='preparing':s.preparing=True;s.cache_ready=False;s.prep_done=3;s.prep_total=9
            guarded=(C.c_uint8*5016)(*[0xA5]*5016)
            self.lib.stage_view_render(C.byref(s),C.cast(C.byref(guarded,8),C.POINTER(C.c_uint8)))
            self.assertEqual(bytes(guarded[:8]),b'\xa5'*8);self.assertEqual(bytes(guarded[-8:]),b'\xa5'*8)


if __name__=='__main__':unittest.main()
