// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Provide access to the Chrome OS master configuration from device tree

#include "chromeos-config/libcros_config/cros_config_fdt.h"

// TODO(sjg@chromium.org): See if this can be accepted upstream.
extern "C" {
#include <libfdt.h>
};

#include <stdlib.h>

#include <iostream>
#include <sstream>
#include <string>

#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/strings/string_split.h>
#include <base/strings/string_util.h>

namespace {
const char kTargetDirsPath[] = "/chromeos/schema/target-dirs";
const char kSchemaPath[] = "/chromeos/schema";
const char kPhandleProperties[] = "phandle-properties";
const char kConfigDtbPath[] = "/usr/share/chromeos-config/config.dtb";
}  // namespace

namespace brillo {

CrosConfigFdt::CrosConfigFdt() {}

CrosConfigFdt::~CrosConfigFdt() {}

bool CrosConfigFdt::InitModel() {
  return InitForConfig(base::FilePath(kConfigDtbPath));
}

std::string CrosConfigFdt::GetFullPath(ConfigNode node) {
  const void* blob = blob_.c_str();
  char buf[256];
  int err;

  err = fdt_get_path(blob, node.GetOffset(), buf, sizeof(buf));
  if (err) {
    CROS_CONFIG_LOG(WARNING) << "Cannot get full path: " << fdt_strerror(err);
    return "unknown";
  }

  return std::string(buf);
}

ConfigNode CrosConfigFdt::GetPathNode(ConfigNode base_node,
                                      const std::string& path) {
  const void* blob = blob_.c_str();
  auto parts = base::SplitString(path.substr(1), "/", base::KEEP_WHITESPACE,
                                 base::SPLIT_WANT_ALL);
  int node = base_node.GetOffset();
  for (auto part : parts) {
    node = fdt_subnode_offset(blob, node, part.c_str());
    if (node < 0) {
      break;
    }
  }
  return ConfigNode(node);
}

bool CrosConfigFdt::LookupPhandle(ConfigNode node,
                                  const std::string& prop_name,
                                  ConfigNode* node_out) {
  const void* blob = blob_.c_str();
  int len;
  const fdt32_t* ptr = static_cast<const fdt32_t*>(
      fdt_getprop(blob, node.GetOffset(), prop_name.c_str(), &len));

  // We probably don't need all these checks since validation will ensure that
  // the config is correct. But this is a critical tool and we want to avoid
  // crashes in any situation.
  *node_out = ConfigNode();
  if (!ptr) {
    return false;
  }
  if (len != sizeof(fdt32_t)) {
    CROS_CONFIG_LOG(ERROR) << prop_name << " phandle for model " << model_
                           << " is of size " << len << " but should be "
                           << sizeof(fdt32_t);
    return false;
  }
  int phandle = fdt32_to_cpu(*ptr);
  int target_node = fdt_node_offset_by_phandle(blob, phandle);
  if (target_node < 0) {
    CROS_CONFIG_LOG(ERROR) << prop_name << "lookup for model " << model_
                           << " failed: " << fdt_strerror(target_node);
    return false;
  }
  *node_out = ConfigNode(target_node);
  return true;
}

int CrosConfigFdt::FindIDsInMap(int node,
                                const std::string& find_name,
                                int find_sku_id,
                                std::string* platform_name_out) {
  const void* blob = blob_.c_str();
  VLOG(1) << "Trying " << fdt_get_name(blob, node, NULL);
  const char* smbios_name = static_cast<const char*>(
      fdt_getprop(blob, node, "smbios-name-match", NULL));
  if (smbios_name &&
      (find_name.empty() || strcmp(smbios_name, find_name.c_str()))) {
    CROS_CONFIG_LOG(INFO) << "SMBIOS name " << smbios_name << " does not match "
                          << find_name;
    return 0;
  }

  // If we have a single SKU, deal with that first
  int len = 0;
  const fdt32_t* data =
      (const fdt32_t*)fdt_getprop(blob, node, "single-sku", &len);
  int found_phandle = 0;
  if (data) {
    if (len != sizeof(fdt32_t)) {
      CROS_CONFIG_LOG(ERROR) << "single-sku: Invalid length " << len;
      return -1;
    }
    found_phandle = fdt32_to_cpu(*data);
    CROS_CONFIG_LOG(INFO) << "Single SKU match";
  } else {
    // Locate the map and make sure it is a multiple of 2 cells (first is SKU
    // ID, second is phandle).
    const fdt32_t *data, *end, *ptr;
    data = static_cast<const fdt32_t*>(
        fdt_getprop(blob, node, "simple-sku-map", &len));
    if (!data) {
      CROS_CONFIG_LOG(ERROR)
          << "Cannot find simple-sku-map: " << fdt_strerror(node);
      return -1;
    }
    if (len % (sizeof(fdt32_t) * 2)) {
      // Validation of configuration should catch this, so this should never
      // happen. But we don't want to crash.
      CROS_CONFIG_LOG(ERROR)
          << "single-sku-map: " << fdt_get_name(blob, node, NULL)
          << " invalid length " << len;
      return -1;
    }

    // Search for the SKU ID in the list.
    for (end = data + len / sizeof(fdt32_t), ptr = data; ptr < end; ptr += 2) {
      int sku_id = fdt32_to_cpu(ptr[0]);
      int phandle = fdt32_to_cpu(ptr[1]);

      if (sku_id == find_sku_id) {
        found_phandle = phandle;
        break;
      }
    }
    if (!found_phandle) {
      VLOG(1) << "SKU ID " << find_sku_id << " not found in mapping";
      return 0;
    }
    CROS_CONFIG_LOG(INFO) << "Simple SKU map match ";
  }

  const char* pname =
      static_cast<const char*>(fdt_getprop(blob, node, "platform-name", NULL));
  if (pname)
    *platform_name_out = pname;
  else
    *platform_name_out = "unknown";
  CROS_CONFIG_LOG(INFO) << "Platform name " << *platform_name_out;

  return found_phandle;
}

int CrosConfigFdt::FindIDsInAllMaps(int mapping_node,
                                    const std::string& find_name,
                                    int find_sku_id,
                                    std::string* platform_name_out) {
  const void* blob = blob_.c_str();
  int subnode;

  fdt_for_each_subnode(subnode, blob, mapping_node) {
    int phandle =
        FindIDsInMap(subnode, find_name, find_sku_id, platform_name_out);
    if (phandle < 0) {
      return -1;
    } else if (phandle > 0) {
      return phandle;
    }
  }
  return 0;
}

int CrosConfigFdt::FollowPhandle(int phandle, int* target_out) {
  const void* blob = blob_.c_str();

  // Follow the phandle to the target
  int node = fdt_node_offset_by_phandle(blob, phandle);
  if (node < 0) {
    CROS_CONFIG_LOG(ERROR) << "Cannot find phandle for sku ID: "
                           << fdt_strerror(node);
    return -1;
  }

  // Figure out whether the target is a model or a sub-model
  int parent = fdt_parent_offset(blob, node);
  if (parent < 0) {
    CROS_CONFIG_LOG(ERROR) << "Cannot find parent of phandle target: "
                           << fdt_strerror(node);
    return -1;
  }
  const char* parent_name = fdt_get_name(blob, parent, NULL);
  int model_node;
  if (!strcmp(parent_name, "submodels")) {
    model_node = fdt_parent_offset(blob, parent);
    if (model_node < 0) {
      CROS_CONFIG_LOG(ERROR)
          << "Cannot find sub-model parent: " << fdt_strerror(node);
      return -1;
    }
  } else if (!strcmp(parent_name, "models")) {
    model_node = node;
  } else {
    CROS_CONFIG_LOG(ERROR) << "Phandle target parent " << parent_name
                           << " is invalid";
    return -1;
  }
  *target_out = node;

  return model_node;
}

bool CrosConfigFdt::SelectModelConfigByIDs(
    const std::string& find_name,
    int find_sku_id,
    const std::string& find_whitelabel_name) {
  const void* blob = blob_.c_str();
  CROS_CONFIG_LOG(INFO) << "Looking up name " << find_name << ", SKU ID "
                        << find_sku_id;

  int mapping_node = fdt_path_offset(blob, "/chromeos/family/mapping");
  if (mapping_node < 0) {
    CROS_CONFIG_LOG(ERROR) << "Cannot find mapping node: "
                           << fdt_strerror(mapping_node);
    return false;
  }

  std::string platform_name;
  int phandle =
      FindIDsInAllMaps(mapping_node, find_name, find_sku_id, &platform_name);
  if (phandle <= 0) {
    return false;
  }
  int target;
  int model_offset = FollowPhandle(phandle, &target);
  if (model_offset < 0) {
    return false;
  }

  //  We found the model node, so set up the data
  platform_name_ = platform_name;
  model_node_ = ConfigNode(model_offset);
  model_name_ = fdt_get_name(blob, model_offset, NULL);
  if (target != model_offset) {
    submodel_node_ = ConfigNode(target);
    submodel_name_ = fdt_get_name(blob, target, NULL);
  } else {
    submodel_node_ = ConfigNode();
    submodel_name_ = "";
  }

  // If this is a whitelabel model, use the tag.
  int firmware_node = fdt_subnode_offset(blob, model_offset, "firmware");
  if (firmware_node >= 0) {
    if (fdt_getprop(blob, firmware_node, "sig-id-in-customization-id", NULL)) {
      int models_node = fdt_path_offset(blob, "/chromeos/models");
      int wl_model =
          fdt_subnode_offset(blob, models_node, find_whitelabel_name.c_str());
      if (wl_model >= 0) {
        whitelabel_node_ = model_node_;
        model_node_ = ConfigNode(wl_model);
      } else {
        CROS_CONFIG_LOG(ERROR)
            << "Cannot find whitelabel model " << find_whitelabel_name
            << ": using " << model_name_ << ": " << fdt_strerror(wl_model);
      }
    }
  }
  int wl_tags_node = fdt_subnode_offset(blob, model_offset, "whitelabels");
  if (wl_tags_node >= 0) {
    int wl_tag =
        fdt_subnode_offset(blob, wl_tags_node, find_whitelabel_name.c_str());
    if (wl_tag >= 0) {
      whitelabel_tag_node_ = ConfigNode(wl_tag);
    } else {
      CROS_CONFIG_LOG(ERROR)
          << "Cannot find whitelabel tag " << find_whitelabel_name << ": using "
          << model_name_ << ": " << fdt_strerror(wl_tag);
    }
  }

  return true;
}

const char* CrosConfigFdt::GetProp(const ConfigNode& node,
                                   std::string name,
                                   int* len_out) {
  const void* blob = blob_.c_str();
  return static_cast<const char*>(
      fdt_getprop(blob, node.GetOffset(), name.c_str(), len_out));
}

bool CrosConfigFdt::ReadConfigFile(const base::FilePath& filepath) {
  if (!base::ReadFileToString(filepath, &blob_)) {
    CROS_CONFIG_LOG(ERROR) << "Could not read file " << filepath.MaybeAsASCII();
    return false;
  }
  const void* blob = blob_.c_str();
  int ret = fdt_check_header(blob);
  if (ret) {
    CROS_CONFIG_LOG(ERROR) << "Config file " << filepath.MaybeAsASCII()
                           << " is invalid: " << fdt_strerror(ret);
    return false;
  }

  int target_dirs_offset = fdt_path_offset(blob, kTargetDirsPath);
  if (target_dirs_offset >= 0) {
    for (int poffset = fdt_first_property_offset(blob, target_dirs_offset);
         poffset >= 0; poffset = fdt_next_property_offset(blob, poffset)) {
      int len;
      const struct fdt_property* prop =
          fdt_get_property_by_offset(blob, poffset, &len);
      const char* name = fdt_string(blob, fdt32_to_cpu(prop->nameoff));
      target_dirs_[name] = prop->data;
    }
  } else if (target_dirs_offset < 0) {
    CROS_CONFIG_LOG(WARNING) << "Cannot find " << kTargetDirsPath
                             << " node: " << fdt_strerror(target_dirs_offset);
  }
  int schema_offset = fdt_path_offset(blob, kSchemaPath);
  if (schema_offset >= 0) {
    int len;
    const char* prop =
        GetProp(ConfigNode(schema_offset), kPhandleProperties, &len);
    if (prop) {
      const char* end = prop + len;
      for (const char* ptr = prop; ptr < end; ptr += strlen(ptr) + 1) {
        phandle_props_.push_back(ptr);
      }
    } else {
      CROS_CONFIG_LOG(WARNING) << "Cannot find property " << kPhandleProperties
                               << " node: " << fdt_strerror(len);
    }
  } else if (schema_offset < 0) {
    CROS_CONFIG_LOG(WARNING) << "Cannot find " << kSchemaPath
                             << " node: " << fdt_strerror(schema_offset);
  }
  return true;
}

}  // namespace brillo