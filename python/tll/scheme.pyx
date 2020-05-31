#!/usr/bin/env python
# vim: sts=4 sw=4 et

from .scheme cimport *
from .s2b cimport *
from libc.stdint cimport int8_t, int16_t, int32_t, int64_t
from libc.string cimport memcpy, memset
from libc.errno cimport EINVAL, EMSGSIZE
from cython cimport typeof
from cpython.version cimport PY_MAJOR_VERSION

from collections import OrderedDict
from decimal import Decimal
import enum
from .error import TLLError
from .buffer cimport *

class Type(enum.Enum):
    Int8 = TLL_SCHEME_FIELD_INT8
    Int16 = TLL_SCHEME_FIELD_INT16
    Int32 = TLL_SCHEME_FIELD_INT32
    Int64 = TLL_SCHEME_FIELD_INT64
    Double = TLL_SCHEME_FIELD_DOUBLE
    Decimal128 = TLL_SCHEME_FIELD_DECIMAL128
    Bytes = TLL_SCHEME_FIELD_BYTES
    Message = TLL_SCHEME_FIELD_MESSAGE
    Array = TLL_SCHEME_FIELD_ARRAY
    Pointer = TLL_SCHEME_FIELD_POINTER

class SubType(enum.Enum):
    NONE = TLL_SCHEME_SUB_NONE
    Enum = TLL_SCHEME_SUB_ENUM
    ByteString = TLL_SCHEME_SUB_BYTE_STRING
    FixedPoint = TLL_SCHEME_SUB_FIXED_POINT
    TimePoint = TLL_SCHEME_SUB_TIME_POINT
    Duration = TLL_SCHEME_SUB_DURATION

cdef class Options(dict):
    @staticmethod
    cdef Options wrap(tll_scheme_option_t * ptr):
        r = Options()
        while ptr != NULL:
            r[b2s(ptr.name)] = b2s(ptr.value)
            ptr = ptr.next
        return r

class Enum(OrderedDict):
    pass

cdef enum_wrap(tll_scheme_enum_t * ptr):
    r = Enum()
    r.options = Options.wrap(ptr.options)
    r.name = b2s(ptr.name)
    r.name_bytes = ptr.name
    #r.values = {}
    cdef tll_scheme_enum_value_t * e = ptr.values
    while e != NULL:
        r[b2s(e.name)] = e.value
        e = e.next
    return r

"""
    def pack_int16(self, dest, v):
        cdef char * ptr = dest
        (<int16_t *>ptr)[0] = v;

    def pack_int32(self, dest, v):
        cdef char * ptr = dest
        (<int32_t *>ptr)[0] = v;

    def pack_int64(self, dest, v):
        cdef char * ptr = dest
        (<int64_t *>ptr)[0] = v;
"""

def memoryview_check(o):
    if not PyMemoryView_Check(o):
        raise TLLError("Need memoryview to pack, got {}".format(type(o)))

ctypedef fused primitive_t:
    int8_t
    int16_t
    int32_t
    int64_t
    double

cdef int pack_fused(primitive_t v, object dest):
    if not PyMemoryView_Check(dest): return EINVAL
    cdef Py_buffer * buf = PyMemoryView_GET_BUFFER(dest)
    if buf.len < <ssize_t>(sizeof(v)): return EMSGSIZE
#        raise TLLError("Dest buffer too small: {} < {}".format(buf.len, sizeof(typeof(v))))
    memcpy(buf.buf, &v, sizeof(v))
    #(<typeof(v) *>buf.buf)[0] = v;

cdef primitive_t unpack_fused(primitive_t v, object src):
    if not PyMemoryView_Check(src): return 0 #EINVAL
    cdef Py_buffer * buf = PyMemoryView_GET_BUFFER(src)
    if buf.len < <ssize_t>(sizeof(v)): return 0 #EMSGSIZE
#        raise TLLError("Dest buffer too small: {} < {}".format(buf.len, sizeof(typeof(v))))
    memcpy(&v, buf.buf, sizeof(v))
    return v
    #(<typeof(v) *>buf.buf)[0] = v;

cdef tll_scheme_offset_ptr_t * read_optr(object src):
    if not PyMemoryView_Check(src): return NULL #EINVAL
    cdef Py_buffer * buf = PyMemoryView_GET_BUFFER(src)
    if buf.len < <ssize_t>(sizeof(tll_scheme_offset_ptr_t)): return NULL #EMSGSIZE
#        raise TLLError("Dest buffer too small: {} < {}".format(buf.len, sizeof(typeof(v))))
    return <tll_scheme_offset_ptr_t *>buf.buf

cdef unpack_vstring(object src):
    cdef tll_scheme_offset_ptr_t * ptr = read_optr(src)
    if ptr == NULL: return None
    if ptr.size == 0:
        return "" #src[sizeof(tll_scheme_offset_ptr_t):sizeof(tll_scheme_offset_ptr_t)]
    r = src[ptr.offset:ptr.offset + ptr.size]
    if PY_MAJOR_VERSION == 2:
        return r.tobytes()
    return str(r, encoding='utf-8')

cdef pack_int8(v, dest, tail, tail_offset): return pack_fused(<int8_t>v, dest)
cdef pack_int16(v, dest, tail, tail_offset): return pack_fused(<int16_t>v, dest)
cdef pack_int32(v, dest, tail, tail_offset): return pack_fused(<int32_t>v, dest)
cdef pack_int64(v, dest, tail, tail_offset): return pack_fused(<int64_t>v, dest)
cdef pack_double(v, dest, tail, tail_offset): return pack_fused(<double>v, dest)

cdef unpack_int8(src): return unpack_fused(<int8_t>0, src)
cdef unpack_int16(src): return unpack_fused(<int16_t>0, src)
cdef unpack_int32(src): return unpack_fused(<int32_t>0, src)
cdef unpack_int64(src): return unpack_fused(<int64_t>0, src)
cdef unpack_double(src): return unpack_fused(<double>0, src)


cdef pack_decimal128(v, dest, tail, tail_offset):
    raise NotImplemented()

cdef unpack_decimal128(src):
    raise NotImplemented()

"""
cdef pack_int8(v, dest):
    memoryview_check(dest)
    cdef Py_buffer * buf = PyMemoryView_GET_BUFFER(dest)
    if buf.len < sizeof(int8_t):
        raise TLLError("Dest buffer too small: {} < {}".format(buf.len, sizeof(int8_t)))
    (<int8_t *>buf.buf)[0] = v;

cdef pack_int16(v, dest):
    memoryview_check(dest)
    cdef Py_buffer * buf = PyMemoryView_GET_BUFFER(dest)
    if buf.len < sizeof(int16_t):
        raise TLLError("Dest buffer too small: {} < {}".format(buf.len, sizeof(int16_t)))
    (<int16_t *>buf.buf)[0] = v;

cdef pack_int32(v, dest):
    memoryview_check(dest)
    cdef Py_buffer * buf = PyMemoryView_GET_BUFFER(dest)
    if buf.len < sizeof(int32_t):
        raise TLLError("Dest buffer too small: {} < {}".format(buf.len, sizeof(int32_t)))
    (<int32_t *>buf.buf)[0] = v;

cdef pack_int64(v, dest):
    memoryview_check(dest)
    cdef Py_buffer * buf = PyMemoryView_GET_BUFFER(dest)
    if buf.len < sizeof(int64_t):
        raise TLLError("Dest buffer too small: {} < {}".format(buf.len, sizeof(int64_t)))
    (<int64_t *>buf.buf)[0] = v;

cdef pack_double(v, dest):
    memoryview_check(dest)
    cdef Py_buffer * buf = PyMemoryView_GET_BUFFER(dest)
    if buf.len < sizeof(double):
        raise TLLError("Dest buffer too small: {} < {}".format(buf.len, sizeof(double)))
    (<double *>buf.buf)[0] = v;
    """

cdef pack_bytes(v, dest, tail, tail_offset):
    if not PyMemoryView_Check(dest): return EINVAL
    cdef Py_buffer * buf = PyMemoryView_GET_BUFFER(dest)
    if not isinstance(v, memoryview):
        v = memoryview(s2b(v))
    cdef Py_buffer * inbuf = PyMemoryView_GET_BUFFER(v)
    if buf.len < inbuf.len: return EMSGSIZE
#        raise TLLError("Dest buffer too small: {} < {}".format(buf.len, inbuf.len))
    memcpy(buf.buf, inbuf.buf, inbuf.len)
    #if inbuf.len < buf.len:
    #    memset(buf.buf + inbuf.len, 0, buf.len - inbuf.len)

cdef unpack_bytes(src):
    if not PyMemoryView_Check(src): return None #EINVAL
    #cdef Py_buffer * buf = PyMemoryView_GET_BUFFER(src)
    return bytearray(src) #<char *>buf.buf, buf.len)

cdef unpack_str(src):
    if not PyMemoryView_Check(src): return None #EINVAL
    cdef Py_buffer * buf = PyMemoryView_GET_BUFFER(src)
    cdef int l = strnlen(<char *>buf.buf, buf.len)
    return b2s(bytearray(src[:l])) #<char *>buf.buf, buf.len)

cdef pack_vstring(v, dest, tail, tail_offset):
    cdef tll_scheme_offset_ptr_t * ptr = read_optr(dest)
    if ptr == NULL: return None
    b = s2b(v)
    ptr.offset = tail_offset + len(tail)
    ptr.size = len(b)
    ptr.entity = 1
    tail.extend(b)

def convert_int8(v): return <int8_t>v
def convert_int16(v): return <int16_t>v
def convert_int32(v): return <int32_t>v
def convert_int64(v): return <int64_t>v
def convert_double(v): return float(v)
def convert_decimal128(v): return Decimal(v)

class Field:
    Sub = SubType
    def init(self, name, type):
        #self.name, self.type, self.sub_type = name, type, sub_type
        if type == Field.Int8:
            self.pack_data, self.unpack_data = pack_int8, unpack_int8
            self.convert = convert_int8
            self.default = int
        elif type == Field.Int16:
            self.pack_data, self.unpack_data = pack_int16, unpack_int16
            self.convert = convert_int16
            self.default = int
        elif type == Field.Int32:
            self.pack_data, self.unpack_data = pack_int32, unpack_int32
            self.convert = convert_int32
            self.default = int
        elif type == Field.Int64:
            self.pack_data, self.unpack_data = pack_int64, unpack_int64
            self.convert = convert_int64
            self.default = int
        elif type == Field.Double:
            self.pack_data, self.unpack_data = pack_double, unpack_double
            self.convert = convert_double
            self.default = float
        elif type == Field.Decimal128:
            self.pack_data, self.unpack_data = pack_decimal128, unpack_decimal128
            self.convert = convert_decimal128
            self.default = Decimal
        elif type == Field.Bytes:
            self.pack_data, self.unpack_data = pack_bytes, unpack_bytes
            self.convert = lambda x: x
            self.default = bytes
            if self.sub_type == SubType.ByteString:
                self.unpack_data = unpack_str
                self.default = str
        elif type == Field.Message:
            self.pack, self.unpack = self.pack_msg, self.unpack_msg
            #self.pack_data, self.unpack_data = self.pack_data_msg, self.unpack_data_msg
            self.pack_data, self.unpack_data = self.type_msg.pack, self.type_msg.unpack
            self.convert = self.convert_msg
            self.default = self.type_msg.klass
        elif type == Field.Array:
            self.pack_data, self.unpack_data = self.pack_array, self.unpack_array
            self.convert = self.convert_array
            self.default = list
        elif type == Field.Pointer:
            if self.sub_type == SubType.ByteString:
                self.pack_data, self.unpack_data = pack_vstring, unpack_vstring
                self.convert = lambda x: x
                self.default = str
            else:
                self.pack_data, self.unpack_data = self.pack_olist, self.unpack_olist
                self.convert = self.convert_olist
                self.default = list

    def __repr__(self):
        return "<Field {0.name}, type: {0.type}, size: {0.size}, offset: {0.offset}>".format(self)

    def pack(self, v, dest, tail, tail_offset = 0):
        memoryview_check(dest)
        print("Pack {}".format(self))
        self.pack_data(v, dest[self.offset:self.offset + self.size])

    def unpack(self, src):
        memoryview_check(src)
        return self.unpack_data(src[self.offset:self.offset + self.size])

    def pack_array(self, v, dest, tail, tail_offset):
        self.count_ptr.pack_data(len(v), dest[:self.count_ptr.size], None, None)
        off = self.type_array.offset
        for e in v[:self.count]:
            self.type_array.pack_data(e, dest[off:off + self.type_array.size], tail, tail_offset - off)
            off += self.type_array.size

    def unpack_array(self, src):
        r = []
        cdef int i = 0
        off = self.type_array.offset
        cdef int size = self.count_ptr.unpack_data(src)
        while i < size:
            r.append(self.type_array.unpack_data(src[off:]))
            off += self.type_array.size
            i += 1
        return r

    def pack_msg(self, v, dest, tail, tail_offset):
        memoryview_check(dest)
        cdef int off = self.offset
        #cdef int end = self.offset + self.size
        return self.pack_data(v, dest[off:off + self.size], tail, tail_offset - off)

    def unpack_msg(self, src):
        memoryview_check(src)
        return self.unpack_data(src[self.offset:self.size])

    """
    def pack_data_msg(self, v, dest, tail, tail_offset):
        self.type_msg.pack(v, dest, tail, tail_offset)

    def unpack_data_msg(self, src):
        return self.unpack_data(src[self.offset:self.size])
        self.type_msg.pack(v, dest, tail, tail_offset)
    """

    def convert_msg(self, v):
        if isinstance(v, dict):
            return self.type_msg.object(**v)
        if isinstance(v, Data):
            if v.SCHEME != self.type_msg:
                raise TypeError("Can not convert message {} to {}".format(v.SCHEME.name, self.type_msg.name))
            return v
        raise TypeError("Can not convert {} to message".format(type(v)))

    def convert_array(self, l):
        if not isinstance(l, (tuple, list)):
            raise TypeError("Invalid type for list: {}".format(type(l)))
        if len(l) > self.count:
            raise ValueError("List too large: {} > {}".format(len(l), self.count))
        return [self.type_array.convert(x) for x in l]

    def convert_olist(self, l):
        if not isinstance(l, (tuple, list)):
            raise TypeError("Invalid type for list: {}".format(type(l)))
        return [self.type_ptr.convert(x) for x in l]

    def unpack_olist(self, src):
        cdef tll_scheme_offset_ptr_t * ptr = read_optr(src)
        if ptr == NULL: return None
        if ptr.size == 0:
            return []
        r = []
        off = ptr.offset
        cdef int i = 0
        while i < ptr.size:
            r.append(self.type_ptr.unpack_data(src[off:]))
            off += self.type_ptr.size
            i += 1
        return r

    def pack_olist(self, v, dest, tail, tail_offset):
        cdef tll_scheme_offset_ptr_t * ptr = read_optr(dest)
        if ptr == NULL: return None
        ptr.offset = tail_offset + len(tail)
        ptr.size = len(v)
        ptr.entity = 1
        b = bytearray(len(v) * self.type_ptr.size)
        view = memoryview(b)
        tnew = bytearray()
        off = 0
        f = self.type_ptr
        for i in v:
            f.pack_data(i, view[off:off+f.size], tnew, len(b) - off)
            off += f.size
        tail.extend(b)
        if tnew: tail.extend(tnew)

for t in Type:
    setattr(Field, t.name, t)

cdef object field_wrap(Scheme s, object m, tll_scheme_field_t * ptr):
    r = Field()
    r.name = b2s(ptr.name)
    r.name_bytes = ptr.name
    r.options = Options.wrap(ptr.options)
    r.type = Type(ptr.type)
    r.sub_type = SubType(ptr.sub_type)
    if r.type == r.Message:
        r.type_msg = s[ptr.type_msg.name]
    elif r.type == r.Array:
        r.count = ptr.count
        r.count_ptr = field_wrap(s, m, ptr.count_ptr)
        r.type_array = field_wrap(s, m, ptr.type_array)
    elif r.type == r.Pointer:
        r.type_ptr = field_wrap(s, m, ptr.type_ptr)
    elif r.sub_type == r.Sub.Enum:
        ename = b2s(ptr.type_enum.name)
        r.type_enum = m.enums.get(ename, s.enums.get(ename, None))
        if r.type_enum is None:
            raise TLLError("Failed to build field {}: Enum {} not found".format(r.name, ename))
    r.size = ptr.size
    r.offset = ptr.offset
    r.init(r.name, r.type) #b2s(ptr.name), Type(ptr.type))
    return r

class Data(object):
    SCHEME = None
    def __init__(self, *a, **kw):
        if a:
            for k,v in zip(a, self.SCHEME.items()):
                setattr(self, k, v)
        if kw:
            for k,v in kw.items():
                setattr(self, k, v)

    def pack(self):
        tail = bytearray()
        dest = bytearray(self.SCHEME.size)
        self.SCHEME.pack(self, memoryview(dest), tail, len(dest))
        return dest + tail

    def unpack(self, data):
        return self.SCHEME.unpack(memoryview(data), self)

    def __setattr__(self, k, v):
        f = self.SCHEME.get(k, None)
        if f is None:
            raise KeyError("No such field in {}: {}".format(self.SCHEME.name, k))
        object.__setattr__(self, k, f.convert(v))

    def __str__(self):
        l = []
        for f in self.SCHEME.fields:
            r = getattr(self, f.name, None)
            if r is None: continue
            if isinstance(r, (tuple, list)):
                l.append('{}: [{}]'.format(f.name, ', '.join([str(x) for x in r])))
            elif isinstance(r, str):
                l.append('{}: {}'.format(f.name, repr(r)))
            else:
                l.append('{}: {}'.format(f.name, str(r)))
        return "<{} {}>".format(self.SCHEME.name, ", ".join(l))

class Message(OrderedDict):
    def object(self, *a, **kw):
        return self.klass(*a, **kw)

    def pack(self, v, dest, tail, tail_offset):
        memoryview_check(dest)
        for f in self.fields:
            i = getattr(v, f.name, None)
            if i is None:
                continue
            off = f.offset
            f.pack_data(i, dest[off:off + f.size], tail, tail_offset - off)

    def unpack(self, src, v=None):
        memoryview_check(src)
        v = self.object() if v is None else v
        for f in self.fields:
            r = f.unpack_data(src[f.offset:])
            object.__setattr__(v, f.name, r)
        return v

    @property
    def fields(s): return s.values()

@staticmethod
cdef object message_wrap(Scheme s, tll_scheme_message_t * ptr):
    r = Message()
    r.name = b2s(ptr.name)
    r.name_bytes = ptr.name
    r.msgid = ptr.msgid
    r.size = ptr.size
    r.options = Options.wrap(ptr.options)
    r.enums = OrderedDict()

    class D(Data):
        SCHEME = r
    D.__name__ = r.name
    r.klass = D

    cdef tll_scheme_enum_t * e = ptr.enums
    while e != NULL:
        tmp = enum_wrap(e)
        r.enums[tmp.name] = tmp
        e = e.next

    cdef tll_scheme_field_t * f = ptr.fields
    while f != NULL:
        tmp = field_wrap(s, r, f)
        r[tmp.name] = tmp
        f = f.next
    return r

cdef class Scheme:
    def __init__(self, uri):
        self._ptr = NULL
        self.messages = []
        if uri is None:
            return
        u = s2b(uri)
        self._ptr = tll_scheme_load(u, len(u))
        if self._ptr == NULL:
            raise RuntimeError("Failed to load scheme")
        self.fill(self._ptr)

    def __dealloc__(self):
        tll_scheme_unref(self._ptr)
        self._ptr = NULL

    def copy(self):
        if not self._ptr:
            raise RuntimeError("Uninitialized pointer")
        return Scheme.wrap(tll_scheme_ref(self._ptr))

    @property
    def options(self): return self.options

    @property
    def enums(self): return self.enums

    @property
    def messages(self): return self.messages

    def dump(self, fmt=None):
        if self._ptr == NULL:
            raise ValueError("Unbound scheme, null pointer")
        bfmt = s2b(fmt) if fmt else None
        cdef const char * f = NULL
        if bfmt:
            f = bfmt
        cdef char * str = tll_scheme_dump(self._ptr, f)
        if str == NULL:
            raise TLLError("Failed to dump scheme")
        b = bytes(str[:])
        return b

    def find(self, k):
        if isinstance(k, int):
            for m in self.messages:
                if m.msgid == k: return m
        elif isinstance(k, bytes):
            for m in self.messages:
                if m.name_bytes == k: return m
        else:
            for m in self.messages:
                if m.name == k: return m
        raise KeyError("Message '{}' not found".format(k))

    def __getitem__(self, k): return self.find(k)

    @staticmethod
    cdef Scheme wrap(const tll_scheme_t * ptr, int ref = 0):
        s = Scheme(None)
        if ref:
            tll_scheme_ref(ptr)
        s._ptr = <tll_scheme_t *>ptr
        s.fill(ptr)
        return s

    cdef same(Scheme self, const tll_scheme_t *ptr):
        return self._ptr == ptr

    cdef fill(Scheme self, const tll_scheme_t *ptr):
        self.options = Options.wrap(ptr.options)
        self.enums = OrderedDict()
        self.messages = []

        cdef tll_scheme_enum_t * e = ptr.enums
        while e != NULL:
            tmp = enum_wrap(e)
            self.enums[tmp.name] = tmp
            e = e.next

        cdef tll_scheme_message_t * m = ptr.messages
        while m != NULL:
            self.messages.append(message_wrap(self, m))
            m = m.next

    def unpack(self, msg):
        if not msg.msgid:
            raise ValueError("No msgid in message")
        m = self.find(msg.msgid)
        return m.unpack(msg.data)