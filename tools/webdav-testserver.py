#!/usr/bin/env python3
"""
A throwaway WebDAV server for testing scripts/common/universal/webdav.c.

Standing up Nextcloud to find out whether the script's PROPFIND parse survives
a real multistatus body is a bad trade. This is the whole of WebDAV that
webdav.c speaks — OPTIONS, PROPFIND (Depth 0/1), MKCOL, PUT, GET, DELETE, HTTP
Basic — over Python's stdlib http.server, storing files in a plain directory.
No dependencies, no accounts, no TLS.

    tools/webdav-testserver.py                  # 0.0.0.0:8080, ckpt/ckpt
    tools/webdav-testserver.py --root /tmp/dav --port 9000 --user u --pass p

It prints the base URL to type into the script's Settings page. Run it on the
PC, put the console on the same Wi-Fi, and every request the script makes shows
up as a log line — method, path, status, bytes — so an upload, a listing and a
restore each verify themselves without a debugger on the console.

Deliberately not hardened: no TLS, one shared account, path traversal is the
only thing it defends against. Do not expose it to a network you do not own.
"""

import argparse
import base64
import datetime
import html
import os
import posixpath
import socket
import sys
import threading
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import quote, unquote

DAV_NS = 'xmlns:d="DAV:"'


def httpdate(ts):
    """RFC 1123, the format getlastmodified is defined in."""
    dt = datetime.datetime.fromtimestamp(ts, datetime.timezone.utc)
    return dt.strftime("%a, %d %b %Y %H:%M:%S GMT")


class Handler(BaseHTTPRequestHandler):
    # Keep-alive matters: the script makes a burst of small calls per title,
    # and curl on the console reconnects per request otherwise.
    protocol_version = "HTTP/1.1"
    server_version = "ckpt-davtest/1"
    # A console that walks off mid-connection must not wedge a thread.
    timeout = 30

    # ---- plumbing --------------------------------------------------------

    def handle_one_request(self):
        """As stdlib, but a client hanging up is an end of connection, not a crash.

        3DS httpc (and libnx's) drops the socket right after reading a response
        instead of reusing it, so the read for the next request on that
        connection lands on a RST. Left alone that reaches socketserver and
        prints a traceback per request, drowning the log this server exists to
        produce.
        """
        try:
            super().handle_one_request()
        except (ConnectionResetError, BrokenPipeError, TimeoutError, socket.timeout):
            self.close_connection = True

    def log_message(self, fmt, *args):  # noqa: A003 - stdlib hook name
        sys.stderr.write("%s  %s\n" % (self.log_date_time_string(), fmt % args))

    def log_call(self, status, extra=""):
        self.log_message(
            "%-8s %-60s -> %s %s", self.command, unquote(self.path), status, extra
        )

    def authorized(self):
        want = self.server.credentials
        if want is None:
            return True
        got = self.headers.get("Authorization", "")
        if not got.startswith("Basic "):
            return False
        try:
            pair = base64.b64decode(got[6:].strip()).decode("utf-8", "replace")
        except Exception:
            return False
        return pair == want

    def deny(self):
        # The unread request body would otherwise be parsed as the next
        # request line on this connection.
        try:
            self.read_body()
        except Exception:
            self.close_connection = True
        body = b"unauthorized\n"
        self.send_response(401)
        self.send_header("WWW-Authenticate", 'Basic realm="checkpoint"')
        self.send_header("Content-Type", "text/plain")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)
        self.log_call(401, "(bad or missing credentials)")

    def fs_path(self):
        """Map the request URL onto a path under --root, or None if it escapes."""
        raw = unquote(self.path.split("?", 1)[0])
        norm = posixpath.normpath("/" + raw.strip("/"))
        if norm == "/":
            return self.server.root
        parts = [p for p in norm.split("/") if p not in ("", ".", "..")]
        full = os.path.join(self.server.root, *parts)
        real = os.path.realpath(full)
        if real != self.server.root and not real.startswith(self.server.root + os.sep):
            return None
        return full

    def url_path(self):
        """The request path, normalised, without a trailing slash."""
        raw = unquote(self.path.split("?", 1)[0])
        p = posixpath.normpath("/" + raw.strip("/"))
        return "" if p == "/" else p

    def read_body(self):
        """The request body, Content-Length or chunked."""
        if self.headers.get("Transfer-Encoding", "").lower() == "chunked":
            out = bytearray()
            while True:
                line = self.rfile.readline().strip()
                size = int(line.split(b";", 1)[0] or b"0", 16)
                if size == 0:
                    self.rfile.readline()
                    break
                out += self.rfile.read(size)
                self.rfile.readline()
            return bytes(out)
        n = int(self.headers.get("Content-Length") or 0)
        return self.rfile.read(n) if n else b""

    def reply(self, status, body=b"", ctype=None, extra_log=""):
        self.send_response(status)
        if ctype:
            self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(body)))
        self.send_header("DAV", "1, 2")
        self.end_headers()
        if body and self.command != "HEAD":
            self.wfile.write(body)
        self.log_call(status, extra_log)

    # ---- methods ---------------------------------------------------------

    def do_OPTIONS(self):
        if not self.authorized():
            return self.deny()
        self.send_response(200)
        self.send_header("DAV", "1, 2")
        self.send_header(
            "Allow", "OPTIONS, GET, HEAD, PUT, DELETE, MKCOL, PROPFIND, MOVE, COPY"
        )
        self.send_header("Content-Length", "0")
        self.end_headers()
        self.log_call(200)

    def do_PROPFIND(self):
        if not self.authorized():
            return self.deny()
        self.read_body()  # the requested prop set is ignored; we always send all
        path = self.fs_path()
        if path is None or not os.path.exists(path):
            return self.reply(404, b"not found\n", "text/plain")

        depth = self.headers.get("Depth", "infinity").strip().lower()
        base = self.url_path()
        entries = [(base, path)]
        if depth != "0" and os.path.isdir(path):
            for name in sorted(os.listdir(path)):
                entries.append((base + "/" + name, os.path.join(path, name)))

        chunks = ['<?xml version="1.0" encoding="utf-8"?>', "<d:multistatus %s>" % DAV_NS]
        for href, fs in entries:
            try:
                st = os.stat(fs)
            except OSError:
                continue
            isdir = os.path.isdir(fs)
            # A collection's href ends in '/' — the detail that broke the
            # script's listing once, so the test server has to get it right.
            enc = quote(href, safe="/") + ("/" if isdir else "")
            if enc == "":
                enc = "/"
            rtype = "<d:collection/>" if isdir else ""
            length = "" if isdir else "<d:getcontentlength>%d</d:getcontentlength>" % st.st_size
            chunks.append(
                "<d:response><d:href>%s</d:href><d:propstat><d:prop>"
                "<d:resourcetype>%s</d:resourcetype>%s"
                "<d:getlastmodified>%s</d:getlastmodified>"
                "</d:prop><d:status>HTTP/1.1 200 OK</d:status></d:propstat></d:response>"
                % (html.escape(enc), rtype, length, httpdate(st.st_mtime))
            )
        chunks.append("</d:multistatus>")
        body = "\n".join(chunks).encode("utf-8")
        self.reply(
            207,
            body,
            'application/xml; charset="utf-8"',
            "(Depth: %s, %d entr%s)"
            % (depth, len(entries), "y" if len(entries) == 1 else "ies"),
        )

    def do_MKCOL(self):
        if not self.authorized():
            return self.deny()
        path = self.fs_path()
        if path is None:
            return self.reply(403, b"forbidden\n", "text/plain")
        if os.path.exists(path):
            # What a real server answers for an existing collection, and what
            # the script reads as "it is already there".
            return self.reply(405, b"already exists\n", "text/plain")
        if not os.path.isdir(os.path.dirname(path)):
            return self.reply(409, b"missing parent\n", "text/plain")
        os.mkdir(path)
        self.reply(201)

    def do_PUT(self):
        if not self.authorized():
            return self.deny()
        path = self.fs_path()
        if path is None:
            return self.reply(403, b"forbidden\n", "text/plain")
        if not os.path.isdir(os.path.dirname(path)):
            return self.reply(409, b"missing parent\n", "text/plain")
        existed = os.path.exists(path)
        data = self.read_body()
        with open(path, "wb") as f:
            f.write(data)
        self.reply(204 if existed else 201, extra_log="(%d bytes)" % len(data))

    def do_GET(self):
        if not self.authorized():
            return self.deny()
        path = self.fs_path()
        if path is None or not os.path.exists(path):
            return self.reply(404, b"not found\n", "text/plain")
        if os.path.isdir(path):
            listing = "\n".join(sorted(os.listdir(path))) + "\n"
            return self.reply(200, listing.encode("utf-8"), "text/plain")
        with open(path, "rb") as f:
            data = f.read()
        self.reply(200, data, "application/octet-stream", "(%d bytes)" % len(data))

    def do_HEAD(self):
        self.do_GET()

    def do_DELETE(self):
        if not self.authorized():
            return self.deny()
        path = self.fs_path()
        if path is None or not os.path.exists(path):
            return self.reply(404, b"not found\n", "text/plain")
        if os.path.isdir(path):
            import shutil

            shutil.rmtree(path)
        else:
            os.remove(path)
        self.reply(204)


class DavServer(ThreadingHTTPServer):
    def handle_error(self, request, client_address):
        """Swallow the same hang-up races when they escape the handler.

        Flushing wfile happens in socketserver's finish(), outside
        handle_one_request, so a console that closed early still lands here.
        Anything else is a real bug and keeps its traceback.
        """
        exc = sys.exc_info()[1]
        if isinstance(exc, (ConnectionResetError, BrokenPipeError, TimeoutError, socket.timeout)):
            return
        super().handle_error(request, client_address)


def lan_ip():
    """The address the console has to dial, not 127.0.0.1."""
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        s.connect(("8.8.8.8", 80))  # no packet is sent; this just picks a route
        return s.getsockname()[0]
    except OSError:
        return "127.0.0.1"
    finally:
        s.close()


def main():
    ap = argparse.ArgumentParser(description=__doc__.strip().splitlines()[0])
    ap.add_argument("--root", default="./davroot", help="directory to serve (created)")
    ap.add_argument("--port", type=int, default=8080)
    ap.add_argument("--bind", default="0.0.0.0")
    ap.add_argument("--user", default="ckpt")
    ap.add_argument("--password", "--pass", dest="password", default="ckpt")
    ap.add_argument(
        "--anonymous", action="store_true", help="accept any request, no Basic auth"
    )
    args = ap.parse_args()

    root = os.path.realpath(args.root)
    os.makedirs(root, exist_ok=True)

    httpd = DavServer((args.bind, args.port), Handler)
    httpd.root = root
    httpd.credentials = None if args.anonymous else "%s:%s" % (args.user, args.password)
    httpd.daemon_threads = True

    ip = lan_ip()
    print("serving %s" % root)
    print("base url : http://%s:%d" % (ip, args.port))
    if httpd.credentials:
        print("username : %s" % args.user)
        print("password : %s" % args.password)
    else:
        print("auth     : none (--anonymous)")
    print("uploads land in %s/Checkpoint/<platform>/<title>/" % root)
    print("ctrl-c to stop\n", flush=True)

    try:
        httpd.serve_forever()
    except KeyboardInterrupt:
        print("\nstopped")


if __name__ == "__main__":
    main()
