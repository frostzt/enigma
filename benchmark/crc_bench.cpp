#include <benchmark/benchmark.h>

#include <cstdint>
#include <random>
#include <vector>

#include "enigmadb/crc32.h"

namespace {

std::vector<uint8_t> MakeRandomBuffer(size_t size) {
    std::vector<uint8_t> buffer(size);
    std::mt19937 rng(42);
    std::uniform_int_distribution<int> dist(0, 255);
    for (auto& byte : buffer) {
        byte = static_cast<uint8_t>(dist(rng));
    }
    return buffer;
}

}  // namespace

static void BM_ComputeCrc32(benchmark::State& state) {
    const auto size = static_cast<size_t>(state.range(0));
    const auto buffer = MakeRandomBuffer(size);

    for (auto _ : state) {
        auto result = enigmadb::compute_crc_32(buffer.data(), buffer.size());
        benchmark::DoNotOptimize(result);
    }

    state.SetBytesProcessed(static_cast<int64_t>(state.iterations()) * static_cast<int64_t>(size));
}
BENCHMARK(BM_ComputeCrc32)->Range(64, 1 << 20)->Unit(benchmark::kMicrosecond);

static void BM_Crc32Scalar(benchmark::State& state) {
    const auto size = static_cast<size_t>(state.range(0));
    const auto buffer = MakeRandomBuffer(size);

    for (auto _ : state) {
        auto result = enigmadb::crc32c_scaler(buffer.data(), buffer.size());
        benchmark::DoNotOptimize(result);
    }

    state.SetBytesProcessed(static_cast<int64_t>(state.iterations()) * static_cast<int64_t>(size));
}
BENCHMARK(BM_Crc32Scalar)->Range(64, 1 << 20)->Unit(benchmark::kMicrosecond);

#if defined(ENIGMADB_CRC32_X86) || defined(ENIGMADB_CRC32_ARM)
static void BM_Crc32Hardware(benchmark::State& state) {
    const auto size = static_cast<size_t>(state.range(0));
    const auto buffer = MakeRandomBuffer(size);

    for (auto _ : state) {
        auto result = enigmadb::crc32c_hw(buffer.data(), buffer.size());
        benchmark::DoNotOptimize(result);
    }

    state.SetBytesProcessed(static_cast<int64_t>(state.iterations()) * static_cast<int64_t>(size));
}
BENCHMARK(BM_Crc32Hardware)->Range(64, 1 << 20)->Unit(benchmark::kMicrosecond);
#endif
