#include "platform_inference.hpp"
#include "runtime_io.hpp"
#include "file_read.hpp"
#include <thread>

namespace penchat_scan {
namespace {
// Only Engine's backend owner calls these methods. UI/capture never opts in.
class CompatibleRknn final : public pen::stream::Inference {
  std::unique_ptr<pen::stream::Inference> backend_;
  const bool sliced_;
  std::thread::id owner_;
public:
  CompatibleRknn(std::unique_ptr<pen::stream::Inference> backend, bool sliced)
      : backend_(std::move(backend)), sliced_(sliced) {}
  ~CompatibleRknn() override { close(); }
  void open() override {
    if (owner_==std::thread::id()) owner_=std::this_thread::get_id();
    if (owner_!=std::this_thread::get_id()) throw std::runtime_error("scan backend owner changed");
    RuntimeIoScope scope(sliced_); backend_->open();
  }
  void infer(const pen::stream::NetworkBatch &batch, pen::stream::Heads &heads) override {
    if (owner_!=std::this_thread::get_id()) throw std::runtime_error("scan inference outside backend owner");
    RuntimeIoScope scope(sliced_); backend_->infer(batch,heads);
  }
  void close() noexcept override {
    RuntimeIoScope scope(sliced_ && owner_==std::this_thread::get_id()); backend_->close();
  }
  size_t managedBytes() const noexcept override { return backend_->managedBytes(); }
};
}
std::string scanRuntimePath(const CaptureProfile &p, const std::string &resources) {
  switch (p.inference.runtime) {
  case RuntimeKind::SystemRknn: return "librknnrt.so";
  case RuntimeKind::PrivateRknn:
    if (resources.empty() || resources.front()!='/')
      throw std::runtime_error("private scan runtime requires absolute resources");
    return resources+"/lib/"+p.inference.privateLibrary;
  case RuntimeKind::Cvi: return "/oem/YoudaoDictPen/output/libs/libcviruntime.so";
  }
  throw std::runtime_error("unsupported scan runtime");
}
std::unique_ptr<pen::stream::Inference> makePlatformInference(const CaptureProfile &p,
                                                            const std::string &resources) {
  validateProfile(p);
  const auto model=resources+"/"+p.inference.model, runtime=scanRuntimePath(p,resources);
#ifdef PENCHAT_CVI_CAPTURE
  if (p.platform!=PlatformKind::Cvi) throw std::runtime_error("RK inference unavailable in CVI executable");
  return pen::stream::makeCvi32(model,runtime);
#else
  if (p.platform!=PlatformKind::Rockchip) throw std::runtime_error("CVI inference unavailable in RK executable");
  // A copied policy alone must not enable the board-specific workaround.
  bool sliced=false;
  if (p.inference.compatibility==RuntimeCompatibility::Y02Dma091 && p.id=="y02-sc035gs") {
    const auto actual=scanProfile(readOptionalFile("/proc/device-tree/model"),
      readOptionalFile("/sys/class/video4linux/v4l-subdev2/name"));
    sliced=actual.id==p.id;
  }
  return std::unique_ptr<pen::stream::Inference>(new CompatibleRknn(pen::stream::makeRknn32(model,runtime),sliced));
#endif
}
} // namespace penchat_scan
