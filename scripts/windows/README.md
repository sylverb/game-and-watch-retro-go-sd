# Toolchain Setup Scripts for Windows

Scripts to automate the installation and removal of a Windows build environment.
The installer supports both MSYS2 and WSL workflows — you'll be prompted to choose during setup.

### What's included

- **Python 3.12 and Git** via Winget, with a `python3` symlink created automatically.
- **ARM GNU Toolchain** (arm-none-eabi), downloaded and installed silently.
- **MSYS2** (optional) with GCC, Make, SDL2, and binutils, plus a `make` symlink.
- **System PATH** entries for all of the above.

### Usage

1. **Install:** Run `install.bat` — it will request admin privileges, then walk you through setup.
2. **Uninstall:** Run `uninstall.bat` — each component is confirmed interactively before removal.

Restart your terminal after installation for PATH changes to take effect.

### Editing the PowerShell scripts on Linux

The `.ps1` scripts be pure ASCII to simplify working on them on POSIX systems. 

Windows PowerShell 5.1 (the default) reads files as Windows-1252 unless a UTF-8 BOM is present, 
so any non-ASCII character — including Unicode dashes in comments — will corrupt the file from 
PowerShell's perspective.

If you edit these on Linux, avoid copy-pasting anything that might introduce smart quotes,
em-dashes, or other Unicode. Stick to plain `-` for separators and `'` for apostrophes.

To check a file for non-ASCII bytes before committing:

```sh
grep -Pn '[^\x00-\x7F]' scripts/windows/*.ps1
```

If you need Unicode and want to add the UTF-8 BOM in vim:

```vim
:set bomb
:w
```

Or add this to `~/.vimrc` to apply it automatically to all `.ps1` files:

```vim
autocmd BufNewFile,BufRead *.ps1 setlocal fileencoding=utf-8 bomb
```
