// The X4 Pro's SD card: native SDMMC in 1-bit mode, FAT mounted at /sdcard.
#pragma once

constexpr const char* kSdMountPoint = "/sdcard";

// Powers the card slot and mounts it. Returns false when there is no card or
// it cannot be read after a few attempts.
bool mount_sd_card();
