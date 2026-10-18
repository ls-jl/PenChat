#include "rknn_api.h"
#include <cstdint>
#include <cstring>
#include <vector>
static std::vector<float> values[9];
static const unsigned bins[9] = {384, 384, 512, 512, 512, 512, 512, 512, 1};
static int expectedValid = 0;
extern "C" void mock_expect_valid(int n) {
    expectedValid = n;
}
extern "C" int rknn_init(rknn_context *c, void *, uint32_t, uint32_t, rknn_init_extend *) {
    *c = 1;
    return 0;
}
extern "C" int rknn_destroy(rknn_context) {
    return 0;
}
extern "C" int rknn_query(rknn_context, rknn_query_cmd command, void *info, uint32_t size) {
    if (command == RKNN_QUERY_IN_OUT_NUM) {
        auto *p = (rknn_input_output_num *)info;
        p->n_input = 1;
        p->n_output = 9;
        return 0;
    }
    if (command == RKNN_QUERY_INPUT_ATTR) {
        auto *a = (rknn_tensor_attr *)info;
        std::memset(a, 0, size);
        a->n_dims = 4;
        a->dims[0] = 32;
        a->dims[1] = 144;
        a->dims[2] = 80;
        a->dims[3] = 16;
        a->fmt = RKNN_TENSOR_NHWC;
        a->type = RKNN_TENSOR_INT8;
        a->zp = -12;
        a->scale = 0.017723115161061287f;
        return 0;
    }
    if (command == RKNN_QUERY_INPUT_DYNAMIC_RANGE) {
        std::memset(info, 0, size);
        return 0;
    }
    return -5;
}
extern "C" int rknn_inputs_set(rknn_context, uint32_t n, rknn_input inputs[]) {
    if (n != 1 || inputs[0].size != 32 * 144 * 80 * 16 || !inputs[0].pass_through ||
        inputs[0].fmt != RKNN_TENSOR_NHWC || inputs[0].type != RKNN_TENSOR_INT8)
        return -51;
    const auto *p = (const std::int8_t *)inputs[0].buf;
    const unsigned mul[] = {31662, 32368, 32225};
    const int off[] = {-131, -127, -114};
    const unsigned pad[] = {124, 116, 104};
    for (unsigned t = 0; t < 32; ++t)
        for (unsigned i = 0; i < 144 * 80; ++i)
            for (unsigned c = 0; c < 16; ++c) {
                int value = -12;
                if (c < 3) {
                    unsigned product = (t < unsigned(expectedValid) ? 150 : pad[c]) * mul[c],
                             q = product >> 15, r = product & 32767;
                    value = int(q + (r > 16384 || (r == 16384 && (q & 1)))) + off[c];
                    if (value < -128)
                        value = -128;
                    if (value > 127)
                        value = 127;
                }
                if (p[(t * 144 * 80 + i) * 16 + c] != value)
                    return -52;
            }
    return 0;
}
extern "C" int rknn_run(rknn_context, rknn_run_extend *) {
    return 0;
}
extern "C" int rknn_outputs_get(rknn_context, uint32_t n, rknn_output output[],
                                rknn_output_extend *) {
    if (n != 9)
        return -5;
    for (unsigned h = 0; h < 9; ++h) {
        values[h].assign(32 * bins[h], 0);
        output[h].buf = values[h].data();
        output[h].size = values[h].size() * 4;
    }
    return 0;
}
extern "C" int rknn_outputs_release(rknn_context, uint32_t, rknn_output[]) {
    return 0;
}
