#!/usr/bin/env python3
# vim: sts=4 sw=4 et

from tll import asynctll
import tll.channel as C
from tll.error import TLLError
from tll.test_util import ports

import common

import http.server
import os
import pytest
import socket
import socketserver

class EchoHandler(http.server.BaseHTTPRequestHandler):
    def do_GET(self):
        self.send_response(200)
        self.send_header("Content-type", "text/plain")
        self.end_headers()

        self.wfile.write(f'GET {self.path}'.encode('utf-8'))

class HTTPServer(socketserver.TCPServer):
    timeout = 0.1
    address_family = socket.AF_INET6
    allow_reuse_address = True

    def __init__(self, *a, **kw):
        super().__init__(*a, **kw)

@pytest.mark.skipif(not C.Context().has_impl('curl'), reason="curl:// channels not supported")
class Test:
    def setup(self):
        self.ctx = C.Context()
        self.loop = asynctll.Loop(context=self.ctx)

    def teardown(self):
        self.loop.stop = 1
        self.loop = None

        self.ctx = None

    async def async_test_autoclose(self):
        with HTTPServer(('::1', ports.TCP6), EchoHandler) as httpd:
            c = self.loop.Channel('curl+http://[::1]:{}/some/path'.format(ports.TCP6), autoclose='yes', dump='text', name='http')
            c.open()

            await self.loop.sleep(0.01)

            httpd.handle_request()

            m = await c.recv()
            assert m.data.tobytes() == b'GET /some/path'

            await self.loop.sleep(0.001)
            assert c.state == c.State.Closed

    def test_autoclose(self):
        self.loop.run(self.async_test_autoclose())

    async def async_test_autoclose_many(self):
        with HTTPServer(('::1', ports.TCP6), EchoHandler) as httpd:
            multi = self.loop.Channel('curl://', name='multi')
            multi.open()

            c0 = self.loop.Channel('curl+http://[::1]:{}/c0'.format(ports.TCP6), autoclose='yes', dump='text', name='c0', master=multi)
            c0.open()

            c1 = self.loop.Channel('curl+http://[::1]:{}/c1'.format(ports.TCP6), autoclose='yes', dump='text', name='c1', master=multi)
            c1.open()

            await self.loop.sleep(0.01)

            httpd.handle_request()

            m = await c0.recv(0.11)
            assert m.data.tobytes() == b'GET /c0'

            httpd.handle_request()

            m = await c1.recv(0.12)
            assert m.data.tobytes() == b'GET /c1'

            await self.loop.sleep(0.001)
            assert c0.state == c0.State.Closed
            assert c1.state == c1.State.Closed

    def test_autoclose_many(self):
        self.loop.run(self.async_test_autoclose_many())
