# Rovari SDK

A hardware verified peripheral layer for WCH RISC-V microcontrollers.

Apache 2.0. Five chip families. Written and tested on real silicon by [Armstrong Subero](https://rvembedded.com).

> **Beta.** This is an early release, published ahead of Rovari Studio so the code is available to people working with these chips today. The API is stable enough to build on but may change before 1.0. Coverage is uneven across targets, and the table below says exactly where.

## What this is

WCH ships an EVT (a vendor HAL) for every chip. It works, and the heavy stacks in it are the only implementations that exist for USB, Ethernet, and the other complex peripherals. It is also inconsistent between peripherals, thinly documented in English, and unpleasant to read.

The Rovari SDK is a clean layer **on top of** the EVT, not a replacement for it. You get a coherent API across five chips, and the vendor stacks stay reachable underneath whenever you need them.

```c
#include "rovari.h"

void app_init(void)
{
    pin_mode(PC1, Output);
}

void app_run(void)
{
    pin_toggle(PC1);
    delay_ms(500);
}
```

One header. `app_init` runs once, `app_run` loops. The real `main()` lives in the SDK and handles clock setup, tick init, and C++ constructor invocation, so a beginner never sees it and a professional can ignore the wrapper and call the HAL directly.

## What this is not

* Not a replacement for the WCH EVT. It sits on it.
* Not a build system. Today the source lists live in Rovari Studio. See [Using it now](#using-it-now).
* Not a minimal register access library. If that is what you want, use [ch32fun](https://github.com/cnlohr/ch32fun), which is excellent and solves a different problem.

## Coverage

| | CH32V003 | CH32V203 | CH32V307 | CH32H417 | Baochip1x |
|---|---|---|---|---|---|
| SDK lines | 28,933 | 5,135 | 12,741 | 9,714 | 4,090 |
| Peripheral modules | 16 | 14 | 20 | 21 | 9 |
| Device drivers | 3 | 2 | 12 | 10 | 3 |
| SEVS runtime | yes | not yet | yes | not yet | yes |
| Vendored stacks | | | FatFs, FreeRTOS, LVGL, lwIP, Helix | FatFs, TJpgDec | FatFs |

### Peripherals by target

**CH32V003** adc, button, capture, comparator, exti, gpio, i2c, pwm, sd, servo, spi, tick, timer, tone, uart, wdg

**CH32V203** adc, capture, crc, exti, flash, gpio, i2c, pwm, rtc, spi, tick, timer, uart, wdg

**CH32V307** adc, capture, crc, dac, dma, exti, flash, gpio, i2c, pwm, rng, rtc, sdcard, spi, st7796s, tick, timer, touch, uart, wdg

**CH32H417** adc, capture, crc, dac, dma, ecdc, exti, gpio, i2c, lptim, opa, psram, pwm, rng, rtc, spi, swi2c, tick, timer, touch, uart, wdg

**Baochip1x** adc, button, gpio, i2c, pwm, spi, tick, tone, uart

### Device drivers

SSD1306 OLED, ST7796S TFT, FT6336U capacitive touch, SD card over both SDIO and SPI with FatFs diskio glue, Ethernet, NeoPixel, and Petit FatFs on the V003.

## CH32H417 dual core

The H417 is an AMP part. The V3F core starts on cold reset and the V5F core must be woken by V3F firmware. The SDK handles this properly rather than pretending the chip is single core:

* `Link_v3f.ld` and `Link_v5f.ld`, separate linker scripts per core
* `startup_ch32h417_v3f.S` and `startup_ch32h417_v5f.S`
* `rovari_main.c` and `rovari_main_v5f.c`, one bootstrap per core

If you are building on the H417 and want the dual core bring up sequence, that is the part worth reading first.

## Baochip1x

The Baochip target is included because the SDK was built for [bunnie Huang's](https://www.bunniestudios.com) Precursor silicon as a separate deliverable. It is a VexRiscv part, not a WCH one, and the same API shape works there.

## SEVS

Sources carry requirement tags (`@req REQ-ROVARI-CORE-0010`) and assertions through a runtime called SEVS, the Subero Embedded Verification Standard, which synthesises practice from JPL, NASA, and DO-178B. Each target reserves the top 256 bytes of SRAM for a crash record.

SEVS is live on V003, V307, and Baochip1x. It is not yet wired on V203 or H417. Those two build and run normally; they simply do not carry the assertion runtime yet.

## Using it now

There is no standalone build system in this release. That is the main thing beta means here. Two paths work today:

**Lift what you need.** The drivers are self contained C. Copy `rovari_spi.c`, `rovari_i2c.c`, or whichever module you want into your own project alongside the WCH EVT and build it however you already build. This is the fastest path if you have a working toolchain and just want tested peripheral code.

**Wait for Rovari Studio.** The IDE bundles this SDK with a toolchain, one click build and flash, and a source level debugger. It handles the source lists, linker scripts, and startup files for you. Coming shortly after this release.

A CMake path and PlatformIO board definitions are planned so the SDK is consumable without the IDE. If either of those matters to you, open an issue and say so; it moves them up.

## Layout

```
targets/<CHIP>/
  vendor/       WCH EVT, unmodified, Apache 2.0
  rovari/       SDK layer
    drivers/    device drivers
  thirdparty/   FatFs, LVGL, lwIP, FreeRTOS, and so on
  startup.S
  link.ld
```

## Chips not yet covered

CH32V103 has a linker script and startup only. No SDK layer yet.

## Licence

Apache License 2.0. Every source file carries an SPDX header. The vendored WCH EVT is Apache 2.0 from WCH and is included unmodified.

Use it in commercial products. Attribution appreciated, not required.

## Books

Written against this SDK, and the reason the API looks the way it does:

* **RISC-V Assembly for Embedded Systems**, free, CH32V003, 263 pages, 30 labs
* **Practical Control Systems with RISC-V**, CH32V003, 185 pages, 25 projects
* **The Dabao Book**, available now at [rvembedded.com](https://rvembedded.com)

## Contact

Issues and pull requests welcome. Hardware quirks in particular: if you hit something on these chips that is not in any datasheet, file it and it goes in the notes.

[rvembedded.com](https://rvembedded.com)
