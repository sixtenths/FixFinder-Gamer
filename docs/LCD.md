# LCD evidence and bring-up

## Reference and board observations

- The [Wayton MFC028D-BQ sheet](Wayton-MFC028D-BQ.pdf) describes a 240 x 320 normally white panel with MCU or four-wire SPI capability. Its drawing numbers 40 flex contacts. The table gives a 2.4-3.3 V module supply and identifies data pins 3-20, SDA 21, read/enable 26, write/read-write 28, D/C or serial clock 29, reset 30, IM0-IM3 at 31-34, INSEL 35, ground 36 and LED anode/cathode at 39/40. Pin 27 repeats the HSYNC label. The table omits an explicit chip-select entry, controller ID, mode strap truth table and LED current rating.
- [Photo 261](photos/261.jpg) confirms the installed module model `MFC028D-BQ` and carries the additional marking `22091`. [Photo 276](photos/276.jpg) shows the populated LCD connector with 40 visible solder contacts. PCB labels 1 and 40 sit near C5/C7 and Q1/L1, respectively. [Photo 262](photos/262.jpg) shows the separate unpopulated U5 footprint labeled 1/24 and 25/48.
- Photo 263 shows the panel flex marked CN2, with 1 and 48 printed at opposite ends. The revision string appears to read `FPC-C028D2-4RH`. The first character after `FPC-` could be G. The Wayton drawing and PCB connector agree on contact count. The electrical correspondence needs a continuity check because the installed flex carries different markings.
- A group of traces fans out from the LCD connector in photos 264 and 276. A parallel interface is a hypothesis worth checking. Q1 has G/D/S labels and sits next to the pin-40 end. It is a candidate backlight switch. FB1, L1 and the flex K/A pads give additional trace points. The gate GPIO, rail voltage and polarity are open.
- The role of U6 near the connector is unknown. NRD_E, NWR_RNW, DNC_SCL and VPP_OTP appear together in both the HX8347-D(T) and HX8347-G(T) datasheets. The panel label confirms the Wayton module model and supplies no controller marking. Once the electrical interface and mode are verified, the documented D(T) check is page 0 register `0x00`, which should return `0x47` in register-content mode. The G(T) preliminary sheet lists `0x75` in prose, while its ID register figure depicts `0x47`. A single `0x47` therefore warrants further corroboration before selecting a controller-specific power sequence. Use the measured data lanes, bus turnaround and read timing. Treat connector pin 27 as unknown until traced. [HX8347-D(T) datasheet](https://www.displayfuture.com/engineering/specs/controller/HX8347-D.pdf), [HX8347-G(T) preliminary datasheet](https://cdn-shop.adafruit.com/datasheets/HX8347-G_DS_T_preliminary_v01_100203.pdf)
- The locally installed Innova updater lists variants `0x064A` and `0x064B`. Its versioned temporary cache had no package for either model. The updater's metadata endpoint returned no firmware for both model-only queries, which supplied neither a device GUID nor a customer ID. This route yielded no stock LCD initialization to inspect.

## First continuity measurements

Use only the experimental board. Disconnect USB and the Pi leads before measuring resistance. Short the probes together first to establish the lead resistance.

| Point A | Point B | What a near-zero result establishes |
| --- | --- | --- |
| LCD connector contact 40, the bottommost solder leg by the 40 silkscreen in photo 276 | Q1 drain, the single large pad next to the D marking | Same PCB net. This supports the switched LED return hypothesis if the flex cathode is on that contact. |
| LCD connector contact 36, the fifth solder leg up from the pin-40 end in photo 276 | J5 hole 6, the GND reference from the SWD mapping | Same ground net and one anchor consistent with the Wayton table. |

Record actual ohms or OL. Check continuity to the flex K and A pads with the flex seated before assigning backlight nets. Trace the Q1 source to its reference and the gate through any resistor to a named MCU pad. Confirm the logic supply and LED circuit separately.

After establishing orientation and connector correspondence, test LCD contact 28 against U3 PD5 (package leg 119, EXMC NWE) and LCD contact 26 against PD4 (leg 118, EXMC NOE). If they match, trace PD7 (leg 123, NE0) and check the connector fan-out against EXMC data pins. Useful anchors are PD14 (leg 85, D0), PE7 (58, D4), PE11 (64, D8) and PD10 (79, D15). Record the actual connector contact for each because MCU D0 and panel DB0 may differ. If the strobes fail to match, trace their board nets before choosing GPIO or SPI. [GigaDevice LQFP144 pin table](https://www.gd32mcu.com/data/documents/datasheet/GD32F427xx_Datasheet_Rev1.9.pdf)

## V9 first hardware check

Confirm interface straps, reset, command/data, chip select, bus direction, supply and backlight circuit before driving outputs. If the controller is still unknown after wiring is confirmed, an ID transaction requires approval as a hardware-control experiment. Start the diagnostic from the V8 USBHS code path. Initialize the confirmed controller, fill a single color without allocating a framebuffer, then cycle red, green, blue, white and black. Verify HID enumeration and reset behavior before adding text or inputs.

The candidate HX8347 register layer in `firmware/lcd/hx8347.c` is ready for a verified board transport. It checks two page-zero ID reads for consistency, programs the inclusive window registers `0x02` through `0x09`, and streams RGB565 to GRAM `0x22`. The board transport is responsible for reset, power sequencing, lane mapping, chip select and bus turnaround. The register layer stays separate from the current firmware build while those details are open.

The candidate adapter in `firmware/lcd/doom_hx8347.c` sends the game's landscape output through the same GRAM window. It reuses the existing scanline and closes the transfer after the last row or the first failed write. It requires an initialized board transport and stays outside the headless build.

Run `python scripts/Check-LcdProtocol.py` with the same Unicorn and pyelftools dependencies as the DOOM smoke test. The mock bus verifies portrait and landscape bounds, all 76,800 pixels in both a color fill and a scaled indexed game frame, rejection of inconsistent and all-low/all-high ID readings, and closure of the pixel stream on a transfer failure.
