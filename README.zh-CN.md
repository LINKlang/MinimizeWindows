<p align="right">
  <a href="README.md">EN</a> | <strong>简</strong>
</p>

<p align="center">
  <img src="assets/logo.svg" alt="MinimizeWindows 标志" width="160" height="160" />
</p>

<h1 align="center">MinimizeWindows</h1>

<p align="center">
  <a href="https://github.com/LINKlang/MinimizeWindows/releases">
    <img src="https://img.shields.io/github/v/release/LINKlang/MinimizeWindows?label=release" alt="Release" />
  </a>
  <a href="https://github.com/LINKlang/MinimizeWindows/stargazers">
    <img src="https://img.shields.io/github/stars/LINKlang/MinimizeWindows?style=flat" alt="GitHub Stars" />
  </a>
  <a href="LICENSE">
    <img src="https://img.shields.io/badge/license-MIT-blue" alt="MIT License" />
  </a>
  <br>
  <a href="https://linux.do">
    <img src="https://img.shields.io/badge/LINUX-DO-FFB003.svg?logo=data:image/svg%2bxml;base64,DQo8c3ZnIHhtbG5zPSJodHRwOi8vd3d3LnczLm9yZy8yMDAwL3N2ZyIgd2lkdGg9IjEwMCIgaGVpZ2h0PSIxMDAiPjxwYXRoIGQ9Ik00Ni44Mi0uMDU1aDYuMjVxMjMuOTY5IDIuMDYyIDM4IDIxLjQyNmM1LjI1OCA3LjY3NiA4LjIxNSAxNi4xNTYgOC44NzUgMjUuNDV2Ni4yNXEtMi4wNjQgMjMuOTY4LTIxLjQzIDM4LTExLjUxMiA3Ljg4NS0yNS40NDUgOC44NzRoLTYuMjVxLTIzLjk3LTIuMDY0LTM4LjAwNC0yMS40M1EuOTcxIDY3LjA1Ni0uMDU0IDUzLjE4di02LjQ3M0MxLjM2MiAzMC43ODEgOC41MDMgMTguMTQ4IDIxLjM3IDguODE3IDI5LjA0NyAzLjU2MiAzNy41MjcuNjA0IDQ2LjgyMS0uMDU2IiBzdHlsZT0ic3Ryb2tlOm5vbmU7ZmlsbC1ydWxlOmV2ZW5vZGQ7ZmlsbDojZWNlY2VjO2ZpbGwtb3BhY2l0eToxIi8%2BPHBhdGggZD0iTTQ3LjI2NiAyLjk1N3EyMi41My0uNjUgMzcuNzc3IDE1LjczOGE0OS43IDQ5LjcgMCAwIDEgNi44NjcgMTAuMTU3cS00MS45NjQuMjIyLTgzLjkzIDAgOS43NS0xOC42MTYgMzAuMDI0LTI0LjM4N2E2MSA2MSAwIDAgMSA5LjI2Mi0xLjUwOCIgc3R5bGU9InN0cm9rZTpub25lO2ZpbGwtcnVsZTpldmVub2RkO2ZpbGw6IzE5MTkxOTtmaWxsLW9wYWNpdHk6MSIvPjxwYXRoIGQ9Ik03Ljk4IDcwLjkyNmMyNy45NzctLjAzNSA1NS45NTQgMCA4My45My4xMTNRODMuNDI2IDg3LjQ3MyA2Ni4xMyA5NC4wODZxLTE4LjgxIDYuNTQ0LTM2LjgzMi0xLjg5OC0xNC4yMDMtNy4wOS0yMS4zMTctMjEuMjYyIiBzdHlsZT0ic3Ryb2tlOm5vbmU7ZmlsbC1ydWxlOmV2ZW5vZGQ7ZmlsbDojZjlhZjAwO2ZpbGwtb3BhY2l0eToxIi8%2BPC9zdmc%2B" alt="LINUX DO" />
  </a>
</p>

一款 Windows 工具，让 **Win+D** 只影响你选择的显示器
支持选择一个或多个显示器返回桌面，其他屏幕的窗口保持不变

## 设计理念

- 在所选显示器上尽可能还原 Windows 原生“显示桌面”的行为
- 保持程序小巧、实现精简

## 使用

1. 运行 `MinimizeWindows.exe`，双击托盘图标打开设置窗口
2. 进入 **Display > Configure**，点击需要参与 Win+D 的显示器，再点击 **Save**
3. 按 **Win+D** 最小化所选屏幕上的窗口；所选屏幕处于桌面状态时，再按一次恢复最近一批由本程序最小化的窗口，原先已经最小化的窗口不会被恢复

关闭设置窗口后，程序继续在后台运行。右键点击托盘图标，选择 **Exit** 退出程序
如果不选择任何显示器，Win+D 将恢复为 Windows 的默认行为

配置文件保存在 `%APPDATA%\MinimizeWindows\config.json`

## 命令行

```powershell
.\MinimizeWindows.exe --list-monitors
.\MinimizeWindows.exe --monitor "\\.\DISPLAY2"
.\MinimizeWindows.exe --help
```

`--monitor` 用于选择并保存单个目标设备。设备名以 `--list-monitors` 的输出为准；选择多个显示器请使用设置窗口

## Roadmap

- [ ] 改进对特殊窗口的识别与处理
- [ ] 支持自定义快捷键

## 构建

需要 Visual Studio 2022、v143 C++ 工具集、Windows 10 SDK 和 C++ ATL

WTL 和 nlohmann/json 已包含在 `third_party` 中

打开 **Developer PowerShell for VS 2022**，切换到项目根目录后执行：

```powershell
msbuild .\MinimizeWindows.sln -m -p:Configuration=Release -p:Platform=x86
```

生成的程序位于 `Release\MinimizeWindows.exe`
使用 `Configuration=Debug` 可构建到 `Debug\` 目录

## 测试

```powershell
powershell.exe -NoProfile -File .\tests\run_tests.ps1
```

## 致谢

灵感来自 [deadem/minimize-windows](https://github.com/deadem/minimize-windows)
原项目简洁的思路和易读的代码让人赏心悦目，也直接启发了本项目的诞生，非常感谢原作者
