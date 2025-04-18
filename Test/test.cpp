#include "pch.h"
#include "../Source/NodeGraph.h"


//TEST(TestCaseName, TestName) {
//  EXPECT_EQ(1, 1);
//  EXPECT_TRUE(true);
//}
//
//TEST(OutputPortPush, CommunicatesWithConnectedInputPort) {
//    iv::InputPort in;
//    iv::OutputPort out;
//    out.connect_to(&in);
//
//    iv::Sample expected = 3.0;
//    out.push(expected);
//    auto actual = in.next();
//    EXPECT_EQ(actual, expected);
//}
//
//TEST(OutputPortPush, LatencyCommunicatesWithConnectedInputPort, Test2) {
//    iv::InputPort in;
//    iv::OutputPort out{1};
//    out.connect_to(&in);
//
//    iv::Sample expected = 3.0;
//    out.push(expected);
//    in.next();
//    auto actual = in.next();
//    EXPECT_EQ(actual, expected);
//}
//
//TEST(Ports, Test3) {
//    iv::InputPort in;
//    iv::OutputPort out{1};
//    out.connect_to(&in);
//
//    iv::Sample expected = 3.0;
//    out.push(expected);
//    auto actual = out.back();
//    EXPECT_EQ(actual, expected);
//}
//
//TEST(Ports, TestIntegration1) {
//    iv::NodeFactory<iv::Graph> factory;
//    auto [sum, sum_id] = factory.add_node<iv::SumNode>(2);
//    auto in1 = factory.add_input_port();
//    auto in2 = factory.add_input_port();
//    auto out1 = factory.add_output_port();
//
//    factory.connect({ iv::NodeFactory<iv::Graph>::GRAPH_ID, in1 }, { sum_id, p1 });
//    factory.connect({ iv::NodeFactory<iv::Graph>::GRAPH_ID, in2 }, { sum_id, p2 });
//    factory.connect({ sum_id, 0 }, { iv::NodeFactory<iv::Graph>::GRAPH_ID, out1 });
//
//    auto graph = factory.create();
//    graph->inputs()[0].update(1.5);
//    graph->inputs()[1].update(2.5);
//    graph->tick({});
//    auto actual = graph->outputs()[0].back();
//
//    EXPECT_EQ(actual, 4);
//}
//
//TEST(Ports, TestIntegration2) {
//    iv::NodeFactory<iv::Graph> factory;
//    auto [_, warp_id] = factory.add_node<iv::WarperNode>();
//    auto in0 = factory.add_input_port();
//    auto in1 = factory.add_input_port();
//    auto out0 = factory.add_output_port();
//    auto out1 = factory.add_output_port();
//
//    factory.connect({ iv::NodeFactory<iv::Graph>::GRAPH_ID, in0 }, { warp_id, 0 });
//    factory.connect({ iv::NodeFactory<iv::Graph>::GRAPH_ID, in1 }, { warp_id, 1 });
//    factory.connect({ warp_id, 0 }, { iv::NodeFactory<iv::Graph>::GRAPH_ID, out0 });
//    factory.connect({ warp_id, 1 }, { iv::NodeFactory<iv::Graph>::GRAPH_ID, out1 });
//
//    // simulate warp discontinuity
//    auto graph_ptr = factory.create();
//    iv::Graph* graph = dynamic_cast<iv::Graph*>(graph_ptr.get());
//    graph->inputs()[in0].update(0.99);
//    graph->inputs()[in1].update(1.0);
//    graph->tick({});
//    auto r0 = graph->outputs()[out0].back();
//    auto q0 = graph->outputs()[out1].back();
//    graph->inputs()[in0].update(1.2);
//    graph->inputs()[in1].update(1.0);
//    graph->tick({});
//    auto r1 = graph->outputs()[out0].back();
//    auto q1 = graph->outputs()[out1].back();
//    graph->inputs()[in0].update(1.2);
//    graph->inputs()[in1].update(1.0);
//    graph->tick({});
//    auto r2 = graph->outputs()[out0].back();
//    auto q2 = graph->outputs()[out1].back();
//
//    auto latency = graph->compute_latency();
//
//    EXPECT_EQ(r0, q0);
//    EXPECT_EQ(r1, q1);
//    EXPECT_NE(r2, q2);
//}
