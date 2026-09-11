# Firmware updates

## First installation on the N32R16V board

The first installation must be a complete wired flash, because it creates the
new 32 MB partition table, OTA metadata and 4 MB factory image:

```powershell
.\tools\build.ps1
.\tools\build.ps1 -IdfArguments @('-p','COM12','flash')
```

Do not try to migrate the old 16 MB board or partition layout by uploading only
an application binary. A full flash is also the recovery procedure if OTA
metadata becomes damaged.

## Normal web update

1. Build with `.\tools\build.ps1`.
2. Connect to `WALL-E-Reborn` and open `http://192.168.4.1`.
3. In **Firmware update**, select
   `build/walle_reborn.bin` and confirm installation.
4. Keep logic and actuator power stable until the page reports installation.
5. Reconnect after the automatic restart and verify the displayed version and
   normal status before placing the tracks on the floor.

The upload is written directly to the inactive slot. Motion commands are
blocked and track PWM is forced to zero. `esp_ota_end()` validates the ESP-IDF
image before it becomes the next boot target. The new image starts in pending
verification state and becomes permanent only after the robot's essential
services initialize. A crash or reset before that point causes rollback to the
previous valid slot.

## Trust boundary

The current updater is intended for the robot's local provisioning AP or trusted
home network. Browser writes require the device key and the active control
lease, but HTTP is not encrypted, so do not expose the endpoint to another
network. OTA images are signed and verified by firmware before boot selection;
the private signing key must stay backed up and offline. Hardware Secure Boot
eFuses are intentionally not burned, so USB recovery remains possible.
