# libfvad source import

This directory contains a source import from
https://github.com/dpirch/libfvad at commit
`532ab666c20d3cfda38bca63abbb0f152706c369`.

Only the public header and source files required by the fixed-frame VAD are
included. The upstream `LICENSE` and `PATENTS` grant are distributed with the
import; build-system files, examples, and tests are omitted.

Three reviewed safety and portability corrections are applied to the imported
source. Their exact scope and rationale are recorded in `PATCHES.md`. All other
imported source files remain unchanged from the pinned commit. See `LICENSE`,
`PATENTS`, `PATCHES.md`, and the SDK-level `THIRD_PARTY_NOTICES.md`.
