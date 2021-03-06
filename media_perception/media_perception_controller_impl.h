// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MEDIA_PERCEPTION_MEDIA_PERCEPTION_CONTROLLER_IMPL_H_
#define MEDIA_PERCEPTION_MEDIA_PERCEPTION_CONTROLLER_IMPL_H_

#include <mojo/public/cpp/bindings/binding.h>

#include "mojom/media_perception_service.mojom.h"

namespace mri {

class MediaPerceptionControllerImpl :
  public chromeos::media_perception::mojom::MediaPerceptionController {
 public:
  MediaPerceptionControllerImpl(
      chromeos::media_perception::mojom::MediaPerceptionControllerRequest
      request);

  void set_connection_error_handler(base::Closure connection_error_handler);

  // chromeos::media_perception::mojom::MediaPerceptionController:
  void ActivateMediaPerception(
      chromeos::media_perception::mojom::MediaPerceptionRequest request)
      override;

 private:
  mojo::Binding<chromeos::media_perception::mojom::MediaPerceptionController>
      binding_;

  DISALLOW_COPY_AND_ASSIGN(MediaPerceptionControllerImpl);
};

}  // namespace mri

#endif  // MEDIA_PERCEPTION_MEDIA_PERCEPTION_CONTROLLER_IMPL_H_
