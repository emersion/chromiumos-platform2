// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROMEOS_PLATFORM_UPDATE_ENGINE_MOCK_PAYLOAD_STATE_H__
#define CHROMEOS_PLATFORM_UPDATE_ENGINE_MOCK_PAYLOAD_STATE_H__

#include "gmock/gmock.h"
#include "update_engine/omaha_request_action.h"
#include "update_engine/payload_state_interface.h"

namespace chromeos_update_engine {

class MockPayloadState: public PayloadStateInterface {
 public:
  bool Initialize(SystemState* system_state) {
    return true;
  }

  // Significant methods.
  MOCK_METHOD1(SetResponse, void(const OmahaResponse& response));
  MOCK_METHOD0(DownloadComplete, void());
  MOCK_METHOD1(DownloadProgress, void(size_t count));
  MOCK_METHOD0(UpdateResumed, void());
  MOCK_METHOD0(UpdateRestarted, void());
  MOCK_METHOD0(UpdateSucceeded, void());
  MOCK_METHOD1(UpdateFailed, void(ActionExitCode error));
  MOCK_METHOD0(ShouldBackoffDownload, bool());

  // Getters.
  MOCK_METHOD0(GetResponseSignature, std::string());
  MOCK_METHOD0(GetPayloadAttemptNumber, uint32_t());
  MOCK_METHOD0(GetUrlIndex, uint32_t());
  MOCK_METHOD0(GetUrlFailureCount, uint32_t());
  MOCK_METHOD0(GetUrlSwitchCount, uint32_t());
  MOCK_METHOD0(GetBackoffExpiryTime, base::Time());
  MOCK_METHOD0(GetUpdateDuration, base::TimeDelta());
  MOCK_METHOD0(GetUpdateDurationUptime, base::TimeDelta());
  MOCK_METHOD1(GetCurrentBytesDownloaded, uint64_t(DownloadSource source));
  MOCK_METHOD1(GetTotalBytesDownloaded, uint64_t(DownloadSource source));
  MOCK_METHOD0(GetNumReboots, uint32_t());
};

}  // namespace chromeos_update_engine

#endif  // CHROMEOS_PLATFORM_UPDATE_ENGINE_MOCK_PAYLOAD_STATE_H__
