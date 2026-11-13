# Shelly EM System Tray Monitor

A lightweight Windows system tray application written in C that displays real-time power measurements from a **Shelly EM (Gen 1)** device. 

The application draws a custom 2-line icon directly in the Windows system tray, allowing you to monitor your home energy consumption and solar production at a glance without opening any browser.

<img width="274" height="47" alt="immagine" src="https://github.com/user-attachments/assets/ed9413c6-f4c0-4154-9589-402f6b92a399" />

## Features

- **Dual Channel Monitoring**: Displays both channels of the Shelly EM simultaneously.
- **Dynamic Color Coding**: The color of Channel 1 changes based on the power flow direction.
- **Real-time Updates**: Polls the Shelly device via HTTP every 3 seconds.
- **Native UI**: Built with Win32 API and GDI+ (no external dependencies required at runtime).
- **Quick Access Menu**: Right-click the tray icon to open Shelly Cloud, access the local web interface, or exit the app.

## Hardware Setup & Channels

This software is designed to work with the **Shelly EM (Gen 1)** device and its two dedicated amperometric clamps:

- **Channel 1 (CH1)**: This clamp should be installed on the main grid line. It measures the power drawn from the grid (import) or fed back into the grid (export).
- **Channel 2 (CH2)**: This clamp is intended for the Photovoltaic (PV) system. It measures the solar power production.

### Color Coding (CH1)
The color of the top row (Channel 1) automatically changes based on the measured value:
- 🟢 **Green**: Negative value (power is being **exported** to the grid / immissione).
- 🟠 **Orange**: Positive value (power is being **imported** from the grid / prelievo).
- 🔵 **Light Blue**: Channel 2 (PV Production) is always displayed in light blue.

## Configuration

Before compiling, you must configure the local IP address of your Shelly EM device. 

Open the `shellyem.c` file and locate the following line:

```c
static const wchar_t* SHELLY_URL        = L"http://192.168.1.11/status";
static const wchar_t* SHELLY_LOCALE_URL = L"http://192.168.1.11";
