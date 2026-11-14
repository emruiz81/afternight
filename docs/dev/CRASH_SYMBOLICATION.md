# Crash Symbolication

This guide covers how to find, analyze, and symbolicate AfterNight crash dumps on Linux and Windows.

## Background

AfterNight uses [Crashpad](https://chromium.googlesource.com/crashpad/crashpad/) on Linux and Windows MSVC builds to capture crash dumps automatically. When the application crashes, Crashpad writes a minidump (`.dmp`) file to a local directory — no data is uploaded anywhere.

For extension-runtime triage inside the app, the Extension Status page shows each extension's last crash or termination and per-extension recovery guidance based on the most recent host launch/termination result. An extension host writes no dump of its own; see [Which Process Crashed](#which-process-crashed).

Minidumps use a compact Breakpad/Crashpad-specific format. On Linux, LLDB can open them directly and resolve symbols from the split `.debug` sidecars that the build produces. On Windows, Visual Studio and WinDbg open `.dmp` files and resolve symbols from `.pdb` files.

The handler lives in the platform library, `AfterNightPlatform` (`src/AfterNightPlatform/include/AfterNightPlatform/CrashHandler.h`), beside the logger, and no engine carries it. The application starts it with `an::platform::initializeCrashHandler` before it creates any Qt object, from `an::platform::defaultCrashHandlerConfig` and the application's own version, `kProductVersion` (`src/AfterNightApp/app/ApplicationSession.h`), which is also the application version Qt reports; the engine's version is not the product's. Every dump carries the annotations `product`, `version`, `build_type` and `compiler`; the application adds `qt_version`, `engine_version` (`an::engineVersionString()`) and `os` once it is running. A test executable started through the fixture harness (`src/AfterNightTests/fixtures/harness/TestHarness.h`) adds `test_suite` and `os` instead, and writes its dumps and metrics under its storage root's `crash_dumps` and `crash_metrics`. The handler is the `crashpad_handler` executable beside the application, and uploading is off. Only the application starts a handler: the command-line tool loads the platform library, which carries Crashpad's client, but starts none, and neither extension host starts one, so a crash in any of them leaves no dump - see [Which Process Crashed](#which-process-crashed). The command-line tool's own frames resolve from its `AfterNightCLI` sidecar or `.pdb`.

## Where Crash Dumps Are Stored

| Platform | Dump location |
|----------|---------------|
| Linux | `$XDG_DATA_HOME/AfterNight/CrashDumps/pending/` (defaults to `~/.local/share/AfterNight/CrashDumps/pending/`) |
| Windows (MSVC) | `%LOCALAPPDATA%\AfterNight\CrashDumps\reports\` |

Metrics (internal Crashpad bookkeeping) are kept alongside the dumps:

| Platform | Metrics location |
|----------|------------------|
| Linux | `$XDG_DATA_HOME/AfterNight/CrashMetrics/` (defaults to `~/.local/share/AfterNight/CrashMetrics/`) |
| Windows (MSVC) | `%LOCALAPPDATA%\AfterNight\CrashMetrics\` |

## Which Process Crashed

Only the application starts a crash handler, so only a crash of the `AfterNight` process - its window and everything loaded into it: Controls, Processes, the consumer runtime, the engine, the platform library - leaves a dump. A test executable started through the fixture harness starts a handler of its own and writes under its storage root.

The other product executables start none:

- The command-line tool, `AfterNightCLI`, loads the platform library and so carries Crashpad's client, but never starts it.
- The full extension host, `AfterNightExtensionHost`, runs the runtime backend (`an_extension_runtime_backend`), the Python interpreter and the native Controls windows an extension opens inside its own process. A crash in any of them ends the host and leaves no dump.
- The Lite host, `AfterNightExtensionHostLite`, links only the protocol library of the product's libraries and runs its extension's Python in its own process; it leaves no dump either.

The application supervises both hosts, so a host that crashes is seen as the host's exit: the Extension Status page records it as that extension's last crash or termination, with recovery guidance. To get a stack for a crash in a host or in the command-line tool, run it under a debugger or let the operating system write its own core or crash dump, then symbolicate it with the same `.debug` sidecars or `.pdb` files described below.

## What the Build Produces

### Linux

Release builds split debug information into sidecar files; a Debug build's `_d` binaries keep their DWARF inside the binary and have no sidecar:

```
bin/clang/release/AfterNight                                  ← stripped binary
bin/clang/release/AfterNight.debug                            ← debug sidecar (DWARF)
bin/clang/release/AfterNightCLI
bin/clang/release/AfterNightCLI.debug
bin/clang/release/AfterNightExtensionHost
bin/clang/release/AfterNightExtensionHost.debug
bin/clang/release/AfterNightExtensionHostLite
bin/clang/release/AfterNightExtensionHostLite.debug
bin/clang/release/libAfterNightPlatform.so
bin/clang/release/libAfterNightPlatform.so.debug
bin/clang/release/libAfterNightEngine.so
bin/clang/release/libAfterNightEngine.so.debug
bin/clang/release/libAfterNightEngineGmicAdapter.so
bin/clang/release/libAfterNightEngineGmicAdapter.so.debug
bin/clang/release/libAfterNightConsumerRuntime.so
bin/clang/release/libAfterNightConsumerRuntime.so.debug
bin/clang/release/libAfterNightControls.so
bin/clang/release/libAfterNightControls.so.debug
bin/clang/release/libAfterNightProcesses.so
bin/clang/release/libAfterNightProcesses.so.debug
bin/clang/release/libAfterNightExtensionProtocol.so
bin/clang/release/libAfterNightExtensionProtocol.so.debug
bin/clang/release/liban_extension_runtime_backend.so
bin/clang/release/liban_extension_runtime_backend.so.debug
bin/clang/release/styles/libqafternight.so                    ← the style plugin
bin/clang/release/styles/libqafternight.so.debug
```

The stripped binary contains a `gnu-debuglink` section pointing at the corresponding `.debug` file. LLDB follows this link automatically when the `.debug` file is in the same directory as the binary. Frames of the logger and the crash handler itself - `an::platform::*`, and Crashpad's client, which is linked into it - resolve from the platform library's sidecar. Frames of `an::consumer::*` - the engine session, the operation scope and the image asset the command-line tool, the application and the full host run on - resolve from the consumer runtime's.

The other frames resolve from the module the layering puts them in:

- **The engine** - PascalCase `an::` names such as `an::blurImageGaussian`, from files such as `Gaussian.cpp` - is `libAfterNightEngine.so`; a G'MIC process reaches the G'MIC library through `libAfterNightEngineGmicAdapter.so`. A dump from release v0.5.1 or earlier also names `AfterNightEngine`, but that is the engine those releases shipped, whose frames are snake_case `an::` names from `an_*.cpp` files; symbolicate such a dump with that release's own sidecars or `.pdb` files.
- **Controls** - the viewer, the history, the process framework, the session bridge and the application's coordinators, such as `ImageViewControl`, `ImageProcessControl` and `an::ui::applyProcessChannelScope` - is `libAfterNightControls.so`.
- **Processes** - every process window, its helpers and the ImageIntegration workflow, such as `GaussianBlurProcess` and `process_utils::blurGaussian` - is `libAfterNightProcesses.so`.
- **The runtime backend** - the `_afternight_runtime` bindings the full host runs - is `liban_extension_runtime_backend.so`.
- **The main window and its context** (`AfterNightApp`, `ApplicationSession`) are the `AfterNight` executable's own, and the context it installs (`ApplicationContext`) is `AfterNightShellCore`'s; the full host's `main.cpp`, context and native window are `AfterNightExtensionHost`'s.

### Windows (MSVC)

MSVC builds produce `.pdb` files alongside the binaries:

```
bin\msvc\release\AfterNight.exe
bin\msvc\release\AfterNight.pdb
bin\msvc\release\AfterNightCLI.exe
bin\msvc\release\AfterNightCLI.pdb
bin\msvc\release\AfterNightExtensionHost.exe
bin\msvc\release\AfterNightExtensionHost.pdb
bin\msvc\release\AfterNightExtensionHostLite.exe
bin\msvc\release\AfterNightExtensionHostLite.pdb
bin\msvc\release\AfterNightPlatform.dll
bin\msvc\release\AfterNightPlatform.pdb
bin\msvc\release\AfterNightEngine.dll
bin\msvc\release\AfterNightEngine.pdb
bin\msvc\release\AfterNightEngineGmicAdapter.dll
bin\msvc\release\AfterNightEngineGmicAdapter.pdb
bin\msvc\release\AfterNightConsumerRuntime.dll
bin\msvc\release\AfterNightConsumerRuntime.pdb
bin\msvc\release\AfterNightControls.dll
bin\msvc\release\AfterNightControls.pdb
bin\msvc\release\AfterNightProcesses.dll
bin\msvc\release\AfterNightProcesses.pdb
bin\msvc\release\AfterNightExtensionProtocol.dll
bin\msvc\release\AfterNightExtensionProtocol.pdb
bin\msvc\release\an_extension_runtime_backend.dll
bin\msvc\release\an_extension_runtime_backend.pdb
bin\msvc\release\styles\qafternight.dll
bin\msvc\release\styles\qafternight.pdb
```

The modules hold the same frames as their Linux counterparts.

The Windows installer (`installer/win_installer.iss`) and the CI pipeline both exclude `.pdb` files from the shipped package.

## Required Tools

### Linux

LLDB is the recommended tool for analyzing Crashpad minidumps on Linux. It has native minidump support and reads DWARF debug information directly from the `.debug` sidecars — no format conversion needed.

`build/scripts/linux/02-install_linux_deps.sh` installs `lldb` through apt.

### Windows

No extra tools are needed. Visual Studio and WinDbg can open `.dmp` files directly and resolve symbols from `.pdb` files.

## Analyzing Crash Dumps — Linux (LLDB)

### Quick Start

After a crash, open the dump in LLDB:

```bash
lldb \
  -c ~/.local/share/AfterNight/CrashDumps/pending/<dump-file>.dmp \
  bin/clang/release/AfterNight
```

LLDB will load the minidump, follow the `gnu-debuglink` to find `AfterNight.debug`, and resolve symbols automatically.

### Basic Commands

Once the dump is loaded, use standard LLDB commands:

```
(lldb) bt                 # backtrace of the crashing thread
(lldb) bt all              # backtrace of all threads
(lldb) thread list         # list all threads with their status
(lldb) thread select 3     # switch to thread 3
(lldb) bt                  # backtrace of the selected thread
```

### Inspecting Frames

```
(lldb) frame select 1      # select frame #1 in the current thread
(lldb) frame variable       # show local variables
(lldb) frame variable plan  # show a specific variable
(lldb) p sourceLayout.extent().width()   # evaluate an expression
(lldb) frame info           # show source file and line number
```

### Navigating Source

```
(lldb) source list          # show source code around the current frame
(lldb) source list -f Gaussian.cpp -l 1278   # show specific file/line
```

### Loading Additional Modules

If LLDB does not resolve the product libraries' symbols automatically (e.g. because the dump was generated on a different machine), point it at the matching binaries and debug files:

```
(lldb) target modules add bin/clang/release/libAfterNightEngine.so
(lldb) target modules add bin/clang/release/libAfterNightControls.so
(lldb) target modules add bin/clang/release/libAfterNightProcesses.so
(lldb) target modules add bin/clang/release/libAfterNightConsumerRuntime.so
(lldb) target modules add bin/clang/release/libAfterNightPlatform.so
```

LLDB will follow the `gnu-debuglink` to find each module's `.debug` sidecar if it is in the same directory. Add the module the unresolved frames are in - the list under [What the Build Produces](#what-the-build-produces) says which holds what; the platform library is the one to add when the crashing frames are the logger's or the crash handler's own.

### Symbolicating Debug Builds

For debug builds, point LLDB at the debug binary:

```bash
lldb \
  -c ~/.local/share/AfterNight/CrashDumps/pending/<dump-file>.dmp \
  bin/clang/debug/AfterNight_d
```

### Using the Latest Dump

To find the most recent dump:

```bash
ls -t ~/.local/share/AfterNight/CrashDumps/pending/*.dmp | head -1
```

Then pass that path to `lldb -c`.

### Reading the Output

A typical LLDB backtrace - here the top of a Gaussian blur's worker thread, abridged - looks like:

```
* thread #24, stop reason = signal SIGSEGV
  * frame #0: 0x00007f... libAfterNightEngine.so`an::(anonymous namespace)::blurImageGaussianImplementation(execution=..., sampleType=Float32, ...) at Gaussian.cpp:1142:52
    frame #1: 0x00007f... libAfterNightEngine.so`an::blurImageGaussian(execution=..., sampleType=Float32, ...) at Gaussian.cpp:1278:12
    frame #2: 0x00007f... libAfterNightProcesses.so`process_utils::blurGaussian(engine=..., execution=..., input=..., output=..., sigma=2, border=ReflectExcludingEdge) at SpatialProcessSupport.cpp:237:25
    frame #3: 0x00007f... libAfterNightProcesses.so`GaussianBlurProcess::executeTypedProcessIteration(input=..., output=..., run=..., params=..., scope=...) at GaussianBlurProcess.cpp:88:12
```

Further down the same thread are the process framework's frames in `libAfterNightControls.so`, such as `an::ui::applyProcessChannelScope` and `ImageProcessControl::executeProcessUsingChannels`.

Each frame shows:
- **Frame number** (`#0`, `#1`, ...)
- **Module** (`libAfterNightEngine.so`, `libAfterNightProcesses.so`, `AfterNight`)
- **Function** with demangled name and parameters
- **Source file and line number**

Unsymbolicated frames show only an address and module offset:

```
    frame #4: 0x00007f... libAfterNightControls.so + 0x1a2b3c
```

This means the `.debug` file for that module is missing or does not match the binary in the dump.

### LLDB vs. GDB

LLDB is recommended over GDB because:
- LLDB reads Crashpad minidumps natively (`lldb -c file.dmp`)
- GDB requires converting the dump to an ELF core file first (using `minidump-2-core` from Breakpad)
- LLDB is already installed as part of the Clang toolchain used to build AfterNight

If you prefer GDB and have the Breakpad `minidump-2-core` tool, you can convert:

```bash
minidump-2-core dump.dmp > dump.core
gdb bin/clang/release/AfterNight dump.core
```

## Analyzing Crash Dumps — Windows (MSVC)

### Using Visual Studio

1. Locate the `.dmp` file in `%LOCALAPPDATA%\AfterNight\CrashDumps\reports\`
2. Double-click the `.dmp` file (or use File → Open → File in Visual Studio)
3. Visual Studio will show a Minidump File Summary. Click **Debug with Native Only**
4. When prompted for symbols, point Visual Studio at the `bin\msvc\release\` folder containing the matching `.pdb` files
5. Visual Studio shows the call stack, locals, and source location of the crash

> The `.pdb` files **must** come from the exact same build as the binaries that crashed. A rebuild — even without code changes — produces different `.pdb` files.

### Using WinDbg

1. Open WinDbg and use File → Open Crash Dump
2. Set the symbol path to include the build output:
   ```
   .sympath+ C:\path\to\afternight\bin\msvc\release
   ```
3. Reload symbols and analyze:
   ```
   .reload /f
   !analyze -v
   ```

### For Crash Dumps from Released Builds

GitHub releases include a `AfterNight-<version>-windows-debug-symbols.zip` asset containing the matching `.pdb` files. Download it and point Visual Studio or WinDbg at the extracted folder.

## Release Build Symbols (CI / GitHub Actions)

The CI pipeline handles debug symbols as follows:

| Platform | Debug format | Included in installer? | Uploaded as CI artifact? | Attached to GitHub release? |
|----------|-------------|----------------------|------------------------|-----------------------------|
| Linux | `.debug` sidecars | No (excluded from AppImage) | Yes, 30-day retention | Yes, as `*-linux-debug-symbols.zip` |
| Windows | `.pdb` files | No (excluded from installer) | Yes, 30-day retention | Yes, as `*-windows-debug-symbols.zip` |

To symbolicate a crash from a released version:

1. Download the matching debug symbols zip from the GitHub release page
2. **Linux**: extract the `.debug` files into the same directory as the binaries, then open the dump in LLDB as described above
3. **Windows**: extract the `.pdb` files and open the `.dmp` in Visual Studio or WinDbg

## Troubleshooting

### AfterNight frames show raw addresses (not symbolicated)

- The `.debug` files (Linux) or `.pdb` files (Windows) do not match the binary that produced the dump. They must come from the exact same build.
- On Linux, ensure the `.debug` files are in the same directory as the stripped binaries so LLDB can follow the `gnu-debuglink`.
- If analyzing a dump from a released build, download the debug symbols zip from the GitHub release page.

### Only system-library frames are symbolicated

This is normal. Only AfterNight modules have matching debug symbols in a typical setup. Third-party libraries (Qt, oneTBB, FFTW, KDDockWidgets, etc.) will appear as raw addresses unless you install their debug packages (e.g. `libqt6core6-dbgsym` on Debian/Ubuntu).

### No `.dmp` files in the dump directory

- Verify Crashpad is enabled for your build
- Check that the crash was the application's: the command-line tool and the extension hosts start no handler (see [Which Process Crashed](#which-process-crashed))
- Check that `crashpad_handler` exists next to the application binary
- On Linux, check both `$XDG_DATA_HOME/AfterNight/CrashDumps/pending/` and `~/.local/share/AfterNight/CrashDumps/pending/`

### LLDB says "unable to locate a platform"

Pass the binary as a positional argument alongside the dump:

```bash
lldb -c dump.dmp bin/clang/release/AfterNight
```

If the binary was built on a different machine, also add shared libraries:

```
(lldb) target modules add bin/clang/release/libAfterNightEngine.so
```

### LLDB hangs or is extremely slow loading the minidump

LLDB can sometimes hang or take very long when loading Crashpad minidumps, especially in batch mode (`-b`, `--source`, or piped commands). If LLDB is unresponsive for more than 30 seconds, use the **Python + addr2line fallback** described below.

## Fallback: Python + addr2line (Linux)

When LLDB is uncooperative, you can analyze a Crashpad minidump using the Python `minidump` library to extract thread stacks and `addr2line` to resolve symbols. This approach is reliable and fast.

### Setup

Install the `minidump` Python package (once):

```bash
pip install minidump
```

### Step 1 — Extract crash info and stack addresses

```python
from minidump.minidumpfile import MinidumpFile
import struct

DUMP = "~/.local/share/AfterNight/CrashDumps/pending/<dump-file>.dmp"

# Read raw bytes (needed for stack memory access)
with open(DUMP, "rb") as f:
    raw = f.read()

mdf = MinidumpFile.parse(DUMP)

# Build module lookup table
modules = sorted(mdf.modules.modules, key=lambda m: m.baseaddress)

def resolve(addr):
    for m in modules:
        if m.baseaddress <= addr < m.baseaddress + m.size:
            offset = addr - m.baseaddress
            name = m.name.split('/')[-1]
            return f"{name}+{hex(offset)}"
    return None

# Print exception info
exc_tid = None
if mdf.exception:
    er = mdf.exception.exception_records[0]
    print(f"Signal:  {er.ExceptionRecord.ExceptionCode}")
    print(f"Address: {hex(er.ExceptionRecord.ExceptionAddress)}")
    print(f"Thread:  {er.ThreadId}")
    exc_tid = er.ThreadId

# Find the crashing thread and scan its stack for return addresses
for t in mdf.threads.threads:
    if t.ThreadId == exc_tid:
        stack_start = t.Stack.StartOfMemoryRange
        stack_data  = raw[t.Stack.Rva : t.Stack.Rva + t.Stack.DataSize]

        print(f"\nStack addresses in crashing thread:")
        for off in range(0, len(stack_data) - 7, 8):
            val = struct.unpack_from('<Q', stack_data, off)[0]
            sym = resolve(val)
            if sym:
                print(f"  [{hex(stack_start + off)}] {sym}")
        break
```

### Step 2 — Symbolicate with addr2line

Take the offsets for `AfterNight` and for each library the output names - for example `libAfterNightEngine.so` - and resolve them to source locations against that module's sidecar:

```bash
# For AfterNight binary addresses
addr2line -f -C -e bin/clang/release/AfterNight.debug 0x6f0e40 0x6e252d

# For engine library addresses
addr2line -f -C -e bin/clang/release/libAfterNightEngine.so.debug 0xf167f4

# For a process's addresses
addr2line -f -C -e bin/clang/release/libAfterNightProcesses.so.debug 0x2b41c0
```

The `-C` flag demangles C++ names. Pass multiple addresses on one command line for efficiency.

### Notes

- The PEB parsing error from the `minidump` library is harmless on Linux — it expects Windows PEB structures that don't exist in a Linux Crashpad dump.
- Stack scanning finds *potential* return addresses by matching values against loaded module ranges. Not every match is a real frame, but the sequence generally reflects the true call stack.
- The `.debug` sidecar files must match the exact build that crashed, just as with LLDB.

## Artifact Retention

For any build you ship or need to debug later, keep these together:

| Artifact | Purpose |
|----------|---------|
| `.dmp` file | The crash dump itself |
| Application binaries | Must match the exact build that crashed |
| `.debug` files (Linux) or `.pdb` files (Windows) | Debug information for symbolication |

GitHub releases attach debug symbol zips automatically. For ad-hoc or development builds, keep the `bin/<toolchain>/release/` directory intact until you no longer need to debug crashes from that build.
