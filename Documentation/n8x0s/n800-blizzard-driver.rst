Nokia N800 Blizzard framebuffer driver
======================================

This driver is a Barebox port of Nokia's patched RX-34 Linux 2.6.21 OMAPFB,
RFBI and Epson S1D1374x display path.  See ``n800-display-source-audit.rst``
for the source mapping and the explicitly documented Barebox adaptations.

The current driver is a NOLO-assisted implementation.  It preserves the
initialization behavior verified on real N800 hardware by displaydiag V1.14.
It is not yet a NOLO-independent cold-init implementation.
