Nokia N800 RX-34 display port audit
===================================

Reference
---------

The hardware reference for this driver is Nokia's RX-34 Linux 2.6.21 tree with
``kernel-source-rx-34_2.6.21.0-osso71.diff`` applied.  The relevant source
files are::

  arch/arm/mach-omap2/board-n800.c
  drivers/video/omap/omapfb_main.c
  drivers/video/omap/dispc.c
  drivers/video/omap/rfbi.c
  drivers/video/omap/blizzard.c

The NOLO-assisted initial RFBI/Blizzard sequence is additionally constrained by
the real-hardware ``displaydiag`` V1.14 result.  V1.14 is the known-good
initialization reference and is not replaced by a speculative cold-init path.

Audit rule
----------

Register sequences, timing equations, controller synchronization and ordering
are ported from RX-34.  A difference is allowed only where Linux infrastructure
has no Barebox equivalent.  Such differences are listed below.

DISPC
-----

``omap2-dispc.c`` contains the GFX/RGB565 subset of RX-34 ``dispc.c`` needed by
the N800:

* ``enable_rfbi_mode(1)`` is retained exactly.  This is also the transition
  verified by displaydiag V1.14 on the real N800.
* the external-mode divisor, GFX FIFO threshold, TFT mode, FRAME_ONLY load
  mode and 16-bit LCD data-line configuration are retained;
* GFX ``_setup_plane()`` uses the RX-34 color code, 8x32 burst, LCD channel,
  BA0, position, size and row-increment equations;
* LCD size and LCD output enable use the RX-34 register fields.

The destructive DISPC soft reset from ``omap_dispc_init()`` is intentionally
not performed.  This is the one initialization exception: V1.14 established a
working NOLO handoff and the project explicitly requires that known-good
handoff to be preserved.  A future NOLO-independent cold-init path is separate.

RFBI
----

``omap2-rfbi.c`` ports RX-34 ``rfbi.c``:

* RFBI reset, SYSCONFIG, CONFIG0, DATA_CYCLE1 and CS0 selection;
* timing conversion and programming;
* 8/16-bit bus-cycle selection;
* command/data/read cycles;
* tear-sync setup and enable/disable;
* ``rfbi_transfer_area()`` ordering: LCD size, PIXEL_CNT, RFBI enable/internal
  trigger, then DISPC LCD output enable;
* transfer stop clears RFBI CONTROL bit 0.

RX-34 completes transfers from the DSS IRQ and invokes ``rfbi_dma_callback()``.
Barebox has no equivalent N800 DSS IRQ integration in this port.  The only
semantic substitution is polling the same DISPC FRAME_DONE status.  Barebox
pollers are called while waiting so the RETU watchdog remains serviceable.

The RX-34 ``rfbi_setup_tearsync()`` source computes updated CONFIG0 polarity
bits but does not write the computed value back.  The Barebox port preserves
that source behavior rather than silently correcting it.

Blizzard
--------

``n800-blizzard-core.c`` ports the N800-relevant RGB565 path from RX-34
``blizzard.c``:

* PLL lock check and Blizzard clock calculation;
* S1D13744/S1D13745 revision detection;
* N800 ``te_connected = 1`` behavior from ``board-n800.c``;
* ``setup_tearsync()`` including display timing register reads, transfer-rate
  calculation and NDISP control programming;
* ``blizzard_wait_line_buffer()`` before every update.  As in RX-34, its
  30-ms timeout reports the condition and then continues;
* the non-tearsync update branch disables RFBI and Blizzard tear sync;
* RGB565 input/output window programming follows ``set_window_regs()``;
* S1D13745 uses ``BLIZZARD_SRC_WRITE_LCD_DESTRUCTIVE`` and S1D13744 uses
  ``BLIZZARD_SRC_WRITE_LCD``;
* bus width is switched to 16 bits immediately before ``transfer_area()``.

Barebox framebuffer integration
-------------------------------

Barebox uses a shadow framebuffer by default.  Before a damage transfer, only
the damaged rows are copied from the shadow buffer to the DMA-visible buffer.
This is Barebox integration and has no RX-34 hardware-register equivalent.
The physical DMA buffer is then passed to the RX-34 GFX plane setup equation.

No driver-specific RETU watchdog servicing is present.  Watchdog servicing is
left to Barebox pollers; the display hardware code has no dependency on RETU.
