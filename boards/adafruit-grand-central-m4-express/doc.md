@defgroup    boards_adafruit-grand-central-m4-express Adafruit Grand Central M4 Express
@ingroup     boards
@brief       Support for the Adafruit Grand Central M4 Express

General information
===================

@image html https://cdn-learn.adafruit.com/assets/assets/000/068/748/medium800/adafruit_products_grand_central_top_angle.jpg "Picture of the Adafruit Grand Central M4 Express"

The main features of the board are:
- ATSAMD51 Cortex M4 running at 120 MHz
- Hardware DSP and floating point support
- 1 MiB Flash
- 256 KiB RAM
- external 8 MB QSPI Flash storage
- 32-bit, 3.3V logic and power
- Micro SD Card slot connected to SPI
- native USB

Pinout
------

@image html https://cdn-learn.adafruit.com/assets/assets/000/115/366/original/adafruit_products_Adafruit-Grand-Central-M4-Express-Pinout.png "Official Pinout of the Grand Central M4 Express"

Links
=====

- [Overview](https://learn.adafruit.com/adafruit-grand-central)
- [Schematics](https://learn.adafruit.com/assets/69175)

Flash the board
===============

The board is flashed using its on-board
[boot loader](https://github.com/adafruit/uf2-samdx1).

The process is automated in the usual `make flash` target.

If RIOT is already running on the board, it will automatically reset the CPU
and enter the bootloader. If some other firmware is running or RIOT crashed,
you need to enter the bootloader manually by double tapping the board's reset
button.

Readiness of the bootloader is indicated by LED pulsing in red.

### Accessing STDIO

The usual way to obtain a console on this board is using an emulated USB serial
port.
