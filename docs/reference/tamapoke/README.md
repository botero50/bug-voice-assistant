# TamaPoke reference (not built here)

Copied from https://github.com/botero50/TamaPoke (MIT, see LICENSE in this folder) so the game rules and their test cases are kept next to Bug:

- `arcade.h`: the rules of Merge, Brick Bash, Marble Roll and the runner as pure functions.
- `arcade_test.cpp`, `games46_test.cpp`: their test cases. They need TamaPoke's emulator (`tools/emu`), so they do not run here;
  the Merge cases were rewritten for C in `server/skills_test/games_test.c` and pass.

The rest of the 60+ test files in TamaPoke's `tools/emu/tests` are about the pet, battles, saves and the installer, which Bug does not have.
