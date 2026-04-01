# Monolith - an operating system in one file :D

Yes, it IS two files.  

However, the .sh is for convenience.

## ok but how does it work
`kernel.c` contains everything: the kernel, the linker script, and the GRUB config, etc. etc.
The build script uses `sed` to extract those embedded sections into temporary files (`linker.ld` and `grub.cfg`) during the build. These extracted files are generated artifacts, not source, they hold the same weight as .o files.

The actual source of the operating system is still **one file.**
