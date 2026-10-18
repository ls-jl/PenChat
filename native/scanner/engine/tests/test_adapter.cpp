#include "pen_stream/stream.hpp"
#include <array>
#include <dlfcn.h>
#include <iostream>
#include <stdexcept>
int main(int argc, char **argv) {
    try {
        if (argc != 2)
            throw std::runtime_error("mock path required");
        void *l = dlopen(argv[1], RTLD_NOW);
        if (!l)
            throw std::runtime_error(dlerror());
        auto expect = reinterpret_cast<void (*)(int)>(dlsym(l, "mock_expect_valid"));
        auto adapter = pen::stream::makeRknn32("contract-test-only", argv[1]);
        adapter->open();
        if (adapter->managedBytes() != 32 * 144 * 80 * 16)
            throw std::runtime_error("adapter allocated more than fixed32 features");
        std::array<std::uint8_t, 144 * 80> gray;
        gray.fill(150);
        pen::stream::Heads heads;
        for (unsigned n = 1; n <= 32; ++n) {
            expect(n);
            pen::stream::NetworkBatch batch;
            batch.validCount = n;
            batch.gray.fill(gray.data());
            adapter->infer(batch, heads);
        }
        adapter->close();
        dlclose(l);
        std::cout << "RKNN32_PACK_PADDING_AND_NINE_HEADS_OK\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << "\n";
        return 1;
    }
}
