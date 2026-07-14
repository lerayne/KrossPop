#pragma once

#include <functional>
#include <memory>
#include <string>

#include "MappedInputManager.h"
#include "activities/Activity.h"

class BmpViewerActivity final : public Activity {
 public:
  BmpViewerActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string filePath);

  void onEnter() override;
  void onExit() override;
  void loop() override;
  bool forceCleanGrayscaleRefresh() override;

 private:
  void loadSiblingImages();
  bool renderPngImage();
  void doSetSleepCover();

  std::string filePath;
  std::vector<std::string> siblingImages;
  int currentImageIndex = -1;

  // Full-panel-sized LSB/MSB grayscale plane buffers for
  // GfxRenderer::drawGrayscaleBitmapSinglePass(). Allocated once on first use
  // and reused across sibling image navigation within this activity's
  // lifetime; freed automatically when the activity is destroyed.
  std::unique_ptr<uint8_t[]> grayscaleLsbBuffer;
  std::unique_ptr<uint8_t[]> grayscaleMsbBuffer;

  // Whether the last image this instance displayed was grayscale — the panel
  // driver only runs its clean-baseline grayscaleRevert() when this was true,
  // so it gates whether the next grayscale switch can safely use FAST_REFRESH.
  bool previousImageWasGreyscale = false;
};
