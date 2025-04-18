#pragma once
#include <vector>
#include <array>
#include <memory>
#include <span>
#include <unordered_map>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <deque>
#include <cassert>
#include <random>
#include <bit>
#include <type_traits>
#include <functional>


namespace constexpr_math {

    // constexpr signbit
    template<typename T>
    constexpr bool signbit(T x) noexcept {
        static_assert(std::is_floating_point_v<T>, "Only floating‑point types supported");
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
        static_assert(std::is_floating_point_v<T>, "Only floating‑point types supported");
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

    constexpr static bool is_power_of_2(size_t n) noexcept
    {
        return n && !(n & (n - 1));
    }

    constexpr static Sample polyblep_phi(Sample sample, Sample warp_threshold) noexcept
    {
        return (sample + warp_threshold) / 2.0;
    }

    constexpr static Sample polyblep_p(Sample phi, Sample delta, Sample warp_threshold, PolyblepSide side) noexcept
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

    constexpr static Sample polyblep_error(Sample sample, Sample delta, Sample warp_threshold, PolyblepSide side) noexcept
    {
        Sample sign = constexpr_math::copysign<Sample>(1.0, delta);
        delta = constexpr_math::copysign<Sample>(delta, 1.0);

        Sample phi = polyblep_phi(sample, warp_threshold);
        Sample p = polyblep_p(phi, delta, warp_threshold, side) * sign;
        return p;
    }

    constexpr Sample int_floor(Sample f)
    {
        const size_t i = static_cast<size_t>(f);
        return f < i ? i - 1 : i;
    }

    constexpr static inline Sample warp_pm1(Sample x, Sample limit) noexcept
    {
        Sample period = 2.0 * limit;
        return x - int_floor((x + limit) / period) * period;
    }

    enum struct MidiMessageType {
        NONE,
        NOTE_ON,
        NOTE_OFF,
    };

    struct MidiMessage {
        MidiMessageType type = MidiMessageType::NONE;
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
        constexpr explicit InputPort(
            SharedPortData& shared_data,
            size_t history
        ):
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
        constexpr explicit OutputPort(SharedPortData& shared_data):
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
    };

    namespace details
    {
        template <typename T>
        class has_outputs
        {
            typedef char one;
            struct two { char x[2]; };

            template <typename C> static one test(decltype(&C::outputs));
            template <typename C> static two test(...);

        public:
            enum { value = sizeof(test<T>(0)) == sizeof(char) };
        };

        template <typename T>
        class has_num_outputs
        {
            typedef char one;
            struct two { char x[2]; };

            template <typename C> static one test(decltype(&C::num_outputs));
            template <typename C> static two test(...);

        public:
            enum { value = sizeof(test<T>(0)) == sizeof(char) };
        };

        template <typename T>
        class has_num_inputs
        {
            typedef char one;
            struct two { char x[2]; };

            template <typename C> static one test(decltype(&C::num_inputs));
            template <typename C> static two test(...);

        public:
            enum { value = sizeof(test<T>(0)) == sizeof(char) };
        };

        template <typename T>
        class has_inputs
        {
            typedef char one;
            struct two { char x[2]; };

            template <typename C> static one test(decltype(&C::inputs));
            template <typename C> static two test(...);

        public:
            enum { value = sizeof(test<T>(0)) == sizeof(char) };
        };

        template <typename T>
        class has_buffer_size
        {
            typedef char one;
            struct two { char x[2]; };

            template <typename C> static one test(decltype(&C::buffer_size));
            template <typename C> static two test(...);

        public:
            enum { value = sizeof(test<T>(0)) == sizeof(char) };
        };

        template <typename T>
        class has_init_buffer
        {
            typedef char one;
            struct two { char x[2]; };

            template <typename C> static one test(decltype(&C::init_buffer));
            template <typename C> static two test(...);

        public:
            enum { value = sizeof(test<T>(0)) == sizeof(char) };
        };
    }

    template<typename Node>
    constexpr auto get_outputs(Node node) noexcept
    {
        if constexpr (details::has_outputs<Node>::value)
        {
            return node.outputs();
        }
        else
        {
            return std::array<OutputConfig, 0>{};
        }
    }

    template<typename Node>
    constexpr auto get_num_outputs(Node node) noexcept
    {
        if constexpr (details::has_num_outputs<Node>::value)
        {
            return node.num_outputs();
        }
        else
        {
            return get_outputs(node).size();
        }
    }

    template<typename Node>
    constexpr auto get_inputs(Node node) noexcept
    {
        if constexpr (details::has_inputs<Node>::value)
        {
            return node.inputs();
        }
        else
        {
            return std::array<InputConfig, 0>{};
        }
    }

    template<typename Node>
    constexpr auto get_num_inputs(Node node) noexcept
    {
        if constexpr (details::has_num_inputs<Node>::value)
        {
            return node.num_inputs();
        }
        else
        {
            return get_inputs(node).size();
        }
    }

    template<typename Node>
    constexpr auto get_buffer_size(Node node) noexcept
    {
        if constexpr (details::has_buffer_size<Node>::value)
        {
            return node.buffer_size();
        }
        else
        {
            return 0;
        }
    }

    template<typename Node>
    constexpr void do_init_buffer(Node node, NodeState const& state) noexcept
    {
        if constexpr (details::has_init_buffer<Node>::value)
        {
            node.init_buffer(state);
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
        constexpr explicit BinaryOpNode(size_t num_inputs) noexcept :
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
                OutputConfig{.latency=1},
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
                Sample delta = (sample - sample_prev) / 2.0;
                out.update(sample_prev - polyblep_error(sample_prev, delta, threshold, PolyblepSide::LEFT));
                sample_warped_aa -= polyblep_error(sample_warped_aa, delta, threshold, PolyblepSide::RIGHT);
            }
            out.push(sample_warped_aa);
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
        {
        }

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

    class DynamicNode {
        std::shared_ptr<void> _node;
        std::vector<InputConfig> _inputs;
        std::vector<OutputConfig> _outputs;
        size_t _buffer_size;
        void (*_init_buffer_fn)(void*, NodeState const&) noexcept;
        void (*_tick_fn)(void*, TickState const&) noexcept;

    public:
        template<typename Node>
        constexpr /*implicit*/ DynamicNode(Node node)
        {
            if constexpr (std::is_empty_v<Node>)
            {
                _node = nullptr;
                _init_buffer_fn = [](void*, NodeState const& state) noexcept { do_init_buffer(Node{}, state); };
                _tick_fn = [](void*, TickState const& state) noexcept { Node{}.tick(state); };
            }
            else
            {
                _node = std::make_shared<Node>(node);
                _init_buffer_fn = [](void* node, NodeState const& state) noexcept { do_init_buffer(*static_cast<Node*>(node), state); };
                _tick_fn = [](void* node, TickState const& state) noexcept { static_cast<Node*>(node)->tick(state); };
            }
            _inputs.assign_range(get_inputs(node));
            _outputs.assign_range(get_outputs(node));
            _buffer_size = get_buffer_size(node);
        }

        constexpr std::span<InputConfig const> inputs() const noexcept
        {
            return _inputs;
        }

        constexpr std::span<OutputConfig const> outputs() const noexcept
        {
            return _outputs;
        }

        constexpr size_t buffer_size() const noexcept
        {
            return _buffer_size;
        }

        constexpr void init_buffer(NodeState const& state) const noexcept
        {
            _init_buffer_fn(_node.get(), state);
        }

        void tick(TickState const& state) noexcept
        {
            _tick_fn(_node.get(), state);
        }
    };

    namespace details {
        template <typename T>
        constexpr bool is_tuple_v = false;
        template <typename... Args>
        constexpr bool is_tuple_v<std::tuple<Args...>> = true;

        template <typename Tuple, typename Func, size_t... I>
        constexpr void tuple_for_each_impl(Tuple&& t, Func&& f, std::index_sequence<I...>)
        {
            (f(std::get<I>(t)), ...);
        }

        template <typename Tuple, typename Func, size_t... I, class... Args>
        constexpr auto declval_tuple_common(Tuple&& t, std::index_sequence<I...>, Func&& f, Args&&... args)
        {
            return std::declval<std::common_type_t<decltype(f(std::get<I>(t), std::forward<Args>(args)...))...>>();
        }

        template <typename Tuple, typename Func>
        constexpr void tuple_for_each(Tuple&& t, Func&& f)
        {
            tuple_for_each_impl(
                std::forward<Tuple>(t),
                std::forward<Func>(f),
                std::make_index_sequence<std::tuple_size_v<std::remove_reference_t<Tuple>>>{}
            );
        }

        template <typename Container, typename Function>
        constexpr void for_each(Container&& container, Function&& function)
        {
            if constexpr (is_tuple_v<std::remove_cvref_t<Container>>)
            {
                tuple_for_each(std::forward<Container>(container), std::forward<Function>(function));
            }
            else
            {
                for (auto& element : container) {
                    function(element);
                }
            }
        }

        template<typename Container, size_t I = 0, class Fn, class... Args>
        auto map_at(Container& container, size_t index, Fn&& fn, Args&&... args)
        {
            if constexpr (is_tuple_v<std::remove_cvref_t<Container>>)
            {
                using Return = decltype(declval_tuple_common(
                    container,
                    std::make_index_sequence<std::tuple_size_v<std::remove_cvref_t<Container>>>{},
                    std::forward<Fn>(fn),
                    std::forward<Args>(args)...
                ));
                if constexpr (I < std::tuple_size_v<Container>) {
                    if (I == index) {
                        return static_cast<Return>(fn(std::get<I>(container), std::forward<Args>(args)...));
                    }
                    else {
                        return map_at<Container, I + 1>(container, index, std::forward<Fn>(fn), std::forward<Args>(args)...);
                    }
                }
                else
                {
                    throw "tuple index out of bounds";
                    return std::declval<Return>();
                }
            }
            else
            {
                return fn(container[index], std::forward<Args>(args)...);
            }
        }

        template<typename Container>
        constexpr size_t size(Container const& container) noexcept
        {
            if constexpr (is_tuple_v<std::remove_cvref_t<Container>>)
            {
                return std::tuple_size_v<Container>;
            }
            else
            {
                return container.size();
            }
        }
    }

    template<typename T>
    union AlignedStorage
    {
        alignas(T) std::byte uninitialized_object[sizeof(T)];
        T object;

        constexpr explicit AlignedStorage() :
            uninitialized_object{}
        {
        }
    };

    struct PortId {
        size_t node;
        size_t port;
    };

    struct GraphEdge {
        PortId source, target;
    };

    template<typename Nodes = std::vector<DynamicNode>, typename Edges = std::vector<GraphEdge>>
    struct GraphNode {
        Nodes _nodes;
        Edges _edges;
        size_t _num_public_inputs;
        size_t _num_public_outputs;

    public:
        static constexpr size_t GRAPH_ID = std::numeric_limits<size_t>::max();

        constexpr explicit GraphNode(Nodes&& nodes, Edges&& edges, size_t num_inputs = 0, size_t num_outputs = 0) noexcept :
            _nodes(std::move(nodes)),
            _edges(std::move(edges)),
            _num_public_inputs(num_inputs),
            _num_public_outputs(num_outputs)
        {}

        constexpr auto inputs() const noexcept
        {
            return std::vector<InputConfig>(_num_public_inputs);
        }

        constexpr auto outputs() const noexcept
        {
            // todo: compute latencies to align all outputs together
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

        constexpr void init_buffer(NodeState const& state) noexcept
        {
            /*
            * struct MemoryLayout {
            *     NodeState[1];    // private inputs outputs pointer
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

            std::span<std::byte> buffer = state.buffer;

            auto advance_buffer = [&](size_t amount)
            {
                buffer = buffer.subspan(amount, buffer.size() - amount);
            };

            auto initialize_array = [&]<typename T>(size_t number)
            {
                auto ptr = ::new (buffer.data()) T[number];
                advance_buffer(number * sizeof(T));
                return std::span<T> { ptr, number };
            };

            auto allocate_array = [&]<typename T>(size_t number)
            {
                std::span<AlignedStorage<T>> span = initialize_array.template operator()<AlignedStorage<T>>(number);
                return std::span<T> { &(span.data()->object), span.size() };
            };

            size_t num_nodes = details::size(_nodes);
            size_t num_ports = _edges.size();

            auto private_input_configs = std::vector<InputConfig>(_num_public_outputs);
            OutputConfig private_outputs_config;

            NodeState& private_node_sate = initialize_array.template operator()<NodeState>(1)[0];
            auto node_states = initialize_array.template operator()<NodeState>(num_nodes);
            private_node_sate.outputs = allocate_array.template operator()<OutputPort>(_num_public_inputs);

            auto calculate_port_buffer_size = [](size_t latency, size_t history)
            {
                size_t min_size = 1 + latency + history;
                size_t pow2_size = size_t(1) << size_t(std::ceil(std::log2(min_size)));
                return pow2_size;
            };

            std::unordered_map<PortId, PortId> out_of_in;
            for (auto const& edge : _edges)
            {
                out_of_in[edge.target] = edge.source;
            }

            auto setup_node_state = [&](auto& node, size_t node_i)
            {
                auto node_state = (node_i != GRAPH_ID)
                    ? node_states[node_i]
                    : private_node_sate;

                std::span<SharedPortData> input_port_data = initialize_array.template operator()<SharedPortData>(get_num_inputs(node));
                if (node_i != GRAPH_ID)
                {
                    node_state.outputs = allocate_array.template operator()<OutputPort>(get_num_outputs(node));
                    node_state.inputs = allocate_array.template operator()<InputPort>(get_num_inputs(node));
                }
                else
                {
                    // private inputs
                    node_state.inputs = allocate_array.template operator()<InputPort>(_num_public_outputs);
                }

                std::span<InputConfig const> target_configs = (node_i != GRAPH_ID)
                    ? get_inputs(node)
                    : private_input_configs;
                std::span<InputPort> target_ports = node_state.inputs;

                for (size_t in_i = 0; in_i < target_configs.size(); ++in_i)
                {
                    InputConfig const& target_config = target_configs[in_i];
                    InputPort& target_port = target_ports[in_i];

                    if (auto it = out_of_in.find({ node_i, in_i }); it != out_of_in.end())
                    {
                        // input is connected to output, let's setup both
                        size_t source_node_i = it->second.node;
                        size_t source_port_i = it->second.port;

                        OutputConfig const& source_config = (source_node_i != GRAPH_ID)
                            ? details::map_at(_nodes, source_node_i, [&](auto& source_node)
                            {
                                return get_outputs(source_node)[source_port_i];
                            })
                            : private_outputs_config;
                        OutputPort& source_port = (source_node_i != GRAPH_ID)
                            ? private_node_sate.outputs[source_port_i]
                            : node_states[source_node_i].outputs[source_port_i];

                        auto num_port_samples = calculate_port_buffer_size(source_config.latency, target_config.history);
                        auto input_samples = initialize_array.template operator()<Sample>(num_port_samples);
                        ::new (&input_port_data[in_i]) SharedPortData(input_samples, source_config.latency);
                        ::new (&source_port) OutputPort(input_port_data[in_i]);
                    }
                    else
                    {
                        // input is disconnected: init dummy buffer
                        auto num_port_samples = calculate_port_buffer_size(0, target_config.history);
                        auto input_samples = initialize_array.template operator()<Sample>(num_port_samples);
                        ::new (&input_port_data[in_i]) SharedPortData(input_samples, 0);
                    }
                    ::new (&target_port) InputPort(input_port_data[in_i], target_config.history);
                    if (target_config.default_value)
                    {
                        std::fill_n(input_port_data[in_i].buffer.begin(), input_port_data[in_i].buffer.end(), target_config.default_value);
                    }
                }
                if (node_i != GRAPH_ID) {
                    node_state.buffer = allocate_array.template operator()<std::byte>(get_buffer_size(node));
                }
            };

            for (size_t node_i = 0; node_i < num_nodes + 1; ++node_i)
            {
                if (node_i < num_nodes)
                {
                    details::map_at(_nodes, node_i, setup_node_state, node_i);
                }
                else if (node_i == num_nodes)
                {
                    setup_node_state(*this, GRAPH_ID);
                }
            }
        }

        constexpr size_t buffer_size() const noexcept
        {
            size_t total = 0;
            return total;
        }

        void tick(TickState const& state) noexcept
        {
            auto& private_state = get_private_state(state.buffer);
            for (size_t i = 0; i < _num_public_inputs; ++i) {
                private_state.outputs[i].push(state.inputs[i].get());
            }
            size_t num_nodes = details::size(_nodes);
            for (size_t i = 0; i < num_nodes; ++i)
            {
                details::map_at(_nodes, i, [&, i=i](auto& node)
                {
                    TickState node_state {
                        get_node_state(state.buffer, i),
                        state.midi,
                    };
                    node.tick(node_state);
                });
            }
            for (size_t i = 0; i < _num_public_outputs; ++i) {
                state.outputs[i].push(private_state.inputs[i].get());
            }
        }

        constexpr NodeState& get_private_state(std::span<std::byte> buffer) const noexcept
        {
            return *(NodeState*)(buffer.data());  // first index in the buffer
        }

        constexpr NodeState& get_node_state(std::span<std::byte> buffer, size_t node_i) const noexcept
        {
            if (node_i >= details::size(_nodes))
            {
                throw "node index out of range";
            }
            return *(node_i + (NodeState*)(buffer.data()));
        }
    };

    //struct InputPort {
    //    Sample _default;
    //    size_t _history;
    //    size_t _latency;
    //    std::vector<Sample> _buffer;
    //    size_t _index;

    //    explicit InputPort(size_t history = 0, Sample default_value = 0.0) noexcept :
    //        _default(default_value),
    //        _history(history),
    //        _latency(0),
    //        _index(0)
    //    {
    //        add_latency(0);  // init buffer
    //    }

    //    ~InputPort() = default;

    //    explicit InputPort(InputPort const&) = delete;
    //    explicit InputPort(InputPort&& other) noexcept {
    //        *this = std::move(other);
    //    }

    //    InputPort& operator=(InputPort const&) = delete;
    //    InputPort& operator=(InputPort&& other) noexcept {
    //        _buffer = std::move(other._buffer);
    //        _index = other._index;
    //        _default = other._default;
    //        _history = other._history;
    //        _latency = other._latency;
    //        return *this;
    //    }

    //    Sample get(size_t offset = 0) noexcept {
    //        if (offset > _history) return 0.0;
    //        size_t idx = (_index + _buffer.size() - offset) & (_buffer.size() - 1);  // buffer size is a power of 2
    //        return _buffer[idx];
    //    }

    //    void push(Sample value) noexcept {
    //        _index = (_index + 1) & (_buffer.size() - 1);  // buffer size is a power of 2
    //        update(value);
    //    }

    //    void update(Sample value, size_t offset = 0) noexcept {
    //        if (offset > _latency) return;
    //        size_t idx = (_index + _buffer.size() + _latency - offset) & (_buffer.size() - 1);  // buffer size is a power of 2
    //        _buffer[idx] = value;
    //    }

    //    void add_latency(size_t latency) noexcept {
    //        _latency += latency;
    //        size_t min_buffer_size = _latency + _history + 1;
    //        _buffer.assign(size_t(1) << size_t(std::ceil(std::log2(min_buffer_size))), _default);
    //        _buffer.shrink_to_fit();
    //    }

    //    size_t get_latency() const noexcept {
    //        return _latency;
    //    }
    //};

    //struct OutputPort {
    //    size_t _latency;
    //    std::vector<InputPort*> fan;
    //    std::vector<Sample> _buffer;
    //    size_t _index;

    //    explicit OutputPort(size_t latency = 0) noexcept :
    //        _latency(latency),
    //        _buffer(size_t(1) << size_t(std::ceil(std::log2(latency + 1)))),
    //        _index(0)
    //    {}

    //    void connect_to(InputPort* in) {
    //        assert(in->get_latency() == 0);
    //        fan.push_back(in);
    //        in->add_latency(_latency);
    //    }

    //    void push(Sample value) noexcept {
    //        _index = (_index + 1) & (_buffer.size() - 1);  // buffer size is a power of 2
    //        size_t idx = (_index + _buffer.size() + _latency) & (_buffer.size() - 1);  // buffer size is a power of 2
    //        _buffer[idx] = value;
    //        for (auto* dst : fan) {
    //            dst->push(value);
    //        }
    //    }

    //    void update(Sample value, size_t offset = 0) noexcept {
    //        if (offset > _latency) return;
    //        size_t idx = (_index + _buffer.size() + _latency - offset) & (_buffer.size() - 1);  // buffer size is a power of 2
    //        _buffer[idx] = value;
    //        for (auto* dst : fan) {
    //            dst->update(value, offset);
    //        }
    //    }

    //    Sample get(size_t offset = 0) const noexcept {
    //        if (offset > _latency) return 0.0;
    //        size_t idx = (_index + _buffer.size() + offset) & (_buffer.size() - 1);  // buffer size is a power of 2
    //        return _buffer[idx];
    //    }

    //    size_t get_latency() const noexcept {
    //        return _latency;
    //    }
    //};

    ///* ─────────────  Node base  ───────────── */
    //struct Node {
    //    virtual ~Node() = default;
    //    virtual void tick(std::span<MidiMessage const> const& midi) noexcept = 0;
    //    virtual size_t inner_latency() const noexcept {
    //        return 0;
    //    }

    //    std::span<InputPort> inputs() noexcept {
    //        auto span = const_cast<Node const*>(this)->inputs_impl();
    //        return { const_cast<InputPort*>(span.data()), span.size(), };
    //    }

    //    std::span<InputPort const> inputs() const noexcept {
    //        return inputs_impl();
    //    }

    //    std::span<OutputPort> outputs() noexcept {
    //        auto span = const_cast<Node const*>(this)->outputs_impl();
    //        return { const_cast<OutputPort*>(span.data()), span.size(), };
    //    }

    //    std::span<OutputPort const> outputs() const noexcept {
    //        return outputs_impl();
    //    }

    //protected:
    //    virtual std::span<InputPort const> inputs_impl() const noexcept {
    //        return {};
    //    };
    //    virtual std::span<OutputPort const> outputs_impl() const noexcept {
    //        return {};
    //    }
    //};

    ///* ─────────────  Concrete nodes  ───────────── */
    //class SumNode : public Node {
    //    std::vector<InputPort> _ins;
    //    OutputPort _out;

    //public:
    //    explicit SumNode(std::vector<InputPort>&& ins) noexcept: _ins(std::move(ins)) {}

    //    void tick(std::span<MidiMessage const> const& midi) noexcept override {
    //        Sample result = 0.0;
    //        for (auto& in : _ins) {
    //            result += in.get();
    //        }
    //        _out.push(result);
    //    }
    //    std::span<InputPort const> inputs_impl() const noexcept override { return _ins; }
    //    std::span<OutputPort const> outputs_impl() const noexcept override { return { &_out, 1 }; }
    //};

    //class MultiplyNode : public Node {
    //    std::vector<InputPort> _ins;
    //    OutputPort _out;

    //public:
    //    explicit MultiplyNode(std::vector<InputPort>&& ins) noexcept : _ins(std::move(ins)) {}

    //    void tick(std::span<MidiMessage const> const& midi) noexcept override {
    //        Sample result = 1.0;
    //        for (auto& in : _ins) {
    //            result *= in.get();
    //        }
    //        _out.push(result);
    //    }
    //    std::span<InputPort const> inputs_impl() const noexcept override { return _ins; }
    //    std::span<OutputPort const> outputs_impl() const noexcept override { return { &_out, 1 }; }
    //};

    //class IntegratorNode : public Node {
    //    InputPort _in_velocity, _in_previous, _in_sample_rate{0, 44100};
    //    OutputPort _out;

    //public:
    //    void tick(std::span<MidiMessage const> const& midi) noexcept override {
    //        _out.push(_in_velocity.get() / _in_sample_rate.get() + _in_previous.get());
    //    }
    //    std::span<InputPort const> inputs_impl() const noexcept override { return { &_in_velocity, 3 }; }
    //    std::span<OutputPort const> outputs_impl() const noexcept override { return { &_out, 1 }; }
    //};

    //struct WarperNode : public Node {
    //    InputPort _in, _in_threshold;
    //    OutputPort _out{ 1 }, _out_aliased;

    //    void tick(std::span<MidiMessage const> const& midi) noexcept override {
    //        Sample threshold = _in_threshold.get();
    //        Sample sample_prev = _out_aliased.get();
    //        Sample sample = _in.get();
    //        Sample sample_warped = sample;
    //        bool warped = false;

    //        if (sample > threshold) { sample_warped = warp_pm1(sample, threshold); warped = true; }
    //        else if (sample < -threshold) { sample_warped = warp_pm1(sample, threshold); warped = true; }
    //        _out_aliased.push(sample_warped);

    //        Sample sample_warped_aa = sample_warped;
    //        if (warped) {
    //            Sample delta = (sample - sample_prev) / 2.0;
    //            _out.update(sample_prev - polyblep_error(sample_prev, delta, threshold, PolyblepSide::LEFT));
    //            sample_warped_aa -= polyblep_error(sample_warped_aa, delta, threshold, PolyblepSide::RIGHT);
    //        }
    //        _out.push(sample_warped_aa);
    //    }
    //    std::span<InputPort const> inputs_impl() const noexcept override { return { &_in, 2 }; }
    //    std::span<OutputPort const> outputs_impl() const noexcept override { return { &_out, 2 }; }
    //};

    //struct IirFilterNode : public Node {
    //    InputPort _in{4}, _inw[4], _outw[3];
    //    OutputPort _out{3};

    //    void tick(std::span<MidiMessage const> const& midi) noexcept override {
    //        Sample weighted_sum = 0.0;
    //        for (size_t i = 0; i < sizeof(_inw) / sizeof(InputPort); ++i) {
    //            weighted_sum += _in.get(i) * _inw[i].get();
    //        }
    //        for (size_t i = 0; i < sizeof(_outw) / sizeof(InputPort); ++i) {
    //            weighted_sum += _out.get(i) * _outw[i].get();
    //        }
    //        _out.push(weighted_sum);
    //    }
    //    std::span<InputPort const> inputs_impl() const noexcept override { return { &_in, 1 + (sizeof(_inw) + sizeof(_outw)) / sizeof(InputPort)}; }
    //    std::span<OutputPort const> outputs_impl() const noexcept override { return { &_out, 1 }; }
    //};

    //class ConstantNode : public Node {
    //    OutputPort _output;
    //    Sample _value;

    //public:
    //    explicit ConstantNode(Sample value) noexcept : _value(value) {}

    //    void tick(std::span<MidiMessage const> const&) noexcept override {
    //        _output.push(_value);
    //    }

    //    std::span<OutputPort const> outputs_impl() const noexcept override { return { &_output, 1 }; }
    //};

    //class UniformNoiseNode : public Node {
    //    std::mt19937 _generator{std::random_device{}()};
    //    std::uniform_real_distribution<Sample> _distribution{-1.0f,1.0f};
    //    OutputPort _output;

    //    void tick(std::span<MidiMessage const> const&) noexcept override {
    //        _output.push(_distribution(_generator));
    //    }

    //    std::span<OutputPort const> outputs_impl() const noexcept override { return { &_output, 1 }; }
    //};

    ///* ─────────────  Patch  ───────────── */
    //class Graph : public Node {
    //    std::vector<std::unique_ptr<Node>> _nodes;
    //    std::vector<InputPort> _private_ins;
    //    std::vector<OutputPort> _private_outs;
    //    std::vector<InputPort> _public_ins;
    //    std::vector<OutputPort> _public_outs;

    //public:
    //    explicit Graph(
    //        std::vector<std::unique_ptr<Node>>&& nodes,
    //        std::vector<InputPort>&& private_ins,
    //        std::vector<OutputPort>&& private_outs
    //    ) noexcept:
    //        _nodes(std::move(nodes)),
    //        _private_ins(std::move(private_ins)),
    //        _private_outs(std::move(private_outs)),
    //        _public_ins(_private_outs.size()),
    //        _public_outs(_private_ins.size())
    //    {
    //        pseudo_topological_sort();
    //        init_buffers();
    //    }

    //private:
    //    void pseudo_topological_sort() {
    //        const size_t n = _nodes.size();

    //        std::unordered_map<InputPort*, size_t> owner_of;
    //        for (size_t i = 0; i < n; ++i) {
    //            for (auto& in : _nodes[i]->inputs()) {
    //                owner_of[&in] = i;
    //            }
    //        }

    //        std::unordered_map<InputPort*, size_t> parent_of;
    //        for (size_t i = 0; i < n; ++i) {
    //            for (auto& out : _nodes[i]->outputs()) {
    //                for (auto const dst : out.fan) {
    //                    parent_of[dst] = i;
    //                }
    //            }
    //        }

    //        auto make_heads_queue = [&]() {
    //            std::deque<size_t> queue;
    //            for (size_t i = 0; i < n; ++i) {
    //                bool all_inputs_disconnected = true;
    //                for (auto& in : _nodes[i]->inputs()) {
    //                    if (parent_of.contains(&in)) {
    //                        all_inputs_disconnected = false;
    //                        break;
    //                    }
    //                }
    //                if (!all_inputs_disconnected) continue;
    //                queue.push_back(i);
    //            }
    //            for (auto& out : _private_outs) {
    //                for (auto& dst : out.fan) {
    //                    if (!owner_of.contains(dst)) continue;
    //                    size_t child = owner_of[dst];
    //                    queue.push_back(child);
    //                }
    //            }
    //            return queue;
    //        };

    //        std::vector<std::unordered_set<size_t>> cyclic_parents_of(n);
    //        {
    //            auto queue = make_heads_queue();
    //            std::vector<bool> seen(n, false);
    //            while (!queue.empty()) {
    //                size_t i = queue.front();
    //                queue.pop_front();
    //                if (seen[i]) continue;
    //                seen[i] = true;

    //                std::vector<bool> inner_seen(n, false);
    //                std::deque<size_t> inner_queue;
    //                inner_queue.push_back(i);

    //                while (!inner_queue.empty()) {
    //                    size_t node = inner_queue.front();
    //                    inner_queue.pop_front();
    //                    if (inner_seen[node]) continue;
    //                    inner_seen[node] = true;

    //                    for (auto& out : _nodes[node]->outputs()) {
    //                        for (auto& dst : out.fan) {
    //                            if (!owner_of.contains(dst)) continue;
    //                            size_t child = owner_of[dst];
    //                            if (!cyclic_parents_of[node].empty()) continue;
    //                            if (child == i) {
    //                                cyclic_parents_of[i].insert(node);
    //                            }
    //                            inner_queue.push_back(child);
    //                        }
    //                    }
    //                }

    //                for (auto& out : _nodes[i]->outputs()) {
    //                    for (auto& dst : out.fan) {
    //                        if (!owner_of.contains(dst)) continue;
    //                        size_t child = owner_of[dst];
    //                        queue.push_back(child);
    //                    }
    //                }
    //            }
    //        }

    //        auto queue = make_heads_queue();
    //        std::vector<bool> placed(n, false);
    //        std::vector<size_t> sorted;
    //        sorted.reserve(n);

    //        while (!queue.empty()) {
    //            size_t node = queue.front();
    //            queue.pop_front();
    //            if (placed[node]) continue;

    //            bool all_dependencies_satisfied = true;
    //            for (auto& in : _nodes[node]->inputs()) {
    //                if (!parent_of.contains(&in)) continue;
    //                size_t parent = parent_of[&in];
    //                if (!placed[parent] && !cyclic_parents_of[node].contains(parent)) {
    //                    all_dependencies_satisfied = false;
    //                    break;
    //                }
    //            }
    //            
    //            if (all_dependencies_satisfied) {
    //                for (auto& out : _nodes[node]->outputs()) {
    //                    for (auto& dst : out.fan) {
    //                        if (!owner_of.contains(dst)) continue;
    //                        size_t child = owner_of[dst];
    //                        queue.push_back(child);
    //                    }
    //                }
    //                sorted.push_back(node);
    //                placed[node] = true;
    //            }
    //            else {
    //                queue.push_back(node);
    //            }
    //        }
    //        
    //        std::vector<std::unique_ptr<Node>> sorted_nodes;
    //        sorted_nodes.reserve(n);
    //        for (size_t i : sorted) {
    //            sorted_nodes.push_back(std::move(_nodes[i]));
    //        }
    //        _nodes.swap(sorted_nodes);
    //    }

    //    void init_buffers() {
    //        std::unordered_map<InputPort const*, size_t> input_global_latencies;

    //        for (size_t node_idx = 0; node_idx < _nodes.size() + 1; ++node_idx) {
    //            // node_idx == _nodes.size() means this graph
    //            auto node = (node_idx < _nodes.size())
    //                ? _nodes[node_idx].get()
    //                : this;
    //            auto node_inputs = (node_idx < _nodes.size())
    //                ? _nodes[node_idx]->inputs()
    //                : _private_ins;

    //            size_t node_global_latency = 0;
    //            for (auto& in : node_inputs) {
    //                node_global_latency = std::max(node_global_latency, input_global_latencies[&in]);
    //            }
    //            for (auto& in : node_inputs) {
    //                in.add_latency(node_global_latency - input_global_latencies[&in]);
    //            }
    //            if (node == this) continue;
    //            node_global_latency += node->inner_latency();
    //            for (auto& out : node->outputs()) {
    //                for (auto& dst : out.fan) {
    //                    input_global_latencies[dst] = node_global_latency + out.get_latency();
    //                }
    //            }
    //        }
    //    }

    //public:
    //    void tick(std::span<MidiMessage const> const& midi) noexcept override {
    //        for (size_t i = 0; i < _private_outs.size(); ++i) {
    //            _private_outs[i].push(_public_ins[i].get());
    //        }
    //        for (auto& node : _nodes) {
    //            node->tick(midi);
    //        }
    //        for (size_t i = 0; i < _private_ins.size(); ++i) {
    //            _public_outs[i].push(_private_ins[i].get());
    //        }
    //    }

    //    size_t inner_latency() const noexcept override {
    //        std::unordered_map<InputPort const*, size_t> input_global_latencies;
    //        size_t max_latency = 0;

    //        for (size_t node_idx = 0; node_idx < _nodes.size() + 1; ++node_idx) {
    //            // node_idx == _nodes.size() means this graph
    //            auto node = (node_idx < _nodes.size())
    //                ? _nodes[node_idx].get()
    //                : this;
    //            auto node_inputs = (node_idx < _nodes.size())
    //                ? const_cast<Node const*>(_nodes[node_idx].get())->inputs()
    //                : _private_ins;

    //            size_t node_global_latency = 0;
    //            for (auto& in : node_inputs) {
    //                node_global_latency = std::max(node_global_latency, input_global_latencies[&in]);
    //            }
    //            if (node == this) continue;
    //            node_global_latency += node->inner_latency();
    //            for (auto& out : node->outputs()) {
    //                for (auto& dst : out.fan) {
    //                    size_t new_latency = node_global_latency + out.get_latency();
    //                    max_latency = std::max(max_latency, new_latency);
    //                    input_global_latencies[dst] = new_latency;
    //                }
    //            }
    //        }

    //        return max_latency;
    //    }

    //    std::span<InputPort const> inputs_impl() const noexcept override { return _public_ins; }
    //    std::span<OutputPort const> outputs_impl() const noexcept override { return _public_outs; }
    //};

    //struct NodeFactoryBase {
    //    virtual ~NodeFactoryBase() = default;
    //    virtual std::unique_ptr<Node> create() const = 0;
    //    virtual std::unique_ptr<NodeFactoryBase> clone() const = 0;
    //};

    //template<class T>
    //struct NodeFactory;

    //template<class T>
    //struct NodeFactoryBaseTemplate : public NodeFactoryBase {
    //    std::unique_ptr<NodeFactoryBase> clone() const override {
    //        return std::make_unique<NodeFactory<T>>();
    //    }
    //    std::unique_ptr<Node> create() const override {
    //        return std::unique_ptr<Node>(create_t().release());
    //    }
    //    virtual std::unique_ptr<T> create_t() const = 0;
    //};

    //template<class T>
    //struct NodeFactory : public NodeFactoryBaseTemplate<T> {
    //    std::unique_ptr<T> create_t() const override {
    //        return std::make_unique<T>();
    //    }
    //};

    //template<>
    //struct NodeFactory<SumNode> : public NodeFactoryBaseTemplate<SumNode> {
    //    size_t _num_inputs;

    //    explicit NodeFactory(size_t num_inputs = 0) noexcept : _num_inputs(num_inputs) {}

    //    std::unique_ptr<SumNode> create_t() const override {
    //        std::vector<InputPort> inputs(_num_inputs);
    //        return std::make_unique<SumNode>(std::move(inputs));
    //    }

    //    std::unique_ptr<NodeFactoryBase> clone() const override {
    //        return std::make_unique<NodeFactory>(_num_inputs);
    //    }

    //    size_t add_input_port() noexcept {
    //        return _num_inputs++;
    //    }
    //};

    //template<>
    //struct NodeFactory<MultiplyNode> : public NodeFactoryBaseTemplate<MultiplyNode> {
    //    size_t _num_inputs;

    //    explicit NodeFactory(size_t num_inputs = 0) noexcept : _num_inputs(num_inputs) {}

    //    std::unique_ptr<MultiplyNode> create_t() const override {
    //        std::vector<InputPort> inputs(_num_inputs);
    //        return std::make_unique<MultiplyNode>(std::move(inputs));
    //    }

    //    std::unique_ptr<NodeFactoryBase> clone() const override {
    //        return std::make_unique<NodeFactory>(_num_inputs);
    //    }

    //    size_t add_input_port() noexcept {
    //        return _num_inputs++;
    //    }
    //};

    //template<>
    //class NodeFactory<ConstantNode> : public NodeFactoryBaseTemplate<ConstantNode> {
    //    Sample _value;

    //public:
    //    explicit NodeFactory(Sample value = 0) noexcept : _value(value) {}

    //    std::unique_ptr<ConstantNode> create_t() const {
    //        return std::make_unique<ConstantNode>(_value);
    //    }

    //    std::unique_ptr<NodeFactoryBase> clone() const override {
    //        return std::make_unique<NodeFactory>(_value);
    //    }

    //    void set_value(Sample value) {
    //        _value = value;
    //    }
    //};

    //using PortId = size_t;
    //struct NodePortId {
    //    size_t node;
    //    PortId port;
    //};

    //template<>
    //struct NodeFactory<Graph> : public NodeFactoryBaseTemplate<Graph> {
    //    static const size_t GRAPH_ID = std::numeric_limits<size_t>::max();

    //    struct Edge { NodePortId source, destination; };

    //    std::vector<std::unique_ptr<NodeFactoryBase>> factories;
    //    std::vector<Edge> edges;
    //    size_t num_inputs{0}, num_outputs{0};

    //    explicit NodeFactory() noexcept {}
    //    explicit NodeFactory(NodeFactory const& other) {
    //        *this = other;
    //    }
    //    explicit NodeFactory(NodeFactory&& other) noexcept
    //    {
    //        *this = std::move(other);
    //    }

    //    NodeFactory& operator=(NodeFactory const& other) {
    //        for (auto const& factory : other.factories) {
    //            factories.emplace_back(factory->clone());
    //        }
    //        edges = other.edges;
    //        num_inputs = other.num_inputs;
    //        num_outputs = other.num_outputs;
    //        return *this;
    //    }

    //    NodeFactory& operator=(NodeFactory&& other) noexcept {
    //        factories = std::move(other.factories);
    //        edges = std::move(other.edges);
    //        num_inputs = other.num_inputs;
    //        num_outputs = other.num_outputs;
    //        other.num_inputs = 0;
    //        other.num_outputs = 0;
    //        return *this;
    //    }

    //    std::unique_ptr<Graph> create_t() const override {
    //        std::vector<std::unique_ptr<Node>> nodes;
    //        std::unordered_map<InputPort*, std::vector<OutputPort*>> port_mappings;

    //        // note: number of private inputs corresponds to number of public outputs
    //        // note: number of private outputs corresponds to number of public inputs
    //        std::vector<InputPort> graph_private_inputs(num_outputs);
    //        std::vector<OutputPort> graph_private_outputs(num_inputs);

    //        for (auto const& factory : factories) {
    //            nodes.emplace_back(factory->create());
    //        }

    //        for (auto const& edge : edges) {
    //            std::span<InputPort> inputs = (edge.destination.node == NodeFactory<Graph>::GRAPH_ID)
    //                ? graph_private_inputs
    //                : nodes[edge.destination.node]->inputs();
    //            std::span<OutputPort> outputs = (edge.source.node == NodeFactory<Graph>::GRAPH_ID)
    //                ? graph_private_outputs
    //                : nodes[edge.source.node]->outputs();

    //            auto const out = &outputs[edge.source.port];
    //            auto const in = &inputs[edge.destination.port];
    //            port_mappings[in].push_back(out);
    //        }

    //        for (auto const& [in, outs] : port_mappings) {
    //            if (outs.size() == 1) {
    //                outs[0]->connect_to(in);
    //            }
    //            else {
    //                auto sum_node = NodeFactory<SumNode>(outs.size()).create();
    //                for (size_t i = 0; i < outs.size(); ++i) {
    //                    outs[i]->connect_to(&sum_node->inputs()[i]);
    //                }
    //                sum_node->outputs()[0].connect_to(in);
    //                nodes.emplace_back(std::move(sum_node));
    //            }
    //        }

    //        return std::make_unique<Graph>(
    //            std::move(nodes),
    //            std::move(graph_private_inputs),
    //            std::move(graph_private_outputs)
    //        );
    //    }

    //    std::unique_ptr<NodeFactoryBase> clone() const override {
    //        auto graph = std::make_unique<NodeFactory<Graph>>();
    //        *graph = *this;
    //        return graph;
    //    }

    //    template<class T, class... Args>
    //    auto add_node(Args&&... args) {
    //        auto node_ptr = std::make_unique<NodeFactory<T>>(std::forward<Args>(args)...);
    //        auto node_ref = node_ptr.get();
    //        factories.emplace_back(std::move(node_ptr));
    //        return std::make_tuple(node_ref, factories.size() - 1);
    //    }

    //    size_t duplicate_node(size_t node_id) {
    //        auto const& factory = factories[node_id];
    //        factories.emplace_back(factory->clone());
    //        size_t new_node_id = factories.size() - 1;
    //        size_t edges_size = edges.size();
    //        for (size_t i = 0; i < edges_size; ++i) {
    //            Edge edge_clone = edges[i];
    //            bool needs_duplicate = false;
    //            if (edge_clone.destination.node == node_id) {
    //                edge_clone.destination.node = new_node_id;
    //                needs_duplicate = true;
    //            }
    //            if (edge_clone.source.node == node_id) {
    //                edge_clone.source.node = new_node_id;
    //                needs_duplicate = true;
    //            }
    //            if (needs_duplicate) {
    //                edges.push_back(edge_clone);
    //            }
    //        }
    //        return new_node_id;
    //    }

    //    PortId add_input_port() noexcept {
    //        return num_inputs++;
    //    }

    //    PortId add_output_port() noexcept {
    //        return num_outputs++;
    //    }

    //    void connect(NodePortId source, NodePortId destination) {
    //        edges.emplace_back(source, destination);
    //    }
    //};

    ////class RemoteSinkNode : public Node {
    ////    InputPort _in;
    ////    std::vector<OutputPort> _remote_outs;

    ////public:
    ////    explicit RemoteSinkNode(std::vector<OutputPort>&& remote_outs) noexcept :
    ////        _remote_outs(std::move(remote_outs))
    ////    {}

    ////    void tick(std::span<MidiMessage const> const&) noexcept override {
    ////        for (auto& remote_out : _remote_outs) {
    ////            remote_out.push(_in.get());
    ////        }
    ////    }

    ////    std::span<InputPort const> inputs_impl() const noexcept override { return { &_in, 1 }; }
    ////    std::span<OutputPort const> outputs_impl() const noexcept override { return _remote_outs; }
    ////};

    ////template<>
    ////class NodeFactory<RemoteSinkNode> : public NodeFactoryBaseTemplate<RemoteSinkNode> {
    ////    size_t _num_remotes;

    ////public:
    ////    template<class... Args>
    ////    explicit NodeFactory() noexcept :
    ////        _num_remotes(0)
    ////    {}

    ////    std::unique_ptr<RemoteSinkNode> create_t() const override {
    ////        std::vector<OutputPort> remote_outs(_num_remotes);
    ////        return std::make_unique<RemoteSinkNode>(std::move(remote_outs));
    ////    }

    ////    std::unique_ptr<NodeFactoryBase> clone() const override {
    ////        assert(false); // cannot clone this, sorry liskov
    ////    }

    ////    PortId add_remote() noexcept {
    ////        return _num_remotes++;
    ////    }
    ////};
    ////
    ////class RemoteSourceNode : public Node {
    ////    InputPort _remote_in;
    ////    OutputPort _out;

    ////public:
    ////    void tick(std::span<MidiMessage const> const&) noexcept override {}

    ////    std::span<InputPort const> inputs_impl() const noexcept override { return { &_remote_in, 1 }; }
    ////    std::span<OutputPort const> outputs_impl() const noexcept override { return { &_out, 1 }; }
    ////};

    ////template<>
    ////class NodeFactory<RemoteSourceNode> : public NodeFactoryBaseTemplate<RemoteSourceNode> {
    ////    NodeFactory<RemoteSinkNode>* _connection;
    ////    PortId _port_id;

    ////public:
    ////    explicit NodeFactory(NodeFactory<RemoteSinkNode>* connection) noexcept :
    ////        _connection(connection),
    ////        _port_id(connection->add_remote())
    ////    {
    ////        _port_id = connection->add_remote();
    ////    }

    ////    std::unique_ptr<RemoteSourceNode> create_t() const override {
    ////        return std::make_unique<RemoteSourceNode>(_connection->get_out_port());
    ////    }

    ////    std::unique_ptr<NodeFactoryBase> clone() const override {
    ////        return std::make_unique<NodeFactory>(_connection);
    ////    }
    ////};

    //struct MidiNode : public Node {
    //    static constexpr size_t const MAX_MIDI_NOTES = 128;
    //    static constexpr size_t const BASE_INPUTS = 1;
    //    static constexpr size_t const BASE_GRAPH_INPUTS = 2;

    //    struct MidiNoteState {
    //        size_t ttl{0};
    //        size_t amplitude{0};
    //    };

    //    std::array<std::unique_ptr<Node>, MAX_MIDI_NOTES> _graphs;
    //    std::array<MidiNoteState, MAX_MIDI_NOTES> _note_states;
    //    std::vector<uint8_t> _active_voices;
    //    size_t _max_ttl;
    //    std::vector<InputPort> _public_inputs;
    //    OutputPort out_mix;

    //    explicit MidiNode(
    //        NodeFactory<Graph> const& voice_factory,
    //        std::vector<InputPort>&& extra_public_inputs
    //    ) noexcept {
    //        _public_inputs.reserve(extra_public_inputs.size() + BASE_INPUTS);
    //        _public_inputs.emplace_back(std::pow(10.0, -60.0 / 20.0)); // silence_threshold, -60db
    //        assert(_public_inputs.size() == BASE_INPUTS);
    //        for (auto& extra_input : extra_public_inputs) {
    //            _public_inputs.emplace_back(std::move(extra_input));
    //        }
    //        assert(_public_inputs.size() == _public_inputs.capacity());

    //        for (size_t i = 0; i < MAX_MIDI_NOTES; ++i) {
    //            _graphs[i] = voice_factory.create();
    //        }
    //        _max_ttl = dynamic_cast<Graph*>(_graphs[0].get())->inner_latency();
    //        _active_voices.reserve(MAX_MIDI_NOTES);
    //    }

    //    void tick(std::span<MidiMessage const> const& midi) noexcept override {
    //        auto silence_threshold = _public_inputs[0].get();

    //        for (auto const& midi_message : midi) {
    //            if (midi_message.type == MidiMessageType::NOTE_ON) {
    //                auto& note_state = _note_states[midi_message.note_on.note_number];
    //                note_state.amplitude = midi_message.note_on.amplitude;
    //                note_state.ttl = _max_ttl;
    //                _active_voices.push_back(midi_message.note_on.note_number);
    //            }
    //            if (midi_message.type == MidiMessageType::NOTE_OFF) {
    //                auto& note_state = _note_states[midi_message.note_off.note_number];
    //                note_state.amplitude = 0;
    //            }
    //        }

    //        Sample result = 0.0;
    //        for (size_t i = 0; i < _active_voices.size(); ++i) {
    //            uint8_t note_number = _active_voices[i];
    //            auto& note_state = _note_states[note_number];
    //            auto& graph = _graphs[note_number];
    //            if (note_state.amplitude) {
    //                note_state.ttl = _max_ttl;
    //            }
    //            else if (note_state.ttl) {
    //                --note_state.ttl;
    //            }
    //            else if (auto last_output = graph->outputs()[0].get(); last_output <= silence_threshold && last_output >= -silence_threshold) {
    //                _active_voices.erase(_active_voices.begin() + i);
    //                --i; // underflows then overflows for i=0 but should be fine
    //                continue;
    //            }

    //            auto const graph_inputs = graph->inputs();
    //            graph_inputs[0].push(note_number_to_frequency(note_number));
    //            graph_inputs[1].push(note_state.amplitude / 127.0);
    //            auto extra_inputs = get_extra_inputs();
    //            for (size_t extra_i = 0; extra_i < extra_inputs.size(); ++extra_i) {
    //                graph_inputs[BASE_GRAPH_INPUTS+extra_i].push(extra_inputs[extra_i].get());
    //            }
    //            graph->tick(midi);
    //            result += graph->outputs()[0].get();
    //        }
    //        out_mix.push(result);
    //    }

    //    size_t inner_latency() const noexcept override {
    //        return _max_ttl;
    //    }

    //    static Sample note_number_to_frequency(uint8_t note_number) {
    //        return Sample(440.0 * std::pow(2.0, (note_number - 69) / 12.0));
    //    }

    //    std::span<InputPort const> inputs_impl() const noexcept override { return _public_inputs; }
    //    std::span<OutputPort const> outputs_impl() const noexcept override { return { &out_mix, 1 }; }

    //    std::span<InputPort> get_extra_inputs() {
    //        return { _public_inputs.data() + BASE_INPUTS, _public_inputs.size() - BASE_INPUTS };
    //    }
    //};

    //template<>
    //class NodeFactory<MidiNode> : public NodeFactoryBaseTemplate<MidiNode> {
    //    NodeFactory<Graph> _voice_factory;
    //    PortId _frequency_port, _amplitude_port;
    //    PortId _output_port;
    //    size_t _num_extra_public_inputs;

    //public:
    //    explicit NodeFactory() noexcept:
    //        _frequency_port(_voice_factory.add_input_port()),
    //        _amplitude_port(_voice_factory.add_input_port()),
    //        _output_port(_voice_factory.add_output_port()),
    //        _num_extra_public_inputs(0)
    //    {}

    //    std::unique_ptr<MidiNode> create_t() const override {
    //        std::vector<InputPort> extra_public_inputs(_num_extra_public_inputs);
    //        return std::make_unique<MidiNode>(_voice_factory, std::move(extra_public_inputs));
    //    }

    //    std::unique_ptr<NodeFactoryBase> clone() const override {
    //        auto factory = std::make_unique<NodeFactory>();
    //        factory->_voice_factory = _voice_factory;
    //        factory->_frequency_port = _frequency_port;
    //        factory->_amplitude_port = _amplitude_port;
    //        factory->_output_port = _output_port;
    //        factory->_num_extra_public_inputs = _num_extra_public_inputs;
    //        return factory;
    //    }

    //    NodeFactory<Graph>& get_voice_factory() {
    //        return _voice_factory;
    //    }

    //    PortId get_voice_frequency_port() const noexcept {
    //        return _frequency_port;
    //    }

    //    PortId get_voice_amplitude_port() const noexcept {
    //        return _amplitude_port;
    //    }

    //    PortId get_voice_output_port() const noexcept {
    //        return _output_port;
    //    }

    //    // returns [midi port id, voice port id]
    //    std::tuple<PortId, PortId> add_forwarding_input_port() noexcept {
    //        PortId midi_id = _num_extra_public_inputs++;
    //        PortId voice_id = _voice_factory.add_input_port();
    //        return std::make_tuple(midi_id + MidiNode::BASE_INPUTS, voice_id);
    //    }
    //};
}
