// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "vm_tools/cicerone/tzif_parser.h"

#include <arpa/inet.h>
#include <stdint.h>
#include <string.h>
#include <utility>
#include <vector>

#include <base/files/file.h>
#include <base/files/file_path.h>
#include <base/logging.h>
#include <base/strings/string_util.h>

namespace {

struct tzif_header {
  char magic[4];
  char version;
  char reserved[15];
  int32_t ttisgmtcnt;
  int32_t ttisstdcnt;
  int32_t leapcnt;
  int32_t timecnt;
  int32_t typecnt;
  int32_t charcnt;
};

bool ReadInt(base::File* file, int32_t* out_int) {
  DCHECK(out_int);
  int32_t buf;
  int read = file->ReadAtCurrentPos(reinterpret_cast<char*>(&buf), sizeof(buf));
  if (read != sizeof(buf)) {
    return false;
  }
  // Values are stored in network byte order (highest-order byte first).
  // We probably need to convert them to match the endianness of our system.
  *out_int = ntohl(buf);
  return true;
}

bool ParseTzifHeader(base::File* tzfile, struct tzif_header* header) {
  DCHECK(header);
  int read = tzfile->ReadAtCurrentPos(header->magic, sizeof(header->magic));
  if (read != sizeof(header->magic)) {
    return false;
  }
  if (memcmp(header->magic, "TZif", 4) != 0) {
    return false;
  }

  read = tzfile->ReadAtCurrentPos(&header->version, sizeof(header->version));
  if (read != sizeof(header->version)) {
    return false;
  }
  if (header->version != '\0' && header->version != '2' &&
      header->version != '3') {
    return false;
  }

  read = tzfile->ReadAtCurrentPos(header->reserved, sizeof(header->reserved));
  if (read != sizeof(header->reserved)) {
    return false;
  }
  for (size_t i = 0; i < sizeof(header->reserved); i++) {
    if (header->reserved[i] != 0) {
      return false;
    }
  }

  if (!ReadInt(tzfile, &header->ttisgmtcnt) || header->ttisgmtcnt < 0) {
    return false;
  }
  if (!ReadInt(tzfile, &header->ttisstdcnt) || header->ttisstdcnt < 0) {
    return false;
  }
  if (!ReadInt(tzfile, &header->leapcnt) || header->leapcnt < 0) {
    return false;
  }
  if (!ReadInt(tzfile, &header->timecnt) || header->timecnt < 0) {
    return false;
  }
  if (!ReadInt(tzfile, &header->typecnt) || header->typecnt < 0) {
    return false;
  }
  if (!ReadInt(tzfile, &header->charcnt) || header->charcnt < 0) {
    return false;
  }
  return true;
}

}  // namespace

TzifParser::TzifParser() : zoneinfo_directory_("/usr/share/zoneinfo") {}

bool TzifParser::GetPosixTimezone(const std::string& timezone_name,
                                  std::string* result) {
  DCHECK(result);
  base::File tzfile(zoneinfo_directory_.Append(timezone_name),
                    base::File::FLAG_OPEN | base::File::FLAG_READ);
  struct tzif_header first_header;
  if (!tzfile.IsValid() || !ParseTzifHeader(&tzfile, &first_header)) {
    return false;
  }

  if (first_header.version == '\0') {
    // Generating a POSIX-style TZ string from a TZif version 1 file is hard;
    // TZ strings need relative dates (1st Sunday in March, 1st Sunday in Nov,
    // etc.), but the version 1 files only give absolute dates for each
    // year up to 2037. Fortunately version 1 files are no longer created by
    // the newer versions of the timezone-data package.
    //
    // Because of this, we're not going to try and handle this, and instead just
    // return an error.
    return false;
  }

  // TZif versions 2 and 3 embed a POSIX-style TZ string after their
  // contents. We read that out and return it.

  // Skip past the first body section and all of the second section. See
  // 'man tzfile' for an explanation of these offset values.
  int64_t first_body_size =
      4 * first_header.timecnt + 1 * first_header.timecnt +
      (4 + 1 + 1) * first_header.typecnt + 1 * first_header.charcnt +
      (4 + 4) * first_header.leapcnt + 1 * first_header.ttisstdcnt +
      1 * first_header.ttisgmtcnt;
  tzfile.Seek(base::File::FROM_CURRENT, first_body_size);

  struct tzif_header second_header;
  if (!ParseTzifHeader(&tzfile, &second_header)) {
    return false;
  }

  int64_t second_body_size =
      8 * second_header.timecnt + 1 * second_header.timecnt +
      (4 + 1 + 1) * second_header.typecnt + 1 * second_header.charcnt +
      (8 + 4) * second_header.leapcnt + 1 * second_header.ttisstdcnt +
      1 * second_header.ttisgmtcnt;
  int64_t offset = tzfile.Seek(base::File::FROM_CURRENT, second_body_size);

  std::vector<char> posix_string(tzfile.GetLength() - offset);
  int read = tzfile.ReadAtCurrentPos(&posix_string[0], posix_string.size());
  if (read != posix_string.size()) {
    return false;
  }

  std::string time_string(&posix_string[0], posix_string.size());
  // According to the spec, the embedded string is enclosed by '\n' characters.
  base::TrimWhitespaceASCII(time_string, base::TRIM_ALL, &time_string);
  *result = std::move(time_string);
  return true;
}

void TzifParser::SetZoneinfoDirectoryForTest(base::FilePath dir) {
  zoneinfo_directory_ = dir;
}
