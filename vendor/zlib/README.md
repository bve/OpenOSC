# zlib

The in-memory deflate and inflate sources are from [zlib 1.4](https://github.com/madler/zlib/tree/99623ef7097352da7993ba614161fd6a311e046e).
OpenOSC builds this subset with `Z_SOLO` and `NO_GZIP`.
The PNG codec supplies the allocator and computes PNG chunk CRCs using OpenOSC's CRC-32.
See [LICENSE](LICENSE).
