// Copyright (c) 2015 GitHub, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef ATOM_COMMON_API_SYNC_MESSAGE_REPLY_H_
#define ATOM_COMMON_API_SYNC_MESSAGE_REPLY_H_

#include <vector>

#include "base/memory/shared_memory.h"
#include "base/strings/string16.h"

struct SyncMessageReply {
  base::string16 json;
  std::vector<base::SharedMemoryHandle> handles;  // shared memorys.
  std::vector<size_t> sizes;  // size of shared memorys.
};

#endif  // ATOM_COMMON_API_SYNC_MESSAGE_REPLY_H_
