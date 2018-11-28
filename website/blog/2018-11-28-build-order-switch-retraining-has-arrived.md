---
title: Build Order Switch Retraining Has Arrived
author: Jonas Gehring
authorURL: https://github.com/jgehring
authorFBID: 100006699451315
---

For this year's [AIIDE competition](https://www.cs.mun.ca/~dchurchill/starcraftaicomp/2018), we deployed a machine learning model for high-level strategy selection dubbed Build Order Switch (BOS).
Our competition model was trained by playing against publicly available bots, e.g. from the [SSCAIT ladder](https://sscaitournament.com/).
Naturally, we could neither train nor test it against the updated and new bots that we were going to compete against.

Now that the tournament is over, many authors provided new versions of their bots to the public.
We added a new opening and simply re-trained our model against newly available and updated opponents.
In internal evaluations, our win rate against the AIIDE 2018 winner [SAIDA](https:/sscaitournament.com/index.php?action=botDetails&bot=SAIDA) is now at about 55-60% (SSCAIT version from 11/14) , up from 17% with the AIIDE versions.

The new model is [available on S3](https://s3.amazonaws.com/torchcraftai/models/1.0/bos_model_20181128.bin) and can be used as described in [the documentation](docs/play-games.html):
```bash
curl -o bwapi-data/AI/bos_model.bin https://s3.amazonaws.com/torchcraftai/models/1.0/bos_model_20181128.bin
cherrypi[.exe] -hostname 127.0.0.1 -port 11111 -bos_model bwapi-data/AI/bos_model.bin -bp_model bwapi-data/AI/bp_model.bin
```

You can get more details about the Build Order Switch model and the training setup in [our recent paper](https://arxiv.org/abs/1811.08568) which will be presented at the [NeurIPS RL PO workshop](https://sites.google.com/site/rlponips2018) in Montréal next week.
For the model that we are making available now, we put an emphasis on newer and stronger bots and increased the training time.

Below are a few games and the value outputs of the BOS model for different build orders.
Throughout the game, the model estimates the probability of winning when switching to one of the available build orders.
If the advantage over the currently active build order is higher than a threshold, CherryPi will transition to the one with the highest estimated value.
These transitions are highlighted in the plots with vertical lines.
Click on the graphs to view the full-resolution versions.

#### Game 1 [[replay](https://s3.amazonaws.com/torchcraftai/assets/bos-game1.rep)]

Played against SAIDA (SSCAIT version from 11/14).
Our confidence in winning the game drops around the 4-minute mark when we become aware of SAIDA's planned expansion and its first military units, but stabilizes again at 5 minute after our Hydras managed to hold off the approaching Vultures.
Ten minutes into the game, SAIDA's army is making its way across the map and we focus on increasing our Hydralisk count.
The drop at 11 minutes marks the destruction of our natural although our Hydralisks are managing to surround the opponent's army and defeat it.
Finally, we transition to a more diverse army composition with the switch to `zvt3hatchlurker` and are confident to win the game at 17:30.

<a href="/TorchCraftAI/blog/assets/bos-game1.svg">
<img src="/TorchCraftAI/blog/assets/bos-game1.svg"/>
</a>

#### Game 2 [[replay](https://s3.amazonaws.com/torchcraftai/assets/bos-game2.rep)]

This is a game against the AIIDE 2018 version of Locutus.
We start the game with `3basepoollings`, an economy-heavy opening.
As we do not sense an early attack from the opponent, we do not consider switching our build until six minutes into the game.
Shortly before the 8-minute mark, we switch to `zvtmacro` which spurs our Zergling production -- Zealots are just arriving at our natural.
They cause significant damage but with numerous Zerglings we are able to hold them off.
We finally defeat Locutus with an army made up of Zerglings, Mutalisks and Ultralisks.

<a href="/TorchCraftAI/blog/assets/bos-game2.svg">
<img src="/TorchCraftAI/blog/assets/bos-game2.svg"/>
</a>

#### Game 3 [[replay](https://s3.amazonaws.com/torchcraftai/assets/bos-game3.rep)]

This is another game against SAIDA (SSCAIT version from 11/14) in which CherryPi loses.
The switch to `zvp10hatch`, which is a  happens quite early in response to early Vultures.
At ten minutes, we successfully held of SAIDA's initial push which results in a higher overall confidence.
However, our army becomes too reliant on Zerglings and can't uphold the pressure, allowing SAIDA's army to leave the natural around 16 minutes into the game.
Switching to `zvtantimech` at around 14:30 was not enough to prevent the eventual defeat.

<a href="/TorchCraftAI/blog/assets/bos-game3.svg">
<img src="/TorchCraftAI/blog/assets/bos-game3.svg"/>
</a>

Finally, we are thankful for the fierce competition in AIIDE 2018 – a bigger pool of bots is good for everyone in the research community, and we hope to see more in the future!
