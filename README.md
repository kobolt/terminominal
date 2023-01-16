# Terminominal
VT100 terminal emulator for Raspberry Pi Pico.

Features:
* Composite video (CVBS) PAL video output.
* PS/2 protocol input, with Norwegian or US keyboard layout.
* Visible picture of 880 dots and 240 scanlines, framed by border.
* Custom 11x10 pixel font, ISO-8859-1 (latin-1) compatible.
* UART baud rate up to 115200 supported.
* Passes some [vttest](https://invisible-island.net/vttest/) cases at least.
* SDL-based Linux version available for test purposes.
* Blinking cursor!

## GPIO Connections
```
|--------|-----------|------------|-------------------------|
| Pin No | Pin Name  | Function   | Connected To            |
|--------|-----------|------------|-------------------------|
| 1      | GP0       | UART TX    |                         |
| 2      | GP1       | UART RX    |                         |
| 6      | GP4       | PS/2 Data  | 3.3V<->5V Level Shifter |
| 7      | GP5       | PS/2 Clock | 3.3V<->5V Level Shifter |
| 21     | GP16      | CVBS DAC   | 680 Ohm Resistor        |
| 22     | GP17      | CVBS DAC   | 220 Ohm Resistor        |
| 36     | 3V3 (OUT) | +3.3V      | 3.3V<->5V Level Shifter |
| 40     | VBUS      | +5V        | 3.3V<->5V Level Shifter |
|--------|-----------|------------|-------------------------|
```

## Compiling
Requirements:
* CMake
* ARM GCC toolchain
* [Pico SDK](https://github.com/raspberrypi/pico-sdk)

Create a build folder and call cmake pointing to the source directory containing the CMakeLists.txt file:
```
mkdir build
cd build
PICO_SDK_PATH=/path/to/pico-sdk cmake /path/to/terminominal/
make
```

Flash the resulting "terminominal.elf" file with SWD or transfer the "terminominal.uf2" file through USB in BOOTSEL mode.

## Further Reading
Information on my blog:
* [VT100 Terminal Emulator on Raspberry Pi Pico](https://kobolt.github.io/article-198.html)

YouTube video:
* [Terminominal running cmatrix](https://www.youtube.com/watch?v=movhRMJprEs)

