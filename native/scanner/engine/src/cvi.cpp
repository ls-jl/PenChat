#include "cvi/runtime_abi.hpp"
#include "geometry_decoder.hpp"
#include "pen_stream/stream.hpp"
#include <array>
#include <cstring>
#include <dlfcn.h>
#include <stdexcept>

namespace pen::stream {
namespace {
class Cvi32 final : public Inference {
  std::string path_, runtime_;
  void *library_ = nullptr, *model_ = nullptr;
  cvi::Tensor *input_ = nullptr;
  cvi::Tensor *output_ = nullptr;
  std::array<cvi::Tensor *, 9> heads_{};
  int (*register_)(const char *, void **) = nullptr;
  int (*config_)(void *, int, ...) = nullptr;
  int (*tensors_)(void *, cvi::Tensor **, int *, cvi::Tensor **, int *) = nullptr;
  int (*cleanup_)(void *) = nullptr;
  int (*version_)(void *, int *, int *) = nullptr;
  const char *(*target_)(void *) = nullptr;
  char *(*name_)(cvi::Tensor *) = nullptr;
  std::size_t (*size_)(cvi::Tensor *) = nullptr;
  void *(*pointer_)(cvi::Tensor *) = nullptr;
  int (*forward_)(void *, cvi::Tensor *, int, cvi::Tensor *, int) = nullptr;

  template <class F> void symbol(F &f, const char *name) {
    f = reinterpret_cast<F>(dlsym(library_, name));
    if (!f) throw std::runtime_error("cvi_missing_public_symbol");
  }
  static void check(int code) {
    if (code) throw std::runtime_error("cvi_error_" + std::to_string(code));
  }
  void validate(cvi::Tensor &tensor, const std::array<int, 4> &shape) {
    std::size_t count = 1;
    for (unsigned i = 0; i < shape.size(); ++i) count *= shape[i];
    if (tensor.format != 0 || tensor.shape.dimensions != 4 ||
        tensor.count != count || tensor.bytes != count * sizeof(float) ||
        !tensor.name || name_(&tensor) != tensor.name ||
        size_(&tensor) != tensor.bytes || !pointer_(&tensor))
      throw std::runtime_error("cvi_tensor_contract_mismatch");
    for (unsigned i = 0; i < shape.size(); ++i)
      if (tensor.shape.dim[i] != shape[i])
        throw std::runtime_error("cvi_tensor_shape_mismatch");
  }

public:
  Cvi32(std::string path, std::string runtime)
      : path_(std::move(path)), runtime_(std::move(runtime)) {}
  ~Cvi32() override { close(); }
  void open() override {
    if (model_) return;
    try {
      library_ = dlopen(runtime_.c_str(), RTLD_NOW | RTLD_LOCAL);
      if (!library_) throw std::runtime_error("cvi_runtime_load_failed");
      symbol(register_, "CVI_NN_RegisterModel");
      symbol(config_, "CVI_NN_SetConfig");
      symbol(tensors_, "CVI_NN_GetInputOutputTensors");
      symbol(cleanup_, "CVI_NN_CleanupModel");
      symbol(version_, "CVI_NN_GetModelVersion");
      symbol(target_, "CVI_NN_GetModelTarget");
      symbol(name_, "CVI_NN_TensorName");
      symbol(size_, "CVI_NN_TensorSize");
      symbol(pointer_, "CVI_NN_TensorPtr");
      symbol(forward_, "CVI_NN_Forward");
      check(register_(path_.c_str(), &model_));
      if (!model_) throw std::runtime_error("cvi_null_model");
      int major = 0, minor = 0;
      check(version_(model_, &major, &minor));
      const char *target = target_(model_);
      if (major != 1 || minor != 4 || !target || std::strcmp(target, "cv182x"))
        throw std::runtime_error("cvi_unverified_model_target_or_version");
      check(config_(model_, 9, 0)); // Public OPTION_PROGRAM_INDEX, fixed32 program.
      int inputs = 0, outputs = 0;
      check(tensors_(model_, &input_, &inputs, &output_, &outputs));
      if (inputs != 1 || outputs != 9 || !input_ || !output_)
        throw std::runtime_error("cvi_wrong_io_count");
      validate(*input_, {32, 3, 144, 80});
      if (std::strcmp(input_->name, "img_blocks"))
        throw std::runtime_error("cvi_input_name_mismatch");
      for (unsigned i = 0; i < 9; ++i) {
        if (!output_[i].name) throw std::runtime_error("cvi_missing_head_name");
        unsigned h = 0;
        while (h < 9 && std::strcmp(output_[i].name, cvi::headNames[h])) ++h;
        if (h == 9 || heads_[h]) throw std::runtime_error("cvi_unknown_or_duplicate_head");
        validate(output_[i], {1, 32, int(y07::kHeadBins[h]), 1});
        heads_[h] = &output_[i];
      }
    } catch (...) {
      close();
      throw;
    }
  }
  void infer(const NetworkBatch &batch, Heads &heads) override {
    if (!model_ || !batch.validCount || batch.validCount > 32)
      throw std::runtime_error("cvi_invalid_batch");
    for (unsigned t = 0; t < batch.validCount; ++t)
      if (!batch.gray[t]) throw std::runtime_error("cvi_null_frame");
    // Write directly into the runtime-owned input. No second 4.22MiB staging copy.
    auto *input = static_cast<float *>(pointer_(input_));
    if (!input) throw std::runtime_error("cvi_input_unavailable");
    constexpr unsigned pixels = 144 * 80;
    constexpr float mean[3] = {123.675f, 116.28f, 103.53f};
    constexpr float deviation[3] = {58.395f, 57.12f, 57.375f};
    constexpr std::uint8_t pad[3] = {124, 116, 104};
    for (unsigned t = 0; t < 32; ++t)
      for (unsigned c = 0; c < 3; ++c) {
        float *out = input + (t * 3 + c) * pixels;
        for (unsigned p = 0; p < pixels; ++p)
          out[p] = (float(t < batch.validCount ? batch.gray[t][p] : pad[c]) - mean[c]) /
                   deviation[c];
      }
    check(forward_(model_, input_, 1, output_, 9));
    for (unsigned h = 0; h < heads_.size(); ++h) {
      const auto *data = static_cast<const float *>(pointer_(heads_[h]));
      if (!data) throw std::runtime_error("cvi_output_unavailable");
      heads[h].assign(data, data + 32 * y07::kHeadBins[h]);
    }
  }
  // Tensor storage belongs to the runtime (including ION); do not count it as
  // application-owned RAM, or report zero as total TPU memory usage.
  std::size_t managedBytes() const noexcept override { return 0; }
  void close() noexcept override {
    if (model_ && cleanup_) cleanup_(model_);
    model_ = nullptr;
    input_ = output_ = nullptr;
    heads_.fill(nullptr);
    if (library_) dlclose(library_);
    library_ = nullptr;
  }
};
} // namespace
std::unique_ptr<Inference> makeCvi32(std::string path, std::string runtime) {
  return std::make_unique<Cvi32>(std::move(path), std::move(runtime));
}
} // namespace pen::stream
