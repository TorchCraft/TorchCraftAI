---
id: defiler-micro-model
title: Defiler Micro: Neural Network Architecture
sidebar_label: Neural Network Architecture
---
Here we will talk about the model we use for controlling the defiler. The model is trained by the micro training loop, so again, you might want to finish the micro tutorial first. But our model here is much simpler, since we have restrted action space. It is friendly for Desktop / Laptop machine even without GPU.

As we discussed in the intro, the action space of the model is a map at build title resolution (64 * 64 cells), where the defiler can cast its spell to - sometimes after moving closer to that location. For each of the 2 spells, the model outputs a probability distribution over the map cells.

# input
Naturally, our model needs to be informed of the current game condition, the unit it is now controlling. Since we are using micro specific test map, so the map features is not important here.
We will use the following features for the model:
* Defogger features at time T : Use `UnitTypeDefoggerFeaturizer`, who produces a aggregated unit count by resolution, it is a [UnitType * H * W] Tensor, to be more specific, it is [2 * 118, 64, 64], for unitType from both parties at the build tile resolution. And the value is how many unit of this type in a single build tile given.
* Defogger features at time T - 1 : The features we get at last step when the model runs, we want the model to predict certain momentum when casting the spell since the units might move during time.
* Position map of the defiler : A Tensor at build tile resolution [64, 64], where 0 everywhere except for the cell where the defiler is.

Some might wonder, why we need the position map since the position of the defiler is already included in the Defogger features? That is because we might want the model to control multiple defiler at the same time by assemble the input at the `batchSize` dimension. And for each input, the model needs to know which defiler it is now controlling. It is true that it doesn't matter if we only have a single defiler like in our case, but it would be extensible for the future. In this guide, we only use single defiler scenarios, so the `batchSize` dimension is omitted.

# Output
The output of the model is a probability distribution over the whole map in build tile resolution for the two alibies the defiler can use, the output will always be a [2, 64, 64] tensor.

# Model Architecture
We are using a simplest [convolutional neural network](https://en.wikipedia.org/wiki/Convolutional_neural_network) you can imagine, the model has only a single convolution. The parameters (padding, stride, kernel size) is designed in the way that we can have 64 * 64 output from a 64 * 64 input. If are you interested, you can read [here](https://arxiv.org/pdf/1603.07285.pdf) for the details.
The model is defined as following:
```
                  ag::Conv2d(4 * n_unit_types_ + 1, 2, 5)
                      .padding(2)
                      .stride(1)
                      .make())
```
You can see the detailed usage [Conv2d](https://pytorch.org/docs/stable/nn.html#torch.nn.Conv2d).
In input channel, we have `2 (from t and t - 1) * 2 (for both parties) * n_unit_types_ + 1 (mark the position of the defiler) ` and we want to transform it to a 2-channel output for plague and dark swarm. The kernel size is 5 and padding is 2.

After that, we do SoftMax over the 2 channels, we can simply pick the one with highest value, who determines which ability and where we should cast it.
