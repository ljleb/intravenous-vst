#pragma once
#include "graph_node.h"
#include "node.h"
#include "alligator.h"
#include "polyblep.h"
#include "fast_bitset.h"
#include "note_number_lookup_table.h"
#include "random123/aes.h"
#include "random123/uniform.hpp"
#include <functional>
#include <array>
#include <optional>
#include <random>
#include <vector>
#include <memory>
#include <cstddef>


namespace iv {
    template<typename BinaryOp>
    constexpr bool binary_op_default_v = 0.0;

    template<typename T>
    constexpr bool binary_op_default_v<std::multiplies<T>> = 1.0;

    template<typename T>
    constexpr bool binary_op_default_v<std::divides<T>> = 1.0;

    template<typename BinaryOp>
    class BinaryOpNode {
        BinaryOp _binary_op;
        size_t _num_inputs;

    public:
        constexpr explicit BinaryOpNode(size_t num_inputs = 2) noexcept :
            _num_inputs(num_inputs)
        {}

        constexpr auto inputs() const noexcept
        {
            return std::vector<InputConfig>(_num_inputs);
        }

        constexpr auto outputs() const noexcept
        {
            return std::array<OutputConfig, 1>{};
        }

        constexpr auto num_inputs() const noexcept
        {
            return _num_inputs;
        }

        constexpr void tick(TickState const& state) noexcept
        {
            auto& out = state.outputs[0];
            Sample result = binary_op_default_v<BinaryOp>;
            for (auto& input : state.inputs)
            {
                result = _binary_op(result, input.get());
            }
            out.push(result);
        }
    };

    using SumNode = BinaryOpNode<std::plus<Sample>>;
    using SubtractNode = BinaryOpNode<std::minus<Sample>>;
    using ProductNode = BinaryOpNode<std::multiplies<Sample>>;
    using QuotientNode = BinaryOpNode<std::divides<Sample>>;

    class BroadcastNode {
        size_t _num_outputs;

    public:
        constexpr explicit BroadcastNode(size_t num_outputs) noexcept :
            _num_outputs(num_outputs)
        {}

        constexpr auto inputs() const noexcept
        {
            return std::array<InputConfig, 1>{};
        }

        constexpr auto outputs() const noexcept
        {
            return std::vector<OutputConfig>(_num_outputs);
        }

        constexpr auto num_outputs() const noexcept
        {
            return _num_outputs;
        }

        constexpr void tick(TickState const& state) noexcept
        {
            auto& in = state.inputs[0];
            Sample sample = in.get();
            for (auto& out : state.outputs)
            {
                out.push(sample);
            }
        }
    };

    struct WarperNode {
        constexpr auto inputs() const noexcept
        {
            return std::array<InputConfig, 2>{};
        }

        constexpr auto outputs() const noexcept
        {
            return std::array {
                OutputConfig { .latency = 1 },
                OutputConfig{},
            };
        }

        void tick(TickState const& state) noexcept
        {
            auto& in = state.inputs[0];
            auto& in_threshold = state.inputs[1];
            auto& out = state.outputs[0];
            auto& out_aliased = state.outputs[1];

            Sample threshold = in_threshold.get();
            Sample sample_prev = out_aliased.get();
            Sample sample = in.get();
            Sample sample_warped = sample;
            bool warped = false;

            if (sample > threshold) { sample_warped = warp_pm1(sample, threshold); warped = true; }
            else if (sample < -threshold) { sample_warped = warp_pm1(sample, threshold); warped = true; }
            out_aliased.push(sample_warped);

            Sample sample_warped_aa = sample_warped;
            if (warped) {
                Sample delta = Sample((sample - sample_prev) / 2.0);
                out.update(sample_prev - polyblep_error(sample_prev, delta, threshold, PolyblepSide::LEFT));
                sample_warped_aa -= polyblep_error(sample_warped_aa, delta, threshold, PolyblepSide::RIGHT);
            }
            out.push(sample_warped_aa);
        }
    };

    class Integrator {
        double const* _update_frequency;

    public:
        constexpr explicit Integrator(double const* update_frequency) noexcept :
            _update_frequency(update_frequency)
        {}

        constexpr auto inputs() const noexcept
        {
            return std::array<iv::InputConfig, 2>{};
        }

        constexpr auto outputs() const noexcept
        {
            return std::array<iv::OutputConfig, 1>{};
        }

        void tick(iv::TickState const& state) noexcept
        {
            auto const& f_prev = state.inputs[0].get();
            auto const& f = state.inputs[1].get();
            auto const& dx = *_update_frequency;
            state.outputs[0].push(f_prev + f * dx);
        }
    };

    struct ConstantNode {
        Sample _value;

        constexpr auto outputs() const noexcept
        {
            return std::array<OutputConfig, 1>{};
        }

        constexpr void tick(TickState const& state) noexcept
        {
            auto& out = state.outputs[0];
            out.push(_value);
        }
    };
    
    class UniformNoiseNode {
        std::optional<std::mt19937> _generator;
        std::optional<std::uniform_real_distribution<Sample>> _distribution;
        Sample _min;
        Sample _max;
        std::optional<unsigned int> _seed;

    public:
        constexpr explicit UniformNoiseNode(
            Sample min = -1.0,
            Sample max = 1.0,
            std::optional<unsigned int> seed = {}
        ) noexcept :
            _min(min),
            _max(max),
            _seed(seed)
        {}

        constexpr auto outputs() const noexcept
        {
            return std::array<OutputConfig, 1>{};
        }

        void tick(TickState const& state) noexcept
        {
            if (!_generator.has_value()) {
                _generator.emplace(_seed.has_value() ? _seed.value() : std::random_device{}());
                _distribution.emplace(_min, _max);
            }
            auto& out = state.outputs[0];
            out.push((*_distribution)(*_generator));
        }
    };

    class DeterministicUniformNoiseNode {
        Sample _min;
        Sample _max;
        size_t _seed;

        uint64_t splitmix64(uint64_t index) const
        {
            size_t z = _seed + index * 0x9e3779b97f4a7c15ULL;
            z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
            z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
            return z ^ (z >> 31);
        }

        double uniform_m11(uint64_t i) const {
            // 1) harvest top 52 bits of i → mantissa
            uint64_t mantissa = i >> (64 - 52);

            // 2) set exponent to (bias+1)=1024 → raw range [2.0, 4.0)
            //    exponent bits = 0x400, so the constant is 0x4000000000000000ULL
            uint64_t bits = 0x4000000000000000ULL | mantissa;

            // 3) reinterpret as double (in [2,4)), then subtract 3.0 → [-1,1)
            double range = (_max - _min)/2.0;
            double min = 2.0*_min - _max;
            return std::bit_cast<double>(bits)*range + min;
        }

    public:
        constexpr explicit DeterministicUniformNoiseNode(
            Sample min = -1.0,
            Sample max = 1.0,
            std::optional<Sample> seed = {}
        ) noexcept :
            _min(min),
            _max(max),
            _seed(seed.has_value() ? *seed : (std::random_device{}() << (sizeof(unsigned int)*CHAR_BIT)) + std::random_device{}())
        {}

        constexpr auto outputs() const noexcept
        {
            return std::array<OutputConfig, 1>{};
        }

        void tick(TickState const& state) noexcept
        {
            auto& out = state.outputs[0];
            uint64_t uniform_int = splitmix64(state.index);
            Sample uniform_float = uniform_m11(uniform_int);
            out.push(uniform_float);
        }
    };

    class DeterministicUniformAESNoiseNode {
        using Rng = r123::AESNI4x32;
        Rng _generator;
        Rng::key_type _seed;
        Sample _min;
        Sample _max;

        static Rng::key_type make_seed(std::optional<uint64_t> seed_opt)
        {
            uint32_t seed_low, seed_high;
            if (seed_opt.has_value())
            {
                uint64_t seed = *seed_opt;
                seed_low = static_cast<uint32_t>(seed);
                seed_high = static_cast<uint32_t>(seed >> 32);
            }
            else
            {
                seed_low = std::random_device{}();
                seed_high = std::random_device{}();
            }
            return Rng::ukey_type { seed_low, seed_high, 0, 0 };
        }

        static Rng::ctr_type make_index(uint64_t index)
        {
            return {
                static_cast<uint32_t>(index),
                static_cast<uint32_t>(index >> 32),
                0,
                0,
            };
        }

    public:
        explicit DeterministicUniformAESNoiseNode(
            Sample min = -1.0,
            Sample max = 1.0,
            std::optional<uint64_t> seed = {}
        ) noexcept :
            _seed(make_seed(seed)),
            _min(min),
            _max(max)
        {
            assert(haveAESNI() && "This machine does not have the AES-NI instruction set, use a different noise node.");
        }

        constexpr auto outputs() const noexcept
        {
            return std::array<OutputConfig, 1>{};
        }

        void tick(TickState const& state) noexcept
        {
            auto& out = state.outputs[0];
            Rng::ctr_type counter = make_index(state.index);
            unsigned int uniform_uint = _generator(counter, _seed)[0];
            Sample uniform_float = r123::uneg11<Sample>(uniform_uint);
            out.push(uniform_float);
        }
    };

    class TypeErasedNode {
        std::shared_ptr<void> _node;
        std::vector<InputConfig> _inputs;
        std::vector<OutputConfig> _outputs;
        size_t _internal_latency;
        std::span<std::byte>(*_init_buffer_fn)(void*, TypeErasedAllocator) noexcept;
        void (*_tick_fn)(void*, TickState const&) noexcept;

    public:
        template<typename Node>
        constexpr /*implicit*/ TypeErasedNode(Node node)
        {
            if constexpr (std::is_empty_v<Node>)
            {
                _node = nullptr;
                _init_buffer_fn = [](void*, TypeErasedAllocator allocator) noexcept { return do_init_buffer(Node{}, allocator); };
                _tick_fn = [](void*, TickState const& state) noexcept { Node{}.tick(state); };
            }
            else
            {
                _node = std::make_shared<Node>(node);
                _init_buffer_fn = [](void* node, TypeErasedAllocator allocator) noexcept { return do_init_buffer(*static_cast<Node*>(node), allocator); };
                _tick_fn = [](void* node, TickState const& state) noexcept { static_cast<Node*>(node)->tick(state); };
            }
            _inputs.assign_range(get_inputs(node));
            _outputs.assign_range(get_outputs(node));
            _internal_latency = get_internal_latency(node);
        }

        constexpr std::span<InputConfig const> inputs() const noexcept
        {
            return _inputs;
        }

        constexpr std::span<OutputConfig const> outputs() const noexcept
        {
            return _outputs;
        }

        constexpr size_t internal_latency() const noexcept
        {
            return _internal_latency;
        }

        template<typename Allocator>
        constexpr std::span<std::byte> init_buffer(Allocator& allocator) const noexcept
        {
            return _init_buffer_fn(_node.get(), TypeErasedAllocator{ allocator });
        }

        void tick(TickState const& state) noexcept
        {
            _tick_fn(_node.get(), state);
        }
    };
}
