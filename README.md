# Checkpoint

A fast and simple save manager for cfw/rosalina-based Homebrew Launchers written in C++.

![](https://i.imgur.com/Nttk8hX.png)

## Why use Checkpoint?

Checkpoint is created with the ideas of simplicity and efficiency. The UI has been designed to condense as many options as possible, while keeping it simple to work with.

Moreover, Checkpoint is extremely lightweight and is built using very few (and up-to-date) libraries. It contains minimal assets, while being packaged with a nice graphic user interface.

Checkpoint supports DS cartridges, normal titles, and demos. It also automatically checks and filters homebrew titles which may not have a save archive to backup or restore, which is done without an external title list and filters. For this reason, Checkpoint doesn't need constant user maintenance to retain full functionality.

## Working path

Checkpoint uses the following folders to store the files it generates. Note that all the working directories are automatically generated on first launch (or when Checkpoint finds a new title that doesn't have a working directory yet).

* **`sdmc:/3ds/Checkpoint`**: root path
* **`sdmc:/3ds/Checkpoint/saves/<unique id> <game title>`**: root path for all the save backups for a generic game
* **`sdmc:/3ds/Checkpoint/extdata/<unique id> <game title>`**: root path for all the extdata backups for a generic game

## Usage

You can use Checkpoint with both cfw and Rosalina-based Homebrew Launcher. *hax-based Homebrew Launchers are not supported by Checkpoint.

The first launch will take considerably longer than usual (usually 1-2 minutes depending on how many titles you have installed), due to the working directories being created - Checkpoint will be significatively faster upon launch from then on.

You can scroll between the title list with the DPAD/LR and target a title with A when the selector is on it. Now, you can use the DPAD or the touchscreen to select a target backup to restore/overwrite.

## Issues

Checkpoint displays error codes when something weird happens or operations fail. If you have any issues, please ensure they haven't already been addressed, and report the error code and a summary of your operations to reproduce it.

## Building

Checkpoint uses [latest libctru](https://github.com/smealum/ctrulib), [latest citro3d](https://github.com/fincs/citro3d) and [latest pp2d](https://github.com/BernardoGiordano/Checkpoint/tree/master/source/pp2d).

## License

This project is licensed under the GNU GPLv3. See [LICENSE.md](https://github.com/BernardoGiordano/Checkpoint/blob/master/LICENSE) for details.

## Credits

Even though this is the result of independent research and work, this couldn't be possible without J-D-K's [JKSM](https://github.com/J-D-K/JKSM), which is an incredible piece of software that you should all be using. Best regards JK, hope you're fine.

TuxSH for [TWLSaveTool](https://github.com/TuxSH/TWLSaveTool), from which SPI code has been taken.
