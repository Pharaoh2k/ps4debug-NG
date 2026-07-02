# ps4debug-NG - Wire Protocol & Kernel Syscall Reference

The canonical protocol specification now lives at the repository root, in a single
maintained file, to avoid the two copies drifting apart:

**➡ [../PROTOCOL.md](../PROTOCOL.md)**

It documents every command (including the Turbo Scan family `0xBDAACC10-0xBDAACC16`,
bulk write `0xBDAACC04`, FS/GS-base `0xBDBB000E/0xBDBB000F`, the assemble opcode
`0xBDAA0024`, and foreground-app `0xBDDD0006`), every packet struct, enum, status
code, and the kernel-side syscall interface.
