"""The pc-containerd binary on its socket: JSON lines, fds both ways, one process per connection.

TrialTest runs a real container as a normal user (--rootless: one id mapped, no root needed) when
PCC_TRIAL_ROOTFS names a rootfs.tar.xz of images.linuxcontainers.org and user namespaces are allowed:

    PCC_TRIAL_ROOTFS=/path/to/rootfs.tar.xz make test
"""
import io
import json
import lzma
import os
import socket
import subprocess
import tarfile
import tempfile
import threading
import time
import unittest
from pathlib import Path

HERE = Path(__file__).resolve().parent
EXE = HERE.parent / "build/pc-containerd"
TRIAL_ROOTFS = os.environ.get("PCC_TRIAL_ROOTFS")


def build_rootfs_tar():
    out = io.BytesIO()
    with tarfile.open(fileobj=out, mode="w", format=tarfile.GNU_FORMAT) as tar:
        def add(name, data, mode=0o644):
            info = tarfile.TarInfo(name)
            info.size, info.mode = len(data), mode
            tar.addfile(info, io.BytesIO(data))
        root = tarfile.TarInfo("./")
        root.type, root.mode = tarfile.DIRTYPE, 0o755
        tar.addfile(root)
        add("./etc/passwd", b"root:x:0:0:root:/root:/bin/sh\n")
        add("./usr/share/applications/term.desktop",
            b"[Desktop Entry]\nType=Application\nName=Term\nExec=term -e %u\nIcon=term\nTerminal=true\n")
        add("./usr/share/icons/hicolor/scalable/apps/term.svg", b"<svg/>")
    return out.getvalue()


class Daemon:
    def __init__(self, rootless=True):
        self.tmp = tempfile.TemporaryDirectory()
        root = Path(self.tmp.name)
        for d in ("linux", "media/0", "wayland", "cgroup"):
            (root / d).mkdir(parents=True)
        self.root = root
        self.path = str(root / "sock")
        args = [str(EXE), "--socket", self.path, "--root", str(root)] + (["--rootless"] if rootless else [])
        self.proc = subprocess.Popen(args)
        for _ in range(100):
            if os.path.exists(self.path):
                break
            time.sleep(0.02)

    def close(self):
        self.proc.kill()
        self.proc.wait()
        subprocess.run(["chmod", "-R", "u+rwX", self.tmp.name], check=False)
        self.tmp.cleanup()


class Client:
    def __init__(self, path):
        self.sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        self.sock.connect(path)
        self.buf = b""
        self.fds = []

    def close(self):
        self.sock.close()

    def send(self, request, fd=None):
        line = (json.dumps(request) + "\n").encode()
        if fd is None:
            self.sock.sendall(line)
        else:
            socket.send_fds(self.sock, [line], [fd])

    def read_line(self):
        while b"\n" not in self.buf:
            data, fds, _, _ = socket.recv_fds(self.sock, 65536, 4)
            if not data:
                raise EOFError
            self.buf += data
            self.fds += fds
        line, self.buf = self.buf.split(b"\n", 1)
        return json.loads(line)

    def ask(self, request, fd=None):
        """The reply and the events before it; fds that came with the reply are in self.fds."""
        self.send(request, fd)
        events = []
        while True:
            reply = self.read_line()
            if "event" in reply:
                events.append(reply)
                continue
            return reply, events

    def take_fd(self):
        return self.fds.pop(0)


class InitSocketTest(unittest.TestCase):
    # On Android init creates /dev/socket/pc-containerd and passes its fd in an environment variable
    # whose name has every non-alphanumeric character as "_": ANDROID_SOCKET_pc_containerd.
    def test_serves_on_the_socket_init_created(self):
        tmp = tempfile.TemporaryDirectory()
        self.addCleanup(tmp.cleanup)
        root = Path(tmp.name)
        for d in ("linux", "media/0", "wayland", "cgroup"):
            (root / d).mkdir(parents=True)
        path = str(root / "init-sock")
        bound = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        bound.bind(path)
        env = dict(os.environ, ANDROID_SOCKET_pc_containerd=str(bound.fileno()))
        proc = subprocess.Popen([str(EXE), "--root", str(root), "--rootless"], env=env,
                                pass_fds=[bound.fileno()])
        self.addCleanup(proc.wait)
        self.addCleanup(proc.kill)
        bound.close()
        reply = None
        for _ in range(100):
            if proc.poll() is not None:
                break
            try:
                client = Client(path)
            except ConnectionRefusedError:
                time.sleep(0.02)
                continue
            reply, _ = client.ask({"id": 1, "cmd": "list"})
            client.close()
            break
        self.assertIsNone(proc.poll(), "pc-containerd exited")
        self.assertTrue(reply and reply.get("ok"), reply)


class SocketTest(unittest.TestCase):
    def setUp(self):
        self.daemon = Daemon()
        self.client = Client(self.daemon.path)

    def tearDown(self):
        self.client.close()
        self.daemon.close()

    def create(self, name="box"):
        r, w = os.pipe()
        data = build_rootfs_tar()
        writer = threading.Thread(target=lambda: (os.write(w, data), os.close(w)))
        writer.start()
        reply, events = self.client.ask({"id": 2, "cmd": "create", "name": name, "distro": "alpine",
                                         "release": "3.24", "user": "mateus"}, r)
        os.close(r)
        writer.join()
        return reply, events

    def test_answers_in_order_on_one_connection(self):
        self.client.send({"id": 1, "cmd": "version"})
        self.client.send({"id": 2, "cmd": "list"})
        self.assertEqual(self.client.read_line(), {"id": 1, "ok": True, "protocol": 1})
        self.assertEqual(self.client.read_line(), {"id": 2, "ok": True, "containers": []})

    def test_creates_from_a_pipe_and_lists_apps(self):
        reply, events = self.create()
        self.assertTrue(reply["ok"], reply)
        self.assertEqual(reply["container"]["state"], "stopped")
        self.assertEqual(events[0]["event"], "progress")
        listed, _ = self.client.ask({"id": 3, "cmd": "list"})
        self.assertEqual([c["name"] for c in listed["containers"]], ["box"])
        apps, _ = self.client.ask({"id": 4, "cmd": "apps", "name": "box"})
        self.assertEqual(apps["apps"][0]["argv"], ["term", "-e"])
        self.assertEqual(apps["apps"][0]["icon_path"], "/usr/share/icons/hicolor/scalable/apps/term.svg")
        self.assertTrue(apps["apps"][0]["terminal"])

    def test_hands_an_icon_back_as_an_fd(self):
        self.create()
        reply, _ = self.client.ask({"id": 5, "cmd": "icon", "name": "box", "app": "term"})
        self.assertEqual(reply, {"id": 5, "ok": True, "fd": True, "format": "svg", "size": 6})
        fd = self.client.take_fd()
        self.assertEqual(os.read(fd, 100), b"<svg/>")
        os.close(fd)

    def test_an_overlong_line_is_refused(self):
        self.client.sock.sendall(b"x" * 70000 + b"\n")
        self.assertEqual(self.client.read_line()["error"], "bad_request")
        self.assertEqual(self.client.ask({"id": 9, "cmd": "version"})[0]["protocol"], 1)

    def test_connections_are_served_side_by_side(self):
        other = Client(self.daemon.path)
        r, w = os.pipe()  # a create that waits for its data
        self.client.send({"id": 1, "cmd": "create", "name": "slow", "distro": "a", "release": "b"}, r)
        os.close(r)
        self.assertEqual(other.ask({"id": 2, "cmd": "version"})[0]["protocol"], 1)
        os.write(w, build_rootfs_tar())
        os.close(w)
        self.assertTrue(self.client.ask({"id": 3, "cmd": "list"})[0]["ok"])
        other.close()


def has_user_namespaces():
    return subprocess.run(["unshare", "-Ur", "true"], capture_output=True).returncode == 0


@unittest.skipUnless(TRIAL_ROOTFS and has_user_namespaces(), "set PCC_TRIAL_ROOTFS to a rootfs.tar.xz")
class TrialTest(unittest.TestCase):
    """A real container, rootless: namespaces, mounts, pivot_root, the pty and the init."""

    def setUp(self):
        self.daemon = Daemon(rootless=True)
        self.client = Client(self.daemon.path)
        r, w = os.pipe()

        def feed():
            with open(TRIAL_ROOTFS, "rb") as f, os.fdopen(w, "wb") as out:
                out.write(lzma.decompress(f.read()))

        writer = threading.Thread(target=feed)
        writer.start()
        reply, _ = self.client.ask({"id": 1, "cmd": "create", "name": "trial", "distro": "alpine",
                                    "release": "x", "user": "mateus"}, r)
        os.close(r)
        writer.join()
        self.assertTrue(reply["ok"], reply)

    def tearDown(self):
        self.client.ask({"id": 99, "cmd": "stop", "name": "trial"})
        self.client.close()
        self.daemon.close()

    def read_pty(self, fd, until, timeout=10):
        out, end = b"", time.time() + timeout
        os.set_blocking(fd, False)
        while until not in out and time.time() < end:
            try:
                out += os.read(fd, 4096)
            except BlockingIOError:
                time.sleep(0.05)
            except OSError:
                break
        return out.decode(errors="replace")

    def test_a_shell_on_a_pty_inside_the_container(self):
        reply, _ = self.client.ask({"id": 2, "cmd": "start", "name": "trial", "dns": ["1.1.1.1"]})
        self.assertEqual(reply, {"id": 2, "ok": True, "state": "running"})
        reply, _ = self.client.ask({"id": 3, "cmd": "exec", "name": "trial", "user": "root", "pty": True,
                                    "argv": ["/bin/sh", "-c", "cat /etc/os-release; echo pid=$$; hostname; "
                                             "cat /etc/resolv.conf; tty; ls /home; echo DONE"]})
        self.assertTrue(reply["ok"], reply)
        self.assertTrue(reply["fd"])
        out = self.read_pty(self.client.take_fd(), b"DONE")
        self.assertIn("Alpine", out)
        self.assertIn("pid=", out)
        self.assertIn("trial", out)
        self.assertIn("nameserver 1.1.1.1", out)
        self.assertIn("/dev/pts/0", out)
        self.assertIn("mateus", out)

    def test_exec_without_pty_and_stats(self):
        reply, _ = self.client.ask({"id": 4, "cmd": "exec", "name": "trial", "user": "root",
                                    "argv": ["sh", "-c", "echo $WAYLAND_DISPLAY > /tmp/env; sleep 1"]})
        self.assertTrue(reply["ok"], reply)
        self.assertNotIn("fd", reply)
        time.sleep(0.5)
        written = self.daemon.root / "linux/0/trial/rootfs/tmp/env"
        self.assertEqual(written.read_text().strip(), "/run/pc-wayland/wayland-0")
        stats, _ = self.client.ask({"id": 5, "cmd": "stats", "name": "trial"})
        self.assertEqual(stats["state"], "running")
        self.assertGreater(stats["disk_bytes"], 1000000)
        failed, _ = self.client.ask({"id": 6, "cmd": "exec", "name": "trial", "argv": ["no-such-command"], "user": "root"})
        self.assertEqual(failed["error"], "exec_failed")
        stopped, _ = self.client.ask({"id": 7, "cmd": "stop", "name": "trial"})
        self.assertEqual(stopped["state"], "stopped")


if __name__ == "__main__":
    unittest.main()
