# Checkpoint <a href="https://github.com/FlagBrew/Checkpoint/releases/latest"><img src="https://img.shields.io/github/downloads/FlagBrew/Checkpoint/total.svg"></a>

A fast and simple homebrew save manager for 3DS and Switch written in C++.

<p align="center"><img src="https://i.imgur.com/GmXss73.jpg" />
<img src="https://i.imgur.com/Y4xJiHs.png" /></p>

## Why use Checkpoint?

Checkpoint is created following ideas of simplicity and efficiency. The UI has been designed to condense as many options as possible, while keeping it simple to work with.

Moreover, Checkpoint is extremely lightweight - while being packaged with a nice graphic user interface - and is built using the most recent libraries available.

Checkpoint for 3DS natively supports 3DS and DS cartridges, digital standard titles and demo titles. It also automatically checks and filters homebrew titles which may not have a save archive to backup or restore, which is done without an external title list and filters. For this reason, Checkpoint doesn't need constant user maintenance to retain full functionality.

Checkpoint for Switch natively supports NAND saves for the titles you have played. Title information are loaded automatically.

## Usage

You can use Checkpoint for 3DS with both cfw and Rosalina-based Homebrew Launchers. *hax-based Homebrew Launchers are not supported by Checkpoint. 

Checkpoint for Switch runs on homebrew launcher. Make sure you're running up-to-date payloads.

The first launch of the 3DS version will take considerably longer than usual (usually 1-2 minutes depending on how many titles you have installed), due to the working directories being created - Checkpoint will be significatively faster upon launch from then on.

You can scroll between the title list with the DPAD/LR and target a title with A when the selector is on it. Now, you can use the DPAD or the touchscreen to select a target backup to restore/overwrite.

## Working path

Checkpoint relies on the following folders to store the files it generates. Note that all the working directories are automatically generated on first launch (or when Checkpoint finds a new title that doesn't have a working directory yet).

### 3DS

* **`sdmc:/3ds/Checkpoint`**: root path
* **`sdmc:/3ds/Checkpoint/config.json`**: custom configuration file
* **`sdmc:/3ds/Checkpoint/saves/<unique id> <game title>`**: root path for all the save backups for a generic game
* **`sdmc:/3ds/Checkpoint/extdata/<unique id> <game title>`**: root path for all the extdata backups for a generic game

### Switch

* **`sdmc:/switch/Checkpoint`**: root path
* **`sdmc:/switch/Checkpoint/config.json`**: custom configuration file
* **`sdmc:/switch/Checkpoint/saves/<title id> <game title>`**: root path for all the save backups for a generic game

## Configuration file

You can add and toggle features to Checkpoint for 3DS by editing the **`config.json`** configuration file.

### Sample configuration file:

```
{
  "filter": [
    "0x000400000011C400",
    "0x000400000014F100"
  ],
  "favorites": [
    "0x000400000011C400"
  ],
  "additional_save_folders": {
    "0x00040000001B5000": {
      "folders": [
        "/3ds/mySaves/1B50",
        "/moreSaves"
      ]
    },
    "0x00040000001B5100": {
      "folders": [
        "/3ds/PKSM/backups"
      ]
    }
  },
  "additional_extdata_folders": {

  },
  "nand_saves": true,
  "version": 2
}
```

## Troubleshooting

Checkpoint displays error codes when something weird happens or operations fail. If you have any issues, please ensure they haven't already been addressed, and report the error code and a summary of your operations to reproduce it.

Additionally, you can receive real-time support by joining FlagBrew's Discord server (link below).

## Building

devkitARM and devkitA64 are required to compile Checkpoint for 3DS and Switch, respectively. Learn more at [devkitpro.org](https://devkitpro.org/wiki/Getting_Started). Install or update dependencies as follows.

### 3DS version

`dkp-pacman -S libctru citro3d citro2d 3ds-bzip2`

### Switch version

`dkp-pacman -S libnx switch-pkg-config switch-freetype switch-libpng switch-libjpeg-turbo switch-sdl2 switch-sdl2_image switch-sdl2_ttf`

## License

This project is licensed under the GNU GPLv3. Additional Terms 7.b and 7.c of GPLv3 apply to this. See [LICENSE.md](https://github.com/FlagBrew/Checkpoint/blob/master/LICENSE) for details.

## Credits

Even though this is the result of independent research and work, Checkpoint for 3DS couldn't be possible without J-D-K's original [JKSM](https://github.com/J-D-K/JKSM) version.

TuxSH for [TWLSaveTool](https://github.com/TuxSH/TWLSaveTool), from which SPI code has been taken.

WinterMute, fincs and [devkitPro](https://devkitpro.org/) contributors for devkitARM, devkitA64 and [dkp-pacman](https://github.com/devkitPro/pacman/releases).

Yellows8 and all the mantainers for [switch-examples](https://github.com/switchbrew/switch-examples).

[rakujira](https://twitter.com/rakujira) for the awesome Checkpoint logo.

Fellow testers and troubleshooters for their help.

---

If you like the work FlagBrew puts into this project and more others, **support FlagBrew on [Patreon](https://www.patreon.com/FlagBrew)**!

[![Discord](https://discordapp.com/api/guilds/278222834633801728/widget.png?style=banner3&time-)](https://discord.gg/bGKEyfY)
