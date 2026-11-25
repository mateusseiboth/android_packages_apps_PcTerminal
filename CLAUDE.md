# PcTerminal: notes for agents

- Never run `m`/`lunch`/soong here to test: `make test` in `containerd/`, `core/run_tests.sh`,
  `python3 -m unittest discover -s tests` and `tools/typecheck.sh` (README, "Tests"). Keep JVMs at -Xmx1.5g or
  less, under `systemd-run --user --scope -p MemoryMax=2G`.
- TDD: test first (`containerd/tests/test_*.c` with `check.h`, `core/tests/*Test.kt`), see it fail, then code.
- `containerd/PROTOCOL.md` is a contract other components (the Wayland compositor) build against: change it
  only together with `containerd/src/server.c`, `core/.../ContainerClient.kt` and the README.
- The daemon's privileged calls go through `sys_ops` (`containerd/src/sysops.h`); host tests use the fake in
  `containerd/tests/fakesys.h`. Every path inside a rootfs goes through `inroot.h` (openat2 RESOLVE_IN_ROOT).
- Tables over if/else (`HANDLERS`, `WRITERS`, `ICON_PLACES`, `DEFAULT_MESSAGES`, `LaunchPlan`). Names start with
  an English verb or prefix.
- UI text: pt-BR in `res/values-pt`, English in `res/values`; what happened and what to do, no em dash, no
  design explanations (`tests/test_strings.py`).
- README in English, ending with the Credits section. Commits: no AI attribution lines.
