# SoftLink

English | [简体中文](README_CN.md)

A lightweight Windows folder migration tool written in C++17 with a
Win32 interface. Move a folder to another location and leave a directory
symbolic link at the original path so applications can continue using it.

This repository contains the standalone C++ version of SoftLink.

## Features

- Folder selection, background migration, operation output, and history details.
- Directory symbolic links created through the Windows API.
- Backup retention when copying fails, with error details in operation history.
- Embedded icon and administrator manifest, with a statically linked MSVC runtime.
- No Python installation or third-party runtime DLLs required.

## Requirements

- Windows 10 (1607 or later) or Windows 11, x64.
- Administrator permission when launching the application.
- For building: Visual Studio 2019 or 2022 with Desktop development with C++,
  a Windows SDK, and CMake 3.16 or later.

## Build

Run these commands from the repository root using a Visual Studio developer
PowerShell or command prompt. With Visual Studio 2022 and CMake 3.21 or later:

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config MinSizeRel
```

With Visual Studio 2019 and CMake 3.16 or later, use
`-G "Visual Studio 16 2019"` instead. Use a fresh build directory when changing
generators.

The output is `dist/SoftLink.exe`. Release builds enforce an executable
size below 1,000,000 bytes. The executable can be distributed on its own;
third-party notices are embedded and accessible through the system menu.

## Use

1. Launch `SoftLink.exe` and accept the administrator prompt.
2. Select the source folder and the destination parent folder.
3. Select the move-and-create-link action and review the operation result.

For example, moving `D:\Games` into `E:\Storage` creates `E:\Storage\Games`
and replaces `D:\Games` with a symbolic link to the new location. Existing
destination folders are not merged.

Operation history is saved as `operations.jsonl` beside the executable. Keep
that file with the executable if you want to retain history; it is excluded
from Git because it may contain local paths and error details.

## Data Safety

The source is renamed to a backup before copying. After copying and linking
succeed, the backup is removed. If copying only partially succeeds, the current
behavior still links the original path to the incomplete destination and
retains the backup. Review all reported errors and preserve the backup until
you have verified your files. Close applications using the source folder and
keep an independent backup of important data.

This is a folder relocation utility, not a disk-imaging or ACL backup tool.
See [implementation notes](src/README.md) for recovery behavior,
compatibility details, and suggested manual acceptance checks.

## Repository Layout

```text
CMakeLists.txt             Build configuration
SoftLink.ico               Embedded application icon
src/                      C++ sources, Windows resources, and build size check
src/third_party/          Vendored picojson header and license
```

Build outputs, Python files, local history, and the original parent repository's
Git history are not included. No build or runtime tests were run as part of
preparing this standalone repository.

## License

The original project does not specify a license for its own source code. This
standalone export does not introduce one; public availability alone does not
grant an open-source license.

The bundled picojson v1.3.0 is distributed under its
[BSD 2-clause license](src/third_party/picojson.LICENSE.txt).

## Contact

haitao@live.cn
