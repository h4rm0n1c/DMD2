DMD2 is an Arduino library designed as an updated replacement for the [original DMD library](http://github.com/freetronics/DMD). Both libraries are designed for use with the [Freetronics Dot Matrix Display](http://www.freetronics.com/collections/display/dmd).

**This library is currently in BETA release meaning the documentation is incomplete, and the implementation may contain bugs. If you want the stable version, please use the [original DMD library](http://github.com/freetronics/DMD) for now not DMD2.**

If you find any bugs, please use the Issues feature in github to report them or [post on the Freetronics Dot Matrix Display forum](http://forum.freetronics.com/viewforum.php?f=26).

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

# ESP8266 Support

Thanks to @h4rm0n1c there is support for DMD2 on ESP8266 using the Arduino environment. See [this comment](https://github.com/freetronics/DMD2/pull/7#issue-97282971) for an explanation of using DMD2 on ESP8266.

Freetronics is unable to guarantee support for DMD2 on ESP8266, but we will try and help if we can.

# About the Makefiles

You'll notice the examples directory contains some files named `Makefile`. You can ignore these if you are using the Arduino IDE.

However, if you want to use other development tools with the DMD library, the Makefiles work with with the [arduino-mk](http://www.mjoldfield.com/atelier/2009/02/arduino-cli.html) package, version 1.3.1. They may need updating to work with newer versions.

## ESP8266 note

For ESP8266 Wi-Fi/WDT concurrency analysis and optimization guidance, see `docs/ESP8266_CONCURRENCY_AUDIT.md`.


### ESP8266 optional `pin_other_cs` optimization

For dedicated DMD/P10 deployments (single display chain, no SPI-CS sharing), you can disable runtime `pin_other_cs` polling on ESP8266 to shave hot-path GPIO overhead:

- Set `DMD2_ESP8266_DISABLE_OTHER_CS_CHECK=1` at compile time.
- Leave it at the default (`0`) if your sketch relies on `setOtherCS()` arbitration behavior.


For ESP8266 builds that defer scan work from ISR, call `BaseDMD::serviceAll()` frequently from `loop()` to service pending refresh ticks in normal task context.


### ESP8266 optional cooperative yield hook

For sketches that perform heavy redraw/text scrolling while WiFiManager/captive portal is active, you can enable cooperative yielding in library hot loops:

- Set `DMD2_ESP8266_ENABLE_COOPERATIVE_YIELD=1` at compile time.
- This enables `DMD2_ESP8266_COOPERATIVE_YIELD()` (calls `yield()` on ESP8266).
- Default is disabled (`0`) to preserve maximum raw draw throughput.


### ESP8266 adaptive timer interval (optional)

The deferred-scan scheduler can adapt timer interval based on measured scan duration:

- Floor: `DMD2_ESP8266_REFRESH_US`
- Ceiling: `DMD2_ESP8266_REFRESH_MAX_US`
- Disable adaptation for deterministic fixed cadence by setting `DMD2_ESP8266_ADAPTIVE_INTERVAL=0`.
