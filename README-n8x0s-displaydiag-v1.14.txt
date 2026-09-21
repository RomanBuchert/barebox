N8x0s Barebox displaydiag V1.14

Command:
  displaydiag nokia-init-v114

Changes from V1.13:
- Performs Nokia's DISPC external/RFBI-mode transition before rfbi_init().
- Logs DISPC_CONTROL and RFBI_CONTROL before and after that transition.
- Preserves unrelated NOLO-initialized DISPC state rather than blindly resetting DISPC.
- Retains PRCM clock reconstruction and calculated RFBI timings.
- Retains the hard stop at an unlocked Blizzard PLL_DIV.
- Retains the final controlled RGB565 validation transfer after successful identification.
