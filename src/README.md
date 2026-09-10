# SoftLink

Windows 10 (1607 or later) / Windows 11, x64. Win32 UI with a statically
linked MSVC runtime. Python and third-party runtime DLLs are not required.
This standalone repository contains only the C++ version. References to
the Python version below describe compatibility with the original project.

## Build

Requirements: Visual Studio 2019 Build Tools with the C++ toolchain and Windows
SDK, plus CMake 3.16 or later. Run from the repository root:

```powershell
cmake -S . -B build -G "Visual Studio 16 2019" -A x64
cmake --build build --config MinSizeRel
```

Output: `dist/SoftLink.exe`. The release build fails if the EXE reaches
1,000,000 bytes. Icons, the administrator manifest, and third-party notices are
embedded. The system menu includes a third-party license viewer.

## Compatibility

- Windows requests administrator access before starting the program.
- The C++ and Python versions use the same single-instance mutex. Close the
  running version before opening another one.
- `operations.jsonl` stays beside the executable, independent of the current
  working directory. Carry it with the EXE to retain history. The JSON field
  names, status values, and error details remain readable by the Python version.
- The interface retains folder selection, background migration, history and
  record details, clear output, usage instructions, and `haitao@live.cn`.
- The progress bar is empty when idle, and shows a moving indicator only during
  an operation. It does not claim a byte percentage.
- The source is first renamed in place to `.__softlink_backup_<timestamp>`, then
  copied to `B/<source-folder-name>`. A directory symbolic link replaces the
  original path. The backup is removed only after copying and linking succeed.
- As in the Python version, partial copying still links the original path to the
  incomplete target, retains the backup, and reports every copy error in history.
  If target linking fails, the program attempts to link back to the backup.
- Target entries are not merged with pre-existing directories. Copy and cleanup
  failures caused by access/sharing errors receive bounded retries.

## Implementation Details

- Link creation uses `CreateSymbolicLinkW`, without launching `cmd.exe`.
- A writable history file is opened before directory changes begin.
- Moving the running executable's containing directory is rejected. Target
  containment checks resolve existing junctions and symbolic links.
- Nested symbolic links and junctions are copied as links. Unsupported reparse
  providers are reported as copy errors, preserving the backup. Cleanup does not
  descend into reparse points.
- `CopyFileW` copies files; directory timestamps and ordinary attributes are
  preserved. This is not a disk-imaging or ACL backup tool.
- The redundant "restart as administrator" button is omitted, since the
  executable requires elevation before opening its window.

## Validation Scope

Build and executable-resource inspection do not establish that all filesystem
workflows have passed runtime testing. No GUI or live migration tests were run
for the initial delivery, following the user's instruction.

For subsequent acceptance testing, use disposable folders and check:

1. Successful same-volume and cross-volume migration, file contents, links,
   backup cleanup, history details, and idle progress.
2. Rename failure with an open handle denying deletion: the source must remain
   intact and no copy or cleanup may begin.
3. Locked files and disk-full copy failures: retain the original backup and show
   the complete copy-error list, including the partial-target warning.
4. Target-link and backup-cleanup failures: verify fallback access and retained
   data, along with the recorded recovery paths.
5. Chinese names, spaces, long paths, read-only files, empty directories, relative
   symbolic links, and junctions. Never delete the external target of a link.
6. Old Python JSONL history, a damaged log line, an unwritable log directory, and
   launch from a different working directory.
7. Cancelled elevation, simultaneous Python/C++ launch, resizing, keyboard
   focus, high-DPI displays, and attempts to close during migration.

## Third-Party Code

`third_party/picojson.h` is the unmodified picojson v1.3.0 header from
<https://github.com/kazuho/picojson/tree/v1.3.0>, under its BSD 2-clause license.
The full notice is embedded in the EXE and retained beside the vendored header.
