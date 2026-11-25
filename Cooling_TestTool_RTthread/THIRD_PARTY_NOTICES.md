# Third-party notices

The RBL CRC32 compatibility behavior in `common/fota_crc.c` was derived from
the Apache-2.0 licensed `rt_fota_crc.c` implementation in the RT-FOTA project:

- Project: `spunky_973/rt-fota`
- Copyright: RT-Thread Development Team and RT-FOTA contributors
- License: Apache License 2.0

The implementation in this repository was rewritten as a small streaming,
table-free variant for the Bootloader while retaining RBL-compatible results.
