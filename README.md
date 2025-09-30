DMD2 is an Arduino library designed as an updated replacement for the [original DMD library](http://github.com/freetronics/DMD). Both libraries are designed for use with the [Freetronics Dot Matrix Display](http://www.freetronics.com/collections/display/dmd).

**This library is currently in BETA release meaning the documentation is incomplete, and the implementation may contain bugs. If you want the stable version, please use the [original DMD library](http://github.com/freetronics/DMD) for now not DMD2.**

If you find any bugs, please use the Issues feature in github to report them or [post on the Freetronics Dot Matrix Display forum](http://forum.freetronics.com/viewforum.php?f=26).

## 2025 refresh highlights

* **ESP8266 stability** – Display refresh is now scheduled cooperatively on ESP8266 hardware rather than running the full scan from the timer0 interrupt. This keeps the WiFi stack responsive and prevents the watchdog resets that previously occurred whenever networking and DMD2 were active together.
* **Shared SPI bus friendliness** – `SPIDMD` refreshes now run inside standard Arduino SPI transactions so they can safely coexist with other peripherals that also negotiate bus ownership.
* **Direct framebuffer accessors** – New `DMDFrame::getByte()` and `DMDFrame::setByte()` helpers expose the packed row data, which is handy for bulk animation effects and font streaming without reimplementing the pixel layout logic.
* **Adjustable SPI frequency** – Sketches can customise the refresh clock via `SPIDMD::setSPIFrequency()` when working with unusually long ribbon cables or particularly fast boards.

# Changes from DMD library

The DMD2 library includes the following new features:

* Supports Arduino Due (IDE 1.5.6 or newer), as well as AVR-based Arduinos.
* Adds new "SoftDMD" support, which allows the standard DMDCon board connections to be used on all standard Arduino compatible models - including Arduino Mega/EtherMega, Arduino Due/EtherDue or Arduino Leonardo.
* Integrated timer management for simpler sketches.
* Improved performance, on an AVR-based Arduino a single DMD panel uses approximately 5-6% CPU overhead to update (including when using the SoftDMD mode rather than hardware SPI.)
* New drawString() methods accept flash strings, or the Arduino String type, directly. See the "AllDrawingOperations" example.
* New DMD_TextBox class supports automatic scrolling, and automatic `print()` interface for writing out numbers, variables, etc. See "Countdown" and "ScrollingAlphabet" examples.
* New dmd.setBrightness() call allows changing DMD brightness (no more blindingly bright displays!)
* New DMDFrame base class allows direct swapping of the DMD framebuffer, supporting double buffering operations and similar (see "GameOfLife" example.)

# Not Yet Implemented

* Test patterns

# Getting Started

The easiest way to install the DMD2 library is to use the Arduino IDE's Library Manager:

* Install Arduino IDE 1.6.5 or newer.
* Under the Sketch menu, choose Include Library -> Manage Libraries.
* After the list finishes updating, type "DMD2" in the search field at the top. The DMD2 library should appear.
* Select the latest DMD2 version from the dropdown and click "Install"

Otherwise, you can [manually install the Arduino library using the steps described here](http://www.freetronics.com/pages/how-to-install-arduino-libraries).

After the library is installed, you will find some example sketches under File -> Examples -> DMD2.

More documentation will be produced before the library reaches final release rather than Beta status. Many of the DMD2 concepts are borrowed from FTOLED library so the [FTOLED wiki](https://github.com/freetronics/FTOLED/wiki/) may be of use.

Feel free to ask [questions on the forum](http://forum.freetronics.com/viewforum.php?f=26) if things don't work.

## Sharing the SPI bus with other devices

From this release onward DMD2 wraps every panel refresh in an Arduino SPI transaction. Sketches that also talk to other SPI peripherals should continue to bracket their own transfers with `SPI.beginTransaction(...)`/`SPI.endTransaction()` so the core can coordinate safe access between devices.

If you need to slow the DMD down for long cable runs or speed it up on faster boards, call `SPIDMD::setSPIFrequency(hz)` before `begin()` – the default is 4&nbsp;MHz on AVR/ESP8266 and 4.2&nbsp;MHz on ARM boards:

```c++
SPIDMD dmd(1, 1);
dmd.setSPIFrequency(2000000); // 2 MHz for really long cables
dmd.begin();
```

# ESP8266 Support

Thanks to @h4rm0n1c there is support for DMD2 on ESP8266 using the Arduino environment. See [this comment](https://github.com/freetronics/DMD2/pull/7#issue-97282971) for an explanation of using DMD2 on ESP8266.

With the 2025 refresh the ESP8266 backend now uses the core `Ticker` scheduler to post refresh work back to the cooperative loop, keeping the WiFi stack responsive. No code changes are required in sketches; the panel will continue to refresh automatically as before, but without triggering watchdog resets when WiFi or the built-in HTTP server are active.

### Cooperative refresh and watchdog safety

* DMD refreshes now run outside of the timer ISR, so the ESP8266 watchdog is serviced while WiFi and HTTP server callbacks execute.
* The library automatically yields control via the SDK scheduler, preventing the `WDT reset` crashes that previously occurred when network traffic and panel scanning overlapped.
* Existing sketches can continue to call `dmd.loop()` from `loop()` if they need deterministic timing; the cooperative scheduler will still manage the hardware refresh cadence.

Brightness scaling is also aligned with the rest of the library: `analogWriteRange(255)` is applied once when the panel starts so the 0–255 values passed to `setBrightness()` map linearly to PWM duty cycle.

Freetronics is unable to guarantee support for DMD2 on ESP8266, but we will try and help if we can.

## Acknowledgements

* Cooperative refresh scheduling was inspired by community reports in the ESP8266 issue tracker.
* The raw framebuffer helpers are based on the excellent groundwork from [@crackwitz](https://github.com/crackwitz/DMD2)'s fork.

# About the Makefiles

You'll notice the examples directory contains some files named `Makefile`. You can ignore these if you are using the Arduino IDE.

However, if you want to use other development tools with the DMD library, the Makefiles work with with the [arduino-mk](http://www.mjoldfield.com/atelier/2009/02/arduino-cli.html) package, version 1.3.1. They may need updating to work with newer versions.
