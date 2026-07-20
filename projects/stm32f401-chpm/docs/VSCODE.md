# VSCode 开发环境

本工程以 VSCode 作为日常开发界面。Keil uVision 不需要打开；编译任务仅在后台调用已安装的 Keil ARMClang 工具链，不执行下载或烧录。

## 打开工程

请在工程根目录打开 `CHPM.code-workspace`。该工作区使用相对路径，可在任意
本地目录中使用。

也可以直接用 VS Code 打开 `projects/stm32f401-chpm`，但使用工作区文件可以
保证窗口标题和工程级配置一致。

工程使用 Microsoft C/C++ 扩展完成跳转和补全。工作区已关闭另一个 clangd 语言服务，避免两个索引器同时提供跳转、诊断和补全。

首次打开后，等待状态栏的 C/C++ 索引完成。随后可使用：

- `F12`：跳转到定义
- `Alt+F12`：内联查看定义
- `Shift+F12`：查找引用
- `Ctrl+T`：按符号搜索

如果旧缓存仍导致跳转异常，执行命令面板中的 `C/C++: Reset IntelliSense Database`，然后执行 `Developer: Reload Window`。

## 编译和测试

- `Ctrl+Shift+B`：ARMClang 全量编译，只生成固件，不烧录
- `Terminal -> Run Task -> CHPM: Static validation`：静态工程检查
- `Terminal -> Run Task -> CHPM: Host tests`：主机端协议测试

编译任务会在后台调用 `scripts\build_keil.ps1`。它使用 uVision 的命令行构建入口读取现有 `F4.uvprojx`，但不会打开 Keil 编辑器，也没有下载参数。

## 配置同步原则

`.vscode\c_cpp_properties.json` 中的宏定义和头文件路径与 `MDK-ARM\F4.uvprojx` 保持一致。以后增加源码目录或编译宏时，两处必须一起更新，否则固件可能可以编译，但 VSCode 会出现红线或无法跳转。
