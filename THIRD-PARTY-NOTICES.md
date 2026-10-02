# Third-party code and references

The project source files do not contain an identified copied or vendored code file from another project. The driver implementation is specific to the Apex 3 TKL and was developed in this project.

The HID LampArray report descriptor uses the public HID Usage Tables definitions. Kernel driver lifecycle and virtual HID behavior use the documented Microsoft KMDF and Virtual HID Framework (VHF) interfaces. These are API/specification references, not source files included in this repository.

The SteelSeries report IDs/commands used to control the keyboard's eight zones were established and checked experimentally on an Apex 3 TKL. No third-party source project for those device-specific command bytes was identified in the checked-in source. If a future contributor adds code from another project, they must record the source, license, and affected files here before publishing it.

References:

- Microsoft, [Write a HID source driver by using VHF](https://learn.microsoft.com/windows-hardware/drivers/hid/virtual-hid-framework--vhf-)
- USB Implementers Forum, [HID Usage Tables](https://usb.org/document-library/hid-usage-tables-15)
- Microsoft, [Windows driver samples](https://github.com/microsoft/Windows-driver-samples) (reference material only; no file from this repository is included here)
