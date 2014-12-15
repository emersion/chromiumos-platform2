// Copyright 2014 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <chromeos/dbus/utils.h>

#include <tuple>
#include <vector>

#include <base/bind.h>
#include <base/memory/scoped_ptr.h>
#include <chromeos/errors/error_codes.h>
#include <chromeos/strings/string_utils.h>

namespace chromeos {
namespace dbus_utils {

std::unique_ptr<dbus::Response> CreateDBusErrorResponse(
    dbus::MethodCall* method_call,
    const std::string& error_name,
    const std::string& error_message) {
  auto resp = dbus::ErrorResponse::FromMethodCall(method_call, error_name,
                                                  error_message);
  return std::unique_ptr<dbus::Response>(resp.release());
}

std::unique_ptr<dbus::Response> GetDBusError(dbus::MethodCall* method_call,
                                             const chromeos::Error* error) {
  CHECK(error) << "Error object must be specified";
  std::string error_name = DBUS_ERROR_FAILED;  // Default error code.
  std::string error_message;

  // Special case for "dbus" error domain.
  // Pop the error code and message from the error chain and use them as the
  // actual D-Bus error message.
  if (error->GetDomain() == errors::dbus::kDomain) {
    error_name = error->GetCode();
    error_message = error->GetMessage();
    error = error->GetInnerError();
  }

  // Append any inner errors to the error message.
  while (error) {
    // Format error string as "domain/code:message".
    if (!error_message.empty())
      error_message += ';';
    error_message += error->GetDomain() + '/' + error->GetCode() + ':' +
               error->GetMessage();
    error = error->GetInnerError();
  }
  return CreateDBusErrorResponse(method_call, error_name, error_message);
}

void AddDBusError(chromeos::ErrorPtr* error,
                  const std::string& dbus_error_name,
                  const std::string& dbus_error_message) {
  std::vector<std::string> parts = string_utils::Split(dbus_error_message, ';');
  std::vector<std::tuple<std::string, std::string, std::string>> errors;
  for (const std::string& part : parts) {
    // Each part should be in format of "domain/code:message"
    size_t slash_pos = part.find('/');
    size_t colon_pos = part.find(':');
    if (slash_pos != std::string::npos &&
        colon_pos != std::string::npos &&
        slash_pos < colon_pos) {
      // If we have both '/' and ':' and in proper order, then we have a
      // correctly encoded error object.
      std::string domain = part.substr(0, slash_pos);
      std::string code = part.substr(slash_pos + 1, colon_pos - slash_pos - 1);
      std::string message = part.substr(colon_pos + 1);
      errors.emplace_back(domain, code, message);
    } else if (slash_pos == std::string::npos &&
               colon_pos == std::string::npos &&
               errors.empty()) {
      // If we don't have both '/' and ':' and this is the first error object,
      // then we had a D-Bus error at the top of the error chain.
      errors.emplace_back(errors::dbus::kDomain, dbus_error_name, part);
    } else {
      // We have a malformed part. The whole D-Bus error was most likely
      // not generated by GetDBusError(). To be safe, stop parsing it
      // and return the error as received from D-Bus.
      errors.clear();  // Remove any errors accumulated so far.
      errors.emplace_back(errors::dbus::kDomain,
                          dbus_error_name,
                          dbus_error_message);
      break;
    }
  }

  // Go backwards and add the parsed errors to the error chain.
  for (auto it = errors.crbegin(); it != errors.crend(); ++it) {
    Error::AddTo(error, FROM_HERE,
                 std::get<0>(*it), std::get<1>(*it), std::get<2>(*it));
  }
}

}  // namespace dbus_utils
}  // namespace chromeos
