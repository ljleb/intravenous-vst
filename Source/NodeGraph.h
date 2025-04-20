#pragma once
#include <vector>
#include <array>
#include <memory>
#include <span>
#include <unordered_map>
#include <unordered_set>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <deque>
#include <cassert>
#include <random>
#include <bit>
#include <type_traits>
#include <functional>
#include <bitset>
#include <variant>
#include <numeric>
#include "note_number_lookup_table.h"


namespace constexpr_math {

    // constexpr signbit
    template<typename T>
    constexpr bool signbit(T x) noexcept {
        static_assert(std::is_floating_point_v<T>, "Only floating point types supported");
        if constexpr (sizeof(T) == sizeof(std::uint32_t)) {
            using UInt = std::uint32_t;
            UInt bits = std::bit_cast<UInt>(x);
            return (bits >> 31) != 0;
        }
        else if constexpr (sizeof(T) == sizeof(std::uint64_t)) {
            using UInt = std::uint64_t;
            UInt bits = std::bit_cast<UInt>(x);
            return (bits >> 63) != 0;
        }
        else {
            // fallback: architecture‑dependent long double
            return x < T(0) || std::isnan(x) && std::signbit(x);
        }
    }

    // constexpr copysign
    template<typename T>
    constexpr T copysign(T magnitude, T sign) noexcept {
        static_assert(std::is_floating_point_v<T>, "Only floating point types supported");
        if constexpr (sizeof(T) == sizeof(std::uint32_t)) {
            using UInt = std::uint32_t;
            constexpr UInt sign_mask = UInt(1) << 31;
            UInt mag_bits = std::bit_cast<UInt>(magnitude) & ~sign_mask;
            UInt sign_bits = std::bit_cast<UInt>(sign) & sign_mask;
            return std::bit_cast<T>(mag_bits | sign_bits);
        }
        else if constexpr (sizeof(T) == sizeof(std::uint64_t)) {
            using UInt = std::uint64_t;
            constexpr UInt sign_mask = UInt(1) << 63;
            UInt mag_bits = std::bit_cast<UInt>(magnitude) & ~sign_mask;
            UInt sign_bits = std::bit_cast<UInt>(sign) & sign_mask;
            return std::bit_cast<T>(mag_bits | sign_bits);
        }
        else {
            // fallback to value‑based copy‑sign
            T mag_abs = magnitude < T(0) ? -magnitude : magnitude;
            return constexpr_math::signbit(sign)
                ? -mag_abs
                : mag_abs;
        }
    }

} // namespace constexpr_math

// Usage examples:
static_assert(!constexpr_math::signbit(+0.0f));
static_assert(constexpr_math::signbit(-0.0f));
static_assert(constexpr_math::signbit(-123.0));
static_assert(!constexpr_math::signbit(+4.56));

static_assert(constexpr_math::copysign(3.14, -1.0) == -3.14);
static_assert(constexpr_math::copysign(-2.71, +1.0) == +2.71);
static_assert(constexpr_math::copysign(0.0, -0.0) == -0.0);
static_assert(constexpr_math::copysign(-0.0, +0.0) == +0.0);


namespace iv {
    using Sample = float;

    enum struct PolyblepSide {
        LEFT,
        RIGHT,
    };

    static bool is_power_of_2(size_t n) noexcept
    {
        return n && !(n & (n - 1));
    }

    static Sample polyblep_phi(Sample sample, Sample warp_threshold) noexcept
    {
        return Sample((sample + warp_threshold) / 2.0);
    }

    static Sample polyblep_p(Sample phi, Sample delta, Sample warp_threshold, PolyblepSide side) noexcept
    {
        if (side == PolyblepSide::RIGHT && phi < delta)
        {
            Sample first_order = 2.f * phi / delta;
            Sample second_order = phi / delta;
            return (first_order - second_order * second_order - 1) * warp_threshold;
        }
        if (side == PolyblepSide::LEFT && delta > warp_threshold - phi)
        {
            Sample second_order = (phi - warp_threshold) / delta + 1;
            return second_order * second_order * warp_threshold;
        }
        return 0;
    }

    static Sample polyblep_error(Sample sample, Sample delta, Sample warp_threshold, PolyblepSide side) noexcept
    {
        Sample sign = std::copysign(1.0, delta);
        delta = std::copysign(delta, 1.0);

        Sample phi = polyblep_phi(sample, warp_threshold);
        Sample p = polyblep_p(phi, delta, warp_threshold, side) * sign;
        return p;
    }

    static inline Sample warp_pm1(Sample x, Sample limit) noexcept
    {
        Sample period = Sample(2.0 * limit);
        return x - std::floor((x + limit) / period) * period;
    }

    enum struct MidiMessageType {
        NOTE_ON,
        NOTE_OFF,
    };

    struct MidiMessage {
        MidiMessageType type;
        union {
            struct {
                uint8_t note_number;
                uint8_t amplitude;
            } note_on;
            struct {
                uint8_t note_number;
            } note_off;
        };
    };

    struct SharedPortData {
        std::span<Sample> buffer;
        size_t position;
        size_t latency;

        constexpr explicit SharedPortData(
            std::span<Sample> buffer = {},
            size_t latency = 0
        ) noexcept :
            buffer(buffer),
            position(0),
            latency(latency)
        {
        }
    };

    class InputPort {
        SharedPortData& _shared_data;
        size_t _history;

    public:
        explicit InputPort(
            SharedPortData& shared_data,
            size_t history
        ) :
            _shared_data(shared_data),
            _history(history)
        {
            if (!is_power_of_2(_shared_data.buffer.size())) {
                throw "buffer size should be a power of 2";
            }
        }

        constexpr Sample get(size_t offset = 0) const noexcept
        {
            if (offset > _history) return 0.0;
            size_t idx = (_shared_data.position + _shared_data.buffer.size() - offset) & (_shared_data.buffer.size() - 1);
            return _shared_data.buffer[idx];
        }

        constexpr void push(Sample value) noexcept
        {
            _shared_data.position = (_shared_data.position + 1) & (_shared_data.buffer.size() - 1);
            _shared_data.buffer[_shared_data.position] = value;
        }

        constexpr void update(Sample value, size_t offset = 0) noexcept
        {
            if (offset > _shared_data.latency) return;
            size_t idx = (_shared_data.position + _shared_data.latency - offset) & (_shared_data.buffer.size() - 1);
            _shared_data.buffer[idx] = value;
        }

        constexpr size_t latency() noexcept
        {
            return _shared_data.latency;
        }

        constexpr size_t buffer_size() noexcept
        {
            return _shared_data.buffer.size();
        }
    };

    class OutputPort {
        SharedPortData& _shared_data;

    public:
        explicit OutputPort(SharedPortData& shared_data) :
            _shared_data(shared_data)
        {
            if (!is_power_of_2(_shared_data.buffer.size())) {
                throw "buffer size should be a power of 2";
            }
        }

        constexpr Sample get(size_t offset = 0) const noexcept
        {
            if (offset > _shared_data.latency) return 0.0;
            size_t idx = (_shared_data.position + offset) & (_shared_data.buffer.size() - 1);
            return _shared_data.buffer[idx];
        }

        constexpr void push(Sample value) noexcept
        {
            _shared_data.position = (_shared_data.position + 1) & (_shared_data.buffer.size() - 1);
            _shared_data.buffer[_shared_data.position] = value;
        }

        constexpr void update(Sample value, size_t offset = 0) noexcept
        {
            if (offset > _shared_data.latency) return;
            size_t idx = (_shared_data.position + _shared_data.latency - offset) & (_shared_data.buffer.size() - 1);
            _shared_data.buffer[idx] = value;
        }
    };

    struct InputConfig {
        size_t history = 0;
        Sample default_value = 0.0;
    };

    struct OutputConfig {
        size_t latency = 0;
    };

    struct NodeState {
        std::span<InputPort> inputs;
        std::span<OutputPort> outputs;
        std::span<std::byte> buffer;
    };

    struct TickState : public NodeState {
        std::span<MidiMessage const> midi;
        double sample_rate;
    };

    namespace details
    {
        template <typename Node>
        concept has_outputs = requires(Node node, std::span<OutputConfig const> outputs)
        {
            outputs = node.outputs();
        };

        template <typename Node>
        concept has_num_outputs = requires(Node node, size_t num_outputs)
        {
            num_outputs = node.num_outputs();
        };

        template <typename Node>
        concept has_inputs = requires(Node node, std::span<InputConfig const> inputs)
        {
            inputs = node.inputs();
        };

        template <typename Node>
        concept has_num_inputs = requires(Node node, size_t num_inputs)
        {
            num_inputs = node.num_inputs();
        };

        template <typename Node, typename Allocator>
        concept has_init_buffer = requires(Node node, Allocator allocator)
        {
            node.init_buffer(allocator);
        };

        template <typename Node>
        concept has_internal_latency = requires(Node node, size_t internal_latency)
        {
            internal_latency = node.internal_latency();
        };
    }

    template<typename Node>
    constexpr auto get_outputs(Node const& node) noexcept
    {
        if constexpr (details::has_outputs<Node>)
        {
            return node.outputs();
        }
        else
        {
            return std::array<OutputConfig, 0>{};
        }
    }

    template<typename Node>
    constexpr auto get_num_outputs(Node const& node) noexcept
    {
        if constexpr (details::has_num_outputs<Node>)
        {
            return node.num_outputs();
        }
        else
        {
            return get_outputs(node).size();
        }
    }

    template<typename Node>
    constexpr auto get_inputs(Node const& node) noexcept
    {
        if constexpr (details::has_inputs<Node>)
        {
            return node.inputs();
        }
        else
        {
            return std::array<InputConfig, 0>{};
        }
    }

    template<typename Node>
    constexpr auto get_num_inputs(Node const& node) noexcept
    {
        if constexpr (details::has_num_inputs<Node>)
        {
            return node.num_inputs();
        }
        else
        {
            return get_inputs(node).size();
        }
    }

    template<typename Node, typename Allocator>
    constexpr std::span<std::byte> do_init_buffer(Node const& node, Allocator& allocator) noexcept
    {
        if constexpr (details::has_init_buffer<Node, Allocator>)
        {
            std::span<std::byte> memory_before = allocator.get_buffer();
            node.init_buffer(allocator);
            std::span<std::byte> memory_after = allocator.get_buffer();

            std::byte* before_end = memory_before.data() + memory_before.size();
            std::byte* after_end = memory_after.data() + memory_after.size();
            assert(before_end == after_end);
            return { memory_before.data(), memory_before.size() - memory_after.size() };
        }
        else
        {
            return { allocator.get_buffer().data(), 0 };
        }
    }

    template<typename Node>
    constexpr size_t get_internal_latency(Node const& node) noexcept
    {
        if constexpr (details::has_internal_latency<Node>)
        {
            return node.internal_latency();
        }
        else
        {
            return 0;
        }
    }

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
        {
        }

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
            if (state.inputs.empty())
            {
                out.push(binary_op_default_v<BinaryOp>);
                return;
            }
            Sample result = state.inputs[0].get();
            size_t inputs_size = state.inputs.size();
            for (size_t i = 1; i < inputs_size; ++i)
            {
                result = _binary_op(result, state.inputs[i].get());
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
        {
        }

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
            return std::array{
                OutputConfig{.latency = 1},
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
        double const* _sample_rate;

    public:
        constexpr explicit Integrator(double const* sample_rate) noexcept :
            _sample_rate(sample_rate)
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
            auto const& inv_dx = *_sample_rate;
            state.outputs[0].push(f_prev + f / inv_dx);
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

    template<typename T>
    union AlignedStorage
    {
        alignas(T) std::byte uninitialized_object[sizeof(T)];
        T object;

        constexpr explicit AlignedStorage() :
            uninitialized_object{}
        {}
    };

    struct PortId {
        size_t node;
        size_t port;

        bool operator==(auto const& other) const noexcept
        {
            return node == other.node && port == other.port;
        }
    };

    struct GraphEdge {
        PortId source, target;

        bool operator==(auto const& other) const noexcept
        {
            return source == other.source && target == other.target;
        }
    };
}

template<>
struct std::hash<iv::PortId>
{
    std::hash<size_t> size_t_hash;
    std::size_t operator()(const iv::PortId& p) const noexcept
    {
        return size_t_hash(p.node) ^ (~size_t_hash(p.port)-1);
    }
};

template<>
struct std::hash<iv::GraphEdge>
{
    std::hash<iv::PortId> port_id_hash;
    std::size_t operator()(const iv::GraphEdge& e) const noexcept
    {
        return port_id_hash(e.source) ^ (~port_id_hash(e.target)-1);
    }
};

namespace iv
{
    struct FixedBufferAllocator
    {
        std::span<std::byte> buffer;

        constexpr bool can_allocate() const noexcept
        {
            return true;
        }

        constexpr std::span<std::byte> get_buffer() const noexcept
        {
            return buffer;
        }

        template<typename T>
        auto initialize_array(size_t number)
        {
            if (number == 0) return std::span<T>{};
            size_t num_bytes = number * sizeof(T);
            size_t const alignment = alignof(T);
            void* buffer_start = buffer.data();
            size_t space_left = buffer.size();
            if (!std::align(alignof(T), num_bytes, buffer_start, space_left)) throw std::bad_alloc();
            T* ptr = ::new (buffer_start) T[number];
            buffer = { static_cast<std::byte*>(buffer_start) + num_bytes, space_left - num_bytes };
            return std::span<T> { ptr, number };
        };

        template<typename T>
        T& initialize_object()
        {
            size_t num_bytes = sizeof(T);
            size_t const alignment = alignof(T);
            void* buffer_start = buffer.data();
            size_t space_left = buffer.size();
            if (!std::align(alignof(T), num_bytes, buffer_start, space_left)) throw std::bad_alloc();
            T* ptr = ::new (buffer_start) T;
            buffer = { static_cast<std::byte*>(buffer_start) + num_bytes, space_left - num_bytes };
            return *ptr;
        }

        template<typename T>
        auto allocate_array(size_t number)
        {
            auto span = initialize_array<AlignedStorage<T>>(number);
            return std::span<T> { &(span.data()->object), span.size() };
        };

        template<typename T, typename... Args>
        void construct_at(T* ptr, Args&&... args) const
        {
            ::new (ptr) T(std::forward<Args>(args)...);
        }

        template<typename T, typename U>
        constexpr T& assign(T& t, U&& u) const
        {
            return t = std::forward<U>(u);
        }

        template<typename T>
        constexpr auto at(std::span<T> t, size_t i) const -> T&
        {
            return t[i];
        }

        template<typename T, size_t N>
        constexpr auto at(std::array<T, N>& t, size_t i) const -> T&
        {
            return t[i];
        }

        template<typename R, typename T>
        void fill_n(R& range, T&& t) const
        {
            std::fill_n(range.begin(), range.size(), std::forward<T>(t));
        }
    };

    class CountingNonAllocator
    {
        static constexpr size_t MAX_ALLOCATION = std::numeric_limits<uint32_t>::max();
        std::byte* _memory_hint = nullptr;
        size_t total_bytes = MAX_ALLOCATION;

    public:
        explicit CountingNonAllocator(std::byte* memory_hint) :
            _memory_hint(memory_hint)
        {}

        constexpr bool can_allocate() const noexcept
        {
            return false;
        }

        constexpr std::span<std::byte> get_buffer() const
        {
            return { _memory_hint, total_bytes };
        }

        constexpr size_t estimate_buffer_size() const
        {
            return MAX_ALLOCATION - total_bytes;
        }

        template<typename T>
        void advance_buffer(size_t number)
        {
            if (number == 0) return;
            size_t const alignment = alignof(T);
            size_t const num_bytes = number * sizeof(T);
            void* buffer_start = static_cast<void*>(_memory_hint);
            if (!std::align(alignment, num_bytes, buffer_start, total_bytes)) throw std::bad_alloc();
            _memory_hint = static_cast<std::byte*>(buffer_start) + num_bytes;
            total_bytes -= num_bytes;
        };

        template<typename T>
        auto initialize_array(size_t number)
        {
            advance_buffer<T>(number);
            return std::span<T> { static_cast<T*>(nullptr), number };
        };

        template<typename T>
        T& initialize_object()
        {
            static AlignedStorage<T> storage;
            advance_buffer<AlignedStorage<T>>(1);
            return storage.object;
        }

        template<typename T>
        auto allocate_array(size_t number)
        {
            std::span<AlignedStorage<T>> span = initialize_array<AlignedStorage<T>>(number);
            return std::span<T> { static_cast<T*>(nullptr), span.size() };
        };

        template<typename T, typename... Args>
        void construct_at(T*, Args&&...) const
        {}

        template<typename T, typename U>
        constexpr T& assign(T&, U&&) const
        {
            static AlignedStorage<T> storage;
            return storage.object;
        }

        template<typename T>
        constexpr auto at(std::span<T>, size_t) const -> T&
        {
            static AlignedStorage<T> storage;
            return storage.object;
        }

        template<typename T, size_t N>
        constexpr auto at(std::array<T, N>&, size_t) const -> T&
        {
            static AlignedStorage<T> storage;
            return storage.object;
        }

        template<typename R, typename T>
        void fill_n(R&, T&&) const
        {}
    };

    struct TypeErasedAllocator
    {
        std::variant<std::reference_wrapper<FixedBufferAllocator>, std::reference_wrapper<CountingNonAllocator>> _allocator;

        constexpr bool can_allocate() const noexcept
        {
            return std::visit([](auto&& allocator) { return allocator.get().can_allocate(); }, _allocator);
        }

        constexpr std::span<std::byte> get_buffer() const
        {
            return std::visit([](auto&& allocator) { return allocator.get().get_buffer(); }, _allocator);
        }

        template<typename T>
        auto initialize_array(size_t number)
        {
            return std::visit([=](auto&& allocator) { return allocator.get().initialize_array<T>(number); }, _allocator);
        };

        template<typename T>
        auto initialize_object() -> T&
        {
            return std::visit([](auto&& allocator) -> T& { return allocator.get().initialize_object<T>(); }, _allocator);
        }

        template<typename T>
        auto allocate_array(size_t number)
        {
            return std::visit([=](auto&& allocator) { return allocator.get().allocate_array<T>(number); }, _allocator);
        };

        template<typename T, typename... Args>
        void construct_at(T* ptr, Args&&... args) const
        {
            std::visit([&](auto&& allocator) { return allocator.get().construct_at(ptr, std::forward<Args>(args)...); }, _allocator);
        }

        template<typename T, typename U>
        constexpr T& assign(T& t, U&& u) const
        {
            return std::visit([&](auto&& allocator) -> T& { return allocator.get().assign(t, std::forward<U>(u)); }, _allocator);
        }

        template<typename T>
        constexpr auto at(std::span<T> t, size_t i) const -> T&
        {
            return std::visit([&](auto&& allocator) -> T& { return allocator.get().at(t, i); }, _allocator);
        }

        template<typename T, size_t N>
        constexpr auto at(std::array<T, N>& t, size_t i) const -> T&
        {
            return std::visit([&](auto&& allocator) -> T& { return allocator.get().at(t, i); }, _allocator);
        }

        template<typename R, typename T>
        void fill_n(R& r, T&& t) const
        {
            std::visit([&](auto&& allocator) { return allocator.get().fill_n(r, std::forward<T>(t)); }, _allocator);
        }
    };

    class TypeErasedNode {
        std::shared_ptr<void> _node;
        std::vector<InputConfig> _inputs;
        std::vector<OutputConfig> _outputs;
        size_t _internal_latency;
        std::span<std::byte> (*_init_buffer_fn)(void*, TypeErasedAllocator) noexcept;
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
            auto inputs = get_inputs(node);
            _inputs.assign_range(inputs);
            auto outputs = get_outputs(node);
            _outputs.assign_range(outputs);
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
            return _init_buffer_fn(_node.get(), TypeErasedAllocator{allocator});
        }

        void tick(TickState const& state) noexcept
        {
            _tick_fn(_node.get(), state);
        }
    };

    static size_t calculate_port_buffer_size(size_t latency, size_t history) noexcept
    {
        size_t min_size = 1 + latency + history;
        size_t pow2_size = size_t(1) << size_t(std::ceil(std::log2(min_size)));
        return pow2_size;
    };

    struct GraphNode {
        using Nodes = std::vector<TypeErasedNode>;
        using Edges = std::unordered_set<GraphEdge>;

        struct GraphState : public NodeState {
            std::span<NodeState> node_states;
        };

        Nodes _nodes;
        Edges _edges;
        size_t _num_public_inputs;
        size_t _num_public_outputs;

    public:
        static constexpr size_t GRAPH_ID = std::numeric_limits<size_t>::max();

        explicit GraphNode(Nodes nodes, Edges edges, size_t num_inputs = 0, size_t num_outputs = 0) :
            _nodes(std::move(nodes)),
            _edges(std::move(edges)),
            _num_public_inputs(num_inputs),
            _num_public_outputs(num_outputs)
        {
            expand_hyperedge_ports();
            sort_nodes();
            validate_graph();
        }

        constexpr auto inputs() const noexcept
        {
            return std::vector<InputConfig>(_num_public_inputs);
        }

        constexpr auto outputs() const noexcept
        {
            return std::vector<OutputConfig>(_num_public_outputs);
        }

        constexpr auto num_inputs() const noexcept
        {
            return _num_public_inputs;
        }

        constexpr auto num_outputs() const noexcept
        {
            return _num_public_outputs;
        }

        template<typename Allocator>
        void init_buffer(Allocator& allocator) const
        {
            /*
            * struct MemoryLayout {
            *     GraphState;    // private inputs outputs pointers
            *     NodeState[n];
            *     OutputPort[pi];  // private outputs connected to public inputs
            *     struct {
            *         // i, o, s and b specific to each node
            *         SharedPortData[i];
            *         OutputPort[o];
            *         InputPort[i];
            *
            *         // input port samples, not output port samples
            *         // honestly not sure which of the two the samples buffer should be stored with
            *         Sample[s];
            *         std::byte[b];
            *     }[n];
            *     SharedPortData[po];
            *     InputPort[po];  // private inputs connected to public outputs
            *     Sample[ps];     // samples for private inputs
            * };
            */

            size_t num_nodes = _nodes.size();

            std::vector<InputConfig> private_input_configs(_num_public_outputs);
            OutputConfig private_outputs_config;

            GraphState& graph_state = allocator.initialize_object<GraphState>();
            allocator.assign(graph_state.node_states, allocator.initialize_array<NodeState>(num_nodes));
            allocator.assign(graph_state.outputs, allocator.allocate_array<OutputPort>(_num_public_inputs));

            std::unordered_map<PortId, PortId> source_of;
            std::unordered_map<PortId, PortId> target_of;
            for (GraphEdge const& edge : _edges)
            {
                source_of[edge.target] = edge.source;
                target_of[edge.source] = edge.target;
            }

            std::unordered_map<PortId, size_t> input_port_global_latencies;
            std::unordered_map<GraphEdge, size_t> corrected_latencies;
            std::unordered_map<PortId, std::span<Sample>> input_ports_samples;
            std::unordered_map<size_t, std::span<SharedPortData>> node_port_data;
            
            auto allocate_node_state = [&](auto const& node, size_t node_i)
            {
                NodeState& node_state = (node_i == GRAPH_ID)
                    ? graph_state
                    : allocator.at(graph_state.node_states, node_i);

                std::span<InputConfig const> input_configs;
                std::span<OutputConfig const> output_configs;
                if (node_i == GRAPH_ID)
                {
                    // private inputs
                    input_configs = private_input_configs;
                    // no outputs, public are handled by parent node and private are handled by the children nodes connected to them
                }
                else
                {
                    input_configs = get_inputs(node);
                    output_configs = get_outputs(node);
                }
                size_t const num_inputs = input_configs.size();
                size_t const num_outputs = output_configs.size();
                node_port_data[node_i] = allocator.allocate_array<SharedPortData>(num_inputs);
                if (node_i != GRAPH_ID) allocator.assign(node_state.outputs, allocator.allocate_array<OutputPort>(num_outputs));
                allocator.assign(node_state.inputs, allocator.allocate_array<InputPort>(num_inputs));

                // align latencies
                size_t node_global_latency = 0;
                std::vector<size_t> input_port_extra_latencies(num_inputs);
                for (size_t in_port = 0; in_port < num_inputs; ++in_port)
                {
                    node_global_latency = std::max(node_global_latency, input_port_global_latencies[{ node_i, in_port }]);
                }
                for (size_t in_port = 0; in_port < num_inputs; ++in_port)
                {
                    input_port_extra_latencies[in_port] += node_global_latency - input_port_global_latencies[{ node_i, in_port }];
                }
                if (node_i != GRAPH_ID) {
                    node_global_latency += get_internal_latency(node);
                    for (size_t out_port = 0; out_port < num_outputs; ++out_port)
                    {
                        size_t output_latency = node_global_latency + output_configs[out_port].latency;
                        PortId connection = target_of[{ node_i, out_port }];
                        input_port_global_latencies[connection] = output_latency;
                    }
                }

                for (size_t input_i = 0; input_i < num_inputs; ++input_i)
                {
                    PortId this_input { node_i, input_i };
                    InputConfig const& input_config = input_configs[input_i];
                    size_t num_port_samples;

                    if (auto it = source_of.find(this_input); it != source_of.end())
                    {
                        // input is connected to output, let's setup both
                        size_t output_node_i = it->second.node;
                        size_t output_port_i = it->second.port;
                        GraphEdge this_edge { it->second, this_input };

                        OutputConfig const& output_config = (output_node_i == GRAPH_ID)
                            ? private_outputs_config
                            : get_outputs(_nodes[output_node_i])[output_port_i];

                        size_t const corrected_latency = output_config.latency + input_port_extra_latencies[input_i];
                        corrected_latencies.insert({ this_edge, corrected_latency });
                        num_port_samples = calculate_port_buffer_size(corrected_latency, input_config.history);
                    }
                    else
                    {
                        // input is disconnected, no corresponding output port
                        num_port_samples = calculate_port_buffer_size(0, input_config.history);
                    }

                    input_ports_samples.insert({ this_input, allocator.allocate_array<Sample>(num_port_samples) });
                }

                if (node_i != GRAPH_ID)
                {
                    CountingNonAllocator counter(allocator.get_buffer().data());
                    do_init_buffer(node, counter);
                    allocator.assign(node_state.buffer, allocator.allocate_array<std::byte>(counter.estimate_buffer_size()));
                }
            };

            for (size_t node_i = 0; node_i < num_nodes + 1; ++node_i)
            {
                if (node_i < num_nodes)
                {
                    allocate_node_state(_nodes[node_i], node_i);
                }
                else if (node_i == num_nodes)
                {
                    allocate_node_state(*this, GRAPH_ID);
                }
            }

            if (allocator.can_allocate())
            {
                // memory was allocated, now let's initialize it

                auto initialize_node_state = [&](auto const& node, size_t node_i)
                {
                    NodeState& node_state = (node_i == GRAPH_ID)
                        ? graph_state
                        : allocator.at(graph_state.node_states, node_i);

                    std::span<InputConfig const> input_configs;
                    std::span<OutputConfig const> output_configs;
                    if (node_i == GRAPH_ID)
                    {
                        // private inputs
                        input_configs = private_input_configs;
                        // no outputs, public are handled by parent node and private are handled by the children nodes connected to them
                    }
                    else
                    {
                        input_configs = get_inputs(node);
                        output_configs = get_outputs(node);
                    }
                    size_t const num_inputs = input_configs.size();
                    size_t const num_outputs = output_configs.size();
                    std::span<SharedPortData> inputs_port_data = node_port_data[node_i];

                    for (size_t input_i = 0; input_i < num_inputs; ++input_i)
                    {
                        PortId this_input { node_i, input_i };
                        InputConfig const& input_config = input_configs[input_i];
                        InputPort& input_port = allocator.at(node_state.inputs, input_i);
                        SharedPortData& input_port_data = allocator.at(inputs_port_data, input_i);
                        std::span<Sample> port_samples = input_ports_samples.at(this_input);

                        if (input_config.default_value)
                        {
                            allocator.fill_n(port_samples, input_config.default_value);
                        }

                        if (auto it = source_of.find({ node_i, input_i }); it != source_of.end())
                        {
                            // input is connected to output, let's setup both
                            size_t output_node_i = it->second.node;
                            size_t output_port_i = it->second.port;
                            GraphEdge this_edge { it->second, this_input };

                            OutputPort& output_port = (output_node_i == GRAPH_ID)
                                ? allocator.at(graph_state.outputs, output_port_i)
                                : allocator.at(allocator.at(graph_state.node_states, output_node_i).outputs, output_port_i);

                            size_t const corrected_latency = corrected_latencies[this_edge];
                            allocator.construct_at(&input_port_data, port_samples, corrected_latency);
                            allocator.construct_at(&output_port, input_port_data);
                        }
                        else
                        {
                            // input is disconnected: init dummy buffer
                            allocator.construct_at(&input_port_data, port_samples, 0);
                        }
                        allocator.construct_at(&input_port, input_port_data, input_config.history);
                    }

                    if (node_i != GRAPH_ID)
                    {
                        FixedBufferAllocator nested_allocator(node_state.buffer);
                        std::span<std::byte> result = do_init_buffer(node, nested_allocator);
                        assert(result.data() == node_state.buffer.data() && result.size() == node_state.buffer.size());
                    }
                };

                for (size_t node_i = 0; node_i < num_nodes + 1; ++node_i)
                {
                    if (node_i < num_nodes)
                    {
                        initialize_node_state(_nodes[node_i], node_i);
                    }
                    else if (node_i == num_nodes)
                    {
                        initialize_node_state(*this, GRAPH_ID);
                    }
                }
            }
        }

        void tick(TickState const& state) noexcept
        {
            auto& private_state = get_private_state(state.buffer);
            for (size_t i = 0; i < _num_public_inputs; ++i) {
                private_state.outputs[i].push(state.inputs[i].get());
            }
            size_t num_nodes = _nodes.size();
            for (size_t i = 0; i < num_nodes; ++i)
            {
                _nodes[i].tick({
                    private_state.node_states[i],
                    state.midi,
                });
            }
            for (size_t i = 0; i < _num_public_outputs; ++i) {
                state.outputs[i].push(private_state.inputs[i].get());
            }
        }

        GraphState& get_private_state(std::span<std::byte> buffer) const
        {
            void* object = buffer.data();
            size_t space = buffer.size();
            return *reinterpret_cast<GraphState*>(std::align(alignof(GraphState), sizeof(GraphState), object, space));  // first index in the buffer
        }

        size_t internal_latency() const noexcept
        {
            std::unordered_map<PortId, PortId> target_of;
            for (GraphEdge const& edge : _edges)
            {
                target_of[edge.source] = edge.target;
            }

            std::unordered_map<PortId, size_t> input_global_latencies;
            size_t max_latency = 0;

            auto process_node = [&](auto& node, size_t node_i)
            {
                size_t node_global_latency = 0;

                size_t const num_inputs = (node_i != GRAPH_ID) ? get_num_inputs(node) : _num_public_outputs;
                for (size_t input_port = 0; input_port < num_inputs; ++input_port)
                {
                    node_global_latency = std::max(node_global_latency, input_global_latencies[{ node_i, input_port }]);
                }

                if (node_i == GRAPH_ID) return;

                node_global_latency += get_internal_latency(node);
                size_t const num_outputs = get_num_outputs(node);
                for (size_t output_port = 0; output_port < num_outputs; ++output_port)
                {
                    if (auto it = target_of.find({ node_i, output_port }); it != target_of.end())
                    {
                        size_t new_latency = node_global_latency + get_outputs(node)[output_port].latency;
                        max_latency = std::max(max_latency, new_latency);
                        input_global_latencies[it->second] = new_latency;
                    }
                }
            };

            for (size_t node_i = 0; node_i < _nodes.size() + 1; ++node_i) {
                if (node_i < _nodes.size())
                {
                    process_node(_nodes[node_i], node_i);
                }
                else
                {
                    process_node(*this, GRAPH_ID);
                }
            }
            return max_latency;
        }

    private:
        void expand_hyperedge_ports()
        {
            std::unordered_map<PortId, std::vector<GraphEdge>> reverse_edges_map;
            for (GraphEdge const& edge : _edges)
            {
                reverse_edges_map[edge.target].push_back(edge);
            }

            size_t nodes_size = _nodes.size();
            for (size_t node = 0; node < nodes_size; ++node)
            {
                size_t const num_inputs = get_num_inputs(_nodes[node]);
                for (size_t in_port = 0; in_port < num_inputs; ++in_port)
                {
                    auto it = reverse_edges_map.find({ node, in_port });
                    if (it == reverse_edges_map.end()) continue;
                    auto const& edges_to_expand = it->second;
                    size_t const port_arity = edges_to_expand.size();
                    if (port_arity <= 1) continue;

                    _nodes.emplace_back(SumNode(port_arity));
                    size_t const sum_node = _nodes.size() - 1;
                    
                    for (size_t out_port = 0; out_port < edges_to_expand.size(); ++out_port)
                    {
                        GraphEdge const& to_rewire = edges_to_expand[out_port];
                        _edges.erase(to_rewire);
                        _edges.insert(GraphEdge { to_rewire.source, { sum_node, out_port } });
                    }
                    _edges.insert(GraphEdge { { sum_node, 0 }, { node, in_port } });
                }
            }

            std::unordered_map<PortId, std::vector<GraphEdge>> edges_map;
            for (GraphEdge const& edge : _edges)
            {
                edges_map[edge.source].push_back(edge);
            }

            nodes_size = _nodes.size();
            for (size_t node = 0; node < nodes_size; ++node)
            {
                size_t const num_outputs = get_num_outputs(_nodes[node]);
                for (size_t out_port = 0; out_port < num_outputs; ++out_port)
                {
                    auto it = edges_map.find({ node, out_port });
                    if (it == edges_map.end()) continue;
                    auto const& edges_to_expand = it->second;
                    size_t const port_arity = edges_to_expand.size();
                    if (port_arity <= 1) continue;

                    _nodes.emplace_back(BroadcastNode(port_arity));
                    size_t const broadcast_node = _nodes.size() - 1;

                    for (size_t in_port = 0; in_port < edges_to_expand.size(); ++in_port)
                    {
                        GraphEdge const& to_rewire = edges_to_expand[in_port];
                        _edges.erase(to_rewire);
                        _edges.insert(GraphEdge { { broadcast_node, in_port }, to_rewire.target });
                    }
                    _edges.insert(GraphEdge { { node, out_port }, { broadcast_node, 0 } });
                }
            }
        }

        void sort_nodes()
        {
            const size_t n = _nodes.size();

            std::unordered_map<PortId, PortId> edges_map;
            std::unordered_map<PortId, PortId> reverse_edges_map;
            for (GraphEdge const& edge : _edges)
            {
                edges_map[edge.source] = edge.target;
                reverse_edges_map[edge.target] = edge.source;
            }

            auto make_heads_queue = [&]()
            {
                std::deque<size_t> queue;

                // include all nodes with 0 connected input port
                for (size_t i = 0; i < n; ++i)
                {
                    bool all_inputs_disconnected = true;
                    size_t const num_inputs = get_num_inputs(_nodes[i]);
                    for (size_t in_i = 0; in_i < num_inputs; ++in_i)
                    {
                        if (reverse_edges_map.contains({i, in_i}))
                        {
                            all_inputs_disconnected = false;
                            break;
                        }
                    }
                    if (!all_inputs_disconnected) continue;
                    queue.push_back(i);
                }
                // include all nodes directly connected to the source ports of the graph
                for (size_t private_out_i = 0; private_out_i < _num_public_inputs; ++private_out_i)
                {
                    if (auto it = edges_map.find({ GRAPH_ID, private_out_i }); it != edges_map.end())
                    {
                        queue.push_back(it->second.node);
                    }
                }
                return queue;
            };

            // traverse the graph for each node to find their respective cyclic parents
            std::unordered_map<size_t, std::unordered_set<size_t>> cyclic_parents_of(n);
            {
                auto queue = make_heads_queue();
                std::vector<bool> seen(n, false);
                while (!queue.empty())
                {
                    size_t initial_node = queue.front();
                    queue.pop_front();
                    if (initial_node == GRAPH_ID || seen[initial_node]) continue;
                    seen[initial_node] = true;

                    std::vector<bool> inner_seen(n, false);
                    std::deque<size_t> inner_queue;
                    inner_queue.push_back(initial_node);

                    while (!inner_queue.empty())
                    {
                        size_t node = inner_queue.front();
                        inner_queue.pop_front();
                        if (node == GRAPH_ID || inner_seen[node]) continue;
                        inner_seen[node] = true;

                        // if we run into a node that already has cycles, then we are in a cycle and this cycle was already resolved
                        if (!cyclic_parents_of[node].empty()) continue;

                        size_t const num_outputs = get_num_outputs(_nodes[node]);
                        for (size_t out_i = 0; out_i < num_outputs; ++out_i)
                        {
                            if (auto it = edges_map.find({ node, out_i }); it != edges_map.end())
                            {
                                size_t child = it->second.node;
                                if (child == initial_node)
                                {
                                    cyclic_parents_of[initial_node].insert(node);
                                }
                                else
                                {
                                    inner_queue.push_back(child);
                                }
                            }
                        }
                    }

                    size_t const num_outputs = get_num_outputs(_nodes[initial_node]);
                    for (size_t out_i = 0; out_i < num_outputs; ++out_i) {
                        if (auto it = edges_map.find({ initial_node, out_i }); it != edges_map.end())
                        {
                            queue.push_back(it->second.node);
                        }
                    }
                }
            }

            auto queue = make_heads_queue();
            std::vector<bool> placed(n, false);
            std::vector<size_t> sorted;
            sorted.reserve(n);

            while (!queue.empty())
            {
                size_t node = queue.front();
                queue.pop_front();
                if (node == GRAPH_ID || placed[node]) continue;

                bool all_dependencies_satisfied = true;
                size_t const num_inputs = get_num_inputs(_nodes[node]);
                for (size_t in_i = 0; in_i < num_inputs; ++in_i) {
                    if (auto it = reverse_edges_map.find({ node, in_i }); it != reverse_edges_map.end())
                    {
                        size_t parent = it->second.node;
                        if (parent != GRAPH_ID && !placed[parent] && !cyclic_parents_of[node].contains(parent)) {
                            all_dependencies_satisfied = false;
                            break;
                        }
                    }
                }

                if (all_dependencies_satisfied) {
                    size_t const num_outputs = get_num_outputs(_nodes[node]);
                    for (size_t out_i = 0; out_i < num_outputs; ++out_i)
                    {
                        if (auto it = edges_map.find({ node, out_i }); it != edges_map.end())
                        {
                            queue.push_back(it->second.node);
                        }
                    }
                    sorted.push_back(node);
                    placed[node] = true;
                }
                else {
                    queue.push_back(node);
                }
            }

            Nodes sorted_nodes;
            sorted_nodes.reserve(n);
            for (size_t old_i = 0; old_i < n; ++old_i)
            {
                sorted_nodes.push_back(std::move(_nodes[sorted[old_i]]));
            }
            _nodes.swap(sorted_nodes);

            std::vector<size_t> reverse_sorted(n);
            std::iota(reverse_sorted.begin(), reverse_sorted.end(), 0);
            std::stable_sort(
                reverse_sorted.begin(), reverse_sorted.end(),
                [&](int i1, int i2) {return sorted[i1] < sorted[i2]; });
            
            Edges sorted_edges;
            sorted_edges.reserve(_edges.size());
            for (GraphEdge edge : _edges)
            {
                if (edge.source.node != GRAPH_ID)
                    edge.source.node = reverse_sorted[edge.source.node];
                if (edge.target.node != GRAPH_ID)
                    edge.target.node = reverse_sorted[edge.target.node];
                sorted_edges.insert(edge);
            }
            _edges.swap(sorted_edges);
        }

        void validate_graph() const
        {
            std::vector<InputConfig> _private_input_configs(_num_public_outputs);
            std::vector<OutputConfig> _private_output_configs(_num_public_inputs);

            for (auto const& edge : _edges)
            {
                std::span<OutputConfig const> source_outputs;
                if (edge.source.node == GRAPH_ID)
                {
                    source_outputs = _private_output_configs;
                }
                else
                {
                    source_outputs = get_outputs(_nodes[edge.source.node]);
                }
                std::span<InputConfig const> target_inputs;
                if (edge.target.node == GRAPH_ID)
                {
                    target_inputs = _private_input_configs;
                }
                else
                {
                    target_inputs = get_inputs(_nodes[edge.target.node]);
                }
                assert(edge.source.port < source_outputs.size() && "bad connection");
                assert(edge.target.port < target_inputs.size() && "bad connection");
            }
        }
    };

    template <size_t quad_words>
    class FastBitset {
        static_assert(quad_words > 0, "Need at least one word");
        std::array<uint64_t, quad_words> data_{};

    public:
        void set(size_t pos) {
            auto& w = data_[pos >> 6];
            w |= uint64_t(1) << (pos & 63);
        }
        void reset(size_t pos) {
            auto& w = data_[pos >> 6];
            w &= ~(uint64_t(1) << (pos & 63));
        }
        bool test(size_t pos) const {
            return (data_[pos >> 6] >> (pos & 63)) & 1;
        }
        void clear() {
            data_.fill(0);
        }

        // forward‑only const iterator
        class const_iterator {
            const FastBitset* bs_;
            size_t word_idx_;
            uint64_t word_;
            size_t idx_;

            void advance_to_next() {
                // fill word_ with next nonzero or mark end
                while (word_ == 0 && ++word_idx_ < quad_words) {
                    word_ = bs_->data_[word_idx_];
                }
                if (word_ != 0) {
                    unsigned tz = std::countr_zero(word_);
                    idx_ = word_idx_ * 64 + tz;
                    word_ &= word_ - 1;
                }
                else {
                    // mark end
                    bs_ = nullptr;
                }
            }

        public:
            // end iterator
            const_iterator() : bs_(nullptr), word_idx_(0), word_(0), idx_(0) {}
            // begin iterator
            explicit const_iterator(const FastBitset* bs)
                : bs_(bs), word_idx_(0), word_(bs->data_[0]), idx_(0)
            {
                if (word_ == 0) advance_to_next();
                else {
                    unsigned tz = std::countr_zero(word_);
                    idx_ = tz;
                    word_ &= word_ - 1;
                }
            }

            size_t operator*() const { return idx_; }

            const_iterator& operator++() {
                if (!bs_) return *this;
                advance_to_next();
                return *this;
            }

            bool operator!=(const const_iterator& o) const {
                return bs_ != o.bs_;
            }
        };

        const_iterator begin() const { return const_iterator(this); }
        const_iterator end()   const { return const_iterator(); }
    };

    class MidiNode {
        GraphNode _graph_node;
        size_t _internal_latency_cache;
        Sample _silence_threshold;

    public:
        static constexpr size_t const MAX_MIDI_NOTES = 128;
        static_assert(MAX_MIDI_NOTES > 0);
        static constexpr size_t const MAX_MIDI_NOTES_UINT64 = (MAX_MIDI_NOTES - 1) / (sizeof(uint64_t) * CHAR_BIT) + 1;
        using Bitset = FastBitset<MAX_MIDI_NOTES_UINT64>;
        static constexpr size_t const MIN_INPUTS = 0;
        static constexpr size_t const MIN_GRAPH_INPUTS = 2;
        static constexpr size_t const MIN_GRAPH_OUTPUTS = 1;

        struct MidiNoteState : public NodeState {
            size_t ttl{0};
            size_t amplitude{0};
        };

        struct MidiState : public NodeState {
            Bitset active_notes;
            std::array<MidiNoteState, MAX_MIDI_NOTES> note_states;
        };

        explicit MidiNode(
            GraphNode voice_node,
            Sample silence_threshold = std::pow(10.0, -60.0 / 20.0)  // -60db
        ) noexcept :
            _graph_node(voice_node),
            _internal_latency_cache(get_internal_latency(_graph_node)),
            _silence_threshold(silence_threshold)
        {
            assert(get_num_inputs(_graph_node) >= MIN_GRAPH_INPUTS && "the voice graph should have at least 2 inputs");
            assert(get_num_outputs(_graph_node) == MIN_GRAPH_OUTPUTS && "the voice graph should have exactly 1 output");
        }

        constexpr auto inputs() const noexcept
        {
            return std::vector<InputConfig>(get_num_inputs(_graph_node) - MIN_GRAPH_INPUTS + MIN_INPUTS);
        }

        constexpr auto outputs() const noexcept
        {
            return std::array<OutputConfig, 1>{};
        }

        void tick(TickState const& state) noexcept
        {
            MidiState& midi_state = get_midi_state(state);

            for (auto const& midi_message : state.midi)
            {
                if (midi_message.type == MidiMessageType::NOTE_ON)
                {
                    MidiNoteState& note_state = midi_state.note_states[midi_message.note_on.note_number];
                    note_state.amplitude = midi_message.note_on.amplitude;
                    note_state.ttl = _internal_latency_cache;
                    midi_state.active_notes.set(midi_message.note_on.note_number);
                }
                if (midi_message.type == MidiMessageType::NOTE_OFF)
                {
                    auto& note_state = midi_state.note_states[midi_message.note_off.note_number];
                    note_state.amplitude = 0;
                }
            }

            Sample result = 0.0;
            for (size_t note_number : midi_state.active_notes)
            {
                MidiNoteState& note_state = midi_state.note_states[note_number];
                if (note_state.amplitude) {
                    note_state.ttl = _internal_latency_cache;
                }
                else if (note_state.ttl)
                {
                    --note_state.ttl;
                }
                else if (auto last_output = note_state.outputs[0].get(); last_output <= _silence_threshold && last_output >= -_silence_threshold)
                {
                    midi_state.active_notes.reset(note_number);
                    continue;
                }

                note_state.inputs[0].push(Sample(note_state.amplitude / 127.0));
                note_state.inputs[1].push(Sample(NOTE_NUMBER_TO_FREQUENCY[note_number]));
                for (size_t extra_i = 0; extra_i < midi_state.inputs.size() - MIN_GRAPH_INPUTS; ++extra_i)
                {
                    note_state.inputs[extra_i+MIN_GRAPH_INPUTS].push(state.inputs[extra_i+MIN_INPUTS].get());
                }
                _graph_node.tick({ note_state, state.midi });
                result += note_state.outputs[0].get();
            }

            auto& out_mix = state.outputs[0];
            out_mix.push(result);
        }

        constexpr size_t internal_latency() const noexcept
        {
            return _internal_latency_cache;
        }

        template<typename Allocator>
        void init_buffer(Allocator& allocator) const
        {
            /*
            * struct MemoryLayout {
            *     State;              // MidiNode state
            *     SharedPortData[i];
            *     InputPort[i];       // voice graph inputs (shared)
            *     Sample[s1];          // voice graph input samples (shared)
            *     struct {
            *         std::byte[b];   // voice graph data
            *     }[n];
            *     SharedPortData[1];
            *     OutputPort[1];      // voice graph output (shared)
            *     Sample[s2];          // voice graph output samples (shared)
            * };
            */

            MidiState& midi_state = allocator.initialize_object<MidiState>();

            auto input_configs = get_inputs(_graph_node);

            std::span<SharedPortData> all_input_data = allocator.allocate_array<SharedPortData>(input_configs.size());
            allocator.assign(midi_state.inputs, allocator.allocate_array<InputPort>(input_configs.size()));

            for (size_t input_i = 0; input_i < input_configs.size(); ++input_i)
            {
                size_t num_samples = calculate_port_buffer_size(0, input_configs[input_i].history);
                std::span<Sample> samples = allocator.initialize_array<Sample>(num_samples);
                allocator.fill_n(samples, input_configs[input_i].default_value);

                SharedPortData& input_data = allocator.at(all_input_data, input_i);
                InputPort& input_port = allocator.at(midi_state.inputs, input_i);
                allocator.construct_at(&input_data, samples, 0);
                allocator.construct_at(&input_port, input_data, input_configs[input_i].history);
            }

            for (size_t note = 0; note < MAX_MIDI_NOTES; ++note)
            {
                MidiNoteState& midi_note_state = allocator.at(midi_state.note_states, note);
                allocator.assign(midi_note_state.buffer, do_init_buffer(_graph_node, allocator));
                allocator.assign(midi_note_state.inputs, midi_state.inputs);
            }

            auto output_configs = get_outputs(_graph_node);

            std::span<SharedPortData> all_output_data = allocator.allocate_array<SharedPortData>(1);
            allocator.assign(midi_state.outputs, allocator.allocate_array<OutputPort>(1));
            size_t num_output_samples = calculate_port_buffer_size(output_configs[0].latency, 0);
            std::span<Sample> output_samples = allocator.initialize_array<Sample>(num_output_samples);

            SharedPortData& output_data = allocator.at(all_output_data, 0);
            OutputPort& output_port = allocator.at(midi_state.outputs, 0);

            allocator.construct_at(&output_data, output_samples, output_configs[0].latency);
            allocator.construct_at(&output_port, output_data);

            for (size_t note = 0; note < MAX_MIDI_NOTES; ++note)
            {
                MidiNoteState& midi_note_state = allocator.at(midi_state.note_states, note);
                allocator.assign(midi_note_state.outputs, midi_state.outputs);
            }
        }

        MidiState& get_midi_state(NodeState const& state) const noexcept
        {
            void* object = state.buffer.data();
            size_t size = state.buffer.size();
            return *reinterpret_cast<MidiState*>(std::align(alignof(MidiState), sizeof(MidiState), object, size));
        }
    };

    struct alignas(max_align_t) AlignedBytes {
        std::byte b[alignof(max_align_t)];
    };

    class NodeProcessor {
        using Buffer = std::vector<AlignedBytes>;

        GraphNode _node;
        Buffer _buffer;
        NodeState _graph_state;

        void resize_buffer()
        {
            _buffer.reserve(1);
            std::byte* byte_data = reinterpret_cast<std::byte*>(_buffer.data());
            CountingNonAllocator counter(byte_data);
            do_init_buffer(_node, counter);
            _buffer.resize(counter.estimate_buffer_size());
        }

        void initialize_graph()
        {
            FixedBufferAllocator allocator({
                reinterpret_cast<std::byte*>(_buffer.data()),
                _buffer.size() * sizeof(AlignedBytes)
            });
            auto allocated = do_init_buffer(_node, allocator);
            assert(
                _buffer.size() - allocated.size() < alignof(max_align_t) &&
                "buffer was over allocated"
            );
        }

    public:
        explicit NodeProcessor(GraphNode node) noexcept :
            _node(std::move(node))
        {
            assert(get_num_inputs(_node) == 0 && "the graph should have 0 inputs");
            assert(get_num_outputs(_node) == 0 && "the graph should have 0 outputs");

            resize_buffer();
            initialize_graph();
        }

        void tick(std::span<MidiMessage const> midi) noexcept
        {
            std::span<std::byte> buffer_span {
                reinterpret_cast<std::byte*>(_buffer.data()),
                _buffer.size() * sizeof(AlignedBytes)
            };
            _node.tick({ NodeState { .buffer = buffer_span }, midi });
        }
    };
}
