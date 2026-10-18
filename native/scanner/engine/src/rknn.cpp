#include "geometry_decoder.hpp"
#include "pen_stream/stream.hpp"
#include "rknn_api.h"
#include <algorithm>
#include <cstring>
#include <dlfcn.h>
#include <stdexcept>
namespace pen::stream {
namespace {
class Rknn32 final : public Inference {
    std::string path_, runtime_;
    void *library_ = nullptr;
    rknn_context context_ = 0;
    std::vector<std::int8_t> features_;
    std::int8_t lut_[3][256]{};
    decltype(&rknn_init) init_ = nullptr;
    decltype(&rknn_query) query_ = nullptr;
    decltype(&rknn_inputs_set) inputs_ = nullptr;
    decltype(&rknn_run) run_ = nullptr;
    decltype(&rknn_outputs_get) get_ = nullptr;
    decltype(&rknn_outputs_release) release_ = nullptr;
    decltype(&rknn_destroy) destroy_ = nullptr;
    template <class F> void symbol(F &f, const char *n) {
        f = reinterpret_cast<F>(dlsym(library_, n));
        if (!f)
            throw std::runtime_error("rknn_missing_public_symbol");
    }
    void check(int rc) {
        if (rc)
            throw std::runtime_error("rknn_error_" + std::to_string(rc));
    }

  public:
    Rknn32(std::string p, std::string r) : path_(std::move(p)), runtime_(std::move(r)) {
        const unsigned mul[3] = {31662, 32368, 32225};
        const int off[3] = {-131, -127, -114};
        for (unsigned c = 0; c < 3; ++c)
            for (unsigned x = 0; x < 256; ++x) {
                unsigned product = x * mul[c], q = product >> 15, rem = product & 32767;
                int v = int(q + (rem > 16384 || (rem == 16384 && (q & 1)))) + off[c];
                lut_[c][x] = std::int8_t(std::clamp(v, -128, 127));
            }
    }
    ~Rknn32() {
        close();
    }
    void open() override {
        if (context_)
            return;
        try {
            library_ = dlopen(runtime_.c_str(), RTLD_NOW | RTLD_LOCAL);
            if (!library_)
                throw std::runtime_error("rknn_runtime_load_failed");
            symbol(init_, "rknn_init");
            symbol(query_, "rknn_query");
            symbol(inputs_, "rknn_inputs_set");
            symbol(run_, "rknn_run");
            symbol(get_, "rknn_outputs_get");
            symbol(release_, "rknn_outputs_release");
            symbol(destroy_, "rknn_destroy");
            check(init_(&context_, const_cast<char *>(path_.c_str()), 0, 0, nullptr));
            rknn_input_output_num io{};
            check(query_(context_, RKNN_QUERY_IN_OUT_NUM, &io, sizeof(io)));
            if (io.n_input != 1 || io.n_output != 9)
                throw std::runtime_error("rknn_wrong_io_count");
            rknn_tensor_attr a{};
            check(query_(context_, RKNN_QUERY_INPUT_ATTR, &a, sizeof(a)));
            bool dims = (a.fmt == RKNN_TENSOR_NHWC && a.dims[1] == 144 && a.dims[2] == 80 &&
                         a.dims[3] == 16) ||
                        (a.fmt == RKNN_TENSOR_NCHW && a.dims[1] == 16 && a.dims[2] == 144 &&
                         a.dims[3] == 80);
            if (a.n_dims != 4 || a.dims[0] != 32 || !dims || a.type != RKNN_TENSOR_INT8 ||
                a.zp != -12 || a.scale != 0.017723115161061287f)
                throw std::runtime_error("rknn_not_verified_fixed32_contract");
            rknn_input_range range{};
            if (query_(context_, RKNN_QUERY_INPUT_DYNAMIC_RANGE, &range, sizeof(range)) == 0 &&
                range.shape_number)
                throw std::runtime_error("rknn_dynamic_model_rejected");
            features_.resize(32 * 144 * 80 * 16);
        } catch (...) {
            close();
            throw;
        }
    }
    void infer(const NetworkBatch &batch, Heads &heads) override {
        if (!context_ || batch.validCount == 0 || batch.validCount > 32)
            throw std::runtime_error("rknn_invalid_batch");
        std::fill(features_.begin(), features_.end(), std::int8_t(-12));
        constexpr unsigned pixels = 144 * 80;
        constexpr std::uint8_t pad[3] = {124, 116, 104};
        for (unsigned t = 0; t < 32; ++t) {
            if (t < batch.validCount && !batch.gray[t])
                throw std::runtime_error("rknn_null_frame");
            auto *out = features_.data() + t * pixels * 16;
            for (unsigned i = 0; i < pixels; ++i)
                for (unsigned c = 0; c < 3; ++c)
                    out[i * 16 + c] = lut_[c][t < batch.validCount ? batch.gray[t][i] : pad[c]];
        }
        rknn_input input{};
        input.buf = features_.data();
        input.size = features_.size();
        input.type = RKNN_TENSOR_INT8;
        input.fmt = RKNN_TENSOR_NHWC;
        input.pass_through = 1;
        check(inputs_(context_, 1, &input));
        check(run_(context_, nullptr));
        rknn_output output[9]{};
        for (unsigned h = 0; h < 9; ++h) {
            output[h].index = h;
            output[h].want_float = 1;
        }
        check(get_(context_, 9, output, nullptr));
        try {
            for (unsigned h = 0; h < 9; ++h) {
                std::size_t n = 32 * y07::kHeadBins[h];
                if (!output[h].buf || output[h].size != n * sizeof(float))
                    throw std::runtime_error("rknn_output_size_mismatch");
                heads[h].resize(n);
                std::memcpy(heads[h].data(), output[h].buf, n * sizeof(float));
            }
        } catch (...) {
            release_(context_, 9, output);
            throw;
        }
        check(release_(context_, 9, output));
    }
    std::size_t managedBytes() const noexcept override {
        return features_.capacity();
    }
    void close() noexcept override {
        if (context_ && destroy_)
            destroy_(context_);
        context_ = 0;
        std::vector<std::int8_t>().swap(features_);
        if (library_)
            dlclose(library_);
        library_ = nullptr;
    }
};
} // namespace
std::unique_ptr<Inference> makeRknn32(std::string p, std::string r) {
    return std::make_unique<Rknn32>(std::move(p), std::move(r));
}
} // namespace pen::stream
