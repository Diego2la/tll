#!/usr/bin/env python3
# vim: sts=4 sw=4 et

import tll.channel as C
from tll.channel.base import Base
from tll.error import TLLError
from tll.test_util import Accum

from tll import asynctll

import common

import pytest
import time

def test_asynctll():
    loop = asynctll.Loop()
    async def main():
        ctx = C.Context()
        s = ctx.Channel("mem://;size=1kb;name=server;dump=yes")
        c = ctx.Channel("mem://;master=server;name=client;dump=yes")

        loop.channel_add(s)
        loop.channel_add(c)

        s.open()
        c.open()

        s.post(b'xxx', seq=100)

        m = await loop.recv(c)
        loop.log.info("Got message {}", m)
        assert m.seq == 100
        assert m.data == b'xxx'

        s.post(b'zzz', seq=200)

        m = await loop.recv(c)
        assert m.seq == 200
        assert m.data == b'zzz'

    loop.run(main())

def test_channel():
    loop = asynctll.Loop(context=C.Context())
    async def main(loop):
        s = loop.Channel("mem://;size=1kb;name=server;dump=yes")
        c = loop.Channel("mem://;master=server;name=client;dump=yes")

        s.open()
        c.open()

        s.post(b'xxx', seq=100)

        m = await c.recv()
        loop.log.info("Got message {}", m)
        assert m.seq == 100
        assert m.data == b'xxx'

        s.post(b'zzz', seq=200)

        m = await loop.recv(c) # Mixed syntax
        assert m.seq == 200
        assert m.data == b'zzz'

    loop.run(main(loop))

def test_nofd():
    loop = asynctll.Loop(context=C.Context())
    async def main(loop):
        s = loop.Channel("mem://;size=1kb;name=server;dump=yes;fd=no")
        c = loop.Channel("mem://;master=server;name=client;dump=yes")

        s.open()
        c.open()

        s.post(b'xxx', seq=100)

        m = await c.recv()
        loop.log.info("Got message {}", m)
        assert m.seq == 100
        assert m.data == b'xxx'

        s.post(b'zzz', seq=200)

        m = await loop.recv(c) # Mixed syntax
        assert m.seq == 200
        assert m.data == b'zzz'

    loop.run(main(loop))

def test_sleep():
    loop = asynctll.Loop(context=C.Context())
    async def main(loop):
        print("Sleep start")
        await loop.sleep(0.001)
        print("Sleep next")
        await loop.sleep(0.001)
        print("Sleep next")
        await loop.sleep(0.001)
        print("Sleep done")

    loop.run(main(loop))

def test_direct_recv():
    loop = asynctll.Loop(context=C.Context())
    async def main(loop):
        s = loop.Channel('direct://;name=server;dump=frame')
        c = loop.Channel('direct://;name=client;dump=frame', master = s)

        s.open()
        c.open()

        c.post(b'xxx', seq=0)

        start = time.time()
        m = await s.recv(0.1)
        dt = time.time() - start
        assert dt < 0.001

    loop.run(main(loop))
