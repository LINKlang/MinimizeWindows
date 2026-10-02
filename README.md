<p align="right">
  <strong>EN</strong> | <a href="README.zh-CN.md">简</a>
</p>

<p align="center">
  <img src="assets/logo.svg" alt="MinimizeWindows logo" width="160" height="160" />
</p>

<h1 align="center">MinimizeWindows</h1>

<p align="center">
  <a href="https://github.com/LINKlang/MinimizeWindows/releases"><img src="https://img.shields.io/github/v/release/LINKlang/MinimizeWindows?label=release" alt="Release" /></a>
  <a href="https://github.com/LINKlang/MinimizeWindows/stargazers"><img src="https://img.shields.io/github/stars/LINKlang/MinimizeWindows?style=flat" alt="GitHub Stars" /></a>
  <a href="LICENSE"><img src="https://img.shields.io/badge/license-MIT-blue" alt="MIT License" /></a>
  <br>
  <a href="https://linux.do"><img src="https://img.shields.io/badge/LINUX-DO-FFB003.svg?logo=data:image/svg%2bxml;base64,DQo8c3ZnIHhtbG5zPSJodHRwOi8vd3d3LnczLm9yZy8yMDAwL3N2ZyIgd2lkdGg9IjEwMCIgaGVpZ2h0PSIxMDAiPjxwYXRoIGQ9Ik00Ni44Mi0uMDU1aDYuMjVxMjMuOTY5IDIuMDYyIDM4IDIxLjQyNmM1LjI1OCA3LjY3NiA4LjIxNSAxNi4xNTYgOC44NzUgMjUuNDV2Ni4yNXEtMi4wNjQgMjMuOTY4LTIxLjQzIDM4LTExLjUxMiA3Ljg4NS0yNS40NDUgOC44NzRoLTYuMjVxLTIzLjk3LTIuMDY0LTM4LjAwNC0yMS40M1EuOTcxIDY3LjA1Ni0uMDU0IDUzLjE4di02LjQ3M0MxLjM2MiAzMC43ODEgOC41MDMgMTguMTQ4IDIxLjM3IDguODE3IDI5LjA0NyAzLjU2MiAzNy41MjcuNjA0IDQ2LjgyMS0uMDU2IiBzdHlsZT0ic3Ryb2tlOm5vbmU7ZmlsbC1ydWxlOmV2ZW5vZGQ7ZmlsbDojZWNlY2VjO2ZpbGwtb3BhY2l0eToxIi8%2BPHBhdGggZD0iTTQ3LjI2NiAyLjk1N3EyMi41My0uNjUgMzcuNzc3IDE1LjczOGE0OS43IDQ5LjcgMCAwIDEgNi44NjcgMTAuMTU3cS00MS45NjQuMjIyLTgzLjkzIDAgOS43NS0xOC42MTYgMzAuMDI0LTI0LjM4N2E2MSA2MSAwIDAgMSA5LjI2Mi0xLjUwOCIgc3R5bGU9InN0cm9rZTpub25lO2ZpbGwtcnVsZTpldmVub2RkO2ZpbGw6IzE5MTkxOTtmaWxsLW9wYWNpdHk6MSIvPjxwYXRoIGQ9Ik03Ljk4IDcwLjkyNmMyNy45NzctLjAzNSA1NS45NTQgMCA4My45My4xMTNRODMuNDI2IDg3LjQ3MyA2Ni4xMyA5NC4wODZxLTE4LjgxIDYuNTQ0LTM2LjgzMi0xLjg5OC0xNC4yMDMtNy4wOS0yMS4zMTctMjEuMjYyIiBzdHlsZT0ic3Ryb2tlOm5vbmU7ZmlsbC1ydWxlOmV2ZW5vZGQ7ZmlsbDojZjlhZjAwO2ZpbGwtb3BhY2l0eToxIi8%2BPC9zdmc%2B" alt="LINUX DO" /></a>
</p>

A Windows app that makes **Win+D** work on the monitors you choose.
Select one or more monitors to show the desktop while keeping other screens unchanged.

## Design philosophy

- Mimic the native Windows "Show Desktop" behavior as closely as possible on selected monitors.
- Keep the app small and the implementation simple.

## Usage

1. Run `MinimizeWindows.exe` and double-click its tray icon to open Settings.
2. Open **Display > Configure**, click the monitors you want to include, then **Save**.
3. Press **Win+D** to minimize windows on those monitors. When the desktop is visible, press it again to restore the most recent batch. Previously minimized windows stay minimized.

Closing Settings keeps the app running. Right-click the tray icon and choose **Exit** to quit.
Save an empty selection to let Windows handle Win+D normally.

Configuration is stored in `%APPDATA%\MinimizeWindows\config.json`.

## Command line

```powershell
.\MinimizeWindows.exe --list-monitors
.\MinimizeWindows.exe --monitor "\\.\DISPLAY2"
.\MinimizeWindows.exe --help
```

`--monitor` selects and saves a single device. Use the device name from
`--list-monitors`; use Settings to select multiple monitors.

## Roadmap

- [ ] Improve detection and handling of special windows.
- [ ] Configurable keyboard shortcuts.

## Build

Requires Visual Studio 2022 with the v143 C++ toolset, Windows 10 SDK, and C++ ATL.
WTL and nlohmann/json are included in `third_party`.

Open **Developer PowerShell for VS 2022**, change to the repository root, and run:

```powershell
msbuild .\MinimizeWindows.sln -m -p:Configuration=Release -p:Platform=x86
```

The executable is written to `Release\MinimizeWindows.exe`.
Use `Configuration=Debug` for a build in `Debug\`.

## Tests

```powershell
powershell.exe -NoProfile -File .\tests\run_tests.ps1
```

## Acknowledgements

Thanks to [deadem/minimize-windows](https://github.com/deadem/minimize-windows) for the inspiration.
Its simple approach and readable code were a pleasure to explore and helped shape this project.
