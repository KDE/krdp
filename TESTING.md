# Testing KRDP

Use the following manual tests to ensure KRDP is working correctly following non-trivial changes, and for releases.

## Codecs

With the following environment variables against `krdpserver`, test that you can see a video stream with the KRDC flatpak:

| Environment variable                  | Codec              |
| ------------------------------------- | ------------------ |
|  -                                    | H.264 (default)    |
| `KRDP_DISABLE_H264=1`                 | Progressive        |
| `KPIPEWIRE_FORCE_ENCODER=h264_vaapi`  | H.264 via VAAPI    |
| `KPIPEWIRE_FORCE_ENCODER=libx264`     | H.264 via libx264  |
| `KPIPEWIRE_FORCE_ENCODER=libopenh264` | H.264 via openh264 |

## Clients

Test KRDP with the following RDP clients:

 - **KRDC** (flatpak) (primarily)
 - **Remmina** (flatpak)
 - Windows' **Remote Desktop Connection** (`mstsc`)
    - Use an up-to-date Windows 11 virtual machine
    - For networking to work correctly, the network adapter in VirtualBox should be set to 'Bridged Adapater' and not 'NAT'

Ensure that connections *(i.e. more than one)* are made correctly and that a video stream is being received, and the mouse is mapped correctly with multiple monitors.

## Modes

Test the following modes (against only one codec) and confirm that things behave properly, in particular: mouse positions, multi-monitor (shared/additional), and display resizing (exclusive).

| Argument                   | Mode                             |
| -------------------------- | -------------------------------- |
| `--mode RemoteAccess`      | Exclusive mode                   |
| `--mode SharedAccess`      | Shared (default)                 |
| `--mode AdditionalDisplay` | Additional display (KDE Connect) |

## Features

 - Only system user (`SystemUserEnabled=true`): Correct/wrong passwords
    - Are login attempts appropriately accepted and rejected?
 - Only non-system user: (`SystemUserEnabled=false`): Correct/wrong passwords
    - Are login attempts appropriately accepted and rejected?
 - Clipboard text
    - Is text correctly copied from the host to the server, and server to host?
    - **You must disable KDE Connect as its shared clipboard will mask failure**
 - Running in background for 3 hours
    - Has the stream degraded? Did it crash?
