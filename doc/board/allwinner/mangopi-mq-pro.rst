.. SPDX-License-Identifier: GPL-2.0+

MangoPi MQ Pro
=============

The MangoPi MQ Pro uses an Allwinner D1 with a T-Head C906 CPU.
The firmware boots through SPL, OpenSBI and U-Boot.
The board configuration enables SD, USB host and EFI boot.

Build
-----

Use a RISC-V cross compiler and an OpenSBI generic fw_dynamic binary::

    make CROSS_COMPILE=riscv64-linux-gnu- mangopi_mq_pro_defconfig
    make CROSS_COMPILE=riscv64-linux-gnu- OPENSBI=/path/to/fw_dynamic.bin

OpenSBI starts at 0x40000000. U-Boot starts at 0x4a000000.
The output is u-boot-sunxi-with-spl.bin.

SD layout
---------

Keep the existing partition table.
Write the firmware at sector 256, before the first partition.
The first partition must start at sector 8192 or later::

    dd if=u-boot-sunxi-with-spl.bin of=/dev/sdX bs=512 seek=256 conv=notrunc,fsync

The SD card must contain an EFI bootloader or an extlinux configuration.

Device trees
------------

U-Boot uses the upstream MangoPi MQ Pro device tree.
The firmware adds the machine timer required by OpenSBI.
The firmware disables Wi-Fi SDIO storage scanning.
Linux must load its separate device tree to enable Wi-Fi.

The NS16550 clock option is enabled only in this board configuration.
The configured DRAM parameters target the 1 GiB MQ Pro board.
