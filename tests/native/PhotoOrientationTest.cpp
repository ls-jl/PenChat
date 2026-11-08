#include "photo_orientation.hpp"
#include <cassert>
#include <iostream>

using namespace penchat;

namespace {
struct Picture {
  std::vector<uint8_t> rgb;
  int width, height;
};
Picture rotate(const Picture &source, int angle) {
  Picture out{rotatePhoto(source.rgb, source.width, source.height, angle),
              source.width, source.height};
  if (angle % 180) std::swap(out.width, out.height);
  return out;
}
void same(const Picture &left, const Picture &right) {
  assert(left.width == right.width && left.height == right.height);
  assert(left.rgb == right.rgb);
}
}

int main() {
  // Asymmetric optical target: both orientation and width/height matter.
  const Picture raw{{1,1,1, 2,2,2, 3,3,3,
                     4,4,4, 5,5,5, 6,6,6}, 3, 2};
  const int expectedCorners[4] = {1,4,6,3};
  int index = 0;
  for (int pose : {0,90,180,270}) {
    const auto landscape = photoTransforms(pose, false);
    const auto portrait = photoTransforms(pose, true);
    assert(landscape.preview == pose && landscape.saved == pose);
    assert(landscape.workspace == (360 - pose) % 360);
    assert(portrait.preview == 0 && portrait.workspace == 0);
    assert(portrait.saved == pose);

    // The original landscape view and fixed portrait view both cancel sensor
    // pose in screen coordinates, without involving Falcon display/touch angles.
    same(rotate(rotate(raw, landscape.preview), landscape.workspace), raw);
    same(rotate(rotate(raw, portrait.preview), portrait.workspace), raw);
    const auto saved = rotate(raw, portrait.saved);
    assert(saved.rgb.front() == expectedCorners[index++]);
    assert(saved.width == (pose % 180 ? raw.height : raw.width));
    assert(saved.height == (pose % 180 ? raw.width : raw.height));
    same(saved, rotate(raw, landscape.saved));

    // Crop reads the already-normalized file in its own pixel coordinates.
    // Reapplying pose here would rotate twice; an identity full-image crop must
    // preserve both its asymmetric corner labels and its source dimensions.
    same(rotate(saved, 0), saved);
    same(rotate(saved, (360 - pose) % 360), raw);
  }
  assert(photoTransforms(-90, true).saved == 270);
  assert(photoTransforms(450, false).preview == 90);
  assert(photoPreviewCorrection("RK3562 Y07 V1") == 270);
  assert(photoPreviewCorrection("Rockchip RK3562 Y07 LP4 V10 Board") == 270);
  assert(photoPreviewCorrection("RK3562 Y02 V1") == 0);
  assert(photoPreviewCorrection("RK3562 ORANGE V1") == 0);
  assert(photoPreviewCorrection("RK3562 Y070 V1") == 0);
  assert(photoPreviewCorrection("") == 0);
  assert(photoAccelerometerCorrection("Rockchip RK3562 Y07 LP4 V10 Board") == 270);
  assert(photoAccelerometerCorrection("RK3562 Y02 V1") == 0);
  assert(photoAccelerometerCorrection("RK3562 ORANGE V1") == 0);
  assert(photoAccelerometerCorrection("RK3562 Y070 V1") == 0);
  const int axes[4][2]={{1024,0},{0,1024},{-1024,0},{0,-1024}};
  for(int i=0;i<4;++i) {
    const int standard=i*90,corrected=(standard+270)%360;
    PhotoOrientation ordinary, y07(270);
    const int starting=(corrected+180)%360;
    assert(ordinary.update(axes[i][0],axes[i][1],0,100,(standard+180)%360)==(standard+180)%360);
    assert(ordinary.update(axes[i][0],axes[i][1],0,700,(standard+180)%360)==standard);
    assert(y07.update(axes[i][0],axes[i][1],0,100,starting)==starting);
    assert(y07.update(axes[i][0],axes[i][1],0,700,starting)==corrected);
    assert(y07.update(0,0,0,800,corrected)==corrected);
  }
  for (int pose : {0,90,180,270}) {
    const auto y07 = photoTransforms(pose, true, 270);
    assert(y07.preview == 270 && y07.workspace == 0 && y07.saved == pose);
    same(rotate(raw, y07.preview), rotate(raw, 270));
    assert(rotate(raw, y07.preview).rgb.front() == 3);
    // A landscape screen is not opted into another board's fixed correction.
    assert(photoTransforms(pose, false, 270).preview == pose);
  }

  // A shutter keeps its frozen angle even if later samples advance the pose.
  const int shutter = photoTransforms(90, true).saved;
  PhotoOrientation gravity;
  assert(gravity.update(-1024,0,0,100,90) == 90);
  assert(gravity.update(-1024,0,0,700,90) == 180);
  assert(shutter == 90);
  std::cout << "four-pose preview/workspace/save/crop transforms passed\n";
}
