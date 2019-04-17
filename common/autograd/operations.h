/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <autogradpp/autograd.h>
#ifndef WITHOUT_POSIX
#include <cnpy/cnpy.h>
#endif // WITHOUT_POSIX

/*
 * Useful helpers for neural networks expressed with Torch.
 */
namespace common {

/**
 * Repeat a 1D tensor so that you end up with a (#channels, sizes[0],
 * size[1]) tensor
 */
torch::Tensor repeat2d(torch::Tensor data, at::IntList sizes);

/**
 * Scatter data into dest at given positions.
 * Depending on device that data lives on, different algorithms will be used:
 * - For CPU tensors, multiple scatter_add_ calls are used in order to sum up
 *   channels for duplicate positions
 * - For GPU tensors, data will be scattered onto all planes in a single
 * scatter_ call and summed up afterwards. This means the function will allocate
 * an intermediate buffer of size (b, c, np, H, W) if there are no more than np
 *   duplicate positions.
 *
 * There's a benchmark for this function in the corresponding unit tests.
 *
 * positions is a (b, n, 2) integer tensor with elements greater than or equal
 * to zero. positions[i][0] refers to the Y position, positions[i][1] to the X
 * position of the data entry i.
 * data is a (b, n, c) tensor. Each of the n entries will be placed in dest
 * according to the respective position.
 * Entries on each batch until the first negative entry will be considered.
 * sizes is the {H, W} tuple of the size of the plane to scatter onto.
 *
 * For single element, it's sufficient to unsqueeze(0) for it to look batched.
 * Positions don't have to be unique -- this function performs sum-pooling
 * by default.
 * The output is of size (b, c, y, x), similar to the input to a convnet.
 */
torch::Tensor
scatterSum2d(torch::Tensor positions, torch::Tensor data, at::IntList sizes);

/**
 * Equivalent to a stack along dim 0 of the input, but with the values
 * padded correct so the size is rectangular
 *
 * For example, if the list has size [(6, 2), (5, 2), (7, 3)]
 * The result is a tensor of (3, 7, 3)
 *
 **/
torch::Tensor makeBatch(ag::tensor_list const&, double pad = 0);

/**
 * This function works similarly as makeBatch but handles more input types.
 * The queries variants are requested to be the same type (we tolerate to mix
 * tensors and size 1 tensor_list)
 * It behaves as follows, depending on the variant type:
 *    - If the queries are tensors, it calls makeBatch and returns a tensor
 *    - If the queries are dict, it batches each key individually, and returns a
 *      dict. Note that all queries must contain the same keys
 *    - If the queriest are a tensor_list, it batches each item of the list
 *      individually, and returns a tensor_list. Note that all the queries must
 *      have the same number of tensors, semantically in the same order.
 */
ag::Variant makeBatchVariant(
    const std::vector<ag::Variant>& queries,
    double pad = 0);

/**
 * This function is the opposite of makeBatchVariant. It assumes that the
 * tensors to be found in the batch have a first dimension of size b,
 * interpreted as the batch dimension. It will take slices of size
 * \param{stride} along that dimension, and put them in the result vector. The
 * size the result vector will be b/stride. Note that if the stride is 1, then
 * the batch dimension will be squeezed out. Additionally, if \param{maskOut} is
 * true, then we mask out any item of value \param{maskValue}
 */
std::vector<ag::Variant> unBatchVariant(
    ag::Variant const& batch,
    int stride = 1,
    bool maskOut = false,
    double maskValue = -1);

/// Zero-padding (only supports 3d input)
torch::Tensor pad2d(torch::Tensor input, at::IntList pad);

/// Zero-padding (for any number of dimensions)
/// For every dimensions of the input, pad contains 2 elements: the padding
/// before and after along this dimension.
torch::Tensor padNd(torch::Tensor input, at::IntList pad);

/**
 * Flips a tensor along a given dimension.
 * y[-a-,i,-b-] = x[-a-,n-i-1,-b-]
 */
torch::Tensor flip(torch::Tensor x, int dim);

/// Mimics pytorch's upsample function
enum class UpsampleMode { Nearest, Linear, Bilinear, Trilinear };
torch::Tensor
upsample(torch::Tensor input, UpsampleMode mode, at::IntList size);
torch::Tensor upsample(torch::Tensor input, UpsampleMode mode, int scaleFactor);

/// Replace (in-place) all zeroes of x by ones.
void zerosToOnes_(torch::Tensor x);

#ifndef WITHOUT_POSIX
torch::Tensor tensorFromNpyArray(cnpy::NpyArray, torch::TensorOptions);
#endif // WITHOUT_POSIX
/**
 * Squash contiguous dimensions of a tensor into a single dimension.
 *
 * The dimensions [i..j] (both included) will be squashed into a single one.
 * So if x is of size s_1 x ... x s_d, the returned tensor will be a view of x
 * of size s_1 x ... x s_i-1 x s_i * s_i+1 * ... * s_j x s_j+1 x ... x s_d.
 */
torch::Tensor squash(torch::Tensor x, int i, int j);

/**
 * Unsquash a dimension of a tensor into several dimensions.
 *
 * Replace the i-th dimension of x by sizes (this augments the number of
 * dimensions of x).
 * The product of the elements of sizes should be x.size(i) (sizes can also
 * contain a -1).
 * If x is of size s_1 x ... x s_d, the returned tensor will be a view of x of
 * size s_1 x ... x s_i-1 x sizes x s_i+1 x ... s_d.
 */
torch::Tensor unsquash(torch::Tensor x, int i, at::IntList sizes);

/**
 * Sum x across non-masked indices.
 * This does not work if x contains NaNs or infinities at masked indices.
 *
 * - mask should be expandable to x's shape
 * - returns a scalar which is a masked sum of x.
 */
torch::Tensor maskedSum(torch::Tensor x, torch::Tensor mask);

/**
 * Average x over non-masked indices, returning 0 if all indices are masked.
 * This does not work if x contains NaNs or infinities at masked indices.
 *
 * - mask should be expandable to x's shape
 * - returns a scalar which is a masked average of x.
 */
torch::Tensor maskedMean(torch::Tensor x, torch::Tensor mask);

/**
 * Computes the MSE loss between x and y.
 *
 * - x and y must have same shape, and mask be expandable to x's shape
 * - if reduce is true, losses will be summed or averaged depending on
 * sizeAverage (averaged over non-masked indices)
 * - returns a scalar if reduce is true, otherwise a x-like tensor (with zeros
 * at masked indices)
 */
torch::Tensor mseLoss(
    torch::Tensor x,
    torch::Tensor y,
    torch::Tensor mask,
    bool sizeAverage = true,
    bool reduce = true);

/*
 * Compute the Cross Entropy Loss between the distribution defined when applying
 * the softmax layer to the input and the distribution defined by target.
 * This can be described as the expectation of the negative log-likelihood of
 * the predicted distribution when sampled according to the target
 * distribution.
 *
 * - input is a *xCx* tensor, defining a probability over {1, .., C}
 * - target is a *xCx* tensor, non-negative and summing to 1 on the C axis
 * - weight is an optional C tensor rescaling each class (useful for unbalanced
 * datasets)
 * - mask is *x1x* (or any other broadcastable shape) boolean (only 0s and 1s)
 * tensor where 0s means that the loss won't be computed for that distribution
 * - if reduce is true, losses will be reduced (over non-masked indices)
 *   Possible arguments are Sum, None, or Mean
 * - returns a scalar if reduce is true, otherwise a *x1x* tensor (with zeros
 * at masked indices)
 */
torch::Tensor crossEntropyLoss(
    torch::Tensor input,
    int dim,
    torch::Tensor target,
    torch::Tensor weight = {},
    torch::Tensor mask = {},
    Reduction::Reduction = Reduction::Reduction::Mean);

/*
 * Compute the Negative Log Likelihood loss between the input and target.
 *
 * - input is a *xCx* tensor, non-negative and summing to 1 on the C axis
 * - target is a *xCx* tensor, non-negative and summing to 1 on the C axis
 * - weight is an optional C tensor rescaling each class (useful for unbalanced
 * datasets)
 * - mask is *x1x* (or any other broadcastable shape) boolean (only 0s and 1s)
 * tensor where 0s means that the loss won't be computed for that distribution
 * - if reduce is true, losses will be reduced (over non-masked indices)
 *   Possible arguments are Sum, None, or Mean
 * - returns a scalar if reduce is true, otherwise a *x1x* tensor (with zeros
 * at masked indices)
 */
torch::Tensor nllLoss(
    torch::Tensor input,
    int dim,
    torch::Tensor target,
    torch::Tensor weight = {},
    torch::Tensor mask = {},
    Reduction::Reduction = Reduction::Reduction::Mean);

/**
 * Rescale gradients so that the norm of all gradients (concatenated) is smaller
 * than maxNorm.
 */
void clipGradientNorms(std::vector<torch::Tensor> parameters, float maxNorm);

/**
 * Compute a masked softmax of a tensor in a numerically stable way by
 * removing the max value before exponentiating.  The passed in mask must be
 * a variable of 0.0's and 1.0's (floats) of the same shape as the input.
 *
 * - input is a float tensor of the same shape as the mask
 * - mask is a binary float tensor of the same shape as the input
 * - dim is the dimension along which to apply the softmax
 * - clampEpsilon is the minimum value to clamp the output to
 *
 * Returns the output after masking and softmaxing.
 */
torch::Tensor maskedSoftmax(
    torch::Tensor input,
    torch::Tensor mask,
    int dim,
    float clampEpsilon = 0);

/**
 * Compute a masked max/argmax of a tensor.
 * The passed in mask must be a variable of 0.0's and 1.0's (floats) of the
 * same shape as the input.
 *
 * - input is a float tensor of the same shape as the mask
 * - mask is a binary float tensor of the same shape as the input
 * - dim is the dimension along which to apply the softmax
 * - keepDim is whether to squeeze the resulting tensors or not
 *
 * Returns the output after masking and softmaxing.
 * NOTE: behavior is undefined if mask is zero for some batch.
 */
std::tuple<torch::Tensor, torch::Tensor> maskedMax(
    torch::Tensor input,
    torch::Tensor mask,
    int dim,
    bool keepDim = false);

/**
 * Compute a weighted masked softmax of a tensor in a numerically stable way by
 * removing the max value before exponentiating.  The passed in mask must be
 * a variable of floats of the same shape as the input.  It should include
 * weighting and masking as desired (it need not be binary).
 *
 * - input is a float tensor of the same shape as the mask
 * - mask is a float tensor of the same shape as the input
 * - dim is the dimension along which to apply the softmax
 * - clampEpsilon is the minimum value to clamp the output to
 *
 * Returns the output after weighting, masking, and softmaxing.
 */
torch::Tensor weightedMaskedSoftmax(
    torch::Tensor input,
    torch::Tensor mask,
    int dim,
    float clampEpsilon = 0);

/**
 * Returns a byte tensor x such that selectIndex(x, y, axis) are only 1s.
 * y is of shape ... x 1 x ...
 * x is of shape ... x d x ...
 */
torch::Tensor extendIndex(torch::Tensor y, int axis, int d);

/**
 * For 1D tensors, this is equivalent to:
 * x[i] <- source[i] if mask[i] == 1
 */
void maskedCopy_(torch::Tensor x, torch::Tensor mask, torch::Tensor source);

/**
 * Immutable masked copy (equivalent to x.clone().maskedCopy_()).
 * NOTE: this does not work if x contains NaNs or infinities!
 * mask should have the same type as x.
 */
torch::Tensor
maskedCopy(torch::Tensor x, torch::Tensor mask, torch::Tensor source);

/**
 * Copies elements from source into x at positions determined by index.
 * If accumulate is true, adds instead of copy (otherwise, indices should appear
 * at most once in index).
 * x has shape X1 x .. x XD
 * index has shape N x D
 * source has shape N
 * For 2D tensors, this is equivalent to:
 * x[index[i][0], index[i][1]] <- source[i]
 */
void putNd_(
    torch::Tensor x,
    torch::Tensor index,
    torch::Tensor source,
    bool accumulate = false);

/**
 * Inverse operation of putNd_.
 * x has shape X1 x .. x Xd
 * index has shape N x d
 * y (return value) has shape N
 * For 2D tensors, this is equivalent to:
 * y[i] = x[index[i][0], index[i][1]];
 */
torch::Tensor takeNd(torch::Tensor x, torch::Tensor index);

/**
 * Like zeros.index_add_ but with the mean.
 * source has shape X1 x ... Xdim-1 x N x Xdim+1 x ... Xd
 * index has shape N, with values ranging from 0 to size - 1
 * x (return value) has shape X1 x ... Xdim-1 x size x Xdim+1 x ... x Xd
 * x[-a-,i,-b-] is the mean of {source[-a-,j,-b-] where index[j]=i} and
 * zero if this set is empty.
 */
torch::Tensor
indexMean(int size, int dim, torch::Tensor index, torch::Tensor source);

/**
 * Do multiple unsqueezes on first and last dimensions.
 */
torch::Tensor unsqueezes(int before, torch::Tensor x, int after);

/**
 * Takes N 1D tensors xi of size Xi and returns a tensor y of size
 * X1 x ... x XN x N such that y[a1]...[aN][i] = xi[ai].
 */
torch::Tensor meshGrid(ag::tensor_list tensors);

/**
 * This is a convenience function to apply a tensor transformation to a complex
 * type. For example, you would like to write something like t = t.view(-1), but
 * t is a tensor_list (and you'd like the operation to be applied to each
 * element of the list). You can write instead t = applyTransform(t,
 * [](torch::Tensor t){return t.view(-1);});
 */
using TensorTransform = std::function<torch::Tensor(torch::Tensor)>;
ag::Variant applyTransform(ag::Variant input, const TensorTransform& fun);

/**
 * Utility to get the device of a variant. If the variants contains several
 * tensors, we assume they have the same device
 */
at::Device getVariantDevice(ag::Variant const& x);

} // namespace common
