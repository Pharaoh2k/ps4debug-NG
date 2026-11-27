# LZ4 1.9.4 (block format only)

Unmodified `lib/lz4.c`, `lib/lz4.h` and `lib/LICENSE` from the LZ4 release
tag `v1.9.4` (commit `5ff839680134437dbf4678f3d0c7b371d84f4964`),
https://github.com/lz4/lz4. BSD 2-Clause license, see `LICENSE`.

| File      | SHA-256                                                            |
|-----------|--------------------------------------------------------------------|
| `lz4.c`   | `b6a85fd8f9be0fedb568abd1338719b23b999583ccda6f3404d5ae11e4ce7b8e` |
| `lz4.h`   | `c1614ecf7ada7b0be1acb560d4239595f96fbb7aa6a79a7c40cb358753830be6` |
| `LICENSE` | `8b58c446121a109ccf32edc094bba3010a3d85e4ee3702950db55e4d3e87736c` |

The debugger builds it with `LZ4_FREESTANDING=1` (no libc headers, no heap
allocation inside LZ4) and maps `LZ4_memcpy`/`LZ4_memmove`/`LZ4_memset` to the
compiler builtins, see `debugger/Makefile`. Only the compressor is linked into
the payload; clients decompress with any LZ4 block decoder.
