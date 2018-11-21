/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include "upc.h"

#include <deque>

namespace cherrypi {

class Module;

/**
 * Base class for data attached to the posting of an UPC.
 *
 * When backpropagating through the bot, this data will be provided for
 * computing gradients for the respective posted UPC. A common use case
 * would be to store the output of a featurizer here.
 */
struct UpcPostData {
  // Base class is empty
  virtual ~UpcPostData() = default;
};

/**
 * Stores information about UPCs that have been posted to the board.
 */
struct UpcPost {
 public:
  /// Game frame at time of post
  FrameNum frame = -1;
  /// Identifier of posted UPC
  UpcId upcId = kInvalidUpcId;
  /// Identifier of source UPC
  UpcId sourceId = kInvalidUpcId;
  /// The module performing the transaction
  Module* module = nullptr;
  /// The actual UPC data
  std::shared_ptr<UPCTuple> upc = nullptr;
  /// Data attached to this transaction
  std::shared_ptr<UpcPostData> data = nullptr;

  UpcPost() {}
  UpcPost(
      FrameNum frame,
      UpcId upcId,
      UpcId sourceId,
      Module* module,
      std::shared_ptr<UPCTuple> upc = nullptr,
      std::shared_ptr<UpcPostData> data = nullptr)
      : frame(frame),
        upcId(upcId),
        sourceId(sourceId),
        module(module),
        upc(std::move(upc)),
        data(std::move(data)) {}
};

/**
 * Stores a graph of UPC communication, including any transactional data.
 *
 * The storage will retain any UPCTuple and accompanying UpcPostData objects
 * added via addUpc(). It is possible to disable permanent storage of tuples and
 * data objects via setPersistent(), which is useful for evaluation settings in
 * which memory is scarce.
 */
class UpcStorage {
 public:
  UpcStorage();
  ~UpcStorage();

  /// Controls whether UPCTuple and UpcPostData objects should be stored.
  void setPersistent(bool persistent);

  /// Adds a UPC tuple with accompanying transaction data.
  /// The returned ID can be used to refer to this UPC in the future.
  UpcId addUpc(
      FrameNum frame,
      UpcId sourceId,
      Module* source,
      std::shared_ptr<UPCTuple> upc,
      std::shared_ptr<UpcPostData> data = nullptr);

  /// Retrieve the source UPC ID for the given UPC ID.
  UpcId sourceId(UpcId id) const;

  /// Recursively retrieve source UPC IDs up to a given module.
  /// If upTo is nullptr, retrieve all source UPC IDs up to and including the
  /// root UPC.
  std::vector<UpcId> sourceIds(UpcId id, Module* upTo = nullptr) const;

  /// Retrieve the UPC Tuple for a given ID.
  /// If the storage is not persistent, this function will return nullptr.
  std::shared_ptr<UPCTuple> upc(UpcId id) const;

  /// Retrieve the full post data for a given ID.
  UpcPost const* post(UpcId id) const;

  /// Retrieve all posts from a given module.
  /// Optionally, this can be restricted to a given frame number. Note that
  /// these pointers might be invalidated in subsequent calls to addUpc().
  std::vector<UpcPost const*> upcPostsFrom(Module* module, FrameNum frame = -1)
      const;

 private:
  /// The UPC IDs we provide are indices to this container.
  /// However, the indices actually start at 1.
  std::vector<UpcPost> posts_;

  bool persistent_ = true;
};

} // namespace cherrypi
