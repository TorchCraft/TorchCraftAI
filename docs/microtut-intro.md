---
id: microtut-intro
title: Micromanagement Scenarios Tutorial
sidebar_label: Micromanagement Intro
---

In this tutorial, we will develop a model for micromanagement scenarios using Evolution Strategies

## Introduction

Micromanagement -- controlling your units in combat situations -- is a key component of winning at StarCraft.
By controlling your units carefully, you can destroy more enemy units while losing fewer of your own.
One example is focus firing: by focusing your attacks onto fewer targets, you eliminate enemy units faster and reduce the damage they deal back to you.
Another example is kiting: if you have a fast unit fighting a slower unit with shorter range or shorter cooldown, you can perform the sequence of attack-run-attack-run, taking reduced damage. If their range is shorter, you can keep them out of range and prevent their attacks. If their cooldown is longer, they can miss opportunities to attack.
We can also withdraw a damaged unit, and use a healthy unit to take damage from the enemy to retain the damaged units' firepower.
Micromanagement is thus a combination of precise movement and targetting that allows us turn win in unfavorable situations.

## Action space

In order to learn a wide range of possible behaviors, we provide a simple action space: where to move, and whom to attack.
The model outputs 3 values:
- A probability distribution over what action to take (attack, or move)
- A probability distribution over which units to attack
- A probability distribution over where to move, in a 20x20 walktile grid centered around our unit.

The model first sample what action to take. Depending on the results, we sample where to move to or whom to attack. 
You can extend this approach to specify new actions, even actions with different output spaces. For example, you could abilities like Burrow or Psionic Storm.
This action space is essentially a specification of the UPC, where we force a decision to be made for each unit, and the position depends on the command to execute.

## Units and Scenarios

StarCraft units have drastically different attributes and benefit from being controlled in different ways.
Here are some of the units in the training scenarios:

- SCV (**sc**) - Basic worker unit. High HP and damage output for a worker, but has very short range is awkward when moving.
- Marine (**mr**) - Basic ranged combat unit. Mobile, but not very fast unless using Stim Packs.
- Firebat (**fi**) - Small melee unit with an anti-ground splash attack.
- Vulture (**vu**) - Fast, ranged, anti-ground unit. Its speed and range let it kite many ground units, and it can lay dangerous anti-ground Spider Mines.
- Goliath (**go**) - Clumsy long-range attacker. 
- Siege_Tank_Tank_Mode (**st**) - Long-range anti-ground unit. Can shoot in any direction and while moving. Can transform into the longer-ranged immobile Siege_Tank_Siege_Mode
- Wraith (**wr**) - Fast, mobile flying unit with strong anti-air attack and a weaker anti-ground attack. Often requires delicate control for maximum efficacy. Can be cloaked.
- Battlecruiser (**bc**) - Slow, powerful flying unit. Its Yamato Cannon can take out large targets from a safe distance.
- Probe (**pr**) - Basic worker unit. Nimble with a short ranged attack.
- Zealot (**zl**) - Sturdy melee unit. Powerful, but vulnerable to kiting.
- Dragoon (**dr**) - Sturdy, fast, ranged unit. Doesn't need to turn to fire. Can be very powerful if microed to move between attacks.
- Archon (**ar**) - Sturdy, fast, short-ranged unit that deals splash damage. Effective against small or stacked units, but vulnerable to kiting.
- Corsair (**co**) - Fast anti-air flyer. Deals splash damage, making it disproportionately strong in large fights.
- Scout (**sc**) - Slow, sturdy flyer. Has a strong anti-air attack but a very weak anti-ground attack.
- Drone (**dn**) - Basic worker unit. Nimble with a short ranged attack.
- Zergling (**zg**) - Fast, fragile melee unit with high damage output.
- Hydralisk (**hy**) - Fast, ranged combat unit. With upgrades, capable of perfect kiting against Zealots.
- Ultralisk (**ul**) - Sturdy, fast melee unit.
- Mutalisk (**mu**) - Fast, mobile flying unit. Often requires delicate control for maximum efficacy.
- Devourer (**de**) - Sturdy, slow anti-air unit which reduces the armor of enemies near its target and renders them weaker to other attacks.
- Infested_Terran (**it**) - Explodes on its target, dealing huge amounts of damage while dying in the process.

We provide a large pool of scenarios. You can see the full list by running with `-list_scenarios` and specify which scenario to use with `-scenario SCENARIO_NAME`:
- Symmetric, same-units: 3-6 same-unit mirror matches like "mr".
- Symmetric air+ground units: Air and ground mirror matches, like "hy+mu".
- Scenarios in which you have weaker units that can kite the enemy's stronger units.
- "big": Symmetric same-unit scenarios with 30 units on each side.
- "surround": Your army starts off surrounding the enemy but with some units too far to fight.
- "conga": Your army starts off in an inferior, "conga line" orientation.

There are also non-combat scenarios with alternative rewards.
* "hugmiddle" and "hugmiddleeasy" - your goal is to send a unit to the middle of the map
* "hugoverlords" - your goal is to send units to each of two harmless Overlords
* "popoverlords" - your goal is to kill eight Overlords as quickly as possible
* "ignorecivilians" - kill enemy High Templar while ignoring enemy Civilians

Instead of playing a specific scenario, you can play a randomly selected scenario from one of these categories:
- "shuffleMirror" -- Random symmetric same-unit scenarios
- "shuffleAirGround" -- Random symmetric air+ground scenarios
- "shuffleBig" -- Random big scenarios
- "shuffleKiting" -- Random kiting scenarios
- "shuffleRegroup" -- Random "surround" or "conga" scenarios

Can you train a model which generalizes to multiple scenarios?

You can also design your own scenarios and rewards. See `scenarios.cpp` and `reward.cpp`.

## Opponents

We provide a few different types of opponents to play against, which you can select with `-opponent`:
- attack_move - This is simply an attack move towards an enemy. This mode is closest to what most previous work on reinforcement learning with micro has done, and it's essentially attack closest but without changing targets until the original is dead.
- closest - Attacks the closest enemy that's a valid target
- weakest - Attacks the weakest enemy that's a valid target, based on hp+shields, with a slight preference for closer targets.
Closest/weakest issue a Delete UPC (which translates into an Attack command) only if the unit's target changes. This is to prevent cancelling attack animations (as can happen especially with Dragoons and Devourers).

"closest" is a strong baseline that is challenging but not unreasonable to defeat.

You can create your own baseline behaviors. See `rule_module.h`.

## Running the code

The code in this example is in `tutorials/micro`

To run this code, please do:
```
./build/tutorials/micro/micro_tutorial -vmodule state=-1,micro_tutorial=1 -scenario vu_zl
```

Some flags that may be useful for you:
- `-enable_gui` shows the progress of a single worker as learning continues by visualising the SC game it is playing.
- `-illustrate` shows in gui the commands they are performing by drawing lines to highlight the command and the target.
- `-resume` resumes a model from a path specified by `-results`
- `-num_threads` controls the number of threads we use to run games
