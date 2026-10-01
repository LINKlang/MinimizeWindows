# MinimizeWindows

A Windows tray app that makes **Win+D** work on the monitors you choose.
Select one or more monitors to show the desktop while keeping other screens unchanged.

## Design philosophy

- Reproduce the observable behavior of Windows Show Desktop as closely as possible on selected monitors.
- Keep the app small and the implementation simple.

## Usage

1. Run `MinimizeWindows.exe` and double-click its tray icon to open Settings.
2. Open **Display > Configure**, click the monitors you want to include, then **Save**.
3. Press **Win+D** to minimize windows on those monitors. When the desktop is clear,
   press it again to restore the most recent batch. Previously minimized windows stay minimized.

Closing Settings keeps the app running. Right-click the tray icon and choose **Exit** to quit.
Save an empty selection to let Windows handle Win+D normally.

Configuration is stored in `%APPDATA%\MinimizeWindows\config.json`.

## Command line

```powershell
.\Release\MinimizeWindows.exe --list-monitors
.\Release\MinimizeWindows.exe --monitor "\\.\DISPLAY2"
.\Release\MinimizeWindows.exe --help
```

`--monitor` selects and saves a single device. Use the device name from
`--list-monitors`; use Settings to select multiple monitors.

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
