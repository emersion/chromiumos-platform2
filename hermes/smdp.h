// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef HERMES_SMDP_H_
#define HERMES_SMDP_H_

#include <cstdint>
#include <string>
#include <vector>

#include <base/callback.h>

namespace hermes {

// Provides an interface through which the LPD can communicate with the SM-DP
// server. This is responsible for opening, maintaining, and closing a HTTPS
// connection to the server.
class Smdp {
 public:
  using DataCallback =
      base::Callback<void(const std::vector<uint8_t>& smdp_data)>;
  using ErrorCallback =
      base::Callback<void(const std::vector<uint8_t>& error_data)>;

  virtual ~Smdp() = default;

  // First, establishes a connection to the SM-DP+ server over which
  // the ES8+ secure channel will be tunneled, then sends server the eSIM
  // challenge and info1 to begin the Authentication procedure. |callback| is
  // called upon server's response, or |error_callback| is called on server
  // error.
  //
  // Parameters
  //    challenge - eSIM challenge as returned by Esim.GetEuiccChallenge
  //    info1 - eSIM info1 as returned by Esim.GetEuiccInfo
  virtual void InitiateAuthentication(
      const std::vector<uint8_t>& challenge,
      const std::vector<uint8_t>& info1,
      const DataCallback& callback,
      const ErrorCallback& error_callback) const = 0;

  virtual void AuthenticateClient(
      const std::vector<uint8_t>& data,
      const DataCallback& callback,
      const ErrorCallback& error_callback) const = 0;

 protected:
  virtual void OpenConnection() const = 0;
  virtual void CloseConnection() const = 0;
  virtual void SendServerMessage() const = 0;
};

}  // namespace hermes

#endif  // HERMES_SMDP_H_