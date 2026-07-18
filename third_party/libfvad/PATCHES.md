<!-- SPDX-License-Identifier: Apache-2.0 -->

# Local libfvad patches

The bundled source is based on libfvad commit
`532ab666c20d3cfda38bca63abbb0f152706c369`. Moonlight applies only these
reviewed corrections:

1. `src/vad/vad_core.c`: replace an intentionally overflowing signed multiply
   with an explicit modulo-2^32 implementation. This preserves the original
   two's-complement result without C signed-overflow undefined behavior.
2. `src/vad/vad_sp.c`: stop the expired-value shift at index 14 so it never
   reads element 16 from a 16-element channel window.
3. `src/signal_processing/resample_by_2_internal.c`: replace two left shifts
   of potentially negative PCM samples with range-equivalent multiplication,
   avoiding C shift undefined behavior while preserving the Q15 conversion.

These patches do not change the public libfvad API, frame sizes, VAD modes, or
model constants.
