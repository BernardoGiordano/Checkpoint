# Checkpoint

A fast and simple homebrew save manager for 3DS and Switch written in C++.

## Why use Checkpoint?

Checkpoint is created following ideas of simplicity and efficiency. The UI has been designed to condense as many options as possible, while keeping it simple to work with.

Moreover, Checkpoint is extremely lightweight - while being packaged with a nice graphic user interface - and is built using the most recent libraries available.

Checkpoint for 3DS natively supports 3DS and DS cartridges, digital standard titles and demo titles. It also automatically checks and filters homebrew titles which may not have a save archive to backup or restore, which is done without an external title list and filters. For this reason, Checkpoint doesn't need constant user maintenance to retain full functionality.

Checkpoint for Switch natively supports NAND saves for the titles you have played. Title informations are loaded automatically.

## Working path

Checkpoint relies on the following folders to store the files it generates. Note that all the working directories are automatically generated on first launch (or when Checkpoint finds a new title that doesn't have a working directory yet).

### 3DS

* **`sdmc:/3ds/Checkpoint`**: root path
* **`sdmc:/3ds/Checkpoint/saves/<unique id> <game title>`**: root path for all the save backups for a generic game
* **`sdmc:/3ds/Checkpoint/extdata/<unique id> <game title>`**: root path for all the extdata backups for a generic game

### Switch

* **`sdmc:/switch/Checkpoint`**: root path
* **`sdmc:/switch/Checkpoint/saves/<title id> <game title>`**: root path for all the save backups for a generic game

## Usage

You can use Checkpoint for 3DS with both cfw and Rosalina-based Homebrew Launchers. *hax-based Homebrew Launchers are not supported by Checkpoint. 

Checkpoint for Switch only runs on homebrew launcher. You're required to reboot your console after restoring a save through Checkpoint for Switch.

The first launch will take considerably longer than usual (usually 1-2 minutes depending on how many titles you have installed), due to the working directories being created - Checkpoint will be significatively faster upon launch from then on.

You can scroll between the title list with the DPAD/LR and target a title with A when the selector is on it. Now, you can use the DPAD or the touchscreen to select a target backup to restore/overwrite.

## Troubleshooting

Checkpoint displays error codes when something weird happens or operations fail. If you have any issues, please ensure they haven't already been addressed, and report the error code and a summary of your operations to reproduce it.

Additionally, you can receive real-time support by joining PKSM's discord server.

[![Discord](https://discordapp.com/api/guilds/278222834633801728/widget.png?style=banner3&time-)](https://discord.gg/bGKEyfY)

## Building

Checkpoint for 3DS relies on [latest libctru](https://github.com/smealum/ctrulib) and [latest citro3d](https://github.com/fincs/citro3d).

Checkpoint for Switch relies on [latest libnx](https://github.com/switchbrew/libnx).

## License

This project is licensed under the GNU GPLv3. Additional Terms 7.b and 7.c of GPLv3 apply to this. See [LICENSE.md](https://github.com/BernardoGiordano/Checkpoint/blob/master/LICENSE) for details.

## Credits

Even though this is the result of independent research and work, Checkpoint for 3DS couldn't be possible without J-D-K's [JKSM](https://gbatemp.net/threads/release-jks-savemanager-homebrew-cia-save-manager.413143/), which is an incredible piece of software that you should all be using. Best regards JK, hope you're fine.

TuxSH for [TWLSaveTool](https://github.com/TuxSH/TWLSaveTool), from which SPI code has been taken.

All the maintainers for [nx-hbmenu](https://github.com/switchbrew/nx-hbmenu), for all the Switch rendering functions.

Yellows8 and all the mantainers for [switch-examples](https://github.com/switchbrew/switch-examples).

Hikari-chin and all the other testers for their help with testing.

If you like my work, **support me on [Patreon](https://www.patreon.com/bernardogiordano)**!
