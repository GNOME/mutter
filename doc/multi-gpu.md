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

## Copy modes

Mutter composites the buffer to be displayed on all displays on the primary GPU,
regardless of which GPU the display is connected to.

Therefore, when a display is connected to a secondary GPU, the contents to be
displayed on that display need to be copied from the primary GPU to the
secondary GPU.

There are 3 copy modes available:

 - Secondary GPU copy mode: The copy is performed by the secondary GPU. This is
   the default copy mode.
 - Zero-copy mode: The primary GPU exports a framebuffer and the secondary GPU
   imports it. This mode is tried if the secondary GPU copy mode fails.
 - Primary GPU copy mode: The primary GPU copies its contents to a dumb buffer
   and the secondary GPU scan-outs from it. First, the GPU is used to perform
   the copy and, if it fails, the copy is perform by the CPU. This mode is used
   if the zero-copy mode fails.

For debug purposes, it is possible to force the copy mode by setting the
environment variable `MUTTER_DEBUG_MULTI_GPU_FORCE_COPY_MODE` to `zero-copy`,
`primary-gpu-gpu` or `primary-gpu-cpu`.
