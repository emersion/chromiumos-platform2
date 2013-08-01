// Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "common/util.h"

#include <glib.h>
#include <base/logging.h>

// NOTE: the coding style asks for syslog.h ("C system header") to be
// included before base/logging.h ("Library .h file") but
// unfortunately this is not possible because of a bug in the latter.
#include <syslog.h>

using std::string;

namespace p2p {

namespace util {

static bool SyslogFunc(int severity,
                       const char* file,
                       int line,
                       size_t message_start,
                       const string& str) {
  int base_severity_to_syslog_priority[logging::LOG_NUM_SEVERITIES] = {
    LOG_INFO,     // logging::LOG_INFO
    LOG_WARNING,  // logging::LOG_WARNING
    LOG_ERR,      // logging::LOG_ERROR
    LOG_CRIT,     // logging::LOG_ERROR_REPORT
    LOG_ALERT,    // logging::LOG_FATAL
  };

  int priority = LOG_NOTICE;
  if (severity >= 0 && severity < logging::LOG_NUM_SEVERITIES)
    priority = base_severity_to_syslog_priority[severity];

  // The logging infrastructure includes a terminating newline at the
  // end of the message. We don't want that. Strip it.
  char* message = strdupa(str.c_str() + message_start);
  size_t message_len = strlen(message);
  for (int n = message_len - 1; n >= 0; --n) {
    if (isspace(message[n]))
      message[n] = 0;
    else
      break;
  }
  syslog(priority, "%s [%s:%d]", message, file, line);

  return false;  // also send message to other logging destinations
}

void SetupSyslog(const char* program_name, bool include_pid) {
  int option = LOG_NDELAY | LOG_CONS;
  if (include_pid)
    option |= LOG_PID;
  openlog(program_name, option, LOG_DAEMON);
  logging::SetLogMessageHandler(SyslogFunc);
}

// The encoding of the D-Bus machine-id plus a NUL terminator is 33
// bytes. See http://dbus.freedesktop.org/doc/dbus-specification.html
const size_t kDBusMachineIdPlusNulSize = 33;

// Reads the D-Bus machine-id into |buf| and ensures that it's
// NUL-terminated. It is a programming error to pass a |buf|
// that is not at least |kDBusMachineIdPlusNulSize| bytes long.
static void ReadMachineId(char *buf) {
  size_t num_read = 0;
  FILE* f = NULL;

  // NUL-terminate ahead of time.
  buf[kDBusMachineIdPlusNulSize - 1] = '\0';

  f = fopen("/var/lib/dbus/machine-id", "r");
  if (f == NULL) {
    LOG(ERROR) << "Error opening /var/lib/dbus/machine-id: " << strerror(errno);
    return;
  }

  // The machine-id file may include a newline so only request 32 bytes.
  num_read = fread(buf, 1, kDBusMachineIdPlusNulSize - 1, f);
  if (num_read != kDBusMachineIdPlusNulSize - 1) {
    LOG(ERROR) << "Error reading from /var/lib/dbus/machine-id, num_read="
               << num_read;
    fclose(f);
    return;
  }

  VLOG(1) << "Read machine-id " << buf;

  fclose(f);
}

const char* GetDBusMachineId() {
  static char machine_id[kDBusMachineIdPlusNulSize] = { 0 };

  if (machine_id[0] == '\0') {
    G_STATIC_ASSERT(sizeof machine_id == kDBusMachineIdPlusNulSize);
    ReadMachineId(machine_id);
  }

  return const_cast<const char *>(machine_id);
}

}  // namespace util

}  // namespace p2p
