---
id: play-games
title: Play games with CherryPi
---

Once you've compiled CherryPi (Instructions for [Linux](https://torchcraft.github.io/torchcraftai/docs/install-linux.html), [Windows](https://torchcraft.github.io/torchcraftai/docs/install-windows.html), and [OSX](https://torchcraft.github.io/torchcraftai/docs/install-osx.html)), here's how to play against it.

## (Recommended) Get trained models

If you have an NVIDIA GPU and are using CUDA, you'll want to download the trained models of the [Build Order Switcher](https://s3.amazonaws.com/torchcraftai/models/1.0/bos_model_20181128.bin) and the [Building Placer](https://s3.amazonaws.com/torchcraftai/models/1.0/bp_model.bin).

To enable the trained models, add the command line arguments specified under "Run the client". CherryPi can play without the trained models, but will be at reduced strength.

## Run CherryPi

There are several ways to play games with CherryPi.

## Linux: Wine
1. [Install Wine](https://wiki.winehq.org/Wine_Installation_and_Configuration)
2. Refer to the Windows/Wine ChaosLauncher instructions right below.

## Windows/Wine ChaosLauncher
This is the recommanded way to run StarCraft. On a Mac, we recommend running a Linux or Windows VM.
1. Install StarCraft *1.16.1* (in Wine if Linux)
2. [Install BWAPI 4.2.0](https://github.com/bwapi/bwapi/releases/tag/v4.2.0)
3. Run `cherrypi.exe -hostname 127.0.0.1` from inside the Starcraft directory. `cherrypi.exe` is created in the `Release` folder after build on Windows.
4. Run `ChaosLauncher.exe`. It's located in the directory where you installed BWAPI
5. Check `BWAPI Injector 4.2.0 [RELEASE]`
6. Click on `BWAPI Injector 4.2.0 [RELEASE]` and then `Config`. This will open bwapi.ini for editing
7. Under the [ai] section replace `ai = ` with `ai = [PATH\TO\BWEnv.dll]` which should be by default `[CherryPi]\bin\BWEnv.dll`
8. Save the config, and click Start!
9. In StarCraft BW, create a custom game. Play as Zerg against one computer opponent.
10. To play *against* the bot, you'll have to launch the game again, and create games in Multiplayer Mode, or use [DropLauncher](https://github.com/adakitesystems/DropLauncher)

## (All platforms) SC-Docker
Here, SC-Docker will only be used to run StarCraft, the opponent bot, and the TorchCraft server (`BWEnv.dll`).
1. [Install Docker](https://docs.docker.com/install/)
2. [Install SC-Docker](https://github.com/Games-and-Simulations/sc-docker) by cloning the GitHub repo and running `python setup.py install` manually. The version available via pip does not support the use-case described here yet.
3. [Download this bot profile](https://s3.amazonaws.com/torchcraftai/aux/1.0/TorchCraft_scbw_profile.zip) and unzip it in `~/.scbw/bots/`. FYI: this is is just a folder having the necessary DLLs: BWAPI and TorchCraft (`BWEnv.dll`), and a minimal json config file.
4. Run a game using SC-Docker. Example: `scbw.play --bots "TorchCraft" "Steamhammer"` (that only injects the TorchCraft server).
5. Run the bot with `./cherrypi -hostname 127.0.0.1 [-bos_model bwapi-data/AI/bos_model.bin] [...]`. If on Mac, run the bot with `-hostname localhost`.

## (All platforms) OpenBW
1. [Install OpenBW](https://github.com/OpenBW/bwapi)
3. [Run OpenBW](https://github.com/OpenBW/bwapi), with BWAPI_CONFIG_AI__AI=/PATH/TO/BWEnv.[so|dll], see [TorchCraft's doc](https://github.com/TorchCraft/TorchCraft/blob/develop/docs/openbw.md#running).

# Set up read/write directories

Create directories for reading/writing game data:
* `[path-to-cherrypi]/bwapi-data/read`
* `[path-to-cherrypi]/bwapi-data/write`
* `[path-to-cherrypi]/bwapi-data/AI`

This directory structure is used in all Brood War AI tournaments.
`AI` is typically used for unchanging bot files, and is a good place to put trained Build Order Switcher/Building Placer models if you're using them.
CherryPi records a history of its games to `bwapi-data/write`. In future games, to adapt to its opponents, it will attempt to read that data from `bwapi-data/read`. We recommend copying the contents of `write` to `read` between games.

# Run the client
CherryPi is a server-client architecture.
BWEnv.[so|dll] only starts the server, which will *hang the game* at the beginning, as it waits for the CherryPi client to connect.

Run the client that you compiled:
* Linux/OSX: `cherrypi`
* Windows: `.\Release\cherrypi`

### Optional: Run CherryPi and StarCraft on separate machines
If you want to run the CherryPi bot client on a different machine (real or virtual) than StarCraft itself:
* Linux/OSX: `cherrypi -hostname [IP ADDRESS] -port [PORT]`
* Windows: `.\Release\cherrypi -hostname [IP ADDRESS] -port [PORT]`

Specify the IP address of the machine hosting StarCraft, and the port used by TorchCraft (by default, 11111).

## Play with trained models

Place the trained models in `bwapi-data/AI`. Run CherryPi with these arguments to use the trained models:

`cherrypi[.exe] -hostname 127.0.0.1 -port 11111 -bos_model bwapi-data/AI/bos_model.bin -bp_model bwapi-data/AI/bp_model.bin`
