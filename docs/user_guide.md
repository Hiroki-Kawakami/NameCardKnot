# NameCardKnot User Guide

NameCardKnot is firmware for ESP32 and ESP32-S3 devices that lets you display a name badge image on an M5Paper or PaperS3 and share name-card data between devices.

## What You Need

- An [M5Paper](https://docs.m5stack.com/en/core/m5paper) or [PaperS3](https://docs.m5stack.com/en/core/PaperS3)
- A microSD card formatted as FAT32
  - The microSD card is used to load your own name-card data and save data received from another device
  - Some microSD cards may not work reliably, depending on their type or condition

## Preparing Name-Card Data to Display and Share

Use the [NameCardKnot Editor](https://hiroki-kawakami.github.io/NameCardKnot/) to enter your name, select images, and configure the other fields. Copy the generated `.mnc.pdf` file to the microSD card.

The following fields are available:

- **Name:** The name included in the shared data
- **URL (optional):** You can include one URL in the shared data. The recipient can view it, and you can also display it as a QR code on your own device
- **Display image:** The image shown on your device. It is converted to fit within the M5Paper / PaperS3 display area, up to 540×960 pixels
- **Share image 1:** An image sent to the recipient. You can also use the display image
- **Share image 2 (optional):** A second image sent to the recipient
- **Share message (optional):** A message included in the shared data for the recipient to view

Share images are converted to a maximum of 405×720 pixels. The original image files are not included in the shared data.

You can open the generated `.mnc.pdf` file in a standard PDF viewer to check its contents. The first page contains the image displayed on the device, and the second page previews the content that will be shared.

## Installing the Firmware and Initial Setup

Download the NameCardKnot firmware for your device from M5Burner and install it on the device.

On first startup, follow the on-screen instructions to select a language and set the date and time. The date and time are used to record when received name-card data is saved.

> [!WARNING]
> If the RTC (real-time clock) contains an incorrect date and time marked as valid, the date and time setup screen may not appear on first startup.
>
> After startup, check the date in the upper-left corner of the screen. If it is incorrect, set the date and time again from Settings.

You can open name-card data directly from the microSD card. For a card you use regularly, however, we recommend importing it to the device from the “My Card” section on the Home screen.

## Automatic Power-Off and Wake-Up

To reduce power consumption, the device automatically powers off after the following periods of inactivity:

- While displaying a name card: 1 minute
- On other screens: 5 minutes

The image remains visible on the e-paper display after the device powers off. The wake-up procedure depends on the device:

- **M5Paper:** Press and hold the side button for about three seconds, until the name-card menu or Home screen appears
- **PaperS3:** Press the side button once and wait for the name-card menu or Home screen to appear. Do not press and hold or double-click the button

## Sharing and Exchanging Name-Card Data

Open the “Share” screen on the sending device and the “Receive” screen on the receiving device. Bring the two screens together, as if placing one on top of the other, and hold them in place for about two seconds.

On PaperS3, a buzzer sounds when you can separate the screens. The name-card data continues transferring after you separate the devices, so wait without operating them until the transfer finishes and the devices restart.

Normally, name-card data is sent only from the sharing device to the receiving device. To exchange cards in both directions, enable both of the following options:

- “Also receive their card in return” on the sharing device
- “Also send my card in return” on the receiving device

Received name-card data is saved in the `ReceivedCards` folder on the microSD card and can be viewed from “Gallery” on the Home screen.

## Settings

You can change the following options from Settings:

- **Date & Time:** Set the current date and time
- **Languages:** Switch the UI language between Japanese and English
- **Flip Screen:** Rotate the screen orientation by 180 degrees
