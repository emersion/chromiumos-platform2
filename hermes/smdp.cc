// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "hermes/smdp.h"

#include <string>
#include <utility>

#include <base/base64.h>
#include <base/bind.h>
#include <base/json/json_reader.h>
#include <base/json/json_writer.h>
#include <base/values.h>

namespace hermes {

Smdp::Smdp(const std::string& server_hostname)
    : server_hostname_(server_hostname),
      server_transport_(brillo::http::Transport::CreateDefault()),
      weak_factory_(this) {}

void Smdp::InitiateAuthentication(
    const std::vector<uint8_t>& info1,
    const std::vector<uint8_t>& challenge,
    const InitiateAuthenticationCallback& data_callback,
    const ErrorCallback& error_callback) {
  std::string encoded_info1(info1.begin(), info1.end());
  base::Base64Encode(encoded_info1, &encoded_info1);
  std::string encoded_challenge(challenge.begin(), challenge.end());
  base::Base64Encode(encoded_challenge, &encoded_challenge);

  base::DictionaryValue http_params;
  http_params.SetString("euiccInfo1", encoded_info1);
  http_params.SetString("euiccChallenge", encoded_challenge);
  http_params.SetString("smdpAddress", server_hostname_);
  std::string http_body;
  base::JSONWriter::Write(http_params, &http_body);

  SendJsonRequest(
      "https://" + server_hostname_ +
          "/gsma/rsp2/es9plus/initiateAuthentication",
      http_body,
      base::Bind(&Smdp::OnInitiateAuthenticationResponse,
                 weak_factory_.GetWeakPtr(), data_callback, error_callback),
      error_callback);
}

void Smdp::OnHttpResponse(
    const base::Callback<void(DictionaryPtr)>& data_callback,
    const ErrorCallback& error_callback,
    brillo::http::RequestID request_id,
    std::unique_ptr<brillo::http::Response> response) {
  if (!response) {
    error_callback.Run(std::vector<uint8_t>());
    return;
  }

  std::string raw_data = response->ExtractDataAsString();

  VLOG(1) << __func__ << ": Response raw_data : " << raw_data;

  std::unique_ptr<base::Value> result = base::JSONReader::Read(raw_data);
  if (result) {
    data_callback.Run(base::DictionaryValue::From(std::move(result)));
  } else {
    error_callback.Run(std::vector<uint8_t>());
    return;
  }
}

void Smdp::OnInitiateAuthenticationResponse(
    const InitiateAuthenticationCallback& data_callback,
    const ErrorCallback& error_callback,
    std::unique_ptr<base::DictionaryValue> json_dict) {
  std::string encoded_buffer;
  std::string server_signed1;
  if (!json_dict->GetString("serverSigned1", &encoded_buffer)) {
    error_callback.Run(std::vector<uint8_t>());
    return;
  }
  base::Base64Decode(encoded_buffer, &server_signed1);

  std::string server_signature1;
  if (!json_dict->GetString("serverSignature1", &encoded_buffer)) {
    error_callback.Run(std::vector<uint8_t>());
    return;
  }
  base::Base64Decode(encoded_buffer, &server_signature1);

  std::string euicc_ci_pk_id_to_be_used;
  if (!json_dict->GetString("euiccCiPKIdToBeUsed", &encoded_buffer)) {
    error_callback.Run(std::vector<uint8_t>());
    return;
  }
  base::Base64Decode(encoded_buffer, &euicc_ci_pk_id_to_be_used);

  std::string server_certificate;
  if (!json_dict->GetString("serverCertificate", &encoded_buffer)) {
    error_callback.Run(std::vector<uint8_t>());
    return;
  }
  base::Base64Decode(encoded_buffer, &server_certificate);

  std::string transaction_id;
  if (!json_dict->GetString("transactionId", &transaction_id)) {
    error_callback.Run(std::vector<uint8_t>());
    return;
  }

  data_callback.Run(
      transaction_id,
      std::vector<uint8_t>(server_signed1.begin(), server_signed1.end()),
      std::vector<uint8_t>(server_signature1.begin(), server_signature1.end()),
      std::vector<uint8_t>(euicc_ci_pk_id_to_be_used.begin(),
                           euicc_ci_pk_id_to_be_used.end()),
      std::vector<uint8_t>(server_certificate.begin(),
                           server_certificate.end()));
}

void Smdp::OnHttpError(const ErrorCallback& error_callback,
                           brillo::http::RequestID request_id,
                           const brillo::Error* error) {
  error_callback.Run(std::vector<uint8_t>());
}

void Smdp::SendJsonRequest(
    const std::string& url,
    const std::string& json_data,
    const base::Callback<void(DictionaryPtr)>& data_callback,
    const ErrorCallback& error_callback) {
  VLOG(1) << __func__ << ": sending data : " << json_data;
  brillo::ErrorPtr error = nullptr;
  brillo::http::Request request(url, brillo::http::request_type::kPost,
                                server_transport_);
  request.SetContentType("application/json");
  request.SetUserAgent("gsma-rsp-lpad");
  request.AddHeader("X-Admin-Protocol", "gsma/rsp/v2.0.0");
  request.AddRequestBody(json_data.data(), json_data.size(), &error);
  CHECK(!error);
  request.GetResponse(
      base::Bind(&Smdp::OnHttpResponse, weak_factory_.GetWeakPtr(),
                 data_callback, error_callback),
      base::Bind(&Smdp::OnHttpError, weak_factory_.GetWeakPtr(),
                 error_callback));
}

void Smdp::AuthenticateClient(
    const std::string& transaction_id,
    const std::vector<uint8_t>& data,
    const AuthenticateClientCallback& data_callback,
    const ErrorCallback& error_callback) {
  std::string encoded_data(data.begin(), data.end());
  base::Base64Encode(encoded_data, &encoded_data);

  base::DictionaryValue http_params;
  http_params.SetString("transactionId", transaction_id);
  http_params.SetString("authenticateServerResponse", encoded_data);
  std::string http_body;
  base::JSONWriter::Write(http_params, &http_body);

  SendJsonRequest(
      "https://" + server_hostname_ + "/gsma/rsp2/es9plus/authenticateClient",
      http_body,
      base::Bind(&Smdp::OnAuthenticateClientResponse,
                 weak_factory_.GetWeakPtr(), data_callback, error_callback),
      error_callback);
}

void Smdp::OnAuthenticateClientResponse(
    const AuthenticateClientCallback& data_callback,
    const ErrorCallback& error_callback,
    DictionaryPtr json_dict) {
  std::string transaction_id;
  if (!json_dict->GetString("transactionId", &transaction_id)) {
    LOG(ERROR) << __func__ << ": transactionId not received";
    error_callback.Run(std::vector<uint8_t>());
    return;
  }

  std::string encoded_buffer;
  std::string profile_metadata;
  if (!json_dict->GetString("profileMetadata", &encoded_buffer)) {
    LOG(ERROR) << __func__ << ": profileMetadata not received";
  }
  base::Base64Decode(encoded_buffer, &profile_metadata);

  std::string smdp_signed2;
  if (!json_dict->GetString("smdpSigned2", &encoded_buffer)) {
    LOG(ERROR) << __func__ << ": smdpSigned2 not received";
    error_callback.Run(std::vector<uint8_t>());
    return;
  }
  base::Base64Decode(encoded_buffer, &smdp_signed2);

  std::string smdp_signature2;
  if (!json_dict->GetString("smdpSignature2", &encoded_buffer)) {
    LOG(ERROR) << __func__ << ": smdpSignature2 not received";
    error_callback.Run(std::vector<uint8_t>());
    return;
  }
  base::Base64Decode(encoded_buffer, &smdp_signature2);

  std::string server_certificate;
  if (!json_dict->GetString("smdpCertificate", &encoded_buffer)) {
    LOG(ERROR) << __func__ << ": smdpCertificate not received";
    error_callback.Run(std::vector<uint8_t>());
    return;
  }
  base::Base64Decode(encoded_buffer, &server_certificate);

  VLOG(1) << __func__ << ": Client authenticated successfully";

  data_callback.Run(
      transaction_id,
      std::vector<uint8_t>(profile_metadata.begin(), profile_metadata.end()),
      std::vector<uint8_t>(smdp_signed2.begin(), smdp_signed2.end()),
      std::vector<uint8_t>(smdp_signature2.begin(), smdp_signature2.end()),
      std::vector<uint8_t>(server_certificate.begin(),
                           server_certificate.end()));
}

void Smdp::GetBoundProfilePackage(
    const std::string& transaction_id,
    const std::vector<uint8_t>& data,
    const GetBoundProfilePackageCallback& data_callback,
    const ErrorCallback& error_callback) {
  std::string encoded_data(data.begin(), data.end());
  base::Base64Encode(encoded_data, &encoded_data);
  // base::Base64Encode(transaction_id, &transaction_id);

  base::DictionaryValue http_params;
  http_params.SetString("transactionId", transaction_id);
  http_params.SetString("prepareDownloadResponse", encoded_data);
  std::string http_body;
  base::JSONWriter::Write(http_params, &http_body);

  SendJsonRequest(
      "https://" + server_hostname_ +
          "/gsma/rsp2/es9plus/getBoundProfilePackage",
      http_body,
      base::Bind(&Smdp::OnGetBoundProfilePackageResponse,
                 weak_factory_.GetWeakPtr(), data_callback, error_callback),
      error_callback);
}

void Smdp::OnGetBoundProfilePackageResponse(
    const GetBoundProfilePackageCallback& data_callback,
    const ErrorCallback& error_callback,
    DictionaryPtr json_dict) {
  std::string transaction_id;
  if (!json_dict->GetString("transactionId", &transaction_id)) {
    LOG(ERROR) << __func__ << ": transactionId not received";
    error_callback.Run(std::vector<uint8_t>());
    return;
  }

  std::string encoded_buffer;
  std::string bound_profile_package;
  if (!json_dict->GetString("boundProfilePackage", &encoded_buffer)) {
    LOG(ERROR) << __func__ << ": boundProfilePackage not received";
    error_callback.Run(std::vector<uint8_t>());
    return;
  }
  base::Base64Decode(encoded_buffer, &bound_profile_package);

  data_callback.Run(transaction_id,
                    std::vector<uint8_t>(bound_profile_package.begin(),
                                         bound_profile_package.end()));
}

}  // namespace hermes
