# 3dsp-usb (Linux 6.12 port)

Driver for 3DSP/Syntek combo (WiFi 802.11g + Bluetooth 2.0+EDR) wireless
adapters, originally released by 3DSP Corporation (~2009) and cleaned up by
Marcel Holtmann (https://github.com/holtmann/3dsp-usb).

**This fork ports the driver to modern kernels (tested on
`6.12.115+deb13.1-amd64`, Debian 13) and Bluetooth WORKS end to end on the
outbound path**: `hci0` comes up, BlueZ registers the controller,
over-the-air device discovery succeeds, **pairing works** and **A2DP audio
streaming works** (verified with PulseAudio against a real A2DP sink).

Tested hardware: Syntek `05e1:0100` (3DSP BlueW-231U, STK9100 + STK9230),
mini-PCIe card presenting as USB, found in ~2008-2012 laptops.

## Status

| Part | State |
|---|---|
| `3dspusbbus.ko` (bus) | builds and loads |
| `3dspusbbt.ko` (Bluetooth) | builds, loads, **functional**: hci0 + BlueZ + pairing + A2DP audio out |
| `3dspusbwlan.ko` (802.11g) | builds and loads; netdev bring-up untested |

Known limitations:

- **Incoming discoverability does not work**: the DSP baseband never
  transmits inquiry-scan (FHS) responses. Everything else about inquiry
  works (the adapter can discover other devices and can be *paged*, i.e.
  outbound connections work). Root cause: the DSP emits periodic
  interrupts of **type `0x40`** (~1 Hz) that this open driver does not
  know (its `USB_INTERRUPT_TYPE` table stops at `0x18`); the handler for
  that interrupt type lives in the proprietary `3dspusbbtpriv` blob of
  the original release (binary, kernel 2.6/3.x only). Also, in combo
  mode the 8051 never answers the BT driver's `MAILBOX_CMD_REQUEST_TO_JOIN`
  (driver side retries 5x then fails with STEP=6).
- One benign `WARNING` remains: `memcpy: detected field-spanning write`
  at `bluetooth/btusb.c` `RegApi_Fill_Cmd_Body()` (2-byte write into a
  1-byte field, vendor protocol quirk).
- `RegAPI_SendTo_MailBox`/`VendorCmdWriteCmdToMailBox` report several
  "Too much retries" messages during startup: they are throttling, the
  operations eventually succeed.

## What was ported (from kernel ~2.6.32/3.x era)

1. `-Werror=date-time`: `__DATE__/__TIME__` in build strings → `-Wno-date-time`.
2. `unlocked_ioctl` prototype changed from `int` to `long` (`bus/busioctl.c`).
3. Old timer API (`init_timer()`, `timer_list.data`, `function(ulong)`) →
   `timer_setup()` / `from_timer()` (`wlan/tdsp_timer.c`, `bluetooth/bt_*`).
4. `do_gettimeofday`/`struct timeval`/`jiffies_to_timeval` →
   `ktime_get_coarse_real_ts64()`/`timespec64`, `msecs_to_jiffies()`.
5. Hand-rolled `struct {timer, work}` for delayed work → real
   `INIT_DELAYED_WORK()` (`bt_pnp.c`, `bt_task.c`).
6. `get_fs()`/`set_fs(KERNEL_DS)` file reads (removed in 5.10) →
   `kernel_read()` (`wlan/tdsp_file.c`, `bluetooth/btdownload.c`).
7. `hci_dev.dev_type`/`HCI_BREDR` (removed) → `hdev->bus = HCI_USB` only.
8. Kernel fortify: `copy_from_user()` into a flexible-array-member struct
   overflowed → bounded heap buffer in the bus `PLUGIN_HARDWARE` ioctl.
9. **The big one (BT)**: vendor command transfer buffers were
   stack-allocated; modern USB core refuses to DMA-map stack memory and
   fails with `-EAGAIN`. `UsbVendorRequest()` now stages every transfer
   through a `kmalloc()`ed buffer (and uses a real 10 s timeout instead
   of `0`).
10. **The same bug in the WLAN driver**: `wlan/tdsp_usb.c`
    `tdsp_usb_ctrl_msg()` had `timeout=0` and passed stack buffers;
    without this fix the WLAN child cannot initialize and the combo
    join is impossible (`-EAGAIN` floods from `UsbDev_BuildVendorRequest`).
11. Missing `<linux/vmalloc.h>` includes.

## New userspace helper: `3dsp_plug`

The bus driver was designed to be driven by the (lost) "3DSP USB Manager"
application: after `insmod`, nothing happens until somebody opens
`/dev/tdspbus` and issues `IOCTL_BUSENUM_PLUGIN_HARDWARE` for each child
(BT = serial 1, WLAN = serial 2). `3dsp_plug.c` implements that, and
replicates the original manager's startup sequence (reverse engineered
from the `uwbtool` sources of the full 3DSP release):

```sh
gcc -O2 -o 3dsp_plug 3dsp_plug.c
./3dsp_plug bt       # BT only: hotkey=1, enum_mode=3, plug serial 1
./3dsp_plug          # combo: hotkey, enum_mode=3, plug WLAN first, then BT
```

Notes discovered by trial and error:

- `IOCTL_BUSENUM_SET_HOTKEY_FLAG=1` makes the driver write the "radio
  fully enabled" value (`0x5A`) instead of `0x55` into register `0x11C`.
- `IOCTL_BUSENUM_WRITE_ENUM_MODE=3` selects the coexist code variant.
- The 8051 MCU inside the chip keeps running across module
  load/unload: to get a fresh firmware download, power-cycle the USB
  device (`USBDEVFS_RESET` after unbinding the drivers), otherwise the
  BT driver skips the download and tries a mailbox join instead.
- With WLAN plugged first, the WLAN driver performs the DSP join; the
  BT driver then requests its own join (`ComboState=SINGLE`) which the
  8051 does not answer (see limitation above).

## Build

```sh
make KERNELDIR=/lib/modules/$(uname -r)/build
```

Cross-building for another machine works too: install the target kernel's
exact headers and point `KERNELDIR` at them.

## Install / run (target machine)

```sh
cp 3dspusbbus.ko 3dspusbbt.ko 3dspusbwlan.ko /opt/3dsp/
cp 3dspcode.bin /lib/firmware/          # firmware blob (2.8 MB, in-repo)
modprobe bluetooth
insmod 3dspusbbus.ko
insmod 3dspusbbt.ko
insmod 3dspusbwlan.ko

MAJOR=$(grep 3dspusbbus /proc/devices | awk '{print $1}')
mknod /dev/tdspbus c $MAJOR 0
./3dsp_plug bt                          # firmware download + hci0 up

hciconfig hci0                          # → UP RUNNING, real BD address
bluetoothctl scan on                    # → finds devices over the air
bluetoothctl connect XX:XX:...          # → pairing + A2DP works
```

## License

Same as the upstream 3DSP release (GPL-2.0 statement in sources; parts of
the original release were closed/binary blobs — this repo contains only the
openable source files as in upstream holtmann/3dsp-usb).
