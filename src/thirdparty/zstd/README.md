Zstandard 1.5.7, upstream commit f8745da6ff1ad1e7bab384bd1f9d742439278e99.
Source https://github.com/facebook/zstd/tree/v1.5.7

Used under the BSD license in LICENSE. zstd.h is the upstream public header.
zstd.c was generated with the upstream build/single_file_libs tool:

    python combine.py -r ../../lib -x legacy/zstd_legacy.h -o zstd.c zstd-in.c

The generated source contains both the compressor and decompressor. Both
platforms compile the same implementation without an external runtime DLL.
