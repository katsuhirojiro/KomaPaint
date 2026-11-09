# Building KomaPaint

These notes describe how to build KomaPaint from source for the currently
supported targets.

## Primary target: Windows (MSVC)

### 1. Install dependencies

* Visual Studio 2022 with the **Desktop development with C++** workload.
* CMake ≥ 3.21 (bundled with Visual Studio or from <https://cmake.org>).
* Ninja (optional, recommended for fast incremental builds).
* Qt 6.5 or newer, with the following modules:
  * qtbase (Core, Gui, Widgets)
  * qtdeclarative (Qml, Quick, Quick Controls 2, Quick Layouts)
  * qttools (for `windeployqt`)

The easiest way to install a reproducible Qt toolchain is
[aqtinstall](https://github.com/miurahr/aqtinstall):

```powershell
pip install aqtinstall
aqt install-qt windows desktop 6.7.3 win64_msvc2022_64 -m qtdeclarative qttools
```

This installs Qt under e.g. `C:\Qt\6.7.3\msvc2022_64`. **Use the
`msvc2022_64` kit to match Visual Studio 2022's v143 toolset.** The older
`msvc2019_64` kit targets the v142 toolset and is not binary-compatible
with MSVC 2022 builds.

### 2. Configure

From a developer command prompt that has MSVC, CMake and Ninja on PATH:

```powershell
cmake -S . -B build -G Ninja ^
  -DCMAKE_BUILD_TYPE=RelWithDebInfo ^
  -DCMAKE_PREFIX_PATH=C:/Qt/6.7.3/msvc2022_64
```

If you are building from Visual Studio's built-in CMake support, point the
`CMakeSettings.json` `cmakeCommandArgs` to the same prefix path.

### 3. Build

```powershell
cmake --build build
```

The output binary is `build/bin/komapaint.exe`.

### 4. Deploy Qt dependencies

KomaPaint bundles its QML into the executable via a Qt resource
(`resources/qml.qrc`), so `windeployqt` discovers the required QML
plugins automatically without needing `--qmldir`:

```powershell
C:/Qt/6.7.3/msvc2022_64/bin/windeployqt.exe build/bin/komapaint.exe
```

After this step, `build/bin/komapaint.exe` runs standalone; no
additional copies of the `ui/` directory are needed.

`komapaint.exe` is built as a Windows GUI-subsystem application, so a
normal launch does not open a separate console window. Development
diagnostics are emitted through Qt logging (`qInfo`, `qWarning`,
`qCritical`, ...). On Windows the desktop entry point routes those
messages to the debugger output channel: view them in the Visual Studio
Output window, in Qt Creator's Application Output pane while debugging,
or with a debug-output viewer such as DebugView. Non-Windows builds keep
the conventional stderr sink.

### 5. Run the tests

```powershell
ctest --test-dir build --output-on-failure -C RelWithDebInfo
```

### 6. `.koma` file association (Windows)

KomaPaint's native format has exactly one extension: `.koma`. On Windows,
the packaged build can register this per-user (no administrator rights,
`HKCU\Software\Classes` only) so double-clicking a `.koma` file opens it
in KomaPaint:

```powershell
komapaint.exe --associate      # register
komapaint.exe --unassociate    # remove the registration again
```

The release ZIP ships `packaging/Register_koma_file_association.cmd`
(copied next to `komapaint.exe`), a thin wrapper around `--associate`.
Inside the app, **File → Associate .koma Files…** performs the same
registration. Registration writes only these keys:

* `HKCU\Software\Classes\.koma` → `KomaPaint.Illustration`
* `HKCU\Software\Classes\KomaPaint.Illustration` (icon + `shell\open\command`)
* `HKCU\Software\Classes\Applications\komapaint.exe\SupportedTypes` (`.koma` value)

and asks Explorer (`SHChangeNotify`) to refresh its associations.
Unregistering removes the `.koma` ProgID binding only when it still points
at KomaPaint's ProgID; a different application's binding is left untouched.

Passing a file path on the command line opens that file directly at
startup: `komapaint.exe artwork.koma`. A file that fails to load opens the
usual error dialog over the default untitled document — it never replaces
or modifies the untitled document's history.

## Linux (unsupported in Phase 1, portable build)

The codebase is expected to build on Linux with `apt-get install qt6-base-dev
qt6-declarative-dev qt6-tools-dev ninja-build cmake` followed by the usual:

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build
ctest --test-dir build --output-on-failure
./build/bin/komapaint
```

Linux and macOS are planned targets but are not validated as part of the
Phase 1 milestone. Package names will differ across distributions; please
open an issue if you maintain a Linux packaging recipe.

## macOS (planned)

macOS builds will use `macdeployqt` similarly to Windows. Instructions
will be added when macOS is formally supported.

## Quick QML preview via PySide6

For iterating on the UI without rebuilding the C++ binary, the repository
includes a small launcher script that uses PySide6 (Python bindings for
Qt 6) to load the same QML files:

```bash
pip install PySide6
python3 tools/run_qml.py
```

This launcher uses Python stand-ins for the C++ backend and is a
convenience for designers, not a supported runtime. Do not rely on it
for functional testing.

## Static analysis / linters

* `qmllint` is part of the Qt installation and can be run directly:

  ```bash
  qmllint -I ui ui/workspaces/MainWindow.qml
  ```

* C++ code is expected to compile cleanly at high warning levels
  (`/W4` on MSVC, `-Wall -Wextra -Wpedantic` on GCC/Clang once those
  are wired in the project options).

## Build options

| Option                    | Default | Description |
| ------------------------- | ------- | ----------- |
| `KOMAPAINT_BUILD_TESTS`   | ON      | Build the QtTest unit-test suite. |
| `KOMAPAINT_ENABLE_WARNINGS`| ON      | Enable strict compiler warnings. |
