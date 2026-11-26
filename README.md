# PcTerminal

Linux containers for LineageOS for PCs, in the spirit of distrobox: install a distribution from
[images.linuxcontainers.org](https://images.linuxcontainers.org), open a shell in it, run its programs on the
Android desktop. Two parts live here:

- **`containerd/`**: `pc-containerd`, a root service in C. It extracts root file systems, runs each container
  in its own namespaces and cgroup, and opens commands in them, with a pseudo terminal when asked. Clients talk
  to it with the JSON lines of [`containerd/PROTOCOL.md`](containerd/PROTOCOL.md).
- **The app** (`core/`, `src/`, `res/`): "Terminal", a Compose app with its own VT100/xterm-256color emulator,
  tabs, and pages to install, manage and watch containers.

This is phase 1. The Wayland compositor that shows the containers' GUI programs is written elsewhere against
the same protocol (`list`, `exec`, `apps`, `icon`).

## Usage

- Open **Terminal**. With one container it opens straight into its shell; with several it asks which one; with
  none it opens the install page.
- **Install a distribution**: pick a distribution and a release (amd64, `default` variant of the image server),
  name the container and install. The app downloads `rootfs.tar.xz`, checks it against the release's
  `SHA256SUMS`, and pc-containerd extracts it. The shell opens when it is done.
- **Manage containers**: open a terminal (as the person's account or as root), start, stop, rename (stopped
  only) and delete. While the page is open it shows each running container's memory, CPU, processes and disk.
- Keys: Ctrl+Shift+T new tab, Ctrl+Shift+W close tab, Ctrl+Tab / Ctrl+PageDown and Ctrl+PageUp switch tabs,
  Ctrl+Shift+V paste, Shift+PageUp/PageDown or the mouse wheel scroll back. Dead keys (ABNT2 accents) compose.

Inside a container the person is `<user>` (uid 1000, from the Android user's name, `user` when it has none),
with passwordless `sudo` when the distribution has sudo, and a root shell one click away otherwise. Their
Android storage is their home folder.

## Design

### The daemon

`pc-containerd` is started by init from boot (`containerd/pc-containerd.rc`) and serves
`/dev/socket/pc-containerd`. Every connection is served by a forked process, so a long `create` never blocks
the compositor. The peer is checked with `SO_PEERCRED`: root and the system app (`android.uid.system`) of any
user; **the Android user a request acts for is always `peer uid / 100000`**, never a field of the request.

State lives in the file system, not in the daemon:

- `/data/linux/<userId>/<name>/rootfs` and `config.json` (distro, release, created, user, uid map).
- `<cgroup2>/pc-containers/u<userId>-<name>`: the container's cgroup. A container runs when its group has a
  process whose pid in its own pid namespace is 1 (`NSpid` in `/proc/<pid>/status`); that is its init.

### A running container

`start` (and `exec` on a stopped container) forks a child that joins the container's cgroup and calls
`unshare(CLONE_NEWUSER | NEWNS | NEWPID | NEWUTS | NEWIPC | NEWCGROUP)`. The daemon writes its uid and gid
maps, then the child forks the container's pid 1, which:

1. makes its mounts private and bind-mounts the rootfs on itself,
2. carries out the mount plan (`containerd/src/mounts.c`) below the rootfs, every target resolved inside the
   root (`openat2(RESOLVE_IN_ROOT)`) and mounted through `/proc/self/fd`,
3. `pivot_root`s into it, sets the host name to the container's name,
4. writes the person's account, sudo rule, `/etc/hosts`, `/etc/hostname`, `/etc/resolv.conf` (from the
   network's name servers the app sends), `/run/user` (0755) and `/run/user/<uid>` (0700, owned by its user),
   `/tmp/.X11-unix` (1777, without the last run's `/tmp/.X0-lock` and `X0` socket),
5. becomes a small init: it reaps orphans and, on SIGTERM, sends SIGTERM to everything, waits 5 s and exits
   (the kernel then ends the pid namespace).

`stop` sends SIGTERM to the init, waits, then uses `cgroup.kill`; `delete` stops and removes the folder without
following links or entering other mounts.

`exec` opens `/proc/<init>/root/dev/pts/ptmx` (the container's own devpts, so `tty` inside says `/dev/pts/N`),
gets the other end with `TIOCGPTPEER`, gives it to the command's user and the container's `tty` group, then
forks a child that joins the cgroup and `setns`es into the init's user, mount, pid, UTS, IPC and cgroup
namespaces. That child forks the command (which lands in the container's pid namespace and is adopted by its
init when the child exits), sets its groups from the container's `/etc/group`, its ids, its folder, and
`execve`s it with the `PATH` of its environment. The pty master goes back to the client with `SCM_RIGHTS`; the
daemon keeps nothing. `resize` takes the master back (also by `SCM_RIGHTS`) and sets `TIOCSWINSZ`.

### Session

There is no systemd in a container, so the daemon starts the person's session itself
(`containerd/src/session.c`): at every `start` and `exec` it looks at what the container's group runs (the base
name of each process's `argv[0]`) and starts, as the container's user with the default `exec` environment and
the same `setns` path as `exec`, each piece that is not running and whose programs are in the rootfs
(`/usr/local/bin`, `/usr/bin`, `/bin`, `/usr/sbin`, `/sbin`) or, for the session bus and X11, in the X11 bundle
(below). A distro's own programs always come first: its Xwayland keeps the GPU. A missing program or a failed start is logged and
skipped; it never fails the request. The pieces land in the container's pid namespace, so its init adopts and
reaps them and they end with the container. A lock on the rootfs keeps two requests from starting a piece twice.
Host trials (`--rootless`) map only container root, so they start no session.

| Piece | Started as | Needs (Fedora / Debian, Ubuntu / Arch) |
| --- | --- | --- |
| session bus | `dbus-daemon --session --address=unix:path=/run/user/1000/bus --nofork` | `dbus-daemon` / `dbus-daemon` / `dbus` |
| session bus, else | `/run/pc-x11/bin/dbus-daemon --config-file=/run/pc-x11/share/dbus-1/session.conf --address=unix:path=/run/user/1000/bus --nofork` | nothing: the X11 bundle |
| sound | `pipewire`, which starts `wireplumber` and `pipewire-pulse` | `pipewire`, `wireplumber`, `pipewire-pulseaudio` / `pipewire`, `wireplumber`, `pipewire-pulse` / the same as Debian |
| X11 | `xwayland-satellite :0` (when the distro has `Xwayland` too: GPU through glamor) | `xwayland-satellite`, `xorg-x11-server-Xwayland` / `xwayland` and a built satellite / `xwayland-satellite`, `xorg-xwayland` |
| X11, else | `/run/pc-x11/bin/xwayland-satellite :0`, running the bundle's Xwayland (shared memory, no GPU) | nothing: the X11 bundle |

- **Sound.** Before it starts PipeWire the daemon writes `/etc/pipewire/pipewire.conf.d/pc-android.conf` (owned
  by container root, 0644): `libpipewire-module-pipe-tunnel` as a sink named `android` (S16LE, 48 kHz,
  `[ FL FR ]`, `priority.session = 3000`, so it is the default) writing to `/run/pc-wayland/audio-out`, and a
  `context.exec` for `wireplumber` and `pipewire-pulse` when they are there (PipeWire starts them once its
  socket listens; nothing else would order them). The compositor makes that FIFO (0666) and plays what
  arrives through `AudioTrack`; the daemon never makes it, and the module is `nofail`, so PipeWire runs without
  it. PulseAudio programs reach `pipewire-pulse` at `/run/user/1000/pulse/native` (`PULSE_SERVER`).
- **The X11 bundle.** `/system_ext/pc-x11`, bind-mounted read-only at `/run/pc-x11` in every container: Xwayland
  (built without glamor, so it needs no Mesa), xwayland-satellite, xkbcomp with the XKB data, and dbus-daemon,
  each with its libraries and its own `ld.so` (`packages/apps/PcWayland/x11`), so they run on any distro and libc.
  `/run/pc-x11/bin` ends the default `PATH`, which is how the bundle's satellite finds its Xwayland.
- **No dbus-broker.** It takes its socket from systemd's user instance, which no container runs; started without
  it, it dies and leaves a socket nobody answers ("Connection refused"), worse for programs than no bus. Fedora,
  whose default bus it is, gets the bundle's `dbus-daemon`.

### User ids

Container uid/gid `n` (0..65535) is host id `2000000000 + userId * 100000 + n`.

- **Below 2^31, so it has network.** netd routes only uids 0..2147483647 (the `uidrange` of its ip
  rules); a higher uid falls through to the `unreachable` rule. The first plan, ids past every possible
  Android uid (from 2147500000), had no network at all. Android uids are `userId * 100000 + appId`, so the
  containers' range meets them only from Android user 20000 on, which no PC reaches; container ranges fit
  below 2^31 for users 0 to 1473 (`MAX_CONTAINER_USER`).
- **100000 per user, aligned.** The suggested `10000000 + userId * 65536` would land on Android user 100 and
  up, and its ranges would not line up with Android's. With ranges that start on a multiple of 100000,
  `host uid % 100000` (the "app id" netd's eBPF programs look at) is the container uid itself: container root is
  app id 0 and the system accounts below 10000 count as system uids, so they keep network access in Doze,
  battery saver and restricted networking, and are never taken for an installed app with that app id. Container
  uids of 10000 and more do count as app ids (see Limits).

### Sharing

| In the container | From the host | How |
| --- | --- | --- |
| `/home/<user>` | `/data/media/<userId>` | id-mapped mount: MediaProvider's uid and `media_rw` are the container user |
| `/run/pc-wayland` | `/data/misc/pc-wayland/<userId>` | bind; `WAYLAND_DISPLAY=/run/pc-wayland/wayland-0` |
| `/dev/dri` | `/dev/dri` | recursive bind (render nodes are 0666) |
| `/dev/snd`, `/dev/fuse` | the same, when present | bind |
| network | the host's | no network namespace, like distrobox |
| `/sys` | the host's | recursive bind, read-only where the kernel allows; cgroup2 of the container's own namespace on `/sys/fs/cgroup` |
| `/proc`, `/dev`, `/dev/pts`, `/dev/shm`, `/dev/mqueue`, `/run` | the container's own | proc, tmpfs, devpts `newinstance`, mqueue |

The storage folder is owned on disk by MediaProvider's uid (from `/data/system/packages.list`) with group
`media_rw`. The daemon (still root in the host namespaces) clones it with `open_tree(OPEN_TREE_CLONE)` and
id-maps it (`mount_setattr(MOUNT_ATTR_IDMAP)`) with a user namespace that maps that uid and gid to the container
user's host id; the container's init attaches it with `move_mount`. Files the container user writes are then
owned by MediaProvider and `media_rw` on disk, as if an Android app had written them. When the kernel or the
file system refuses, the folder is bound as is (see Limits).

`/data/misc/pc-wayland/<userId>` belongs to the user's system uid (`userId * 100000 + 1000`, the uid the
compositor connects as), mode 0711: the daemon makes it, and puts owner and mode right, as soon as that uid
connects (the compositor starts at login, before any container) and at every container start. The compositor creates `wayland-0` in it with mode 0666, so the container's uids can connect.

### The app

`core/` is plain Kotlin, tested on the host: the terminal emulator (`core/.../terminal/`: UTF-8 decoder, DEC-style
escape parser, screen with scrollback and alternate screen, SGR colors, modes, key encoding, snapshots), the
protocol client (`ContainerClient`, generic over the platform's fd type), the image index, names, tabs, the
launch decision and formats. `src/` wires Android: `LocalSocket` with fd passing, the download (`xz-java` decodes
the image into a pipe for the daemon), the pty reader thread, and Compose drawing (a `Canvas` with the platform's
monospace font).

The app runs as `android.uid.system` (platform signed, privileged) because pc-containerd only answers that uid.

## Protocol

[`containerd/PROTOCOL.md`](containerd/PROTOCOL.md) is the contract: one JSON object per line, `version`, `list`,
`create`, `start`, `stop`, `delete`, `rename`, `exec`, `resize`, `apps`, `icon`, `stats`, progress events, fds by
`SCM_RIGHTS`, error codes. Change it only together with `containerd/src/server.c`, `ContainerClient.kt` and this
README.

## Tests

```sh
make -C containerd test                      # C tests of the daemon on a fake machine + its socket (python)
PCC_TRIAL_ROOTFS=/path/rootfs.tar.xz make -C containerd test   # also real rootless containers (see below)
core/run_tests.sh                            # JUnit tests of core/ with external/kotlinc and JUnit from prebuilts
python3 -m unittest discover -s tests        # UI strings: both languages, placeholders, no em dash, ids exist
tools/typecheck.sh                           # compiles core + Compose UI against a previous build's jars
```

- `containerd/tests/test_*.c` use `check.h`; the privileged calls go through `sys_ops` and the tests use the
  fake machine of `containerd/tests/fakesys.h` (a fake init "runs" by writing its pid into a cgroup folder).
- `containerd/tests/test_daemon.py` runs the binary on a socket in a temporary folder: JSON lines, fds both
  ways (`socket.send_fds`/`recv_fds`), one process per connection.
- **Host trials**: `pc-containerd --socket PATH --root DIR --rootless` runs real containers as a normal user:
  only container id 0 is mapped (to the caller), files keep the caller as owner, a plain folder stands in for
  the cgroup and there is no id-mapped storage. `TrialTest` in `test_daemon.py` uses it (when `PCC_TRIAL_ROOTFS`
  names an image and user namespaces are allowed) to extract an image, start it, open a shell on a pty, run a
  command without a pty and stop it.
- The JVMs stay small (`-Xmx1g`, `-Xmx1536m`) and run under `systemd-run --user --scope -p MemoryMax=2G` when
  available. `tools/typecheck.sh` needs `out/soong/.intermediates` of a previous build (framework, ART module,
  Connectivity module, xz-java and coroutines jars); it builds no APK.
- In Soong the core tests are `PcTerminalCoreTests` (`atest`-able).

## Build integration

- local manifest: `<project path="packages/apps/PcTerminal" name="android_packages_apps_PcTerminal" remote="pc" revision="lineage-24.0" />`
- `device.mk`: `PRODUCT_PACKAGES += PcTerminal pc-containerd` (the app also `required`s pc-containerd)
- `BoardConfig.mk`: `SYSTEM_EXT_PRIVATE_SEPOLICY_DIRS += packages/apps/PcTerminal/containerd/sepolicy/private`
- Modules: `pc-containerd` (`cc_binary`, system_ext, with `pc-containerd.rc`), `PcTerminalCore`
  (`java_library`, host supported), `PcTerminalCoreTests`, `PcTerminal` (`android_app`, platform certificate,
  privileged, uses `xz-java`).

## SELinux

`pc_containerd` is a permissive domain without allow rules (the whole device is permissive for now). It labels
the binary, the socket (`pc_containerd_socket`), `/data/linux` (`pc_linux_data_file`) and
`/data/misc/pc-wayland` (`pc_wayland_data_file`), and lets `system_app` connect. An enforcing build needs: mount
permissions for proc, sysfs, tmpfs, devpts, mqueue, cgroup2 and binds below `/data/linux` (AOSP's neverallows
forbid most of them to a coredomain), `pivot_root`, `cap_userns` capabilities, `open_tree`/`mount_setattr` on
`media_rw_data_file`, writes to `/sys/fs/cgroup`, and above all a domain of their own for the containers'
programs (a transition on exec from `pc_linux_data_file`, outside `coredomain`), or a vendor-side helper.

## Kernel

The kernel has what this needs: user, pid, mount, UTS, IPC and cgroup namespaces, cgroup v2 with memory and pids,
overlayfs, FUSE, seccomp, Landlock, `openat2`, `open_tree`/`move_mount`/`mount_setattr`, `TIOCGPTPEER`,
`cgroup.kill`. Missing: **`CONFIG_SYSVIPC`** (Android turns System V IPC off), so programs that need `shmget`
or System V semaphores (X11's MIT-SHM, some databases) fail inside; `CONFIG_BINFMT_MISC` is a module.

## Limits

- **Not built with Soong or run on a device yet**: the host tests, a rootless host trial with Alpine, a syntax
  check of the daemon against bionic's headers and the app's host typecheck have run.
- **Root shells on the host trial only.** As a normal user only container root is mapped, so the person's
  account is exercised on the device only.
- **Storage without id-mapped mounts** (old kernel, a file system without support) is bound as is: its files
  show as `nobody` and the container user cannot write them.
- **Name servers** are written at start; a network change while the container runs keeps the old ones until it
  restarts.
- **`/dev/snd`** is shared, but its nodes are `0660 system:audio`: the container's uids cannot open them, and
  Android's audio server holds the cards. Sound goes through PipeWire and the compositor's pipe (see Session).
- **The session** is checked only at `start` and `exec`: a piece that stops is started again at the next one.
  PipeWire started before the compositor made `/run/pc-wayland/audio-out` has no `android` sink until it is
  started again (stop the container, or end `pipewire` and run something).
- **Container uids of 10000 and up** count, for netd's eBPF, as the app id of the same number (rare: distros
  use them for little more than `nobody` 65534).
- **xattrs and file capabilities** in images are not restored (e.g. `ping`'s `cap_net_raw`); device nodes in
  images are skipped (the container gets its `/dev` from the host).
- **The image** is checked against `SHA256SUMS` from the same server; its GPG signature is not checked.
- **No input method**: the terminal takes hardware keys (with dead keys); the on-screen keyboard and IME
  composition are not wired. No text selection or copy yet, no mouse reporting, no reflow on resize.
- `/data/linux` is encrypted with the system's device key, not the user's credential key.
- The daemon does not track the commands it started: no exit status, no list of sessions.

## Credits

This app is part of LineageOS for PCs and builds on:

- [LineageOS](https://lineageos.org) ([GitHub](https://github.com/LineageOS)): the platform and build tree.
- [The Android Open Source Project](https://source.android.com): the framework, bionic, the Kotlin compiler, the
  Jetpack Compose and AndroidX prebuilts, and [XZ for Java](https://tukaani.org/xz/java.html) (`external/xz-java`).
- [Linux Containers image server](https://images.linuxcontainers.org) ([GitHub](https://github.com/lxc/lxc-ci)):
  the distribution images and their index.
- [distrobox](https://distrobox.it) ([GitHub](https://github.com/89luca89/distrobox)): the idea and the sharing
  model (home, devices, host network); no code was taken from it.
- [LXC](https://linuxcontainers.org/lxc/) and [runc](https://github.com/opencontainers/runc): the container
  techniques used as references (mounting through `/proc/self/fd`, `pivot_root(".", ".")`); no code was taken.
- [xterm](https://invisible-island.net/xterm/) and [VT100.net](https://vt100.net): the control sequences and the
  DEC parser model the terminal follows; the emulator is our own code.
- [PipeWire](https://pipewire.org) and [WirePlumber](https://pipewire.pages.freedesktop.org/wireplumber/)
  ([GitLab](https://gitlab.freedesktop.org/pipewire)), [D-Bus](https://www.freedesktop.org/wiki/Software/dbus/),
  [dbus-broker](https://github.com/bus1/dbus-broker), [systemd](https://systemd.io)'s `systemd-socket-activate` and
  [xwayland-satellite](https://github.com/Supreeeme/xwayland-satellite): the session the daemon starts inside a
  container runs the distribution's own copies of them. The PipeWire fragment uses only the documented options of
  `libpipewire-module-pipe-tunnel` and `context.exec`; no code or configuration was taken from these projects.
