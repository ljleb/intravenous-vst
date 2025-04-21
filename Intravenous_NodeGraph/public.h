#pragma once
#include "node.h"
#include <atomic>


namespace iv {
    size_t init_graph(
        double* frequency_value,
        Sample* write_buffer[2],
        std::atomic<float>* warp_threshold,
        std::atomic<float>* noise_level,
        std::atomic<float>* noise_filter
    ) noexcept;
    void tick(std::span<MidiMessage const> midi) noexcept;
    void free_graph();
}
