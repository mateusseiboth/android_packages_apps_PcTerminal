# pc-containerd protocol (version 1)

`pc-containerd` is the root service that keeps the Linux containers of LineageOS for PCs. Clients (the
PcTerminal app, the Wayland compositor) talk to it over a Unix stream socket. This file is the contract:
the daemon (`src/server.c`) and every client follow it exactly.

## Transport

- Socket: `/dev/socket/pc-containerd` (`SOCK_STREAM`, created by init from `pc-containerd.rc`). In Java,
  `LocalSocketAddress("pc-containerd", LocalSocketAddress.Namespace.RESERVED)`.
- Who may connect: root and the system app (`android.uid.system`) of any user, that is a peer uid with
  `uid == 0` or `uid % 100000 == 1000` (`SO_PEERCRED`). Other peers are disconnected without a reply.
- One JSON object per line, UTF-8, ended by `\n`. A line is at most 65536 bytes; a longer line is answered
  with `bad_request` and dropped.
- Requests on one connection are answered in order, one at a time. A client that wants to do something
  while a long request (`create`) runs opens another connection. A connection may stay open and idle.
- **The Android user is never sent.** Every request acts on the containers of the Android user of the
  peer: `userId = peer uid / 100000`. Root (uid 0) is user 0. A `user_id` (or any other user field) in
  the JSON is ignored. A client that serves one user (the compositor of user 10) connects as that user's
  system uid, `userId * 100000 + 1000` (1001000 for user 10); there is no exception for root.

## File descriptors

File descriptors travel as `SCM_RIGHTS` ancillary data on the `sendmsg` that carries the line.

- Client to daemon (`create`, `resize`): attach exactly one fd to the bytes of the request line. In Java,
  `LocalSocket.setFileDescriptorsForSend(arrayOf(fd))` and then write the whole line in one `write`.
- Daemon to client (`exec` with `pty`, `icon`): the fd is attached to the reply line, and the reply has
  `"fd":true`. In Java, read the line, then `LocalSocket.getAncillaryFileDescriptors()`. A reply without
  `"fd":true` carries no fd.

## Messages

Request: `{"id":N,"cmd":"<command>", ...arguments}`. `id` is any integer the client picks; replies and
events repeat it. Unknown arguments are ignored.

Success: `{"id":N,"ok":true, ...results}`

Failure: `{"id":N,"ok":false,"error":"<code>","message":"<English detail for logs>"}`. Clients show their
own text for the `error` code; `message` is for logs only.

Event (zero or more, before the final reply of the same id): `{"id":N,"event":"progress", ...}`

A request that is not valid JSON, has no integer `id` or no string `cmd` is answered with `"id":0` and
`bad_request`.

## Names

- Container `name`: `[a-z0-9][a-z0-9-]{0,30}` (also the container's host name and the cgroup name).
- Container `user`: the account the person uses inside, `[a-z_][a-z0-9_-]{0,31}`, default `user`. It
  always has uid and gid 1000 inside the container.
- A container `state` is `"running"` or `"stopped"`.

## Commands

### `version`

`{"id":1,"cmd":"version"}` -> `{"id":1,"ok":true,"protocol":1}`

### `list`

`{"id":1,"cmd":"list"}` ->

```json
{"id":1,"ok":true,"containers":[
  {"name":"debian","distro":"debian","release":"trixie","created":1764072000,"user":"mateus","state":"running"}
]}
```

`created` is Unix time in seconds. Sorted by name.

### `create`

Extracts a rootfs into a new container. The client downloads and decompresses the image itself and
passes an fd (a pipe or a file) that reads as a **plain, uncompressed tar** stream (the `rootfs.tar.xz` of
images.linuxcontainers.org after xz decoding).

`{"id":2,"cmd":"create","name":"debian","distro":"debian","release":"trixie","user":"mateus"}` + one fd

Events while reading: `{"id":2,"event":"progress","bytes":104857600,"files":2048}` (bytes of tar read so
far, entries extracted), at most about every 250 ms.

Reply: `{"id":2,"ok":true,"container":{...same fields as in list...}}`. The container is stopped.

On failure nothing is left behind. Errors: `bad_name`, `bad_user`, `exists`, `no_fd`, `bad_archive`,
`no_space`, `io`.

### `start`

`{"id":3,"cmd":"start","name":"debian","dns":["192.168.1.1","1.1.1.1"]}` -> `{"id":3,"ok":true,"state":"running"}`

Starts the container's init, then the session of the container's user: a D-Bus session bus, PipeWire with
`pipewire-pulse`, and `xwayland-satellite` on `:0`, each only when its programs are in the container (README,
"Session"). A missing piece never fails the request. `dns` (optional, at most 3 IPv4 or IPv6 addresses) is written to the
container's `/etc/resolv.conf`; without it the file is left as it is. Starting a running container
succeeds and only starts the session pieces that are not running. Errors: `not_found`, `start_failed`.

### `stop`

`{"id":4,"cmd":"stop","name":"debian"}` -> `{"id":4,"ok":true,"state":"stopped"}`

Sends SIGTERM to every process of the container, waits up to 5 s, then kills what is left. Stopping a
stopped container succeeds. Errors: `not_found`.

### `delete`

`{"id":5,"cmd":"delete","name":"debian"}` -> `{"id":5,"ok":true}`

Stops the container if needed and removes it with its files. Errors: `not_found`, `io`.

### `rename`

`{"id":6,"cmd":"rename","name":"debian","new_name":"work"}` -> `{"id":6,"ok":true,"container":{...}}`

Only a stopped container. Errors: `not_found`, `bad_name`, `exists`, `running`.

### `exec`

Runs a command inside the container. Starts the container first when it is stopped, and the session pieces
that are not running (see `start`) before the command.

```json
{"id":7,"cmd":"exec","name":"debian","argv":["gedit","/home/mateus/a.txt"],
 "env":{"GDK_BACKEND":"wayland"},"cwd":"/home/mateus","user":"mateus","pty":false}
```

- `argv` (optional): the command. A name without `/` is looked up in the container's `PATH`. Default: the
  user's login shell from the container's `/etc/passwd` (falls back to `/bin/sh`) as a login shell
  (`["/bin/bash","-l"]`).
- `user` (optional): `"root"` or the container's `user`; default the container's `user`.
- `cwd` (optional): default the user's home (`/home/<user>`, `/root` for root).
- `env` (optional): string values added over the defaults below.
- `pty` (optional, default `false`): when `true`, the daemon creates a pseudo terminal inside the
  container, the command gets it as its controlling terminal on stdin/stdout/stderr, and the **master fd
  is attached to the reply**. `rows`/`cols` (optional, default 24x80) set its size. When `false`, stdin,
  stdout and stderr are `/dev/null` and nothing is attached.

Default environment: `PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin:/run/pc-x11/bin` (the X11 bundle last), `HOME`, `USER`,
`LOGNAME`, `SHELL`, `LANG=C.UTF-8`, `TERM=xterm-256color` (with `pty`), `XDG_RUNTIME_DIR=/run/user/<uid>`,
`WAYLAND_DISPLAY=/run/pc-wayland/wayland-0`, `XDG_SESSION_TYPE=wayland`, `QT_QPA_PLATFORM=wayland;xcb`,
`DISPLAY=:0`, `DBUS_SESSION_BUS_ADDRESS=unix:path=/run/user/<uid>/bus`,
`PULSE_SERVER=unix:/run/user/<uid>/pulse/native`, `container=pc-containerd`. `<uid>` is 1000 for the
container's user and 0 for root.

Reply: `{"id":7,"ok":true,"pid":42}` (`pid` inside the container's pid namespace), plus `"fd":true` and
the pty master with `pty`. The process is not tied to the connection: closing the socket does not stop
it. Closing the pty master hangs up the terminal (SIGHUP to its session).

Errors: `not_found`, `start_failed`, `bad_user`, `exec_failed` (the command could not be run; `message`
says why, e.g. `No such file or directory`).

### `resize`

`{"id":8,"cmd":"resize","rows":40,"cols":120}` + the pty master fd -> `{"id":8,"ok":true}`

Sets the terminal size (`TIOCSWINSZ`; the command gets SIGWINCH). Errors: `no_fd`, `bad_request`.

### `apps`

The container's applications, from `/usr/share/applications/*.desktop` (and `/usr/local/share/...`).
The container does not have to be running.

`{"id":9,"cmd":"apps","name":"debian","locale":"pt_BR"}` ->

```json
{"id":9,"ok":true,"apps":[
  {"id":"org.gnome.TextEditor","name":"Editor de texto","exec":"gnome-text-editor %U",
   "argv":["gnome-text-editor"],"icon":"org.gnome.TextEditor",
   "icon_path":"/usr/share/icons/hicolor/scalable/apps/org.gnome.TextEditor.svg","icon_format":"svg",
   "terminal":false,"categories":["GNOME","GTK","Utility","TextEditor"]}
]}
```

- Only `Type=Application` entries without `NoDisplay=true` or `Hidden=true`. Sorted by `id`.
- `id` is the desktop file id (file name without `.desktop`).
- `name`: `Name[<locale>]`, else `Name[<language>]`, else `Name`. `locale` is optional.
- `argv`: `Exec` split by the Desktop Entry quoting rules, field codes (`%f %F %u %U %i %c %k`...)
  removed, `%%` turned into `%`. Run it with `exec`.
- `icon_path`: a path inside the container: `Icon` when it is absolute, else the best file found in
  `/usr/share/icons/hicolor/<size>/apps/` (PNG of 96 px or more preferred, the largest PNG first),
  `scalable/apps` (SVG), then smaller PNGs, then `/usr/share/pixmaps`. `icon_path` and `icon_format`
  (`"png"`, `"svg"` or `"xpm"`) are `null` when nothing was found.

Errors: `not_found`.

### `icon`

The icon file of an app, opened read-only.

`{"id":10,"cmd":"icon","name":"debian","app":"org.gnome.TextEditor"}` or
`{"id":10,"cmd":"icon","name":"debian","path":"/usr/share/pixmaps/foo.png"}` ->
`{"id":10,"ok":true,"fd":true,"format":"svg","size":4182}` + the fd.

`path` must end in `.png`, `.svg` or `.xpm` and is resolved inside the container's root
(symbolic links never leave it). At most 4 MiB. Errors: `not_found`, `no_icon`, `bad_request`.

### `stats`

`{"id":11,"cmd":"stats","name":"debian"}` ->

```json
{"id":11,"ok":true,"state":"running","memory_bytes":73400320,"cpu_usec":1834000,"pids":7,"disk_bytes":412000000}
```

`memory_bytes` (cgroup `memory.current`), `cpu_usec` (`cpu.stat` `usage_usec`) and `pids`
(`pids.current`, or the number of processes) are `null` when the container is stopped or the value cannot
be read. `disk_bytes` is the space the container's files use. Errors: `not_found`.

## Error codes

| code | meaning |
| --- | --- |
| `bad_request` | not JSON, missing or wrong argument, unknown command |
| `bad_name` | the container name is not valid |
| `bad_user` | the user name is not valid, or `exec` asked for another user |
| `not_found` | no container with that name for this Android user |
| `exists` | a container with that name already exists |
| `running` | the container must be stopped first |
| `no_fd` | the request needs an fd and none was attached |
| `bad_archive` | the fd is not a tar stream, or an entry could not be extracted |
| `no_space` | the disk is full |
| `start_failed` | the container could not be started |
| `exec_failed` | the command could not be run |
| `no_icon` | the app has no icon file |
| `io` | anything else that failed on the daemon's side |

## Container layout (for reference)

- Files: `/data/linux/<userId>/<name>/rootfs` and `/data/linux/<userId>/<name>/config.json`.
- Ids: container uid/gid `n` (0..65535) is host uid/gid `2000000000 + userId * 100000 + n` (below 2^31: netd routes only uids up to 2147483647).
- Shared with the container: `/data/media/<userId>` on `/home/<user>`, `/data/misc/pc-wayland/<userId>` on
  `/run/pc-wayland`, `/dev/dri`, `/dev/snd` and `/dev/fuse` when present, the host network.
- The compositor puts its socket at `/data/misc/pc-wayland/<userId>/wayland-0` (mode 0666, since the
  container's uids are not Android uids). pc-containerd makes that directory when the user's system uid
  connects (the compositor at login, before its first request) and at every container start, and puts it
  right when it differs (never following a link): owner and group the user's system uid (`userId * 100000 + 1000`), mode
  0711. Its parent `/data/misc/pc-wayland` is root's, 0711.
