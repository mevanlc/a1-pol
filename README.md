# a1-pol

A1 proof-of-life helpers.

## `a1-pol-mem`

Allocates a percentage of total system RAM, fills it with random bytes, keeps a
global reference to the allocation, and stays alive until terminated.

```sh
make
./build/a1-pol-mem start 50
./build/a1-pol-mem start 50 --foreground
pkill a1-pol-mem
```

Portable CMake builds are also supported:

```sh
cmake -S . -B build-cmake -DCMAKE_BUILD_TYPE=Release
cmake --build build-cmake
```

On Windows with MSVC:

```powershell
cmake -S . -B build-msvc
cmake --build build-msvc --config Release
.\build-msvc\Release\a1-pol-mem.exe start 50
```

On MSYS2/MinGW, either CMake or `make` can be used. The Makefile links
`bcrypt` automatically when `OS=Windows_NT`.

By default `start` detaches from the controlling terminal. With
`--foreground`, it stays attached and logs startup/shutdown status to stderr.

`pkill a1-pol-mem` sends `SIGTERM`; the process traps it, frees the allocation,
and exits with status 0 on Unix-like systems. Windows builds trap Ctrl-C,
Ctrl-Break, console close/logoff/shutdown events, and C runtime `SIGTERM` where
the runtime delivers it.

## GitHub Actions runner hooks

`action-runner-hooks/job-started.{sh,ps1}` stops existing `a1-pol-mem`
processes before a job starts.

`action-runner-hooks/job-completed.{sh,ps1}` starts `a1-pol-mem` after a job
completes. It defaults to `50` percent memory usage. Override that with
`A1_POL_MEM_PERCENT`.

The completed hook locates the binary in this order:

1. `A1_POL_MEM_BIN`
2. recursive search from the repository root when `.git` is present
3. `PATH`
