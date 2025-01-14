# Multi-GPU

## Selecting the primary GPU

When multiple GPUs are present in the same system, Mutter applies a series of
rules to select one of them as primary. However, it is possible to manually
select a different GPU by using a udev rule.

To illustrate it with an example, I'm going to make the NVIDIA GPU in my system
to be selected as the primary GPU.

Start by finding the vendor and device IDs of the target GPU:

```bash
$ lspci -nn
[...]
24:00.0 VGA compatible controller [0300]: NVIDIA Corporation AD107 [GeForce RTX 4060] [10de:2882] (rev a1)
```

In this case, we are looking for the vendor ID (10de) and the product (2882).

Next, create rule to set the desired GPU as primary. Make sure to replace the
vendor and device IDs:

```bash
$ cat /etc/udev/rules.d/61-mutter-preferred-primary-gpu.rules
SUBSYSTEM=="drm", ENV{DEVTYPE}=="drm_minor", ENV{DEVNAME}=="/dev/dri/card[0-9]", SUBSYSTEMS=="pci", ATTRS{vendor}=="0x10de", ATTRS{device}=="0x2882", TAG+="mutter-device-preferred-primary"
```

Finally, restart and check that the desired GPU was selected:

```bash
$ journalctl -b0 /usr/bin/gnome-shell
[...]
Device '/dev/dri/card1' prefers shadow buffer
Added device '/dev/dri/card1' (nouveau) using non-atomic mode setting.
Device '/dev/dri/card2' prefers shadow buffer
Added device '/dev/dri/card2' (amdgpu) using atomic mode setting.
Created gbm renderer for '/dev/dri/card1'
Created gbm renderer for '/dev/dri/card2'
GPU /dev/dri/card1 selected primary given udev rule
```
