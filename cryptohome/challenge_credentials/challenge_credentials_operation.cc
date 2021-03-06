// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/challenge_credentials/challenge_credentials_operation.h"

#include <base/bind.h>
#include <base/logging.h>

#include "cryptohome/key_challenge_service.h"

#include "rpc.pb.h"  // NOLINT(build/include)

using brillo::Blob;
using brillo::BlobFromString;
using brillo::BlobToString;

namespace cryptohome {

namespace {

// Is called when a response is received for the sent signature challenge
// request.
void OnKeySignatureChallengeResponse(
    const ChallengeCredentialsOperation::KeySignatureChallengeCallback&
        response_callback,
    std::unique_ptr<KeyChallengeResponse> response) {
  if (!response) {
    LOG(ERROR) << "Signature challenge request failed";
    response_callback.Run(nullptr /* signature */);
    return;
  }
  if (!response->has_signature_response_data() ||
      !response->signature_response_data().has_signature()) {
    LOG(ERROR) << "Signature challenge response is invalid";
    response_callback.Run(nullptr /* signature */);
    return;
  }
  response_callback.Run(std::make_unique<Blob>(
      BlobFromString(response->signature_response_data().signature())));
}

}  // namespace

ChallengeCredentialsOperation::~ChallengeCredentialsOperation() {
  DCHECK(thread_checker_.CalledOnValidThread());
}

ChallengeCredentialsOperation::ChallengeCredentialsOperation(
    KeyChallengeService* key_challenge_service)
    : key_challenge_service_(key_challenge_service) {}

void ChallengeCredentialsOperation::MakeKeySignatureChallenge(
    const std::string& account_id,
    const Blob& public_key_spki_der,
    const Blob& data_to_sign,
    ChallengeSignatureAlgorithm signature_algorithm,
    const KeySignatureChallengeCallback& response_callback) {
  DCHECK(thread_checker_.CalledOnValidThread());

  AccountIdentifier account_identifier;
  account_identifier.set_account_id(account_id);

  KeyChallengeRequest challenge_request;
  challenge_request.set_challenge_type(
      KeyChallengeRequest::CHALLENGE_TYPE_SIGNATURE);
  SignatureKeyChallengeRequestData& challenge_request_data =
      *challenge_request.mutable_signature_request_data();
  challenge_request_data.set_data_to_sign(BlobToString(data_to_sign));
  challenge_request_data.set_public_key_spki_der(
      BlobToString(public_key_spki_der));
  challenge_request_data.set_signature_algorithm(signature_algorithm);

  key_challenge_service_->ChallengeKey(
      account_identifier, challenge_request,
      base::Bind(&OnKeySignatureChallengeResponse, response_callback));
}

}  // namespace cryptohome
