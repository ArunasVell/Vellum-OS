// XTEINK X4 Pro board profile.
//
// Pin map from the FreeInk SDK bring-up notes (MIT), confirmed there on
// hardware unless marked otherwise:
// https://github.com/Free-Ink/freeink-sdk/blob/main/docs/xteink-x4pro-support.md
#pragma once

#include "vellum/hal.h"

namespace x4pro {

// Display (SSD1677 / UC8179 / UC8279 depending on batch), 800x480, SPI write-only.
constexpr int kEpdSclk = 12;
constexpr int kEpdMosi = 11;
constexpr int kEpdCs = 13;
constexpr int kEpdDc = 18;
constexpr int kEpdRst = 14;
constexpr int kEpdBusy = 6;  // busy level depends on the controller
constexpr int kPanelWidth = 800;
constexpr int kPanelHeight = 480;

// Buttons: digital, active low, internal pull-up. GPIO0 is a boot strap pin.
constexpr int kBtnPrev = 0;
constexpr int kBtnNext = 7;
constexpr int kBtnPower = 3;

// Shared I2C bus: GT911 touch 0x5D, BM8563 RTC 0x51, CW2017 fuel gauge 0x63.
constexpr int kI2cSda = 39;
constexpr int kI2cScl = 38;
constexpr int kI2cHz = 400000;
constexpr int kTouchInt = 10;
constexpr int kTouchRst = 4;
constexpr int kTouchPowerN = 2;  // active low

// Frontlight PWM, active high.
constexpr int kLightCool = 8;
constexpr int kLightWarm = 9;
constexpr int kLightFreqHz = 25000;

// SD card over native SDMMC, 1-bit.
constexpr int kSdClk = 41;
constexpr int kSdCmd = 42;
constexpr int kSdD0 = 40;
constexpr int kSdPowerN = 5;  // active low

constexpr int kPeripheralRail = 1;  // held high by stock firmware
constexpr int kChargeStatus = 21;   // high while charging

}  // namespace x4pro

// Brings up the board and returns it. Call once from app_main.
vellum::Board& board_x4pro_init();
