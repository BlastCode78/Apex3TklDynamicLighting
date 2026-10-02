# Apex 3 TKL Dynamic Lighting filter

Free and open source under the MIT License. See [LICENSE](LICENSE).

KMDF HID upper filter for the SteelSeries Apex 3 TKL lighting interface (`HID\VID_1038&PID_1622&MI_01`). It exposes a Windows LampArray device through VHF and translates Windows LampArray color updates to the keyboard's eight physical lighting zones.

This is an experimental, test-signed driver for this specific keyboard and Windows setup. It is not Microsoft-certified. Loading it requires Windows Test Mode and administrator rights. It is separate from the user-mode WPF application in the `Apex3TklColorControl` repository.

## Build

Install Visual Studio with the Windows Driver Kit (WDK), open `Apex3TklDynamicLighting.slnx`, restore the NuGet packages listed in `packages.config`, then build **Debug | x64**. The build generates the package consumed by the installer:

```text
x64\Debug\Apex3TklDynamicLighting\Apex3TklDynamicLighting.inf
```

The Debug package is test-signed. Its use requires Windows Test Mode. Keep Test Mode enabled while the project driver is installed.

## Test Mode

Run these commands in an elevated PowerShell or Command Prompt window. Restart Windows after either command for the change to take effect.

Enable Test Mode:

```powershell
bcdedit.exe /set testsigning on
```

Disable Test Mode **only after restoring the Microsoft driver** with the restore script below:

```powershell
bcdedit.exe /set testsigning off
```

Do not turn Test Mode off while this test-signed project driver is installed; Windows may refuse to load it. If enabling Test Mode is blocked by Secure Boot policy, stop before changing UEFI settings.

## Install for continued use

Open PowerShell as administrator, change to the extracted project folder, then run:

```powershell
Set-Location "C:\path\to\Apex3TklDynamicLighting"
powershell.exe -NoProfile -ExecutionPolicy Bypass -File ".\installa-apex-persistente.ps1"
```

The installer checks Test Mode and the device's initial state, installs the built package, restarts the HID interface, then verifies the filter and virtual HID device. It leaves the package installed when those checks pass and attempts to remove it if verification fails.

To restore the Microsoft HID driver, run this command from the same elevated PowerShell window:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File ".\ripristina-apex-microsoft.ps1"
```

Confirm that the physical interface reports `Status: OK` and its active driver is `input.inf`. After that, you may disable Test Mode and restart.

## Verified on this PC

- Windows 11 Pro, build 26100, with Test Mode enabled.
- The filter starts successfully and creates its VHF LampArray HID child.
- Windows Dynamic Lighting's static color palette changes all eight keyboard zones.
- The chosen color remained configured across restart and full power-off/on while the project filter was installed.
- The restore script removed the project package and returned the physical interface to Microsoft `input.inf`, with `Status: OK`.
- The persistent installer subsequently installed package `oem37.inf`; the physical interface is `Status: OK`, its stack contains `Apex3TklDynamicLighting`, `vhf`, and `HidUsb`, and the VHF HID children are present with `Status: OK`.

The Debug package created by Visual Studio is test-signed and intended for development. This project does not include a Microsoft production signature or a certificate that would make it load outside Test Mode.

## Archive contents

## Source origins and dependencies

No source file from another project is vendored in this repository. The KMDF filter and SteelSeries report handling are project-specific implementation. The HID LampArray report descriptor follows the public HID Usage Tables and LampArray report model; VHF/KMDF calls use Microsoft's documented WDK interfaces. The report command values for the eight physical zones were validated experimentally against an Apex 3 TKL; they are not copied from a third-party source file.

References:

- [Microsoft: Write a HID source driver by using VHF](https://learn.microsoft.com/windows-hardware/drivers/hid/virtual-hid-framework--vhf-)
- [USB-IF: HID Usage Tables](https://usb.org/document-library/hid-usage-tables-15)
- [Microsoft: Windows driver samples](https://github.com/microsoft/Windows-driver-samples)

External build requirements: Visual Studio with the C++ driver workload, Windows SDK/WDK, and the pinned NuGet packages listed in `packages.config`. The `packages`, `.vs`, `x64`, and `Apex3Tkl.C349C0F1` folders are generated or cached and should not be committed. Keep `packages.config` so NuGet can restore dependencies when building.
