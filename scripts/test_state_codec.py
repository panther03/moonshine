"""Run the production heapless state codec against bounded host buffers."""
import ctypes as C
from pathlib import Path
import random
import struct
import subprocess
import tempfile
import unittest
import zlib

ROOT = Path(__file__).resolve().parents[1]
SUCCESS, INVALID, WORKSPACE, FULL, CORRUPT, ERROR, COMMIT = range(7)
QUICK_BLOCK = 0x20000


def reference_lz4_block(encoded, expected):
    """Small independent decoder for the standard LZ4 block token format."""
    result = bytearray()
    cursor = 0

    def extended(length):
        nonlocal cursor
        if length == 15:
            while True:
                if cursor == len(encoded):
                    raise ValueError('Missing length extension')
                value = encoded[cursor]
                cursor += 1
                length += value
                if value != 255:
                    break
        return length

    while cursor < len(encoded):
        token = encoded[cursor]
        cursor += 1
        literals = extended(token >> 4)
        if literals > len(encoded) - cursor or literals > expected - len(result):
            raise ValueError('Literal overrun')
        result.extend(encoded[cursor:cursor + literals])
        cursor += literals
        if cursor == len(encoded):
            break
        if cursor + 2 > len(encoded):
            raise ValueError('Missing match offset')
        distance = int.from_bytes(encoded[cursor:cursor + 2], 'little')
        cursor += 2
        length = extended(token & 15) + 4
        if not distance or distance > len(result) or length > expected - len(result):
            raise ValueError('Match overrun')
        for _ in range(length):
            result.append(result[-distance])
    if len(result) != expected:
        raise ValueError('Wrong block length')
    return bytes(result)


def reference_quick_frame(encoded, expected):
    if len(encoded) < 8 or encoded[:8] != struct.pack('>II', 0x4D534C34, QUICK_BLOCK):
        raise ValueError('Wrong frame header')
    cursor = 8
    result = bytearray()
    while len(result) < expected:
        if cursor + 8 > len(encoded):
            raise ValueError('Missing block header')
        raw, packed = struct.unpack_from('>II', encoded, cursor)
        cursor += 8
        plain = bool(packed & 0x80000000)
        packed &= 0x7FFFFFFF
        if raw != min(QUICK_BLOCK, expected - len(result)) or not packed:
            raise ValueError('Wrong block length')
        if (packed != raw if plain else packed >= raw) or cursor + packed > len(encoded):
            raise ValueError('Wrong payload length')
        block = encoded[cursor:cursor + packed]
        cursor += packed
        result.extend(block if plain else reference_lz4_block(block, raw))
    if cursor != len(encoded):
        raise ValueError('Trailing frame bytes')
    return bytes(result)


class Span(C.Structure):
    _fields_ = [("data", C.c_void_p), ("size", C.c_uint)]


ReadWindow = C.CFUNCTYPE(C.c_bool, C.c_void_p, C.c_uint, C.POINTER(Span))


class StreamSource(C.Structure):
    _fields_ = [('buffer', Span), ('packed', C.c_uint), ('read', ReadWindow), ('context', C.c_void_p)]


class Result(C.Structure):
    _fields_ = [("status", C.c_int), ("compressed", C.c_uint),
                ("raw", C.c_uint), ("adler", C.c_uint)]


class DirectPolicy(C.Structure):
    _fields_ = [(name, C.c_size_t) for name in
                ('copies', 'copied_bytes', 'queries', 'allowed', 'first', 'last')]


class Guarded:
    def __init__(self, size):
        self.size = size
        self.buf = C.create_string_buffer(size + 128)
        C.memset(C.addressof(self.buf), 0xA7, len(self.buf))
        self.ptr = (C.addressof(self.buf) + 63) & ~31

    def data(self):
        return C.string_at(self.ptr, self.size)

    def guards(self):
        before = self.ptr - C.addressof(self.buf)
        return (self.buf.raw[:before] == bytes([0xA7]) * before and
                self.buf.raw[before + self.size:] ==
                bytes([0xA7]) * (len(self.buf) - before - self.size))


class StateCodecTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        compiler = ROOT / "toolchain/clang++.exe"
        if not compiler.exists():
            raise unittest.SkipTest("Bundled Windows compiler required")
        cls.folder = tempfile.TemporaryDirectory()
        cls.addClassCleanup(cls.folder.cleanup)
        shim = Path(cls.folder.name) / "shim.cpp"
        shim.write_text(r'''
#include "../src/state_codec.cpp"
extern "C" {
__declspec(dllexport) int referenceQuick(void *w,const char *source,char *dest,int size,int capacity) {
 return LZ4_compress_fast_extState(w,source,dest,size,capacity,1);
}
void *memcpy(void *d,const void *s,__SIZE_TYPE__ n) {
    unsigned char *o=(unsigned char*)d;const unsigned char *i=(const unsigned char*)s;
    while(n--)*o++=*i++;return d;
}
void *memset(void *d,int b,__SIZE_TYPE__ n) {
    unsigned char *o=(unsigned char*)d;while(n--)*o++=(unsigned char)b;return d;
}
void *memmove(void *d,const void *s,__SIZE_TYPE__ n) {
    unsigned char *o=(unsigned char*)d;const unsigned char *i=(const unsigned char*)s;
    if(o<i){for(__SIZE_TYPE__ j=0;j<n;++j)o[j]=i[j];}
    else{while(n){--n;o[n]=i[n];}}return d;
}
int memcmp(const void *a,const void *b,__SIZE_TYPE__ n) {
    const unsigned char *x=(const unsigned char*)a,*y=(const unsigned char*)b;
    while(n--){if(*x!=*y)return *x-*y;++x;++y;}return 0;
}
__declspec(dllexport) unsigned int workspace() {return StateCodec::workspaceSize();}
__declspec(dllexport) void pack(void *w,unsigned int ws,const StateCodec::ReadSpan *s,
 unsigned int n,const StateCodec::WriteSpan *d,StateCodec::Result *r) {
 *r=StateCodec::compress(w,ws,s,n,d);
}
__declspec(dllexport) void packMany(void *w,unsigned int ws,const StateCodec::ReadSpan *s,
 unsigned int n,const StateCodec::WriteSpan *d,unsigned int dn,StateCodec::Result *r) {
 *r=StateCodec::compress(w,ws,s,n,d,dn);
}
__declspec(dllexport) void packMode(void *w,unsigned int ws,const StateCodec::ReadSpan *s,
 unsigned int n,const StateCodec::WriteSpan *d,unsigned int dn,unsigned int compact,StateCodec::Result *r) {
 *r=StateCodec::compress(w,ws,s,n,d,dn,compact!=0);
}
__declspec(dllexport) void packQuick(void *w,unsigned int ws,const StateCodec::ReadSpan *s,
 unsigned int n,const StateCodec::WriteSpan *d,unsigned int dn,StateCodec::Result *r) {
 *r=StateCodec::compress(w,ws,s,n,d,dn,false,true);
}
__declspec(dllexport) void repack(void *w,unsigned int ws,void *pw,unsigned int pws,
 const StateCodec::ReadSpan *s,unsigned int n,const StateCodec::WriteSpan *d,
 unsigned int dn,unsigned int raw,unsigned int adler,unsigned int compact,StateCodec::Result *r) {
 *r=StateCodec::repack(w,ws,pw,pws,s,n,d,dn,raw,adler,compact!=0);
}
__declspec(dllexport) int check(void *w,unsigned int ws,const StateCodec::ReadSpan *s,
 unsigned int n,unsigned int raw,unsigned int adler) {
 return StateCodec::validate(w,ws,s,n,raw,adler);
}
__declspec(dllexport) int unpack(void *w,unsigned int ws,const StateCodec::ReadSpan *s,
 unsigned int n,const StateCodec::WriteSpan *d,unsigned int dn,unsigned int raw,unsigned int adler) {
 return StateCodec::decompress(w,ws,s,n,d,dn,raw,adler);
}
__declspec(dllexport) int unpackVerified(void *w,unsigned int ws,const StateCodec::ReadSpan *s,
 unsigned int n,const StateCodec::WriteSpan *d,unsigned int dn,unsigned int raw,unsigned int adler) {
 return StateCodec::decompressVerified(w,ws,s,n,d,dn,raw,adler);
}
__declspec(dllexport) int checkStream(void*w,unsigned int ws,const StateCodec::StreamSource*s,
 unsigned int raw,unsigned int adler){return StateCodec::validateStream(w,ws,*s,raw,adler);}
__declspec(dllexport) int unpackStream(void*w,unsigned int ws,const StateCodec::StreamSource*s,
 const StateCodec::WriteSpan*d,unsigned int n,unsigned int raw,unsigned int adler){
 return StateCodec::decompressStreamVerified(w,ws,*s,d,n,raw,adler);}
struct CopyPolicy {unsigned int calls,bytes;};
void retainPolicy(void *p,void *,const void *,unsigned int n) {
 CopyPolicy *policy=(CopyPolicy*)p;++policy->calls;policy->bytes+=n;
}
__declspec(dllexport) int unpackRetained(void *w,unsigned int ws,const StateCodec::ReadSpan *s,
 unsigned int n,const StateCodec::WriteSpan *d,unsigned int dn,unsigned int raw,unsigned int adler,
 CopyPolicy *policy) {
 return StateCodec::decompressVerified(w,ws,s,n,d,dn,raw,adler,retainPolicy,policy);
}
void copyPolicy(void *p,void *d,const void *s,unsigned int n) {
 CopyPolicy *policy=(CopyPolicy*)p;++policy->calls;policy->bytes+=n;
 memcpy(d,s,n);
}
__declspec(dllexport) int unpackPolicy(void *w,unsigned int ws,const StateCodec::ReadSpan *s,
 unsigned int n,const StateCodec::WriteSpan *d,unsigned int dn,unsigned int raw,unsigned int adler,
 CopyPolicy *policy) {
 return StateCodec::decompress(w,ws,s,n,d,dn,raw,adler,copyPolicy,policy);
}
__declspec(dllexport) int unpackVerifiedPolicy(void *w,unsigned int ws,const StateCodec::ReadSpan *s,
 unsigned int n,const StateCodec::WriteSpan *d,unsigned int dn,unsigned int raw,unsigned int adler,
 CopyPolicy *policy) {
 return StateCodec::decompressVerified(w,ws,s,n,d,dn,raw,adler,copyPolicy,policy);
}
struct DirectPolicy {__UINTPTR_TYPE__ copies,bytes,queries,allowed,first,last;};
bool directPolicy(void *p,void *d,unsigned int n) {
 DirectPolicy *policy=(DirectPolicy*)p;++policy->queries;
 __UINTPTR_TYPE__ first=(__UINTPTR_TYPE__)d;
 if(policy->last>first&&policy->first<first+n)return false;
 ++policy->allowed;return true;
}
void selectiveCopy(void *p,void *d,const void *s,unsigned int n) {
 DirectPolicy *policy=(DirectPolicy*)p;++policy->copies;policy->bytes+=n;
 unsigned char *out=(unsigned char*)d;const unsigned char *in=(const unsigned char*)s;
 for(unsigned int i=0;i<n;++i){__UINTPTR_TYPE__ address=(__UINTPTR_TYPE__)(out+i);
  if(address<policy->first||address>=policy->last)out[i]=in[i];}
}
__declspec(dllexport) int unpackDirect(void *w,unsigned int ws,const StateCodec::ReadSpan *s,
 unsigned int n,const StateCodec::WriteSpan *d,unsigned int dn,unsigned int raw,unsigned int adler,
 DirectPolicy *policy) {
 return StateCodec::decompressVerified(w,ws,s,n,d,dn,raw,adler,selectiveCopy,policy,directPolicy);
}
}
''', encoding="ascii")
        production = (ROOT / "src/state_codec.cpp").read_text()
        counter = production[production.index('struct PackSink {'):production.index('struct ScatterSink {')]
        word = production[production.index('bool packWord('):production.index('Result quickPack(')]
        with shim.open('a', encoding='ascii') as stream:
            stream.write('\nnamespace CounterBoundary {\nusing StateCodec::WriteSpan;\n'
                         'const unsigned int kMaxSize=0xffffffffu;\n' + counter + word + r'''
}
extern "C" __declspec(dllexport) int counterBoundary(unsigned int start,int length,
 unsigned int word,unsigned int *after) {
 CounterBoundary::PackSink sink={0,0,start};
 int result=word?CounterBoundary::packWord(sink,0):CounterBoundary::packOutput(0,length,&sink);
 *after=sink.written;return result;
}
''')
        library = shim.with_suffix(".dll")
        subprocess.run([str(compiler), "--target=x86_64-pc-windows-msvc", "-shared",
                        "-nostdlib", "-fuse-ld=lld", "-Wl,/noentry", "-O2",
                        "-fno-builtin", "-mno-stack-arg-probe", "-I", str(ROOT / "include"),
                        str(shim), "-o", str(library)], check=True)
        cls.lib = C.CDLL(str(library))
        cls.addClassCleanup(lambda: C.windll.kernel32.FreeLibrary(C.c_void_p(cls.lib._handle)))
        cls.lib.workspace.restype = C.c_uint
        cls.lib.referenceQuick.argtypes = [C.c_void_p,C.c_void_p,C.c_void_p,C.c_int,C.c_int]
        cls.lib.pack.argtypes = [C.c_void_p, C.c_uint, C.POINTER(Span), C.c_uint,
                                 C.POINTER(Span), C.POINTER(Result)]
        cls.lib.packMany.argtypes = [C.c_void_p, C.c_uint, C.POINTER(Span), C.c_uint,
                                     C.POINTER(Span), C.c_uint, C.POINTER(Result)]
        cls.lib.packMode.argtypes = cls.lib.packMany.argtypes[:-1]+[C.c_uint,C.POINTER(Result)]
        cls.lib.packQuick.argtypes = cls.lib.packMany.argtypes
        cls.lib.repack.argtypes = [C.c_void_p,C.c_uint,C.c_void_p,C.c_uint,
            C.POINTER(Span),C.c_uint,C.POINTER(Span),C.c_uint,C.c_uint,C.c_uint,
            C.c_uint,C.POINTER(Result)]
        cls.lib.check.argtypes = [C.c_void_p, C.c_uint, C.POINTER(Span), C.c_uint,
                                  C.c_uint, C.c_uint]
        cls.lib.unpack.argtypes = [C.c_void_p, C.c_uint, C.POINTER(Span), C.c_uint,
                                   C.POINTER(Span), C.c_uint, C.c_uint, C.c_uint]
        cls.lib.unpackPolicy.argtypes = cls.lib.unpack.argtypes + [C.POINTER(C.c_uint)]
        cls.lib.unpackVerified.argtypes = cls.lib.unpack.argtypes
        cls.lib.unpackVerifiedPolicy.argtypes = cls.lib.unpackPolicy.argtypes
        cls.lib.unpackDirect.argtypes = cls.lib.unpack.argtypes + [C.POINTER(DirectPolicy)]
        cls.lib.unpackRetained.argtypes = cls.lib.unpackPolicy.argtypes
        cls.lib.counterBoundary.argtypes = [C.c_uint, C.c_int, C.c_uint, C.POINTER(C.c_uint)]
        cls.lib.checkStream.argtypes = [C.c_void_p, C.c_uint, C.POINTER(StreamSource), C.c_uint, C.c_uint]
        cls.lib.unpackStream.argtypes = [C.c_void_p, C.c_uint, C.POINTER(StreamSource), C.POINTER(Span), C.c_uint, C.c_uint, C.c_uint]

    def setUp(self):
        self.work = Guarded(self.lib.workspace())
        self.assertLessEqual(self.work.size, 0x50000)

    def test_specialized_quick_compressor_matches_original_exactly(self):
        rng = random.Random(73)
        for size in (1, 12, 255, 65545, 65546, 65547, QUICK_BLOCK):
            for kind in range(3):
                with self.subTest(size=size,kind=kind):
                    raw = (bytes(size) if kind == 0 else
                           (b'Moonshine state 123'*((size+17)//18))[:size] if kind == 1 else
                           rng.randbytes(size))
                    source = C.create_string_buffer(raw)
                    reference = Guarded(QUICK_BLOCK + QUICK_BLOCK//255 + 16)
                    length = self.lib.referenceQuick(self.work.ptr,source,reference.ptr,size,reference.size)
                    self.assertGreater(length,0)
                    result,encoded = self.pack(self.source(raw),[len(raw)+32],quick=True)
                    self.assertEqual(result.status,SUCCESS)
                    block = raw if length >= size else C.string_at(reference.ptr,length)
                    self.assertEqual(encoded[16:result.compressed],block)
                    self.assertTrue(reference.guards())

    def streamed(self, encoded, window, fail_at=None, invalid=None):
        buffer = Guarded(window)
        calls = []

        @ReadWindow
        def reader(context, offset, out):
            calls.append(offset)
            if fail_at is not None and offset >= fail_at:
                return False
            start = (offset // window) * window
            data = encoded[start:start + window]
            if not data:
                return False
            C.memmove(buffer.ptr, data, len(data))
            out[0] = Span(buffer.ptr + offset - start, len(data) - (offset - start))
            if invalid == 'outside':out[0].data = buffer.ptr + window
            elif invalid == 'oversize':out[0].size = window + 1
            elif invalid == 'zero':out[0].size = 0
            elif invalid == 'wrapped':out[0] = Span(C.c_void_p(-2).value, 8)
            return True

        source = StreamSource(Span(buffer.ptr, buffer.size), len(encoded), reader, None)
        return source, buffer, reader, calls

    def test_streamed_zlib_and_quick_cross_header_block_and_dictionary_boundaries(self):
        raw = random.Random(331).randbytes(QUICK_BLOCK * 2 + 91)
        quick = b'MSL4' + struct.pack('>I', QUICK_BLOCK)
        for at in range(0, len(raw), QUICK_BLOCK):
            block = raw[at:at + QUICK_BLOCK]
            quick += struct.pack('>II', len(block), len(block) | 0x80000000) + block
        for encoded in (zlib.compress(raw), quick):
            for window in (1, 7, 32767, 131073, 4 * 1024 * 1024):
                with self.subTest(format=encoded[:4], window=window):
                    source, buffer, reader, calls = self.streamed(encoded, window)
                    out = Guarded(len(raw))
                    pieces = (Span * 3)(Span(out.ptr, 31), Span(out.ptr + 31, 131000),
                                        Span(out.ptr + 131031, len(raw) - 131031))
                    self.assertEqual(self.lib.checkStream(self.work.ptr, self.work.size, C.byref(source),
                        len(raw), zlib.adler32(raw)), SUCCESS)
                    self.assertEqual(out.data(), b'\xa7' * len(raw))
                    self.assertEqual(self.lib.unpackStream(self.work.ptr, self.work.size, C.byref(source),
                        pieces, 3, len(raw), zlib.adler32(raw)), SUCCESS)
                    self.assertEqual(out.data(), raw)
                    self.assertTrue(out.guards() and buffer.guards() and self.work.guards())
                    self.assertGreater(len(calls), 1)

    def repack(self, encoded, raw, compact=False, capacities=None):
        packed_work = Guarded(self.lib.workspace())
        source = self.source(encoded, sorted(set((0, 1, 7, len(encoded) // 2, len(encoded) - 1))))
        before = b''.join(C.string_at(s.data, s.size) for s in source)
        buffers = [] if capacities is None else [Guarded(n) for n in capacities]
        output = None if capacities is None else (Span * len(buffers))(*[Span(b.ptr,b.size) for b in buffers])
        result = Result()
        self.lib.repack(self.work.ptr,self.work.size,packed_work.ptr,packed_work.size,
            source,len(source),output,len(buffers),len(raw),zlib.adler32(raw),compact,C.byref(result))
        self.assertEqual(b''.join(C.string_at(s.data,s.size) for s in source), before)
        self.assertTrue(packed_work.guards() and all(b.guards() for b in buffers))
        return result, b''.join(b.data() for b in buffers)[:result.compressed]

    def test_repack_quick_and_zlib_retain_exact_bytes_across_blocks_and_dictionary(self):
        random_bytes = random.Random(67021).randbytes(70013)
        for raw in (b'x', bytes(range(251))*1600 + random_bytes, random_bytes*3):
            measured,_ = self.pack(self.source(raw),quick=True)
            _,quick = self.pack(self.source(raw),[measured.compressed],quick=True)
            for encoded in (quick,zlib.compress(raw)):
                for compact in (False,True):
                    count,_ = self.repack(encoded,raw,compact)
                    self.assertEqual(count.status,SUCCESS)
                    result,packed = self.repack(encoded,raw,compact,[1,count.compressed//2,count.compressed-1-count.compressed//2])
                    self.assertEqual((result.status,result.raw,result.adler),(SUCCESS,len(raw),zlib.adler32(raw)))
                    self.assertEqual(zlib.decompress(packed),raw)
                    self.assertEqual(result.compressed,count.compressed)
                    self.assertEqual(self.lib.check(self.work.ptr,self.work.size,self.source(packed),1,len(raw),result.adler),SUCCESS)

    def test_repack_full_counts_complete_size_and_never_changes_source(self):
        raw = random.Random(2304).randbytes(270001)
        encoded = zlib.compress(raw)
        count,_ = self.repack(encoded,raw,True)
        for capacity in (0,1,count.compressed-1,count.compressed):
            result,_ = self.repack(encoded,raw,True,[capacity])
            self.assertEqual(result.status,SUCCESS if capacity==count.compressed else FULL)
            self.assertEqual((result.compressed,result.raw,result.adler),(count.compressed,len(raw),zlib.adler32(raw)))

    def test_repack_miniz_streams_keeps_exact_bytes_when_size_reduction_is_small(self):
        for raw in (random.Random(903).randbytes(100000),
                    random.Random(913).randbytes(32768)*4):
            for initial_compact in (False,True):
                count,_ = self.pack(self.source(raw),compact=initial_compact)
                _,encoded = self.pack(self.source(raw),[count.compressed],compact=initial_compact)
                for compact in (False,True):
                    result,packed = self.repack(encoded,raw,compact,[len(raw)+1024])
                    self.assertEqual(result.status,SUCCESS)
                    self.assertEqual(zlib.decompress(packed),raw)
                    self.assertEqual(result.adler,zlib.adler32(raw))

    def test_repack_corrupt_stream_cannot_be_committed(self):
        raw = bytes(range(253))*800
        measured,_ = self.pack(self.source(raw),quick=True)
        _,quick = self.pack(self.source(raw),[measured.compressed],quick=True)
        for encoded in (quick,zlib.compress(raw)):
            for broken,expected in ((encoded[:-1],raw),(encoded+b'\0',raw),(encoded,raw+b'\0'),
                                    (encoded,bytes([raw[0]^1])+raw[1:])):
                result,_ = self.repack(broken,expected,capacities=[len(raw)])
                self.assertEqual(result.status,CORRUPT)
                self.assertEqual(result.compressed,0)

    def test_repack_rejects_workspace_and_output_aliases_before_writing(self):
        raw = b'ownership'*10000
        source = self.source(zlib.compress(raw))
        work = Guarded(self.lib.workspace())
        target = Guarded(len(raw))
        good = (Span*1)(Span(target.ptr,target.size))
        cases = [(self.work.ptr,self.work.size,good,1,INVALID),
                 (work.ptr+1,work.size-1,good,1,INVALID),
                 (work.ptr,work.size-1,good,1,WORKSPACE),
                 (work.ptr,work.size,(Span*1)(Span(work.ptr,100)),1,INVALID),
                 (work.ptr,work.size,(Span*1)(Span(self.work.ptr,100)),1,INVALID),
                 (work.ptr,work.size,(Span*1)(Span(source[0].data,source[0].size)),1,INVALID),
                 (work.ptr,work.size,C.cast(work.ptr,C.POINTER(Span)),1,INVALID),
                 (work.ptr,work.size,good,65,INVALID)]
        for pointer,size,output,count,expected in cases:
            before = (work.data(),self.work.data(),target.data())
            result = Result()
            self.lib.repack(self.work.ptr,self.work.size,pointer,size,source,1,output,count,
                len(raw),zlib.adler32(raw),False,C.byref(result))
            self.assertEqual(result.status,expected)
            self.assertEqual((work.data(),self.work.data(),target.data()),before)
        self.assertTrue(work.guards() and target.guards())

    def test_stream_reader_faults_and_malformed_input_do_not_write_during_validation(self):
        raw = random.Random(199).randbytes(90000)
        packed = zlib.compress(raw)
        for invalid in ('outside', 'oversize', 'zero', 'wrapped'):
            source, buffer, reader, calls = self.streamed(packed, 1024, invalid=invalid)
            self.assertEqual(self.lib.checkStream(self.work.ptr, self.work.size, C.byref(source),
                len(raw), zlib.adler32(raw)), CORRUPT)
            self.assertTrue(buffer.guards() and self.work.guards())
        quick = b'MSL4' + struct.pack('>III', QUICK_BLOCK, len(raw), len(raw) | 0x80000000) + raw
        for encoded in (packed, quick):
            for data, adler, failure in ((encoded[:-1], zlib.adler32(raw), None),
                    (encoded + b'\0', zlib.adler32(raw), None),
                    (encoded, zlib.adler32(raw) ^ 1, None),
                    (encoded, zlib.adler32(raw), 30000)):
                source, buffer, reader, calls = self.streamed(data, 1024, fail_at=failure)
                self.assertEqual(self.lib.checkStream(self.work.ptr, self.work.size, C.byref(source), len(raw), adler), CORRUPT)

    def test_stream_writing_failure_reports_commit_and_descriptor_bounds_are_checked(self):
        raw = random.Random(200).randbytes(150000)
        packed = zlib.compress(raw)
        source, buffer, reader, calls = self.streamed(packed, 4096, fail_at=80000)
        out = Guarded(len(raw))
        dest = (Span * 1)(Span(out.ptr, out.size))
        self.assertEqual(self.lib.unpackStream(self.work.ptr, self.work.size, C.byref(source),
            dest, 1, len(raw), zlib.adler32(raw)), COMMIT)
        self.assertNotEqual(out.data(), b'\xa7' * len(raw))
        self.assertTrue(out.guards() and buffer.guards() and self.work.guards())
        source, buffer, reader, calls = self.streamed(packed, 4096)
        for span, count, expected in ((Span(buffer.ptr, 4096), 1, 4096),
                (Span(self.work.ptr, len(raw)), 1, len(raw)),
                (Span(out.ptr, len(raw)), 1, len(raw) - 1)):
            dest = (Span * 1)(span)
            self.assertEqual(self.lib.unpackStream(self.work.ptr, self.work.size, C.byref(source),
                dest, count, expected, zlib.adler32(raw)), INVALID)
        self.assertEqual(calls, [])

    def test_copy_policy_runs_only_after_complete_validation(self):
        data = bytes(range(256)) * 300
        encoded = zlib.compress(data)
        source = self.source(encoded, [3, len(encoded) // 2])
        buffers = [Guarded(12345), Guarded(len(data) - 12345)]
        output = (Span * 2)(*[Span(b.ptr, b.size) for b in buffers])
        policy = (C.c_uint * 2)()
        args = (self.work.ptr, self.work.size, source, len(source), output, 2,
                len(data), zlib.adler32(data))
        self.assertEqual(self.lib.unpackPolicy(*args, policy), SUCCESS)
        self.assertEqual(policy[1], len(data))
        self.assertGreater(policy[0], 2)
        self.assertEqual(b''.join(b.data() for b in buffers), data)
        for b in buffers:
            self.assertTrue(b.guards())
        broken = bytearray(encoded)
        broken[-1] ^= 1
        source = self.source(bytes(broken))
        policy = (C.c_uint * 2)()
        before = [b.data() for b in buffers]
        status = self.lib.unpackPolicy(self.work.ptr, self.work.size, source, 1,
                                      output, 2, len(data), zlib.adler32(data), policy)
        self.assertEqual((status, policy[0], policy[1]), (CORRUPT, 0, 0))
        self.assertEqual([b.data() for b in buffers], before)

    def test_verified_scatter_decode_retains_copy_policy_and_exact_output(self):
        for compact in (False, True):
            data=bytes(range(251))*401+random.Random(678).randbytes(65001)
            measured,_=self.pack(self.source(data),compact=compact)
            packed,encoded=self.pack(self.source(data,[1,32767,32768]),
                                     [1,measured.compressed-1],compact=compact)
            self.assertEqual(packed.status,SUCCESS)
            source=self.source(encoded,[0,1,2,32768,len(encoded)-1])
            buffers=[Guarded(n) for n in (0,1,32766,2,len(data)-32769)]
            output=(Span*len(buffers))(*[Span(b.ptr,b.size) for b in buffers])
            policy=(C.c_uint*2)()
            status=self.lib.unpackVerifiedPolicy(self.work.ptr,self.work.size,source,len(source),
                output,len(output),len(data),packed.adler,policy)
            self.assertEqual(status,SUCCESS)
            self.assertEqual(policy[1],len(data))
            self.assertGreater(policy[0],3)
            self.assertEqual(b''.join(b.data()for b in buffers),data)
            self.assertTrue(all(b.guards()for b in buffers))

    def test_verified_decode_keeps_workspace_descriptor_source_and_destination_guards(self):
        data=b'verified ownership'*3000
        source=self.source(zlib.compress(data))
        target=Guarded(len(data))
        output=(Span*1)(Span(target.ptr,target.size))
        source_bytes=C.string_at(C.addressof(source),C.sizeof(source))
        output_bytes=C.string_at(C.addressof(output),C.sizeof(output))
        invalid_sources=[
            (self.work.ptr+1,self.work.size,source,1,INVALID),
            (self.work.ptr,self.work.size-1,source,1,WORKSPACE),
            (self.work.ptr,self.work.size,source,0,INVALID),
            (self.work.ptr,self.work.size,source,65,INVALID),
            (self.work.ptr,self.work.size,C.cast(self.work.ptr,C.POINTER(Span)),1,INVALID),
            (self.work.ptr,self.work.size,(Span*1)(Span(self.work.ptr,1)),1,INVALID),
        ]
        for wp,ws,sp,count,expected in invalid_sources:
            self.assertEqual(self.lib.unpackVerified(wp,ws,sp,count,output,1,
                len(data),zlib.adler32(data)),expected)
        self_overlapping=(Span*1)()
        self_overlapping[0]=Span(C.addressof(self_overlapping),len(data))
        for dest,count,raw in (
            (output,0,len(data)),(output,65,len(data)),
            (C.cast(self.work.ptr,C.POINTER(Span)),1,len(data)),
            ((Span*1)(Span(self.work.ptr,len(data))),1,len(data)),
            ((Span*1)(Span(source[0].data,len(data))),1,len(data)),
            ((Span*1)(Span(C.addressof(source),len(data))),1,len(data)),
            (self_overlapping,1,len(data)),
            ((Span*2)(Span(target.ptr,len(data)//2),Span(target.ptr+1,len(data)-len(data)//2)),2,len(data)),
            (output,1,len(data)+1),(output,1,0),
        ):
            self.assertEqual(self.lib.unpackVerified(self.work.ptr,self.work.size,source,1,
                dest,count,raw,zlib.adler32(data)),INVALID)
        self.assertEqual(target.data(),bytes([0xa7])*len(data))
        self.assertTrue(target.guards())
        self.assertEqual(C.string_at(C.addressof(source),C.sizeof(source)),source_bytes)
        self.assertEqual(C.string_at(C.addressof(output),C.sizeof(output)),output_bytes)

    def test_verified_decode_failure_is_fatal_commit_failure_not_safe_corruption(self):
        data=random.Random(241).randbytes(70003)
        encoded=zlib.compress(data)
        target=Guarded(len(data));output=(Span*1)(Span(target.ptr,target.size))
        for compressed,adler in ((encoded,zlib.adler32(data)^1),(encoded[:-1],zlib.adler32(data))):
            source=self.source(compressed)
            C.memset(target.ptr,0xa7,len(data))
            status=self.lib.unpackVerified(self.work.ptr,self.work.size,source,1,output,1,len(data),adler)
            self.assertEqual(status,COMMIT)
            self.assertNotEqual(target.data(),bytes([0xa7])*len(data))
            self.assertTrue(target.guards())

    def tearDown(self):
        self.assertTrue(self.work.guards())

    def source(self, data, cuts=()):
        edges = [0, *cuts, len(data)]
        buffers = [C.create_string_buffer(data[a:b]) for a, b in zip(edges, edges[1:])]
        spans = (Span * len(buffers))(*[Span(C.addressof(b), y - x)
                                      for b, x, y in zip(buffers, edges, edges[1:])])
        spans._owners = buffers
        return spans

    def pack(self, source, capacities=None, compact=False, quick=False):
        guards = [Guarded(n) for n in capacities] if capacities is not None else []
        output = (Span * len(guards))(*[Span(g.ptr, g.size) for g in guards]) if guards else None
        result = Result()
        args = (self.work.ptr, self.work.size, source, len(source), output, len(guards))
        if quick:
            self.lib.packQuick(*args, C.byref(result))
        else:
            self.lib.packMode(*args, compact, C.byref(result))
        for guard in guards:
            self.assertTrue(guard.guards())
        return result, b"".join(g.data() for g in guards)

    def decode(self, encoded, raw_size, adler, cuts=(), output_sizes=None):
        source = self.source(encoded, cuts)
        sizes = output_sizes or [raw_size]
        guards = [Guarded(n) for n in sizes]
        output = (Span * len(guards))(*[Span(g.ptr, g.size) for g in guards])
        status = self.lib.unpack(self.work.ptr, self.work.size, source, len(source), output,
                                 len(output), raw_size, adler)
        for guard in guards:
            self.assertTrue(guard.guards())
        return status, b"".join(g.data() for g in guards)

    def test_scatter_compress_two_outputs_and_wrapping_scatter_inflate(self):
        data = (bytes(range(256)) * 4000) + random.Random(57).randbytes(50001)
        source = self.source(data, [0, 1, 7, 32767, 32768, 32769, 70001])
        measured, _ = self.pack(source)
        self.assertEqual((measured.status, measured.raw, measured.adler),
                         (SUCCESS, len(data), zlib.adler32(data)))
        packed, output = self.pack(source, [1, measured.compressed - 1])
        self.assertEqual(packed.status, SUCCESS)
        self.assertEqual(packed.compressed, measured.compressed)
        self.assertEqual(zlib.decompress(output), data)
        status, decoded = self.decode(output, len(data), measured.adler, cuts=[0, 1, 2, 3, 9],
                                      output_sizes=[0, 3, 32765, 1, 55555, len(data) - 88324])
        self.assertEqual((status, decoded), (SUCCESS, data))

    def test_output_full_counts_exact_size_without_touching_guards(self):
        data = random.Random(88).randbytes(100001)
        source = self.source(data, [50000])
        expected, _ = self.pack(source)
        for capacity in (0, 1, 7, expected.compressed - 1):
            packed, _ = self.pack(source, [capacity // 2, capacity - capacity // 2])
            self.assertEqual((packed.status, packed.compressed, packed.adler),
                             (FULL, expected.compressed, zlib.adler32(data)))

    def test_three_compression_outputs_cross_both_pool_banks_and_validate_extra_span(self):
        data = random.Random(52).randbytes(70001)
        source = self.source(data, [12345])
        expected, _ = self.pack(source)
        for sizes in ((4096, 13000, expected.compressed - 17096),
                      (0, 1, expected.compressed - 1), (1, 0, expected.compressed - 1),
                      (1, 2, expected.compressed - 4)):
            guards = [Guarded(n) for n in sizes]
            output = (Span * 3)(*[Span(g.ptr, g.size) for g in guards])
            result = Result()
            self.lib.packMany(self.work.ptr, self.work.size, source, len(source),
                              output, 3, C.byref(result))
            self.assertEqual((result.compressed, result.raw, result.adler),
                             (expected.compressed, len(data), zlib.adler32(data)))
            if sum(sizes) == expected.compressed:
                self.assertEqual(result.status, SUCCESS)
                self.assertEqual(zlib.decompress(b"".join(g.data() for g in guards)), data)
            else:
                self.assertEqual(result.status, FULL)
            for guard in guards:
                self.assertTrue(guard.guards())
        original = [g.data() for g in guards]
        for count in (0, 65):
            self.lib.packMany(self.work.ptr, self.work.size, source, len(source),
                              output, count, C.byref(result))
            self.assertEqual(result.status, INVALID)
        output[2] = Span(source[0].data, 8)
        self.lib.packMany(self.work.ptr, self.work.size, source, len(source),
                          output, 3, C.byref(result))
        self.assertEqual(result.status, INVALID)
        output[2] = Span(guards[0].ptr, 1)
        self.lib.packMany(self.work.ptr, self.work.size, source, len(source),
                          output, 3, C.byref(result))
        self.assertEqual(result.status, INVALID)
        self.assertEqual([g.data() for g in guards], original)

    def test_standard_zlib_stream_and_split_header_checksum(self):
        data = b"abc123" * 18000
        encoded = zlib.compress(data, 9)
        for cut in (0, 1, 2, 3, len(encoded) - 4, len(encoded) - 1, len(encoded)):
            status, decoded = self.decode(encoded, len(data), zlib.adler32(data), cuts=[cut])
            self.assertEqual((status, decoded), (SUCCESS, data))

    def test_corruption_truncation_wrong_sizes_and_trailing_data_write_nothing(self):
        data = random.Random(26).randbytes(70003)
        encoded = zlib.compress(data)
        bad = [encoded[:n] for n in (0, 1, 2, 7, len(encoded) // 2, len(encoded) - 1)]
        bad += [encoded + b"\0", encoded + encoded]
        for at in (0, 1, 10, 32768, len(encoded) - 4, len(encoded) - 1):
            changed = bytearray(encoded)
            changed[at] ^= 0x80
            bad.append(bytes(changed))
        for item in bad:
            status, decoded = self.decode(item, len(data), zlib.adler32(data),
                                          output_sizes=[17, len(data) - 17])
            self.assertIn(status, (INVALID, CORRUPT))
            self.assertEqual(decoded, bytes([0xA7]) * len(data))
        for expected, adler in ((len(data) - 1, zlib.adler32(data)),
                                (len(data) + 1, zlib.adler32(data)),
                                (len(data), zlib.adler32(data) ^ 1)):
            status, decoded = self.decode(encoded, expected, adler)
            self.assertEqual(status, CORRUPT)
            self.assertEqual(decoded, bytes([0xA7]) * expected)

    def test_destination_length_mismatch_writes_nothing(self):
        data = b"saved state" * 100
        for capacity in (len(data) - 1, len(data) + 1):
            status, decoded = self.decode(zlib.compress(data), len(data), zlib.adler32(data),
                                          output_sizes=[capacity])
            self.assertEqual((status, decoded), (INVALID, bytes([0xA7]) * capacity))

    def test_arguments_overlap_and_workspace_bounds(self):
        data = b"guard test" * 100
        source = self.source(data)
        result = Result()
        for pointer, size, count, expected in (
                (self.work.ptr + 1, self.work.size, 1, INVALID),
                (self.work.ptr, self.work.size - 1, 1, WORKSPACE),
                (self.work.ptr, self.work.size, 0, INVALID),
                (self.work.ptr, self.work.size, 65, INVALID)):
            self.lib.pack(pointer, size, source, count, None, C.byref(result))
            self.assertEqual(result.status, expected)
        for bad in ((Span * 1)(Span(self.work.ptr, 4)),
                    (Span * 1)(Span(None, 4)),
                    (Span * 1)(Span((1 << (C.sizeof(C.c_void_p) * 8)) - 2, 4)),
                    (Span * 2)(Span(4096, 0xfffffff0), Span(8192, 0x20))):
            self.lib.pack(self.work.ptr, self.work.size, bad, len(bad), None, C.byref(result))
            self.assertEqual(result.status, INVALID)
        for output in ((Span * 2)(Span(source[0].data, 4), Span(None, 0)),
                       (Span * 2)(Span(self.work.ptr, 4), Span(None, 0))):
            self.lib.pack(self.work.ptr, self.work.size, source, len(source), output, C.byref(result))
            self.assertEqual(result.status, INVALID)
        encoded = self.source(zlib.compress(data))
        guard = Guarded(len(data))
        for output in ((Span * 2)(Span(guard.ptr, 600), Span(guard.ptr + 500, 400)),
                       (Span * 1)(Span(encoded[0].data, len(data))),
                       (Span * 1)(Span(self.work.ptr, len(data)))):
            self.assertEqual(self.lib.unpack(self.work.ptr, self.work.size, encoded, 1,
                output, len(output), len(data), zlib.adler32(data)), INVALID)
        self.assertEqual(guard.data(), bytes([0xA7]) * len(data))

    def test_private_real_sunshine_capture_when_available(self):
        path = ROOT / "build/foxtrot-held-input/private-us-bianco-state.bin"
        if not path.exists():
            self.skipTest("Private Sunshine capture is deliberately not distributed")
        data = path.read_bytes()
        source = self.source(data, [448, 0x1000, 0x50000, 0x100000, 0x800000])
        result, _ = self.pack(source)
        self.assertEqual(result.status, SUCCESS)
        packed, encoded = self.pack(source, [result.compressed // 3,
                                            result.compressed - result.compressed // 3])
        self.assertEqual(packed.status, SUCCESS)
        status, decoded = self.decode(encoded, len(data), result.adler,
            cuts=[1, 0x8000, result.compressed // 2],
            output_sizes=[448, 0x50000 - 448, len(data) - 0x50000])
        self.assertEqual((status, decoded), (SUCCESS, data))
        self.assertEqual(zlib.decompress(encoded), data)
        print(f"Private Sunshine sample: raw={len(data)}, deflate={result.compressed}, "
              f"workspace={self.work.size}; one Bianco sample, not an all-scene bound")

    def test_fast_and_compact_fuzz_cover_short_inputs_dict_wrap_and_scatter_boundaries(self):
        randomizer=random.Random(50821)
        sizes=[1,2,3,4,5,7,255,256,257,258,259,4095,4096,4097,
               32766,32767,32768,32769,32770,65535,65536,65537,98307]
        sizes += [randomizer.randrange(1,180000) for _ in range(24)]
        for index,size in enumerate(sizes):
            for kind in range(4):
                if kind==0:data=bytes([index&255])*size
                elif kind==1:data=(bytes(range(251))*((size+250)//251))[:size]
                elif kind==2:data=randomizer.randbytes(size)
                else:
                    block=randomizer.randbytes(min(size,8191))
                    data=bytearray((block*((size+len(block)-1)//len(block)))[:size])
                    for at in range(1,size,257):data[at]^=(at&255)
                    data=bytes(data)
                cuts=sorted([0,size]+[randomizer.randrange(size+1) for _ in range(7)])
                source=self.source(data,cuts)
                for compact in (False,True):
                    with self.subTest(size=size,kind=kind,compact=compact):
                        measured,_=self.pack(source,compact=compact)
                        self.assertEqual((measured.status,measured.raw,measured.adler),
                                         (SUCCESS,size,zlib.adler32(data)))
                        split=randomizer.randrange(measured.compressed+1)
                        packed,encoded=self.pack(source,[split,measured.compressed-split],compact)
                        self.assertEqual((packed.status,packed.compressed),(SUCCESS,measured.compressed))
                        self.assertEqual(zlib.decompress(encoded),data)
                        status,decoded=self.decode(encoded,size,measured.adler,cuts=[1,len(encoded)-1])
                        self.assertEqual((status,decoded),(SUCCESS,data))
                        if index%8==0:
                            short,_=self.pack(source,[1,measured.compressed-2],compact)
                            self.assertEqual((short.status,short.compressed),(FULL,measured.compressed))

    def test_compact_real_capture_roundtrip_and_full_count(self):
        path=ROOT/'build/foxtrot-held-input/private-us-bianco-state.bin'
        if not path.exists():self.skipTest('Private Sunshine capture is deliberately not distributed')
        data=path.read_bytes();source=self.source(data,[448,32767,32768,65537,0x800000])
        compact,_=self.pack(source,compact=True)
        fast,_=self.pack(source)
        self.assertLess(compact.compressed,fast.compressed)
        packed,encoded=self.pack(source,[compact.compressed//2,compact.compressed-compact.compressed//2],True)
        self.assertEqual(packed.status,SUCCESS)
        self.assertEqual(zlib.decompress(encoded),data)
        self.assertEqual(self.decode(encoded,len(data),compact.adler),(SUCCESS,data))
        short,_=self.pack(source,[0,compact.compressed-1],True)
        self.assertEqual((short.status,short.compressed,short.adler),(FULL,compact.compressed,compact.adler))

    def test_quick_frames_match_independent_decoder_across_block_and_scatter_boundaries(self):
        rng = random.Random(410293)
        sizes = (1, 4, 12, 255, 65535, 65536, QUICK_BLOCK - 1,
                 QUICK_BLOCK, QUICK_BLOCK + 1, QUICK_BLOCK * 2 + 29)
        for size in sizes:
            for kind in ('repeat', 'random', 'mixed'):
                if kind == 'repeat':
                    data = (b'abc123' * ((size + 5) // 6))[:size]
                elif kind == 'random':
                    data = rng.randbytes(size)
                else:
                    prefix = rng.randbytes(min(size, QUICK_BLOCK))
                    data = prefix + bytes(size - len(prefix))
                with self.subTest(size=size, kind=kind):
                    cuts = sorted([0, size] + [rng.randrange(size + 1) for _ in range(17)])
                    source = self.source(data, cuts)
                    counted, _ = self.pack(source, quick=True)
                    self.assertEqual((counted.status, counted.raw, counted.adler),
                                     (SUCCESS, size, zlib.adler32(data)))
                    edges = sorted([0, counted.compressed] +
                                   [rng.randrange(counted.compressed + 1) for _ in range(17)])
                    capacities = [b - a for a, b in zip(edges, edges[1:])]
                    packed, encoded = self.pack(source, capacities, quick=True)
                    self.assertEqual((packed.status, packed.compressed),
                                     (SUCCESS, counted.compressed))
                    self.assertEqual(reference_quick_frame(encoded, size), data)
                    read_cuts = sorted(list(range(min(18, len(encoded)))) +
                                       [rng.randrange(len(encoded) + 1) for _ in range(19)])
                    edges = sorted([0, size] + [rng.randrange(size + 1) for _ in range(17)])
                    status, decoded = self.decode(encoded, size, counted.adler, read_cuts,
                        [b - a for a, b in zip(edges, edges[1:])])
                    self.assertEqual((status, decoded), (SUCCESS, data))

    def test_quick_accepts_independently_constructed_plain_and_lz4_blocks(self):
        raw = b'A' * QUICK_BLOCK
        length = QUICK_BLOCK - 25
        extensions = bytes([255]) * (length // 255) + bytes([length % 255])
        packed = b'\x1fA\x01\x00' + extensions + b'\x50AAAAA'
        self.assertEqual(reference_lz4_block(packed, len(raw)), raw)
        tail = bytes(range(37))
        encoded = (struct.pack('>IIII', 0x4D534C34, QUICK_BLOCK, len(raw), len(packed)) + packed +
                   struct.pack('>II', len(tail), len(tail) | 0x80000000) + tail)
        data = raw + tail
        self.assertEqual(reference_quick_frame(encoded, len(data)), data)
        status, decoded = self.decode(encoded, len(data), zlib.adler32(data),
            cuts=[1, 7, 9, 15, 16, 19, len(packed) + 15, len(packed) + 17, len(encoded) - 1],
            output_sizes=[1, QUICK_BLOCK - 2, 2, len(tail) - 1])
        self.assertEqual((status, decoded), (SUCCESS, data))

    def test_quick_contiguous_decode_respects_retained_owner_filter(self):
        data = bytes(QUICK_BLOCK) + random.Random(921).randbytes(QUICK_BLOCK) + b'x' * 53
        measured, _ = self.pack(self.source(data), quick=True)
        packed, encoded = self.pack(self.source(data), [measured.compressed], quick=True)
        source = self.source(encoded, [7, QUICK_BLOCK - 3])
        for sizes in ([len(data)], [0, QUICK_BLOCK, 0, QUICK_BLOCK, 53],
                      [QUICK_BLOCK - 1, 2, QUICK_BLOCK + 52]):
            with self.subTest(sizes=sizes):
                buffers = [Guarded(n) for n in sizes]
                out = (Span * len(buffers))(*[Span(b.ptr, b.size) for b in buffers])
                args = (self.work.ptr, self.work.size, source, len(source), out, len(out),
                        len(data), packed.adler)
                policy = (C.c_uint * 2)()
                self.assertEqual(self.lib.unpackRetained(*args, policy), SUCCESS)
                self.assertEqual(policy[1], len(data))
                self.assertGreaterEqual(policy[0], 3)
                self.assertEqual(b''.join(b.data() for b in buffers), b'\xa7' * len(data))
                self.assertEqual(self.lib.unpackVerified(*args), SUCCESS)
                self.assertEqual(b''.join(b.data() for b in buffers), data)
                self.assertTrue(all(b.guards() for b in buffers))

    def test_quick_count_only_and_exact_capacity_preserve_allocation_guards(self):
        data = random.Random(7013).randbytes(QUICK_BLOCK * 2 + 31)
        source = self.source(data, [1, QUICK_BLOCK - 1, QUICK_BLOCK + 1])
        counted, _ = self.pack(source, quick=True)
        self.assertEqual(counted.compressed, len(data) + 8 + 3 * 8)
        for capacity in (0, 1, 7, 8, 15, 16, 37, QUICK_BLOCK, counted.compressed - 1,
                         counted.compressed):
            with self.subTest(capacity=capacity):
                packed, encoded = self.pack(source, [0, capacity // 2, 0,
                                                     capacity - capacity // 2], quick=True)
                self.assertEqual((packed.status, packed.compressed, packed.raw, packed.adler),
                    (SUCCESS if capacity == counted.compressed else FULL,
                     counted.compressed, len(data), zlib.adler32(data)))
                if packed.status == SUCCESS:
                    self.assertEqual(reference_quick_frame(encoded, len(data)), data)
        result = Result()
        invalid_source = (Span * 2)(Span(4096, 0xFFFFFFF0), Span(8192, 0x20))
        self.lib.packQuick(self.work.ptr, self.work.size, invalid_source, 2, None, 0, C.byref(result))
        self.assertEqual(result.status, INVALID)
        invalid_output = (Span * 2)(Span(4096, 0xFFFFFFF0), Span(0x100010000, 0x20))
        self.lib.packQuick(self.work.ptr, self.work.size, source, len(source),
                           invalid_output, 2, C.byref(result))
        self.assertEqual(result.status, INVALID)

    def test_direct_decode_keeps_protected_bytes_and_scattered_span_boundaries(self):
        data = bytes(QUICK_BLOCK * 3 + 53)
        measured, _ = self.pack(self.source(data), quick=True)
        packed, encoded = self.pack(self.source(data), [measured.compressed], quick=True)
        source = self.source(encoded, [7, 19, len(encoded)-1])
        for sizes in ([len(data)], [0, QUICK_BLOCK, 0, QUICK_BLOCK, QUICK_BLOCK+53],
                      [QUICK_BLOCK-1, 2, QUICK_BLOCK*2+52]):
            with self.subTest(sizes=sizes):
                buffers = [Guarded(n) for n in sizes]
                out = (Span * len(buffers))(*[Span(b.ptr, b.size) for b in buffers])
                kept = next(b for b in buffers if b.size >= QUICK_BLOCK)
                policy = DirectPolicy(first=kept.ptr+17, last=kept.ptr+19)
                self.assertEqual(self.lib.unpackDirect(self.work.ptr,self.work.size,source,len(source),
                    out,len(out),len(data),packed.adler,C.byref(policy)), SUCCESS)
                for b in buffers:
                    expected = bytearray(b.size)
                    if b is kept: expected[17:19] = b'\xa7\xa7'
                    self.assertEqual(b.data(), expected)
                    self.assertTrue(b.guards())
                self.assertGreater(policy.copies, 0)
                self.assertGreater(policy.allowed, 0)
                self.assertLess(policy.copied_bytes, len(data))
                self.assertGreaterEqual(policy.queries, policy.allowed)
                if len(sizes) == 1: self.assertGreater(policy.queries, policy.allowed)
        self.assertTrue(self.work.guards())

    def test_direct_permission_follows_frame_and_output_bounds_and_does_not_apply_to_plain(self):
        raw = random.Random(812).randbytes(QUICK_BLOCK)
        for quick in (False, True):
            measured, _ = self.pack(self.source(raw), quick=quick)
            packed, encoded = self.pack(self.source(raw), [measured.compressed], quick=quick)
            source = self.source(encoded)
            out = Guarded(len(raw)); spans = (Span * 1)(Span(out.ptr,out.size)); policy = DirectPolicy()
            self.assertEqual(self.lib.unpackDirect(self.work.ptr,self.work.size,source,len(source),
                spans,1,len(raw),packed.adler,C.byref(policy)), SUCCESS)
            self.assertEqual(policy.queries, 0)
            self.assertEqual(policy.copied_bytes, len(raw))
            self.assertEqual(out.data(), raw); self.assertTrue(out.guards())
        raw = bytes(QUICK_BLOCK)
        measured, _ = self.pack(self.source(raw), quick=True)
        packed, encoded = self.pack(self.source(raw), [measured.compressed], quick=True)
        for offset, value in ((4,1),(8,QUICK_BLOCK+1),(12,0),(12,0x7FFFFFFF)):
            damaged = bytearray(encoded); struct.pack_into('>I',damaged,offset,value)
            source = self.source(bytes(damaged)); out = Guarded(len(raw)); spans=(Span*1)(Span(out.ptr,out.size))
            policy = DirectPolicy()
            self.assertEqual(self.lib.unpackDirect(self.work.ptr,self.work.size,source,len(source),
                spans,1,len(raw),packed.adler,C.byref(policy)), COMMIT)
            self.assertEqual((policy.queries,policy.copies), (0,0))
            self.assertEqual(out.data(), b'\xa7'*len(raw));self.assertTrue(out.guards())
        for payload in (b'\x10A\x00\x00', b'\x10A\x02\x00', b'\xf0\xff', b'\x1fA\x01\x00\xff'):
            # A violated verified-stream promise may have written destination
            # bytes, but must remain bounded and report fatal commit failure.
            encoded = struct.pack('>IIII',0x4D534C34,QUICK_BLOCK,100,len(payload))+payload
            source=self.source(encoded);out=Guarded(100);spans=(Span*1)(Span(out.ptr,out.size))
            policy=DirectPolicy()
            self.assertEqual(self.lib.unpackDirect(self.work.ptr,self.work.size,source,len(source),
                spans,1,100,1,C.byref(policy)), COMMIT)
            self.assertEqual((policy.queries,policy.allowed,policy.copies),(1,1,0))
            self.assertTrue(out.guards());self.assertTrue(self.work.guards())
    def test_quick_header_and_payload_counters_refuse_uint32_wrap(self):
        for start, length, word, expected, final in (
                (0xFFFFFFFB, 0, 1, 1, 0xFFFFFFFF),
                (0xFFFFFFFC, 0, 1, 0, 0xFFFFFFFC),
                (0xFFFFFFFF, 0, 1, 0, 0xFFFFFFFF),
                (0xFFFFFFFA, 5, 0, 1, 0xFFFFFFFF),
                (0xFFFFFFFA, 6, 0, 0, 0xFFFFFFFA),
                (0xFFFFFFFF, 0, 0, 1, 0xFFFFFFFF),
                (3, -1, 0, 0, 3)):
            after = C.c_uint()
            self.assertEqual(self.lib.counterBoundary(start, length, word, C.byref(after)), expected)
            self.assertEqual(after.value, final)

    def test_quick_malformed_frames_and_final_adler_reject_before_any_copy(self):
        data = random.Random(6902).randbytes(QUICK_BLOCK) + b'A' * (QUICK_BLOCK + 17)
        source = self.source(data)
        measured, _ = self.pack(source, quick=True)
        _, encoded = self.pack(source, [measured.compressed], quick=True)
        second = 16 + QUICK_BLOCK
        cuts = [0, 1, 3, 7, 8, 9, 15, 16, second - 1, second, second + 1,
                second + 7, second + 8, len(encoded) // 2, len(encoded) - 1]
        bad = [encoded[:n] for n in cuts]
        bad += [encoded + b'\0', encoded + encoded]
        for offset, value in ((4, 0), (4, QUICK_BLOCK * 2), (8, 0), (8, QUICK_BLOCK + 1),
                              (12, 0), (12, QUICK_BLOCK), (12, 0xFFFFFFFF),
                              (second, 1), (second + 4, 0), (second + 4, 0x7FFFFFFF)):
            changed = bytearray(encoded)
            struct.pack_into('>I', changed, offset, value)
            bad.append(bytes(changed))
        for offset in (0, 16, 1000, second + 8, len(encoded) - 1):
            changed = bytearray(encoded)
            changed[offset] ^= 0x80
            bad.append(bytes(changed))
        for item, adler in [(item, zlib.adler32(data)) for item in bad] + [
                (encoded, zlib.adler32(data) ^ 1)]:
            with self.subTest(size=len(item), adler=adler, prefix=item[:16]):
                source = self.source(item, sorted({0, min(7, len(item)), len(item)}))
                guards = [Guarded(37), Guarded(len(data) - 37)]
                output = (Span * 2)(*[Span(g.ptr, g.size) for g in guards])
                policy = (C.c_uint * 2)()
                status = self.lib.unpackPolicy(self.work.ptr, self.work.size, source, len(source),
                    output, len(output), len(data), adler, policy)
                self.assertIn(status, (INVALID, CORRUPT))
                self.assertEqual(tuple(policy), (0, 0))
                for guard in guards:
                    self.assertEqual(guard.data(), bytes([0xA7]) * guard.size)
                    self.assertTrue(guard.guards())
        for expected in (len(data) - 1, len(data) + 1):
            status, decoded = self.decode(encoded, expected, zlib.adler32(data))
            self.assertEqual((status, decoded), (CORRUPT, bytes([0xA7]) * expected))

    def test_quick_verified_load_reports_commit_failure_for_late_corruption(self):
        data = random.Random(967).randbytes(QUICK_BLOCK + 35)
        measured, _ = self.pack(self.source(data), quick=True)
        _, encoded = self.pack(self.source(data), [measured.compressed], quick=True)
        for item, adler in ((encoded, zlib.adler32(data)),
                            (encoded, zlib.adler32(data) ^ 1),
                            (encoded[:-1], zlib.adler32(data)),
                            (encoded + b'x', zlib.adler32(data))):
            source = self.source(item, [1, 7, 9, 17])
            target = Guarded(len(data))
            output = (Span * 1)(Span(target.ptr, target.size))
            policy = (C.c_uint * 2)()
            status = self.lib.unpackVerifiedPolicy(self.work.ptr, self.work.size, source, len(source),
                output, 1, len(data), adler, policy)
            self.assertEqual(status, SUCCESS if item == encoded and adler == zlib.adler32(data) else COMMIT)
            self.assertGreater(policy[1], 0)
            self.assertTrue(target.guards())
            if status == SUCCESS:
                self.assertEqual(target.data(), data)

    def test_quick_corrupt_lz4_offsets_and_extensions_never_reach_destinations(self):
        payloads = (b'\x10A\x00\x00', b'\x10A\x02\x00', b'\x10A\x01',
                    b'\xf0\xff', b'\x1fA\x01\x00\xff', b'\x00\x01\x00')
        for packed in payloads:
            encoded = struct.pack('>IIII', 0x4D534C34, QUICK_BLOCK, 100, len(packed)) + packed
            status, decoded = self.decode(encoded, 100, zlib.adler32(b'A' * 100), cuts=[15, 16])
            self.assertEqual((status, decoded), (CORRUPT, bytes([0xA7]) * 100))


if __name__ == "__main__":
    unittest.main()
