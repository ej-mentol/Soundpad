# Third-party notices

| Component | Location | License | Local changes |
|-----------|----------|---------|---------------|
| minimp3 | `src/minimp3.h` | CC0 1.0 (public domain dedication) | 9 lines: macro arguments parenthesised (`PEEK_BITS`, `VSAVE2`, `VSAVE4`), static-analysis marker `//-V::all` at the top |

No other third-party code is bundled. Voice encoding (Opus) is owned by
VoiceExtension, which is distributed separately; its headers and static
library are not part of this plugin.
