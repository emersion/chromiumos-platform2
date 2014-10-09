// Copyright 2014 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peerd/peer.h"

#include <base/format_macros.h>
#include <base/guid.h>
#include <base/memory/weak_ptr.h>
#include <base/strings/string_util.h>
#include <chromeos/dbus/exported_object_manager.h>

#include "peerd/dbus_constants.h"
#include "peerd/typedefs.h"

using base::Time;
using base::TimeDelta;
using chromeos::Error;
using chromeos::dbus_utils::AsyncEventSequencer;
using chromeos::dbus_utils::DBusInterface;
using chromeos::dbus_utils::DBusObject;
using chromeos::dbus_utils::ExportedObjectManager;
using dbus::ObjectPath;
using peerd::dbus_constants::kPeerFriendlyName;
using peerd::dbus_constants::kPeerInterface;
using peerd::dbus_constants::kPeerLastSeen;
using peerd::dbus_constants::kPeerNote;
using peerd::dbus_constants::kPeerUUID;
using peerd::dbus_constants::kServicePathFragment;
using std::map;
using std::string;
using std::vector;
using std::unique_ptr;

namespace {

const size_t kMaxFriendlyNameLength = 31;
const size_t kMaxNoteLength = 255;
const char kValidFriendlyNameCharacters[] = "abcdefghijklmnopqrstuvwxyz"
                                            "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
                                            "0123456789"
                                            "_-,.?! ";
const char kValidNoteCharacters[] = "abcdefghijklmnopqrstuvwxyz"
                                    "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
                                    "0123456789"
                                    "_-,.?! ";

}  // namespace

namespace peerd {

namespace errors {
namespace peer {

const char kInvalidUUID[] = "peer.uuid";
const char kInvalidName[] = "peer.name";
const char kInvalidNote[] = "peer.note";
const char kInvalidTime[] = "peer.time";
const char kUnknownService[] = "peer.unknown_service";

}  // namespace peer
}  // namespace errors

Peer::Peer(const scoped_refptr<dbus::Bus>& bus,
           ExportedObjectManager* object_manager,
           const ObjectPath& path)
    : bus_(bus),
      dbus_object_(new DBusObject{object_manager, bus, path}),
      service_path_prefix_(path.value() + kServicePathFragment) {
}

bool Peer::RegisterAsync(
    chromeos::ErrorPtr* error,
    const std::string& uuid,
    const std::string& friendly_name,
    const std::string& note,
    const Time& last_seen,
    const CompletionAction& completion_callback) {
  if (!base::IsValidGUID(uuid)) {
    Error::AddTo(error, kPeerdErrorDomain, errors::peer::kInvalidUUID,
                 "Invalid UUID for peer.");
    return false;
  }
  if (!SetFriendlyName(error, friendly_name)) { return false; }
  if (!SetNote(error, note)) { return false; }
  if (!SetLastSeen(error, last_seen)) { return false; }
  DBusInterface* itf = dbus_object_->AddOrGetInterface(kPeerInterface);
  itf->AddProperty(kPeerUUID, &uuid_);
  itf->AddProperty(kPeerFriendlyName, &name_);
  itf->AddProperty(kPeerNote, &note_);
  itf->AddProperty(kPeerLastSeen, &last_seen_);
  dbus_object_->RegisterAsync(completion_callback);
  return true;
}

bool Peer::SetFriendlyName(chromeos::ErrorPtr* error,
                           const string& friendly_name) {
  if (!IsValidFriendlyName(error, friendly_name)) { return false; }
  name_.SetValue(friendly_name);
  return true;
}

bool Peer::SetNote(chromeos::ErrorPtr* error, const string& note) {
  if (!IsValidNote(error, note)) { return false; }
  note_.SetValue(note);
  return true;
}

bool Peer::SetLastSeen(chromeos::ErrorPtr* error, const Time& last_seen) {
  if (!IsValidUpdateTime(error, last_seen)) {
    return false;
  }
  uint64_t milliseconds_since_epoch = 0;
  CHECK(TimeToMillisecondsSinceEpoch(last_seen, &milliseconds_since_epoch));
  last_seen_.SetValue(milliseconds_since_epoch);
  return true;
}

std::string Peer::GetUUID() const {
  return uuid_.value();
}

std::string Peer::GetFriendlyName() const {
  return name_.value();
}

std::string Peer::GetNote() const {
  return note_.value();
}

Time Peer::GetLastSeen() const {
  return TimeDelta::FromMilliseconds(last_seen_.value()) + Time::UnixEpoch();
}

bool Peer::IsValidFriendlyName(chromeos::ErrorPtr* error,
                               const std::string& friendly_name) const {
  if (friendly_name.length() > kMaxFriendlyNameLength) {
    Error::AddToPrintf(error, kPeerdErrorDomain, errors::peer::kInvalidName,
                       "Bad length for %s: %" PRIuS,
                       kPeerFriendlyName, friendly_name.length());
    return false;
  }
  if (!base::ContainsOnlyChars(friendly_name, kValidFriendlyNameCharacters)) {
    Error::AddToPrintf(error, kPeerdErrorDomain, errors::peer::kInvalidName,
                       "Invalid characters in %s.", kPeerFriendlyName);
    return false;
  }
  return true;
}

bool Peer::IsValidNote(chromeos::ErrorPtr* error,
                       const std::string& note) const {
  if (note.length() > kMaxNoteLength) {
    Error::AddToPrintf(error, kPeerdErrorDomain, errors::peer::kInvalidNote,
                       "Bad length for %s: %" PRIuS,
                       kPeerNote, note.length());
    return false;
  }
  if (!base::ContainsOnlyChars(note, kValidNoteCharacters)) {
    Error::AddToPrintf(error, kPeerdErrorDomain, errors::peer::kInvalidNote,
                       "Invalid characters in %s.", kPeerNote);
    return false;
  }
  return true;
}

bool Peer::IsValidUpdateTime(chromeos::ErrorPtr* error,
                             const base::Time& last_seen) const {
  uint64_t milliseconds_since_epoch = 0;
  if (!TimeToMillisecondsSinceEpoch(last_seen, &milliseconds_since_epoch)) {
    Error::AddTo(error, kPeerdErrorDomain, errors::peer::kInvalidTime,
                 "Negative time update is invalid.");
    return false;
  }
  if (milliseconds_since_epoch < last_seen_.value()) {
    Error::AddTo(error, kPeerdErrorDomain, errors::peer::kInvalidTime,
                 "Discarding update to last seen time as stale.");
    return false;
  }
  return true;
}

bool Peer::AddService(chromeos::ErrorPtr* error,
                      const string& service_id,
                      const vector<ip_addr>& addresses,
                      const map<string, string>& service_info) {
  ObjectPath service_path(service_path_prefix_.value() +
                          std::to_string(++services_added_));
  // TODO(wiley): There is a potential race here.  If we remove the service
  //              too quickly, we race with DBus export completing.  We'll
  //              have to be careful in Service not to assume export has
  //              finished.
  scoped_refptr<AsyncEventSequencer> sequencer(new AsyncEventSequencer());
  unique_ptr<Service> new_service{new Service{
      bus_, dbus_object_->GetObjectManager().get(), service_path}};
  const bool success = new_service->RegisterAsync(
      error, service_id, addresses, service_info,
      sequencer->GetHandler("Failed exporting service.", true));
  if (success) {
    services_.emplace(service_id, std::move(new_service));
    sequencer->OnAllTasksCompletedCall({});
  }
  return success;
}

bool Peer::RemoveService(chromeos::ErrorPtr* error,
                         const string& service_id) {
  if (services_.erase(service_id) == 0) {
    Error::AddTo(error,
                 kPeerdErrorDomain,
                 errors::peer::kUnknownService,
                 "Unknown service id.");
    return false;
  }
  return true;
}

bool Peer::TimeToMillisecondsSinceEpoch(const Time& time, uint64_t* ret) const {
  int64_t time_diff = (time - Time::UnixEpoch()).InMilliseconds();
  if (time_diff < 0) {
    return false;
  }
  *ret = static_cast<uint64_t>(time_diff);
  return true;
}

}  // namespace peerd
