# Growing Action Spaces for StarCraft Micro

Code in this branch produces the results of our paper [Growing Action Spaces](https://arxiv.org/abs/1906.12266).
The code in `gas_micro/` produces the results on StarCraft micro, while the code in `gas_demo/` produces the proof-of-concept results in discretised continuous control.

Build following the instructions for TCAI, found below.

Run with fixed level of detail 0, growing action spaces from lod 0 to 2, or with our impala baseline with these commands:

```
./build/gas_micro/train_micro --v=-3 --vmodule=gasmicromodule=0,train_micro=0 --trainer=gas
--optim=adam --lr=0.00025 --optim_eps=0.0001
--epsilon_max=1.0 --epsilon_min=0.1 --epsilon_decay_length=20000
--min_lod=0 --max_lod=0
--custom_scenario_num=80 --custom_scenario_advantage=5 --custom_scenario_unit=mr --custom_scenario_enemy=mr
--results=/tmp/gas/lod0/ --resume=/tmp/gas/lod0/ --dump_replays=never --illustrate=1

./build/gas_micro/train_micro --v=-3 --vmodule=gasmicromodule=0,train_micro=0 --trainer=gas
--optim=adam --lr=0.00025 --optim_eps=0.0001
--epsilon_max=1.0 --epsilon_min=0.1 --epsilon_decay_length=20000
--min_lod=0 --max_lod=2 --lod_growth_length=10000  --lod_lead_in=5000
--custom_scenario_num=80 --custom_scenario_advantage=5 --custom_scenario_unit=mr --custom_scenario_enemy=mr
--results=/tmp/gas/gas2/ --resume=/tmp/gas/gas2/ --dump_replays=never --illustrate=1

./build/gas_micro/train_micro --v=-3 --vmodule=gasmicromodule=0,train_micro=0 --trainer=impala
--optim=adam --lr=0.0001 --optim_eps=0.0001 --entropy_loss_coef=0.008
--min_lod=0 --max_lod=2 --lod_growth_length=10000  --lod_lead_in=5000
--custom_scenario_num=80 --custom_scenario_advantage=5 --custom_scenario_unit=mr --custom_scenario_enemy=mr
--results=/tmp/gas/mm/ --resume=/tmp/gas/mm/ --dump_replays=never --illustrate=1
```

# TorchCraftAI

TorchCraftAI is a platform that lets you build agents to play (and learn to play) *StarCraft®: Brood War®*†. TorchCraftAI includes:
- A modular framework for building StarCraft agents
- CherryPi, a bot which plays complete games of StarCraft (1st place SSCAIT 2017-18)
- A reinforcement learning environment with minigames, models, and training loops
- TorchCraft support for TCP communication with StarCraft and BWAPI
- Support for Linux, Windows, and OSX

## Get started

See guides for:

- [Linux](https://torchcraft.github.io/TorchCraftAI/docs/install-linux.html)
- [Windows](https://torchcraft.github.io/TorchCraftAI/docs/install-windows.html)
- [OSX](https://torchcraft.github.io/TorchCraftAI/docs/install-macos.html)

## Documentation

* [Home](https://torchcraft.github.io/TorchCraftAI)
* [Architecture overview](https://torchcraft.github.io/TorchCraftAI/docs/architecture.html)
* [Code reference](https://torchcraft.github.io/TorchCraftAI/reference/)

### Tutorials

* [Train a model to place buildings](https://torchcraft.github.io/TorchCraftAI/docs/bptut-intro.html)
* [Train a model to fight](https://torchcraft.github.io/TorchCraftAI/docs/microtut-intro.html)

## Licensing

We encourage you to experiment with TorchCraftAI! See [LICENSE](https://github.com/TorchCraft/TorchCraftAI/blob/master/LICENSE), plus more on [contributing](https://github.com/TorchCraft/TorchCraftAI/blob/master/CONTRIBUTING.md) and our [code of conduct](https://github.com/TorchCraft/TorchCraftAI/blob/master/CODE_OF_CONDUCT.md).

†: StarCraft is a trademark or registered trademark of Blizzard Entertainment, Inc., in the U.S. and/or other countries.  Nothing in this repository should be construed as approval, endorsement, or sponsorship by Blizzard Entertainment, Inc.
