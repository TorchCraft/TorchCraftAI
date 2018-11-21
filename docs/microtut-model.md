---
id: microtut-model
title: Micromanagement Scenarios Tutorial
sidebar_label: Model
---

Here, we will provide a detailed description of the model we have provided for controlling units.

As discussed in the [previous section](microtut-model.md), the model should output several different probabilities: whether to attack or move, where to move, and whom to attack.
The model design, which is inspired by [potential fields](http://aigamedev.com/open/tutorials/potential-fields/), is designed to learn the effective ranges of units and when to approach or flee enemies.

## Input

Our input features are somewhat raw. We consider terrain features plus unit features.

The map features include:
- Walkability: Whether ground units can walk on this tile
- Buildability: Whether buildings can be placed on this tile
- Ground height: High altitude in StarCraft can cause ranged units shooting uphill to miss attacks. Map "doodads" like trees have the same effect.
- Fog of war: Whether the tile is currently visible
- X,Y coordinates: The (X,Y) location of the tile

The first 4 features are the same as described in the [Building Placer](bptut-model.md) tutorial.

The unit features are more detailed, and include:
- Location 
- Velocity
- Health
- Shield
- Energy
- Range
- Damage
- Damage type
- ... etc

Full details of the featurization are in `unitsfeatures.cpp`. Many features are normalized such that their range approaches [-1, 1].

## Model

The code for our model can be found in `modelpf.cpp`

The model takes 5 things as input:
- `mapFeats`, the map features
- `ourLocs`, an Ux2 tensor of our U units' locations, indexed as (y, x)
- `ourFeats`, an UxF tensor of our U units' features.
- `nmyLocs`, an Ux2 tensor of the enemy's U units' locations, indexed as (y, x)
- `nmyFeats`, an UxF tensor of the enemy's U units' features.

First, we have a combined MLP that acts as an encoder for unit features, the `unitBaseEncoder_`.
```cpp
auto ourBase = at::relu(unitBaseEncoder_->forward({ourFeats})[0]);                                                     
auto nmyBase = at::relu(unitBaseEncoder_->forward({nmyFeats})[0]);
```

Then, we have four different networks to compute both an embedding an a potential field parameterization for our and our enemy's units.
These are `ourEmbHead_`, `ourPotHead_`, `nmyEmbHead_`, `nmyPotHead_`.
The embedding head generates an UxE tensor, and the potential field we use is parameterized with 2 parameters, so it will generate an Ux2 tensor.
```cpp
auto ourEmb = ourEmbHead_->forward({ourBase})[0];                                                                      
auto nmyEmb = nmyEmbHead_->forward({nmyBase})[0];
auto ourPotParams = ourPotHead_->forward({ourBase})[0];                                                                
auto nmyPotParams = nmyPotHead_->forward({nmyBase})[0];                                                                
```

A potential field can be thought of as a region of influence, centered around our unit, and extending outwards.
One example of what it could represent is the "threat" of a unit.
The embedding tensor contains _what_ is in the potential field, the parameterization decides how it should fall off as a afunction of distance.
We call this parameterization the "kernel", and we use a kernel with two parameters.
Let's call the embedding `e_i` for the `i`-th unit.
For each coordinate in our spatial grid, the potential `P` that the unit emits is a function of `d`, the distance from the unit to the point in the grid:

$$
F(e, w_1, w_2) = 
e \times 
\begin{cases}
1 & d <= w_1 \cr
\frac{w_2 + w_1 - d}{w_1} & w_1 < d <= w_1 + w_2 \cr
0 & d > w_2
\end{cases}
$$

In essence, this function looks something like:
```
 |
 |---------
 |         \
 |          \
 ------------------------
          |  |
        w_1  w_2
```
And `w_1` and `w_2` are the two parameters outputted by the kernel parameterization head.

Now, we can finally do the sum and max of potentials generated at each x-y coordinate across all units, to create a HxWx2E spatial potential field.
The spatial embedding for each unit is the value of the field at the unit's location.
```cpp
auto ourPot = kernel_->forward(ourLocs, ourPotParams);                                                                 
auto nmyPot = kernel_->forward(nmyLocs, nmyPotParams);                                                                 

auto spatialPotFieldSum = ourPot.matmul(ourEmb) + nmyPot.matmul(nmyEmb);                                               
auto spatialPotFieldMax =                                                                                              
  at::cat({ourPot.unsqueeze(-1) * ourEmb, nmyPot.unsqueeze(-1) * nmyEmb}, 2)                                         
  .max_values(2);                                                                                                
auto spatialPotField = at::cat({spatialPotFieldSum, spatialPotFieldMax}, 2);

auto ourSpatialEmbs = indexSpatialEmbeddings(ourLocsCPU);
auto nmySpatialEmbs = indexSpatialEmbeddings(nmyLocsCPU);
```
Check out the code for how the kernel works!

For the movement action head, we take a 20x20 slice of the spatial potential plane around our unit and run a small 3-layer convolutional networks to generate the movement scores.

We call the "final unit embeddings" the concatentation of the unit embeddings with the spatial embeddings for each unit.
```cpp
auto ourFinalEmb = at::cat({ourEmb, ourSpatialEmbs}, 1);                                                               
auto nmyFinalEmb = at::cat({nmyEmb, nmySpatialEmbs}, 1); 
```

Then, the action score, for whether our unit `i` should attack enemy unit `j` is an MLP on the concatentation of `ourFinalEmb[i]` with `nmyFinalEmb[j]`.
We also use their relative distances as an extra feature:
```cpp
auto ourActionEmbs = at::cat(                                                                                          
    {ourFinalEmb.unsqueeze(1).expand({-1, nmyNumUnits, -1}),                                                           
    nmyFinalEmb.unsqueeze(0).expand({ourNumUnits, -1, -1}),                                                           
    relDist},                                                                                                         
    2);
ourActionEmbs = ourActionEmbs.view({-1, ourActionEmbs.size(2)});                                                       
auto ourAttackScores = attackNetwork_->forward({ourActionEmbs})[0].view({ourNumUnits, nmyNumUnits}); 
```

Finally, the command scores of whether we should attack or move is simply dependent on our final embedding.
```cpp
auto ourCommandScores = commandNetwork_->forward({ourFinalEmb})[0]; 
```
