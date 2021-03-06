//
// Copyright (C) 2015 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//

#include "tpm_manager/common/mock_tpm_nvram_interface.h"

using testing::_;
using testing::Invoke;
using testing::WithArgs;

namespace {

template <typename ReplyProtoType>
void RunCallback(const base::Callback<void(const ReplyProtoType&)>& callback) {
  ReplyProtoType empty_proto;
  callback.Run(empty_proto);
}

}  // namespace

namespace tpm_manager {

MockTpmNvramInterface::MockTpmNvramInterface() {
  ON_CALL(*this, DefineSpace(_, _))
      .WillByDefault(WithArgs<1>(Invoke(RunCallback<DefineSpaceReply>)));
  ON_CALL(*this, DestroySpace(_, _))
      .WillByDefault(WithArgs<1>(Invoke(RunCallback<DestroySpaceReply>)));
  ON_CALL(*this, WriteSpace(_, _))
      .WillByDefault(WithArgs<1>(Invoke(RunCallback<WriteSpaceReply>)));
  ON_CALL(*this, ReadSpace(_, _))
      .WillByDefault(WithArgs<1>(Invoke(RunCallback<ReadSpaceReply>)));
  ON_CALL(*this, LockSpace(_, _))
      .WillByDefault(WithArgs<1>(Invoke(RunCallback<LockSpaceReply>)));
  ON_CALL(*this, ListSpaces(_, _))
      .WillByDefault(WithArgs<1>(Invoke(RunCallback<ListSpacesReply>)));
  ON_CALL(*this, GetSpaceInfo(_, _))
      .WillByDefault(WithArgs<1>(Invoke(RunCallback<GetSpaceInfoReply>)));
}

MockTpmNvramInterface::~MockTpmNvramInterface() {}

}  // namespace tpm_manager
