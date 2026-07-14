#include "BmpViewerActivity.h"

#include <Bitmap.h>
#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Memory.h>

#include <algorithm>

#include "CrossPointSettings.h"
#include "Epub/converters/PngToFramebufferConverter.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {

bool isViewableImageFile(const std::string& filename) {
  return FsHelpers::hasBmpExtension(filename) || FsHelpers::hasPngExtension(filename);
}

bool isMacOSSidecarFile(const std::string& filename) { return filename.rfind("._", 0) == 0; }

void drawImageError(GfxRenderer& renderer, const MappedInputManager& mappedInput, const char* message) {
  renderer.clearScreen();
  renderer.drawCenteredText(UI_10_FONT_ID, renderer.getScreenHeight() / 2, message);
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer(HalDisplay::HALF_REFRESH);
}

}  // namespace

BmpViewerActivity::BmpViewerActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string path)
    : Activity("BmpViewer", renderer, mappedInput), filePath(std::move(path)) {}

void BmpViewerActivity::loadSiblingImages() {
  siblingImages.clear();
  currentImageIndex = -1;

  if (filePath.empty()) return;

  std::string dirPath = FsHelpers::extractFolderPath(filePath);
  size_t lastSlash = filePath.find_last_of('/');
  std::string fileName = (lastSlash != std::string::npos) ? filePath.substr(lastSlash + 1) : filePath;

  auto dir = Storage.open(dirPath.c_str());
  if (!dir || !dir.isDirectory()) {
    if (dir) dir.close();
    return;
  }

  char name[500];
  for (auto file = dir.openNextFile(); file; file = dir.openNextFile()) {
    if (!file.isDirectory()) {
      file.getName(name, sizeof(name));
      if (name[0] != '.' && !isMacOSSidecarFile(name)) {
        std::string fname(name);
        if (isViewableImageFile(fname)) {
          siblingImages.push_back(fname);
        }
      }
    }
    file.close();
  }
  dir.close();

  FsHelpers::sortFileList(siblingImages);

  for (size_t i = 0; i < siblingImages.size(); ++i) {
    if (siblingImages[i] == fileName) {
      currentImageIndex = static_cast<int>(i);
      break;
    }
  }
}

bool BmpViewerActivity::renderPngImage() {
  ImageDimensions dims;
  if (!PngToFramebufferConverter::getDimensionsStatic(filePath, dims)) {
    drawImageError(renderer, mappedInput, "Invalid PNG File");
    return false;
  }

  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  float scale = 1.0f;
  if (dims.width > pageWidth || dims.height > pageHeight) {
    const float scaleX = static_cast<float>(pageWidth) / static_cast<float>(dims.width);
    const float scaleY = static_cast<float>(pageHeight) / static_cast<float>(dims.height);
    scale = std::min(scaleX, scaleY);
  }

  const int drawWidth = std::max(1, static_cast<int>(static_cast<float>(dims.width) * scale));
  const int drawHeight = std::max(1, static_cast<int>(static_cast<float>(dims.height) * scale));
  const int x = (pageWidth - drawWidth) / 2;
  const int y = (pageHeight - drawHeight) / 2;

  RenderConfig config;
  config.x = x;
  config.y = y;
  config.maxWidth = drawWidth;
  config.maxHeight = drawHeight;
  config.useGrayscale = true;
  config.useDithering = true;
  config.performanceMode = false;
  config.useExactDimensions = true;

  PngToFramebufferConverter converter;
  renderer.clearScreen();
  if (!converter.decodeToFramebuffer(filePath, renderer, config)) {
    drawImageError(renderer, mappedInput, "Invalid PNG File");
    return false;
  }

  bool hasPrevious = (siblingImages.size() > 1 && currentImageIndex > 0);
  bool hasNext = (siblingImages.size() > 1 && currentImageIndex != -1 &&
                  currentImageIndex < static_cast<int>(siblingImages.size()) - 1);

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", (hasPrevious ? "<" : ""), (hasNext ? ">" : ""));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
  return true;
}

void BmpViewerActivity::onEnter() {
  Activity::onEnter();

  if (siblingImages.empty() && !filePath.empty()) {
    loadSiblingImages();
  }

  HalFile file;

  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  if (FsHelpers::hasPngExtension(filePath)) {
    renderPngImage();
    return;
  }

  // 1. Open the file
  if (Storage.openFileForRead("BMP", filePath, file)) {
    Bitmap bitmap(file, true);

    // 2. Parse headers to get dimensions
    if (bitmap.parseHeaders() == BmpReaderError::Ok) {
      int x, y;

      if (bitmap.getWidth() > pageWidth || bitmap.getHeight() > pageHeight) {
        float ratio = static_cast<float>(bitmap.getWidth()) / static_cast<float>(bitmap.getHeight());
        const float screenRatio = static_cast<float>(pageWidth) / static_cast<float>(pageHeight);

        if (ratio > screenRatio) {
          // Wider than screen
          x = 0;
          y = std::round((static_cast<float>(pageHeight) - static_cast<float>(pageWidth) / ratio) / 2);
        } else {
          // Taller than screen
          x = std::round((static_cast<float>(pageWidth) - static_cast<float>(pageHeight) * ratio) / 2);
          y = 0;
        }
      } else {
        // Center small images
        x = (pageWidth - bitmap.getWidth()) / 2;
        y = (pageHeight - bitmap.getHeight()) / 2;
      }

      // 4. Prepare Rendering
      bool hasPrevious = (siblingImages.size() > 1 && currentImageIndex > 0);
      bool hasNext = (siblingImages.size() > 1 && currentImageIndex != -1 &&
                      currentImageIndex < static_cast<int>(siblingImages.size()) - 1);

      const auto labels =
          mappedInput.mapLabels(tr(STR_BACK), tr(STR_SET_SLEEP_COVER), (hasPrevious ? "<" : ""), (hasNext ? ">" : ""));

      const bool hasGreyscale = bitmap.hasGreyscale();
      bool grayscaleSinglePassUsed = false;

      renderer.clearScreen();

      if (hasGreyscale) {
        // Try the single SD-read-pass path first: draws the B/W base directly into
        // the framebuffer and the LSB/MSB planes into our own buffers from one
        // sequential read of the file, instead of drawBitmap() re-reading the whole
        // file from SD once per plane. Only bails out for scaled images (see
        // GfxRenderer::drawGrayscaleBitmapSinglePass) — orientation isn't a factor
        // here since it writes full-panel buffers, not physical row-bands.
        if (!grayscaleLsbBuffer) {
          grayscaleLsbBuffer = makeUniqueNoThrow<uint8_t[]>(renderer.getBufferSize());
          grayscaleMsbBuffer = makeUniqueNoThrow<uint8_t[]>(renderer.getBufferSize());
          if (!grayscaleLsbBuffer || !grayscaleMsbBuffer) {
            LOG_ERR("BMP", "Failed to allocate grayscale plane buffers (%zu bytes each)", renderer.getBufferSize());
            grayscaleLsbBuffer.reset();
            grayscaleMsbBuffer.reset();
          }
        }

        if (grayscaleLsbBuffer && grayscaleMsbBuffer) {
          grayscaleSinglePassUsed = renderer.drawGrayscaleBitmapSinglePass(
              bitmap, x, y, pageWidth, pageHeight, grayscaleLsbBuffer.get(), grayscaleMsbBuffer.get());
        }

        if (!grayscaleSinglePassUsed) {
          bitmap.rewindToData();
        }
      }

      if (!grayscaleSinglePassUsed) {
        // Assuming drawBitmap defaults to 0,0 crop if omitted, or pass explicitly: drawBitmap(bitmap, x, y, pageWidth,
        // pageHeight, 0, 0)
        renderer.drawBitmap(bitmap, x, y, pageWidth, pageHeight, 0, 0);
      }

      // Draw UI hints on the base layer
      GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

      if (hasGreyscale) {
        // FAST_REFRESH only skips ghosting when the previous image was also
        // grayscale: the driver's own grayscaleRevert() then fires first and
        // establishes a clean baseline, making our own resync redundant. Otherwise
        // force it ourselves. bmpViewerFastRedraw lets users disable the fast path,
        // which can still leave faint residual ghosting over repeated switches.
        const HalDisplay::RefreshMode baseMode = (SETTINGS.bmpViewerFastRedraw && previousImageWasGreyscale)
                                                     ? HalDisplay::FAST_REFRESH
                                                     : HalDisplay::HALF_REFRESH;
        renderer.displayGrayscaleBase(baseMode);
      } else {
        // Single pass for non-grayscale images
        renderer.displayBuffer(HalDisplay::HALF_REFRESH);
      }
      previousImageWasGreyscale = hasGreyscale;

      if (hasGreyscale && grayscaleSinglePassUsed) {
        // The single pass wrote LSB/MSB into our own buffers, never touching the
        // shared framebuffer, so there's nothing to store/restore around this.
        renderer.copyGrayscaleLsbBuffers(grayscaleLsbBuffer.get());
        renderer.copyGrayscaleMsbBuffers(grayscaleMsbBuffer.get());
        renderer.displayGrayBuffer();
      } else if (hasGreyscale) {
        // Fallback path: re-reads the whole file from SD once per plane.
        // The LSB/MSB passes below reuse the shared framebuffer and leave it holding
        // GRAYSCALE_MSB-mode content, not the base B/W image + button hints. Snapshot
        // the base here and restore it after, or a later plain displayBuffer() call
        // (e.g. the power button's FORCE_REFRESH) redisplays that stale, wrong-mode
        // content instead of the image actually on screen.
        const bool bwBufferStored = renderer.storeBwBuffer();
        if (!bwBufferStored) {
          LOG_ERR("BMP", "Failed to store BW buffer before grayscale render");
        }

        bitmap.rewindToData();
        renderer.clearScreen(0x00);
        renderer.setRenderMode(GfxRenderer::GRAYSCALE_LSB);
        renderer.drawBitmap(bitmap, x, y, pageWidth, pageHeight, 0, 0);
        renderer.copyGrayscaleLsbBuffers();

        bitmap.rewindToData();
        renderer.clearScreen(0x00);
        renderer.setRenderMode(GfxRenderer::GRAYSCALE_MSB);
        renderer.drawBitmap(bitmap, x, y, pageWidth, pageHeight, 0, 0);
        renderer.copyGrayscaleMsbBuffers();

        renderer.displayGrayBuffer();
        if (bwBufferStored) {
          renderer.restoreBwBuffer();
        }
        renderer.setRenderMode(GfxRenderer::BW);
      }

    } else {
      // Handle file parsing error
      drawImageError(renderer, mappedInput, "Invalid BMP File");
    }

    file.close();
  } else {
    // Handle file open error
    drawImageError(renderer, mappedInput, "Could not open file");
  }
}

void BmpViewerActivity::onExit() {
  Activity::onExit();
  renderer.clearScreen();
  renderer.displayBuffer(HalDisplay::HALF_REFRESH);
}

bool BmpViewerActivity::forceCleanGrayscaleRefresh() {
  if (!previousImageWasGreyscale) {
    // Not grayscale — the generic plain-B/W FORCE_REFRESH already handles this.
    return false;
  }
  // Forces onEnter() onto the HALF_REFRESH clean-base path instead of FAST_REFRESH.
  previousImageWasGreyscale = false;
  onEnter();
  return true;
}

void BmpViewerActivity::doSetSleepCover() {
  GUI.drawPopup(renderer, tr(STR_LOADING_POPUP));

  bool success = false;
  HalFile inFile, outFile;
  if (Storage.openFileForRead("BMP", filePath, inFile)) {
    if (Storage.openFileForWrite("BMP", "/sleep.bmp", outFile)) {
      char buffer[2048];
      int bytesRead;
      success = true;
      while ((bytesRead = inFile.read(buffer, sizeof(buffer))) > 0) {
        if (outFile.write(buffer, bytesRead) != bytesRead) {
          success = false;
          break;
        }
      }
      outFile.close();
    }
    inFile.close();
  }

  if (success) {
    SETTINGS.sleepScreen = CrossPointSettings::SLEEP_SCREEN_MODE::CUSTOM;
    SETTINGS.saveToFile();
    GUI.drawPopup(renderer, tr(STR_DONE));
  } else {
    GUI.drawPopup(renderer, tr(STR_FAILED_LOWER));
  }

  delay(1000);
  onEnter();
}

void BmpViewerActivity::loop() {
  // Keep CPU awake/polling so 1st click works
  Activity::loop();

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    activityManager.goToFileBrowser(filePath);
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    if (FsHelpers::hasBmpExtension(filePath)) {
      doSetSleepCover();
    }
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Left) ||
      mappedInput.wasReleased(MappedInputManager::Button::Up)) {
    if (siblingImages.size() > 1 && currentImageIndex > 0) {
      currentImageIndex--;
      std::string dirPath = FsHelpers::extractFolderPath(filePath);
      if (dirPath.back() != '/') dirPath += "/";
      filePath = dirPath + siblingImages[currentImageIndex];
      onEnter();
    }
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Right) ||
      mappedInput.wasReleased(MappedInputManager::Button::Down)) {
    if (siblingImages.size() > 1 && currentImageIndex != -1 &&
        currentImageIndex < static_cast<int>(siblingImages.size()) - 1) {
      currentImageIndex++;
      std::string dirPath = FsHelpers::extractFolderPath(filePath);
      if (dirPath.back() != '/') dirPath += "/";
      filePath = dirPath + siblingImages[currentImageIndex];
      onEnter();
    }
    return;
  }
}
