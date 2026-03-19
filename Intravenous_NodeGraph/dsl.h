#pragma once

#include "graph_node.h"
#include "basic_nodes.h"
#include "node.h"

#include <array>
#include <cassert>
#include <concepts>
#include <cstddef>
#include <functional>
#include <initializer_list>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace iv {
    namespace details {
        [[noreturn]] static void error(std::string_view msg)
        {
            throw std::logic_error(std::string(msg));
        }
    }

    class GraphBuilder;

    struct SignalRef {
        GraphBuilder* graph_builder;
        size_t node_index;
        size_t output_port;

        explicit SignalRef(GraphBuilder& graph_builder, size_t node_index, size_t output_port):
            graph_builder(&graph_builder),
            node_index(node_index),
            output_port(output_port)
        {
            if (node_index >= graph_builder._nodes.size()) {
                details::error(
                    "node at index " + std::to_string(node_index) + " "
                    "is out of bounds in builder " + graph_builder._parent_path + ", "
                    "nodes.size() = " + std::to_string(graph_builder._nodes.size())
                );
            }

            auto& node = graph_builder._nodes[node_index];
            size_t num_outputs = get_num_outputs(node);
            if (output_port >= get_num_outputs(node)) {
                details::error(
                    "output port " + std::to_string(output_port) + " of "
                    "node at index " + std::to_string(node_index) + " in "
                    "builder " + graph_builder._parent_path + " "
                    "is out of bounds, get_num_outputs(nodes) = " + std::to_string(num_outputs)
                );
            }
        }

        std::string to_string() const {
            return "signal at address " + graph_builder->debug_node_id(node_index) + ":" + std::to_string(output_port);
        }
    };

    struct NamedRef {
        std::string_view name;
        SignalRef signal;
    };

    class NodeRef {
        GraphBuilder* _graph_builder;
        size_t _index;

    public:
        constexpr NodeRef(GraphBuilder& graph_builder, size_t index) noexcept:
            _graph_builder(&graph_builder),
            _index(index)
        {}

        TypeErasedNode& node() const {
            return _graph_builder->_nodes[_index];
        }

        std::string to_string() const {
            return "node at address " + _graph_builder->debug_node_id(_index);
        }

        SignalRef operator[](size_t output_index) const {
            return SignalRef(*_graph_builder, _index, output_index);
        }

        SignalRef operator[](std::string_view output_name) const {
            auto outputs = get_outputs(node());
            for (size_t output_port = 0; output_port < outputs.size(); ++output_port) {
                if (outputs[output_port].name == output_name) {
                    return SignalRef(*_graph_builder, _index, output_port);
                }
            }
            details::error(
                "an output port named " + std::string(output_name) + " "
                "does not exist on" + to_string()
            );
        }

        operator SignalRef() const {
            if (get_num_outputs(node()) != 1) {
                details::error(
                    to_string() + " "
                    "does not have exactly 1 output port: it cannot be implicitly converted to signal"
                );
            }
            return SignalRef(*_graph_builder, _index, 0);
        }

        template<class... Refs>
        void operator()(Refs&&... refs) const {
            std::array<SignalRef, sizeof...(Refs)> refs_array{ std::forward<Refs>(refs)... };
            if (_graph_builder->_placed_nodes.contains(_index)) {
                details::error(
                    to_string() + " "
                    "was already placed in the graph"
                );
            }
            size_t num_inputs = get_num_inputs(node());
            if (sizeof...(Refs) >= num_inputs) {
                details::error(
                    to_string() + " "
                    "has at most " + std::to_string(num_inputs) + " inputs, "
                    "got " + std::to_string(sizeof...(Refs))
                );
            }
            for (size_t input_port = 0; input_port < refs_array.size(); ++input_port) {
                SignalRef& ref = refs_array[input_port];
                PortId source { ref.node_index, ref.output_port };
                PortId target { _index, input_port };
                _graph_builder->_edges.emplace(source, target);
            }
            _graph_builder->_placed_nodes.insert(_index);
        }

        void operator()(std::initializer_list<NamedRef> refs) const {
            auto inputs = get_inputs(node());

            for (auto ref_i = refs.begin(); ref_i != refs.end(); ++ref_i) {
                auto& ref = *ref_i;
                PortId source { ref.signal.node_index, ref.signal.output_port };

                if (ref.signal.graph_builder != _graph_builder) {
                    details::error(
                        ref.signal.to_string() + " "
                        "does not belong to the same builder as " + to_string()
                    );
                }

                bool placed = false;
                for (size_t input_port = 0; input_port < inputs.size(); ++input_port) {
                    if (ref.name == inputs[input_port].name) {
                        PortId target { _index, input_port };
                        _graph_builder->_edges.emplace(source, target);
                        placed = true;
                        break;
                    }
                }
                if (!placed) {
                    details::error(
                        "an output port named " + std::string(ref.name) + " "
                        "does not exist on node at index " + _graph_builder->debug_node_id(_index)
                    );
                }
            }
            _graph_builder->_placed_nodes.insert(_index);
        }
    };

    class GraphBuilder {
        friend class NodeRef;
        friend class SignalRef;

        std::string _parent_path;

        GraphNode::Nodes _nodes;
        GraphNode::Edges _edges;
        std::unordered_set<size_t> _placed_nodes;

        std::vector<InputConfig> _public_inputs;
        std::unordered_map<std::string_view, size_t> _input_name_to_index;

        std::vector<OutputConfig> _public_outputs;
        bool _outputs_defined{};

    public:
        explicit GraphBuilder(std::string_view parent_path = {}) noexcept:
            _parent_path(parent_path)
        {}

        std::string debug_node_id(size_t index) const {
            assert(index < _nodes.size() && "node index out of bounds");
            std::string nested_path = _parent_path;
            if (!nested_path.empty()) {
                nested_path += ".";
            }
            nested_path += std::to_string(index);
            return nested_path;
        }

        template <class... Ts>
        NodeRef input(Ts&&... args) {
            auto& input_config = _public_inputs.emplace_back(std::forward<Ts>(args)...);
            if (!input_config.name.empty()) {
                if (_input_name_to_index.contains(input_config.name)) {
                    details::error("input " + std::string(input_config.name) + " already exists");
                }
                _input_name_to_index.emplace(input_config.name, _public_inputs.size() - 1);
            }
            return { this };
        }

        template<class Node>
        NodeRef node(Node node) {
            _nodes.push_back(node);
            return { this };
        }

        template<class Fn>
        NodeRef subgraph(Fn&& fn) {
            std::string nested_path = _parent_path;
            if (!nested_path.empty()) {
                nested_path += ".";
            }
            nested_path += std::to_string(_nodes.size());

            GraphBuilder g(nested_path);
            std::forward<Fn>(fn)(g);
            return node(g.build());
        }

        template<class... Refs>
        void outputs(Refs&&... refs) {
            std::array<SignalRef, sizeof...(Refs)> refs_array { std::forward<Refs>(refs)... };
            for (size_t i = 0; i < refs_array.size(); ++i) {
                auto& ref = refs_array[i];
                if (ref.graph_builder != this) {
                    details::error(
                        "builder " + _parent_path + ": outputs(...): "
                        "SignalRef at index " + std::to_string(i) + " "
                        "belongs to builder " + ref.graph_builder->_parent_path
                    );
                }
                refs_array[i];
            }
        }

        void outputs(std::initializer_list<NamedRef> refs);

        GraphNode build() &&;
    };
}
