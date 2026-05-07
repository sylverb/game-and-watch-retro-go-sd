# Toolchain Setup Scripts for Windows

This repository contains scripts to automate the installation and removal of a development environment using MSYS2 (not WSL).

### What's included
The setup installs and configures the following:
* **Package Management:** Winget (used to fetch Python 3.12, Git, and MSYS2).
* **Compilers & Build Tools:** MSYS2 with GCC, Make, and SDL2.
* **ARM Toolchain:** Downloads and installs the ARM GNU Toolchain (none-eabi).
* **Environment Config:** * Prepends toolchain binaries to the System PATH.
    * Creates symbolic links for `make` and `python3`.
    * Removes the default PowerShell `wget` alias to prioritize the CLI tool.

### Usage
1.  **Install:** Run `install.bat`. It will request administrative privileges to modify the System PATH and install packages.
2.  **Uninstall:** Run `uninstall.bat` to remove the installed Winget packages, the ARM Toolchain, and clean up the MSYS2 directories and PATH entries.

*Note: Restart your terminal after installation for PATH changes to take effect.*

Once installed, you can build the project like normal. 
