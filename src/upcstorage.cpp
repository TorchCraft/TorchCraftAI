/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "upcstorage.h"

namespace cherrypi {

UpcStorage::UpcStorage() {}

UpcStorage::~UpcStorage() {}

void UpcStorage::setPersistent(bool persistent) {
  persistent_ = persistent;
}

UpcId UpcStorage::addUpc(
    FrameNum frame,
    UpcId sourceId,
    Module* source,
    std::shared_ptr<UPCTuple> upc,
    std::shared_ptr<UpcPostData> data) {
  UpcId id = static_cast<UpcId>(posts_.size() + 1);
  if (persistent_) {
    posts_.emplace_back(
        frame, id, sourceId, source, std::move(upc), std::move(data));
  } else {
    posts_.emplace_back(frame, id, sourceId, source);
  }
  return id;
}

UpcId UpcStorage::sourceId(UpcId id) const {
  if (id > 0 && id <= UpcId(posts_.size())) {
    return posts_[id - 1].sourceId;
  }
  return kInvalidUpcId;
}

std::vector<UpcId> UpcStorage::sourceIds(UpcId id, Module* upTo) const {
  auto curId = id;
  std::vector<UpcId> sources;
  while (curId > 0 && curId <= UpcId(posts_.size())) {
    auto& post = posts_[curId - 1];
    if (curId != id && upTo != nullptr && post.module == upTo) {
      break;
    }
    sources.push_back(post.sourceId);
    curId = post.sourceId;
  }
  return sources;
}

std::shared_ptr<UPCTuple> UpcStorage::upc(UpcId id) const {
  if (id > 0 && id <= UpcId(posts_.size())) {
    return posts_[id - 1].upc;
  }
  return nullptr;
}

UpcPost const* UpcStorage::post(UpcId id) const {
  if (id > 0 && id <= UpcId(posts_.size())) {
    return &posts_[id - 1];
  }
  return nullptr;
}

std::vector<UpcPost const*> UpcStorage::upcPostsFrom(
    Module* module,
    FrameNum frame) const {
  std::vector<UpcPost const*> result;
  for (auto& post : posts_) {
    if (post.module == module && (frame < 0 || post.frame == frame)) {
      result.push_back(&post);
    }
  }
  return result;
}

} // namespace cherrypi
