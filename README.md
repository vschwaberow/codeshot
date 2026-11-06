# codeshot

Turns code (incl. 68k, x86, x64, 6510, 65816 assembler) into a shareable PNG with syntax highlighting. Paste or open a file, then save the image or copy it to the clipboard. The PNG can carry a window frame: Omarchy (accent border, the default), Windows, macOS, Atari TOS, Amiga OS 1.3, Amiga OS 3.x, or none.

    codeshot [file]

| Key | Action |
| --- | --- |
| Esc / Ctrl+L | Leave / focus the editor |
| Space, Ctrl+Shift+C | Copy image |
| Ctrl+S | Save PNG to ~/Pictures |
| L | Next language |
| W | Next window frame |
| ? | Shortcuts |
| Q, Ctrl+Q | Quit |

Build with `bin/build`, test with `bin/test`, install with `bin/install`.
