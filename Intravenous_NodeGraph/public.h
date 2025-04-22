#pragma once
#include "node.h"
#include <atomic>


namespace iv {
    size_t init_graph(
        double* frequency_value,
        Sample* write_buffer[2],
        std::atomic<float>* warp_threshold,
        std::atomic<float>* noise_level,
        std::atomic<float>* noise_lo_pass,
        std::atomic<float>* noise_hi_pass
    ) noexcept;
    void tick(std::span<MidiMessage const> midi, size_t index) noexcept;
    void free_graph();
}
