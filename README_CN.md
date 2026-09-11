# SoftLink

[English](README.md) | 简体中文

SoftLink 是一款使用 C++17 和 Win32 界面开发的轻量级 Windows 文件夹迁移工具。
它将文件夹迁移到其他位置，并在原路径创建目录符号链接，让应用程序仍可通过原路径访问文件。

通俗地说，就是把文件夹搬到其他磁盘，同时在原位置保留一个指向新位置的入口。

本仓库提供 SoftLink 的独立 C++ 版本。

## 功能

- 选择原目录与目标目录，在后台执行迁移并显示操作输出。
- 通过 Windows API 创建目录符号链接。
- 查看操作历史、记录详情和错误信息。
- 复制失败时保留原目录备份，便于恢复数据。
- 内嵌程序图标和管理员权限清单，静态链接 MSVC 运行库。
- 运行时不需要安装 Python 或附带第三方运行库 DLL。

## 环境要求

- Windows 10（1607 或更新版本）或 Windows 11，x64。
- 启动程序时需要授予管理员权限。
- 编译需要 Visual Studio 2019 或 2022，安装“使用 C++ 的桌面开发”工作负载和 Windows SDK。
- CMake 3.16 或更新版本；使用 Visual Studio 2022 生成器时需要 CMake 3.21 或更新版本。

## 编译

在仓库根目录打开 Visual Studio 开发者 PowerShell 或开发者命令提示符。

Visual Studio 2022：

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config MinSizeRel
```

Visual Studio 2019：

```powershell
cmake -S . -B build -G "Visual Studio 16 2019" -A x64
cmake --build build --config MinSizeRel
```

两种方式任选其一。切换生成器时，请使用新的构建目录。

生成的程序为 `dist/SoftLink.exe`。发布构建要求可执行文件小于 1,000,000 字节，
超过限制时构建会报告失败。程序可以作为单个 EXE 分发，第三方许可声明已经嵌入，
可以通过窗口系统菜单查看。

## 自动构建与发布

使用 PowerShell 一条命令生成 EXE、包含说明文档和第三方许可的 ZIP，以及 SHA-256 校验文件：

```powershell
./scripts/build-release.ps1 -Generator "Visual Studio 16 2019"
```

不传生成器参数时默认使用 Visual Studio 2022。产物位于 `dist/v0.0.1/`。
切换生成器时，请使用新的 `build/release` 构建目录。

将工作流和版本改动提交到 GitHub 后，推送对应版本 tag：

```powershell
git push origin main
git tag v0.0.1
git push origin v0.0.1
```

GitHub Actions 会在 Windows 2022 环境自动编译，并创建附带 EXE、ZIP 和校验文件的
**Release 草稿**。在 Releases 页面检查草稿后点击发布即可。
也可以从 Actions 页面手动执行构建；手动执行仅上传构建附件，不创建 Release。

后续发布时，先同步修改 `CMakeLists.txt` 的项目版本和 `src/SoftLink.manifest` 的版本，
再提交并创建对应 tag。版本不一致时脚本会停止，避免给错误版本打包。
这套自动化负责构建打包，不包含实际目录迁移测试。

## 使用方法

1. 启动 `SoftLink.exe`，同意管理员权限请求。
2. 选择需要迁移的原目录 A。
3. 选择存放该目录的目标父目录 B。
4. 点击“移动并建立软链接”，等待完成并检查操作结果。

例如，将 `D:\Games` 迁移到目标父目录 `E:\Storage`：

```text
迁移前：D:\Games
迁移后：E:\Storage\Games
原路径：D:\Games -> E:\Storage\Games
```

目标位置如果已经存在同名目录，程序不会将两者合并。

操作历史保存在 EXE 同目录下的 `operations.jsonl` 中，与启动时的工作目录无关。
需要保留历史时，请随 EXE 一起保留该文件。历史中可能包含本机路径和错误详情，
因此 `.gitignore` 已将其排除。

## 数据安全与恢复

程序先将原目录重命名为备份，再复制到目标位置。复制和链接创建成功后，才会删除备份。

**当前行为：如果仅部分文件复制成功，程序仍会将原路径链接到不完整的目标目录，
同时保留原目录备份。** 遇到这种情况，请检查全部错误信息；确认文件完整并完成恢复前，
不要删除备份。如果目标链接创建失败，程序会尝试让原路径链接回备份目录。

迁移前请关闭正在使用原目录的应用程序，并为重要数据保留独立备份。
本工具用于文件夹迁移，不是磁盘镜像或完整的文件权限（ACL）备份工具。

更多兼容性说明、恢复行为和手动验收建议见[实现说明（英文）](src/README.md)。

## 目录结构

```text
CMakeLists.txt             CMake 构建配置
SoftLink.ico               内嵌程序图标
README.md                  英文说明
README_CN.md               中文说明
src/                      C++ 源码、Windows 资源和体积检查脚本
src/third_party/          picojson 头文件与许可证
```

仓库不包含 Python 程序、构建产物或本机操作历史。本次独立整理及命名调整未执行编译或运行测试。

## 许可证

原项目尚未为自身源码指定许可证，本仓库也未新增主项目许可证。
公开源码并不自动等同于授予开源许可。

随源码提供的 picojson v1.3.0 使用 [BSD 2-clause 许可证](src/third_party/picojson.LICENSE.txt)。

## 联系方式

haitao@live.cn
