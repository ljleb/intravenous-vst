#pragma once
#include "graph_node.h"
#include "node.h"
#include "alligator.h"
#include "polyblep.h"
#include "fast_bitset.h"
#include "note_number_lookup_table.h"
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
