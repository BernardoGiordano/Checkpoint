# Checkpoint

A fast and simple save manager for cfw/rosalina written in C++.

![](https://i.imgur.com/dl6ihmC.png)

## Why using Checkpoint?

Checkpoint is created with the idea of simplicity and efficiency. The UI has been designed to condense more option as possible but keeping it simple to do operations with.

Moreover, Checkpoint is really lightweight and it's built using very few (and up-to-date) libraries. It also doesn't have any assets in it, while being packaged with a nice graphic user interface.

Checkpoint supports normal titles and demos. It also automatically checks and filters homebrew titles which may not have any save archive to backup or restore: this is done without the need of external lists of titles to filter needing to be updated periodically with new titles. For this reason, Checkpoint doesn't need the user maintenance to always be fully functional.

## Working path

Checkpoint uses the following folders to store the files it generates. Note that all the working directories are automatically generated on first launch (or when Checkpoint finds a new title that doesn't have a working directory yet).

* **`sdmc:/Checkpoint`**: root path
* **`sdmc:/Checkpoint/saves/<game title>`**: root path for all the save backups for a generic game
* **`sdmc:/CHeckpoint/extdata/<game title>`**: root path for all the extdata backups for a generic game

## Usage

You can use Checkpoint with both cfw and rosalina-based Homebrew Launcher. *hax-based Homebrew Launcher entrypoint is not supported by Checkpoint.

The first launch will take a long amount of time (usually 2-3 minutes depending on how many titles you have installed), due to the working directories being created. The next time you launch Checkpoint it will be significatively faster.

You can scroll between the title list with the DPAD/LR and target a title with A when the selector is on it. Now, you can use the DPAD or the touchscreen to select a target backup to restore/overwrite.

## Issues

Checkpoint displays error codes when something weird happens or operations fail. Report back the error code and a summary of your operations to reproduce it if the issue hasn't been discussed in the past ones yet.

## Building

Checkpoint uses [latest libctru](https://github.com/smealum/ctrulib), [latest citro3d](https://github.com/fincs/citro3d) and [latest pp2d](https://github.com/BernardoGiordano/pp2d).

## License

This project is licensed under the GNU GPLv3. See [LICENSE.md](https://github.com/BernardoGiordano/Checkpoint/blob/master/LICENSE) for details.

## Credits

Even though this is the result of independent research and work, this couldn't be possible without J-D-K's [JKSM](https://github.com/J-D-K/JKSM), which is an incredible piece of software and you all should be using it. Best regards JK, hope you're fine.