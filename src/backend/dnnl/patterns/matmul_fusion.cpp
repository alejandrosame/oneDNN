/*******************************************************************************
* Copyright 2020-2022 Intel Corporation
*
* Licensed under the Apache License, Version 2.0 (the "License");
* you may not use this file except in compliance with the License.
* You may obtain a copy of the License at
*
*     http://www.apache.org/licenses/LICENSE-2.0
*
* Unless required by applicable law or agreed to in writing, software
* distributed under the License is distributed on an "AS IS" BASIS,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
* See the License for the specific language governing permissions and
* limitations under the License.
*******************************************************************************/

#include "backend/dnnl/patterns/fusions.hpp"

#include "utils/pm/pbuilder.hpp"

namespace dnnl {
namespace graph {
namespace impl {
namespace dnnl_impl {
namespace pass {

namespace pm = impl::utils::pm;
using in_edges_t = pm::in_edges_t;
using pb_graph_t = pm::pb_graph_t;
using FCreateV2FusedOp = impl::pass::FCreateV2FusedOp;
using FCreateV2Pattern = impl::pass::FCreateV2Pattern;

/*!
 * \brief This provides matmul-related fusion, i.e.
 *        matmul-relu fusion
 *        The process includes follow steps:
 *          1. look for fusion pattern on the graph
 *          2. If found, verify if this transformation is safe / correct
 *          3. replace the pattern with a fused op, update the graph
 */
DNNL_BACKEND_REGISTER_PASSES_DEF_BEGIN(matmul_fusion)

DNNL_BACKEND_REGISTER_TRANSFORMATION_PASS(dnnl, matmul_bias_swish_fusion)
        .set_priority(9.0f)
        .set_attr<FCreateV2Pattern>("FCreateV2Pattern",
                [](const std::shared_ptr<pb_graph_t> &pgraph) -> void {
                    pm::pb_op *matmul
                            = pgraph->append_op(impl::op_kind::MatMul);
                    matmul->append_decision_function(check_input_num<2>);

                    pm::pb_op *bias = pgraph->append_op(impl::op_kind::BiasAdd,
                            in_edges_t {in_edge(0, matmul, 0)});
                    pm::pb_op *sigmoid
                            = pgraph->append_op(impl::op_kind::Sigmoid,
                                    in_edges_t {in_edge(0, bias, 0)});
                    pgraph->append_op(impl::op_kind::Multiply,
                            in_edges_t {in_edge(0, bias, 0),
                                    in_edge(1, sigmoid, 0)});
                })
        .set_attr<FCreateV2Pattern>("FCreateV2Pattern",
                [](const std::shared_ptr<pb_graph_t> &pgraph) -> void {
                    pm::pb_op *matmul
                            = pgraph->append_op(impl::op_kind::MatMul);
                    matmul->append_decision_function(check_input_num<3>);

                    pm::pb_op *sigmoid
                            = pgraph->append_op(impl::op_kind::Sigmoid,
                                    in_edges_t {in_edge(0, matmul, 0)});
                    pgraph->append_op(impl::op_kind::Multiply,
                            in_edges_t {in_edge(0, matmul, 0),
                                    in_edge(1, sigmoid, 0)});
                })
        .set_attr<FCreateV2FusedOp>(
                "FCreateV2FusedOp", []() -> std::shared_ptr<op_t> {
                    std::shared_ptr<op_t> fused_op = std::make_shared<op_t>(
                            op_kind::matmul_bias_swish);
                    fused_op->set_attr<std::string>("backend", "dnnl");
                    return fused_op;
                });

DNNL_BACKEND_REGISTER_TRANSFORMATION_PASS(dnnl, matmul_post_ops_chain_fusion)
        .set_priority(8.8f)
        .set_attr<FCreateV2Pattern>("FCreateV2Pattern",
                [](const std::shared_ptr<pb_graph_t> &pgraph) -> void {
                    pm::pb_op *pmatmul
                            = pgraph->append_op(impl::op_kind::MatMul);
                    pmatmul->append_decision_function(check_input_num<2>);

                    // Optional BN
                    auto popt_graph
                            = std::make_shared<pb_graph_t>("poptional_bn");
                    auto pbn = popt_graph->append_op(
                            impl::op_kind::BatchNormInference, "pbn");
                    popt_graph->create_input_port(0, pbn, 0);
                    popt_graph->create_output_port(0, pbn, 0);
                    auto popt = pgraph->append_optional(
                            popt_graph, {in_edge(0, pmatmul, 0)}, "popt");

                    // TODO(Yixin): special post op handle: swish is composed
                    // by sigmoid and multiply
                    auto other_postop_graph = std::make_shared<pb_graph_t>(
                            "pother_postop_graph");
                    pm::pb_op *pop = other_postop_graph->append_alternation(
                            {impl::op_kind::Abs, impl::op_kind::Clamp,
                                    impl::op_kind::Elu, impl::op_kind::GELU,
                                    impl::op_kind::HardTanh, impl::op_kind::Log,
                                    impl::op_kind::Sigmoid,
                                    impl::op_kind::SoftPlus, impl::op_kind::Pow,
                                    impl::op_kind::ReLU, impl::op_kind::Round,
                                    impl::op_kind::Sqrt, impl::op_kind::Square,
                                    impl::op_kind::Tanh, impl::op_kind::Add,
                                    impl::op_kind::Multiply,
                                    impl::op_kind::Maximum,
                                    impl::op_kind::Minimum,
                                    impl::op_kind::Divide,
                                    impl::op_kind::Subtract},
                            "pother_postop");
                    other_postop_graph->create_input_port(0, pop, 0);
                    other_postop_graph->create_input_port(1, pop, 1);
                    other_postop_graph->create_output_port(0, pop, 0);

                    pgraph->append_repetition(other_postop_graph, {0, 0}, 0,
                            MAX_REPETITION, in_edges_t {in_edge(0, popt, 0)},
                            "prepetition");
                })
        .set_attr<FCreateV2FusedOp>(
                "FCreateV2FusedOp", []() -> std::shared_ptr<op_t> {
                    std::shared_ptr<op_t> fused_op = std::make_shared<op_t>(
                            op_kind::matmul_post_ops_chain_fusion);
                    fused_op->set_attr<std::string>("backend", "dnnl");
                    return fused_op;
                });

DNNL_BACKEND_REGISTER_TRANSFORMATION_PASS(
        dnnl, matmul_bias_post_ops_chain_fusion)
        .set_priority(8.9f)
        .set_attr<FCreateV2Pattern>("FCreateV2Pattern",
                [](const std::shared_ptr<pb_graph_t> &pgraph) -> void {
                    pm::pb_op *pmatmul
                            = pgraph->append_op(impl::op_kind::MatMul);
                    pmatmul->append_decision_function(check_input_num<2>);
                    pm::pb_op *biasadd
                            = pgraph->append_op(impl::op_kind::BiasAdd,
                                    in_edges_t {in_edge(0, pmatmul, 0)});

                    // Optional BN
                    auto popt_graph
                            = std::make_shared<pb_graph_t>("poptional_bn");
                    auto pbn = popt_graph->append_op(
                            impl::op_kind::BatchNormInference, "pbn");
                    popt_graph->create_input_port(0, pbn, 0);
                    popt_graph->create_output_port(0, pbn, 0);
                    auto popt = pgraph->append_optional(
                            popt_graph, {in_edge(0, biasadd, 0)}, "popt");

                    // TODO(Yixin): special post op handle: swish is composed
                    // by sigmoid and multiply
                    auto other_postop_graph = std::make_shared<pb_graph_t>(
                            "pother_postop_graph");
                    pm::pb_op *pop = other_postop_graph->append_alternation(
                            {impl::op_kind::Abs, impl::op_kind::Clamp,
                                    impl::op_kind::Elu, impl::op_kind::GELU,
                                    impl::op_kind::HardTanh, impl::op_kind::Log,
                                    impl::op_kind::Sigmoid,
                                    impl::op_kind::SoftPlus, impl::op_kind::Pow,
                                    impl::op_kind::ReLU, impl::op_kind::Round,
                                    impl::op_kind::Sqrt, impl::op_kind::Square,
                                    impl::op_kind::Tanh, impl::op_kind::Add,
                                    impl::op_kind::Multiply,
                                    impl::op_kind::Maximum,
                                    impl::op_kind::Minimum,
                                    impl::op_kind::Divide,
                                    impl::op_kind::Subtract},
                            "pother_postop");
                    other_postop_graph->create_input_port(0, pop, 0);
                    other_postop_graph->create_input_port(1, pop, 1);
                    other_postop_graph->create_output_port(0, pop, 0);

                    pgraph->append_repetition(other_postop_graph, {0, 0}, 0,
                            MAX_REPETITION, in_edges_t {in_edge(0, popt, 0)},
                            "prepetition");
                })
        .set_attr<FCreateV2Pattern>("FCreateV2Pattern",
                [](const std::shared_ptr<pb_graph_t> &pgraph) -> void {
                    pm::pb_op *pmatmul
                            = pgraph->append_op(impl::op_kind::MatMul);
                    pmatmul->append_decision_function(check_input_num<3>);

                    // Optional BN
                    auto popt_graph
                            = std::make_shared<pb_graph_t>("poptional_bn");
                    auto pbn = popt_graph->append_op(
                            impl::op_kind::BatchNormInference, "pbn");
                    popt_graph->create_input_port(0, pbn, 0);
                    popt_graph->create_output_port(0, pbn, 0);
                    auto popt = pgraph->append_optional(
                            popt_graph, {in_edge(0, pmatmul, 0)}, "popt");

                    // TODO(Yixin): special post op handle: swish is composed
                    // by sigmoid and multiply
                    auto other_postop_graph = std::make_shared<pb_graph_t>(
                            "pother_postop_graph");
                    pm::pb_op *pop = other_postop_graph->append_alternation(
                            {impl::op_kind::Abs, impl::op_kind::Clamp,
                                    impl::op_kind::Elu, impl::op_kind::GELU,
                                    impl::op_kind::HardTanh, impl::op_kind::Log,
                                    impl::op_kind::Sigmoid,
                                    impl::op_kind::SoftPlus, impl::op_kind::Pow,
                                    impl::op_kind::ReLU, impl::op_kind::Round,
                                    impl::op_kind::Sqrt, impl::op_kind::Square,
                                    impl::op_kind::Tanh, impl::op_kind::Add,
                                    impl::op_kind::Multiply,
                                    impl::op_kind::Maximum,
                                    impl::op_kind::Minimum,
                                    impl::op_kind::Divide,
                                    impl::op_kind::Subtract},
                            "pother_postop");
                    other_postop_graph->create_input_port(0, pop, 0);
                    other_postop_graph->create_input_port(1, pop, 1);
                    other_postop_graph->create_output_port(0, pop, 0);

                    pgraph->append_repetition(other_postop_graph, {0, 0}, 0,
                            MAX_REPETITION, in_edges_t {in_edge(0, popt, 0)},
                            "prepetition");
                })
        .set_attr<FCreateV2FusedOp>(
                "FCreateV2FusedOp", []() -> std::shared_ptr<op_t> {
                    std::shared_ptr<op_t> fused_op = std::make_shared<op_t>(
                            op_kind::matmul_bias_post_ops_chain_fusion);
                    fused_op->set_attr<std::string>("backend", "dnnl");
                    return fused_op;
                });

DNNL_BACKEND_REGISTER_TRANSFORMATION_PASS(dnnl, int8_matmul_fusion)
        .set_priority(9.9f)
        .set_attr<FCreateV2Pattern>("FCreateV2Pattern",
                [](const std::shared_ptr<pb_graph_t> &pgraph) -> void {
                    pm::pb_op *dequant_data
                            = pgraph->append_op(impl::op_kind::Dequantize);
                    pm::pb_op *dequant_weight
                            = pgraph->append_op(impl::op_kind::Dequantize);
                    pm::pb_op *matmul = pgraph->append_op(impl::op_kind::MatMul,
                            in_edges_t {in_edge(0, dequant_data, 0),
                                    in_edge(1, dequant_weight, 0)});
                    matmul->append_decision_function(check_input_num<2>);

                    pgraph->append_op(impl::op_kind::Quantize,
                            in_edges_t {in_edge(0, matmul, 0)});
                })
        .set_attr<FCreateV2Pattern>("FCreateV2Pattern",
                [](const std::shared_ptr<pb_graph_t> &pgraph) -> void {
                    pm::pb_op *dequant_data
                            = pgraph->append_op(impl::op_kind::Dequantize);
                    pm::pb_op *dequant_weight
                            = pgraph->append_op(impl::op_kind::Dequantize);
                    // this pattern requires the weight should be s8
                    dequant_weight->append_decision_function(
                            check_input_dtype<impl::data_type::s8>);
                    pm::pb_op *typecast_data
                            = pgraph->append_op(impl::op_kind::TypeCast,
                                    in_edges_t {in_edge(0, dequant_data, 0)});
                    typecast_data->append_decision_function(
                            check_output_dtype<impl::data_type::bf16>);
                    pm::pb_op *typecast_weight
                            = pgraph->append_op(impl::op_kind::TypeCast,
                                    in_edges_t {in_edge(0, dequant_weight, 0)});
                    typecast_weight->append_decision_function(
                            check_output_dtype<impl::data_type::bf16>);
                    pm::pb_op *matmul = pgraph->append_op(impl::op_kind::MatMul,
                            in_edges_t {in_edge(0, typecast_data, 0),
                                    in_edge(1, typecast_weight, 0)});
                    matmul->append_decision_function(check_input_num<2>);

                    pm::pb_op *typecast_dst
                            = pgraph->append_op(impl::op_kind::TypeCast,
                                    in_edges_t {in_edge(0, matmul, 0)});
                    typecast_dst->append_decision_function(
                            check_input_dtype<impl::data_type::bf16>);
                    pgraph->append_op(impl::op_kind::Quantize,
                            in_edges_t {in_edge(0, typecast_dst, 0)});
                })
        .set_attr<FCreateV2FusedOp>(
                "FCreateV2FusedOp", []() -> std::shared_ptr<op_t> {
                    std::shared_ptr<op_t> fused_op
                            = std::make_shared<op_t>(op_kind::int8_matmul);
                    fused_op->set_attr<std::string>("backend", "dnnl");
                    return fused_op;
                });

DNNL_BACKEND_REGISTER_TRANSFORMATION_PASS(dnnl, int8_quant_wei_matmul_fusion)
        .set_priority(10.0f)
        .set_attr<FCreateV2Pattern>("FCreateV2Pattern",
                [](const std::shared_ptr<pb_graph_t> &pgraph) -> void {
                    pm::pb_op *dequant_data
                            = pgraph->append_op(impl::op_kind::Dequantize);
                    pm::pb_op *quant_weight
                            = pgraph->append_op(impl::op_kind::Quantize);
                    pm::pb_op *dequant_weight
                            = pgraph->append_op(impl::op_kind::Dequantize,
                                    in_edges_t {in_edge(0, quant_weight, 0)});
                    dequant_weight->append_decision_function(
                            check_input_dtype<impl::data_type::s8>);
                    pm::pb_op *matmul = pgraph->append_op(impl::op_kind::MatMul,
                            in_edges_t {in_edge(0, dequant_data, 0),
                                    in_edge(1, dequant_weight, 0)});
                    matmul->append_decision_function(check_input_num<2>);
                    pgraph->append_op(impl::op_kind::Quantize,
                            in_edges_t {in_edge(0, matmul, 0)});
                })
        .set_attr<FCreateV2FusedOp>(
                "FCreateV2FusedOp", []() -> std::shared_ptr<op_t> {
                    std::shared_ptr<op_t> fused_op = std::make_shared<op_t>(
                            op_kind::int8_quant_wei_matmul);
                    fused_op->set_attr<std::string>("backend", "dnnl");
                    return fused_op;
                });

DNNL_BACKEND_REGISTER_TRANSFORMATION_PASS(dnnl, int8_matmul_bias_fusion)
        .set_priority(10.5f)
        .set_attr<FCreateV2Pattern>("FCreateV2Pattern",
                [](const std::shared_ptr<pb_graph_t> &pgraph) -> void {
                    pm::pb_op *dequant_data
                            = pgraph->append_op(impl::op_kind::Dequantize);
                    pm::pb_op *dequant_weight
                            = pgraph->append_op(impl::op_kind::Dequantize);
                    dequant_weight->append_decision_function(
                            check_input_dtype<impl::data_type::s8>);

                    pm::pb_op *matmul = pgraph->append_op(impl::op_kind::MatMul,
                            in_edges_t {in_edge(0, dequant_data, 0),
                                    in_edge(1, dequant_weight, 0)});
                    matmul->append_decision_function(check_input_num<2>);

                    pm::pb_op *bias = pgraph->append_op(impl::op_kind::BiasAdd,
                            in_edges_t {in_edge(0, matmul, 0)});
                    pgraph->append_op(impl::op_kind::Quantize,
                            in_edges_t {in_edge(0, bias, 0)});
                })
        .set_attr<FCreateV2Pattern>("FCreateV2Pattern",
                [](const std::shared_ptr<pb_graph_t> &pgraph) -> void {
                    pm::pb_op *dequant_data
                            = pgraph->append_op(impl::op_kind::Dequantize);
                    pm::pb_op *dequant_weight
                            = pgraph->append_op(impl::op_kind::Dequantize);
                    dequant_weight->append_decision_function(
                            check_input_dtype<impl::data_type::s8>);

                    pm::pb_op *matmul = pgraph->append_op(impl::op_kind::MatMul,
                            in_edges_t {in_edge(0, dequant_data, 0),
                                    in_edge(1, dequant_weight, 0)});
                    matmul->append_decision_function(check_input_num<3>);

                    pgraph->append_op(impl::op_kind::Quantize,
                            in_edges_t {in_edge(0, matmul, 0)});
                })
        .set_attr<FCreateV2Pattern>("FCreateV2Pattern",
                [](const std::shared_ptr<pb_graph_t> &pgraph) -> void {
                    pm::pb_op *dequant_data
                            = pgraph->append_op(impl::op_kind::Dequantize);
                    pm::pb_op *dequant_weight
                            = pgraph->append_op(impl::op_kind::Dequantize);
                    // this pattern requires the weight should be s8
                    dequant_weight->append_decision_function(
                            check_input_dtype<impl::data_type::s8>);

                    pm::pb_op *typecast_data
                            = pgraph->append_op(impl::op_kind::TypeCast,
                                    in_edges_t {in_edge(0, dequant_data, 0)});
                    typecast_data->append_decision_function(
                            check_output_dtype<impl::data_type::bf16>);

                    pm::pb_op *typecast_weight
                            = pgraph->append_op(impl::op_kind::TypeCast,
                                    in_edges_t {in_edge(0, dequant_weight, 0)});
                    typecast_weight->append_decision_function(
                            check_output_dtype<impl::data_type::bf16>);
                    pm::pb_op *matmul = pgraph->append_op(impl::op_kind::MatMul,
                            in_edges_t {in_edge(0, typecast_data, 0),
                                    in_edge(1, typecast_weight, 0)});
                    matmul->append_decision_function(check_input_num<3>);

                    pm::pb_op *typecast_dst
                            = pgraph->append_op(impl::op_kind::TypeCast,
                                    in_edges_t {in_edge(0, matmul, 0)});
                    typecast_dst->append_decision_function(
                            check_input_dtype<impl::data_type::bf16>);
                    pgraph->append_op(impl::op_kind::Quantize,
                            in_edges_t {in_edge(0, typecast_dst, 0)});
                })
        .set_attr<FCreateV2FusedOp>(
                "FCreateV2FusedOp", []() -> std::shared_ptr<op_t> {
                    std::shared_ptr<op_t> fused_op
                            = std::make_shared<op_t>(op_kind::int8_matmul_bias);
                    fused_op->set_attr<std::string>("backend", "dnnl");
                    return fused_op;
                });

DNNL_BACKEND_REGISTER_TRANSFORMATION_PASS(
        dnnl, int8_quant_wei_matmul_bias_fusion)
        .set_priority(10.6f)
        .set_attr<FCreateV2Pattern>("FCreateV2Pattern",
                [](const std::shared_ptr<pb_graph_t> &pgraph) -> void {
                    pm::pb_op *dequant_data
                            = pgraph->append_op(impl::op_kind::Dequantize);
                    pm::pb_op *quant_weight
                            = pgraph->append_op(impl::op_kind::Quantize);
                    pm::pb_op *dequant_weight
                            = pgraph->append_op(impl::op_kind::Dequantize,
                                    in_edges_t {in_edge(0, quant_weight, 0)});
                    dequant_weight->append_decision_function(
                            check_input_dtype<impl::data_type::s8>);

                    pm::pb_op *matmul = pgraph->append_op(impl::op_kind::MatMul,
                            in_edges_t {in_edge(0, dequant_data, 0),
                                    in_edge(1, dequant_weight, 0)});
                    matmul->append_decision_function(check_input_num<3>);

                    pgraph->append_op(impl::op_kind::Quantize,
                            in_edges_t {in_edge(0, matmul, 0)});
                })
        .set_attr<FCreateV2Pattern>("FCreateV2Pattern",
                [](const std::shared_ptr<pb_graph_t> &pgraph) -> void {
                    pm::pb_op *dequant_data
                            = pgraph->append_op(impl::op_kind::Dequantize);
                    pm::pb_op *quant_weight
                            = pgraph->append_op(impl::op_kind::Quantize);
                    pm::pb_op *dequant_weight
                            = pgraph->append_op(impl::op_kind::Dequantize,
                                    in_edges_t {in_edge(0, quant_weight, 0)});
                    dequant_weight->append_decision_function(
                            check_input_dtype<impl::data_type::s8>);

                    pm::pb_op *matmul = pgraph->append_op(impl::op_kind::MatMul,
                            in_edges_t {in_edge(0, dequant_data, 0),
                                    in_edge(1, dequant_weight, 0)});
                    matmul->append_decision_function(check_input_num<2>);

                    pm::pb_op *bias = pgraph->append_op(impl::op_kind::BiasAdd,
                            in_edges_t {in_edge(0, matmul, 0)});
                    pgraph->append_op(impl::op_kind::Quantize,
                            in_edges_t {in_edge(0, bias, 0)});
                })
        .set_attr<FCreateV2FusedOp>(
                "FCreateV2FusedOp", []() -> std::shared_ptr<op_t> {
                    std::shared_ptr<op_t> fused_op = std::make_shared<op_t>(
                            op_kind::int8_quant_wei_matmul_bias);
                    fused_op->set_attr<std::string>("backend", "dnnl");
                    return fused_op;
                });

DNNL_BACKEND_REGISTER_TRANSFORMATION_PASS(dnnl, int8_matmul_relu_fusion)
        .set_priority(9.9f)
        .set_attr<FCreateV2Pattern>("FCreateV2Pattern",
                [](const std::shared_ptr<pb_graph_t> &pgraph) -> void {
                    pm::pb_op *dequant_data
                            = pgraph->append_op(impl::op_kind::Dequantize);
                    pm::pb_op *dequant_weight
                            = pgraph->append_op(impl::op_kind::Dequantize);
                    dequant_weight->append_decision_function(
                            check_input_dtype<impl::data_type::s8>);

                    pm::pb_op *matmul = pgraph->append_op(impl::op_kind::MatMul,
                            in_edges_t {in_edge(0, dequant_data, 0),
                                    in_edge(1, dequant_weight, 0)});
                    matmul->append_decision_function(check_input_num<2>);

                    pm::pb_op *relu = pgraph->append_op(impl::op_kind::ReLU,
                            in_edges_t {in_edge(0, matmul, 0)});

                    pgraph->append_op(impl::op_kind::Quantize,
                            in_edges_t {in_edge(0, relu, 0)});
                })
        .set_attr<FCreateV2FusedOp>(
                "FCreateV2FusedOp", []() -> std::shared_ptr<op_t> {
                    std::shared_ptr<op_t> fused_op
                            = std::make_shared<op_t>(op_kind::int8_matmul_relu);
                    fused_op->set_attr<std::string>("backend", "dnnl");
                    return fused_op;
                });

DNNL_BACKEND_REGISTER_TRANSFORMATION_PASS(
        dnnl, int8_quant_wei_matmul_relu_fusion)
        .set_priority(10.6f)
        .set_attr<FCreateV2Pattern>("FCreateV2Pattern",
                [](const std::shared_ptr<pb_graph_t> &pgraph) -> void {
                    pm::pb_op *dequant_data
                            = pgraph->append_op(impl::op_kind::Dequantize);
                    pm::pb_op *quant_weight
                            = pgraph->append_op(impl::op_kind::Quantize);
                    pm::pb_op *dequant_weight
                            = pgraph->append_op(impl::op_kind::Dequantize,
                                    in_edges_t {in_edge(0, quant_weight, 0)});
                    dequant_weight->append_decision_function(
                            check_input_dtype<impl::data_type::s8>);

                    pm::pb_op *matmul = pgraph->append_op(impl::op_kind::MatMul,
                            in_edges_t {in_edge(0, dequant_data, 0),
                                    in_edge(1, dequant_weight, 0)});
                    matmul->append_decision_function(check_input_num<2>);

                    pm::pb_op *relu = pgraph->append_op(impl::op_kind::ReLU,
                            in_edges_t {in_edge(0, matmul, 0)});
                    pgraph->append_op(impl::op_kind::Quantize,
                            in_edges_t {in_edge(0, relu, 0)});
                })
        .set_attr<FCreateV2FusedOp>(
                "FCreateV2FusedOp", []() -> std::shared_ptr<op_t> {
                    std::shared_ptr<op_t> fused_op = std::make_shared<op_t>(
                            op_kind::int8_quant_wei_matmul_relu);
                    fused_op->set_attr<std::string>("backend", "dnnl");
                    return fused_op;
                });

DNNL_BACKEND_REGISTER_TRANSFORMATION_PASS(dnnl, int8_matmul_bias_relu_fusion)
        .set_priority(9.9f)
        .set_attr<FCreateV2Pattern>("FCreateV2Pattern",
                [](const std::shared_ptr<pb_graph_t> &pgraph) -> void {
                    pm::pb_op *dequant_data
                            = pgraph->append_op(impl::op_kind::Dequantize);
                    pm::pb_op *dequant_weight
                            = pgraph->append_op(impl::op_kind::Dequantize);
                    dequant_weight->append_decision_function(
                            check_input_dtype<impl::data_type::s8>);

                    pm::pb_op *matmul = pgraph->append_op(impl::op_kind::MatMul,
                            in_edges_t {in_edge(0, dequant_data, 0),
                                    in_edge(1, dequant_weight, 0)});
                    matmul->append_decision_function(check_input_num<2>);

                    pm::pb_op *bias = pgraph->append_op(impl::op_kind::BiasAdd,
                            in_edges_t {in_edge(0, matmul, 0)});
                    pm::pb_op *relu = pgraph->append_op(impl::op_kind::ReLU,
                            in_edges_t {in_edge(0, bias, 0)});
                    pgraph->append_op(impl::op_kind::Quantize,
                            in_edges_t {in_edge(0, relu, 0)});
                })
        .set_attr<FCreateV2Pattern>("FCreateV2Pattern",
                [](const std::shared_ptr<pb_graph_t> &pgraph) -> void {
                    pm::pb_op *dequant_data
                            = pgraph->append_op(impl::op_kind::Dequantize);
                    pm::pb_op *dequant_weight
                            = pgraph->append_op(impl::op_kind::Dequantize);
                    dequant_weight->append_decision_function(
                            check_input_dtype<impl::data_type::s8>);

                    pm::pb_op *matmul = pgraph->append_op(impl::op_kind::MatMul,
                            in_edges_t {in_edge(0, dequant_data, 0),
                                    in_edge(1, dequant_weight, 0)});
                    matmul->append_decision_function(check_input_num<3>);

                    pm::pb_op *relu = pgraph->append_op(impl::op_kind::ReLU,
                            in_edges_t {in_edge(0, matmul, 0)});
                    pgraph->append_op(impl::op_kind::Quantize,
                            in_edges_t {in_edge(0, relu, 0)});
                })
        .set_attr<FCreateV2FusedOp>(
                "FCreateV2FusedOp", []() -> std::shared_ptr<op_t> {
                    std::shared_ptr<op_t> fused_op = std::make_shared<op_t>(
                            op_kind::int8_matmul_bias_relu);
                    fused_op->set_attr<std::string>("backend", "dnnl");
                    return fused_op;
                });

DNNL_BACKEND_REGISTER_TRANSFORMATION_PASS(
        dnnl, int8_quant_wei_matmul_bias_relu_fusion)
        .set_priority(10.6f)
        .set_attr<FCreateV2Pattern>("FCreateV2Pattern",
                [](const std::shared_ptr<pb_graph_t> &pgraph) -> void {
                    pm::pb_op *dequant_data
                            = pgraph->append_op(impl::op_kind::Dequantize);
                    pm::pb_op *quant_weight
                            = pgraph->append_op(impl::op_kind::Quantize);
                    pm::pb_op *dequant_weight
                            = pgraph->append_op(impl::op_kind::Dequantize,
                                    in_edges_t {in_edge(0, quant_weight, 0)});
                    dequant_weight->append_decision_function(
                            check_input_dtype<impl::data_type::s8>);

                    pm::pb_op *matmul = pgraph->append_op(impl::op_kind::MatMul,
                            in_edges_t {in_edge(0, dequant_data, 0),
                                    in_edge(1, dequant_weight, 0)});
                    matmul->append_decision_function(check_input_num<3>);

                    pm::pb_op *relu = pgraph->append_op(impl::op_kind::ReLU,
                            in_edges_t {in_edge(0, matmul, 0)});
                    pgraph->append_op(impl::op_kind::Quantize,
                            in_edges_t {in_edge(0, relu, 0)});
                })
        .set_attr<FCreateV2Pattern>("FCreateV2Pattern",
                [](const std::shared_ptr<pb_graph_t> &pgraph) -> void {
                    pm::pb_op *dequant_data
                            = pgraph->append_op(impl::op_kind::Dequantize);
                    pm::pb_op *quant_weight
                            = pgraph->append_op(impl::op_kind::Quantize);
                    pm::pb_op *dequant_weight
                            = pgraph->append_op(impl::op_kind::Dequantize,
                                    in_edges_t {in_edge(0, quant_weight, 0)});
                    dequant_weight->append_decision_function(
                            check_input_dtype<impl::data_type::s8>);

                    pm::pb_op *matmul = pgraph->append_op(impl::op_kind::MatMul,
                            in_edges_t {in_edge(0, dequant_data, 0),
                                    in_edge(1, dequant_weight, 0)});
                    matmul->append_decision_function(check_input_num<2>);

                    pm::pb_op *bias = pgraph->append_op(impl::op_kind::BiasAdd,
                            in_edges_t {in_edge(0, matmul, 0)});
                    pm::pb_op *relu = pgraph->append_op(impl::op_kind::ReLU,
                            in_edges_t {in_edge(0, bias, 0)});
                    pgraph->append_op(impl::op_kind::Quantize,
                            in_edges_t {in_edge(0, relu, 0)});
                })
        .set_attr<FCreateV2FusedOp>(
                "FCreateV2FusedOp", []() -> std::shared_ptr<op_t> {
                    std::shared_ptr<op_t> fused_op = std::make_shared<op_t>(
                            op_kind::int8_quant_wei_matmul_bias_relu);
                    fused_op->set_attr<std::string>("backend", "dnnl");
                    return fused_op;
                });

DNNL_BACKEND_REGISTER_TRANSFORMATION_PASS(dnnl, int8_matmul_sigmoid_fusion)
        .set_priority(9.9f)
        .set_attr<FCreateV2Pattern>("FCreateV2Pattern",
                [](const std::shared_ptr<pb_graph_t> &pgraph) -> void {
                    pm::pb_op *dequant_data
                            = pgraph->append_op(impl::op_kind::Dequantize);
                    pm::pb_op *dequant_weight
                            = pgraph->append_op(impl::op_kind::Dequantize);
                    dequant_weight->append_decision_function(
                            check_input_dtype<impl::data_type::s8>);

                    pm::pb_op *matmul = pgraph->append_op(impl::op_kind::MatMul,
                            in_edges_t {in_edge(0, dequant_data, 0),
                                    in_edge(1, dequant_weight, 0)});
                    matmul->append_decision_function(check_input_num<2>);

                    pm::pb_op *sigmoid
                            = pgraph->append_op(impl::op_kind::Sigmoid,
                                    in_edges_t {in_edge(0, matmul, 0)});
                    pgraph->append_op(impl::op_kind::Quantize,
                            in_edges_t {in_edge(0, sigmoid, 0)});
                })
        .set_attr<FCreateV2FusedOp>(
                "FCreateV2FusedOp", []() -> std::shared_ptr<op_t> {
                    std::shared_ptr<op_t> fused_op = std::make_shared<op_t>(
                            op_kind::int8_matmul_sigmoid);
                    fused_op->set_attr<std::string>("backend", "dnnl");
                    return fused_op;
                });

DNNL_BACKEND_REGISTER_TRANSFORMATION_PASS(
        dnnl, int8_quant_wei_matmul_sigmoid_fusion)
        .set_priority(10.6f)
        .set_attr<FCreateV2Pattern>("FCreateV2Pattern",
                [](const std::shared_ptr<pb_graph_t> &pgraph) -> void {
                    pm::pb_op *dequant_data
                            = pgraph->append_op(impl::op_kind::Dequantize);
                    pm::pb_op *quant_weight
                            = pgraph->append_op(impl::op_kind::Quantize);
                    pm::pb_op *dequant_weight
                            = pgraph->append_op(impl::op_kind::Dequantize,
                                    in_edges_t {in_edge(0, quant_weight, 0)});
                    dequant_weight->append_decision_function(
                            check_input_dtype<impl::data_type::s8>);

                    pm::pb_op *matmul = pgraph->append_op(impl::op_kind::MatMul,
                            in_edges_t {in_edge(0, dequant_data, 0),
                                    in_edge(1, dequant_weight, 0)});
                    matmul->append_decision_function(check_input_num<2>);
                    pm::pb_op *sigmoid
                            = pgraph->append_op(impl::op_kind::Sigmoid,
                                    in_edges_t {in_edge(0, matmul, 0)});
                    pgraph->append_op(impl::op_kind::Quantize,
                            in_edges_t {in_edge(0, sigmoid, 0)});
                })
        .set_attr<FCreateV2FusedOp>(
                "FCreateV2FusedOp", []() -> std::shared_ptr<op_t> {
                    std::shared_ptr<op_t> fused_op = std::make_shared<op_t>(
                            op_kind::int8_quant_wei_matmul_sigmoid);
                    fused_op->set_attr<std::string>("backend", "dnnl");
                    return fused_op;
                });

DNNL_BACKEND_REGISTER_TRANSFORMATION_PASS(dnnl, int8_matmul_bias_sigmoid_fusion)
        .set_priority(9.9f)
        .set_attr<FCreateV2Pattern>("FCreateV2Pattern",
                [](const std::shared_ptr<pb_graph_t> &pgraph) -> void {
                    pm::pb_op *dequant_data
                            = pgraph->append_op(impl::op_kind::Dequantize);
                    pm::pb_op *dequant_weight
                            = pgraph->append_op(impl::op_kind::Dequantize);
                    dequant_weight->append_decision_function(
                            check_input_dtype<impl::data_type::s8>);

                    pm::pb_op *matmul = pgraph->append_op(impl::op_kind::MatMul,
                            in_edges_t {in_edge(0, dequant_data, 0),
                                    in_edge(1, dequant_weight, 0)});
                    matmul->append_decision_function(check_input_num<2>);

                    pm::pb_op *bias = pgraph->append_op(impl::op_kind::BiasAdd,
                            in_edges_t {in_edge(0, matmul, 0)});
                    pm::pb_op *sigmoid
                            = pgraph->append_op(impl::op_kind::Sigmoid,
                                    in_edges_t {in_edge(0, bias, 0)});
                    pgraph->append_op(impl::op_kind::Quantize,
                            in_edges_t {in_edge(0, sigmoid, 0)});
                })
        .set_attr<FCreateV2Pattern>("FCreateV2Pattern",
                [](const std::shared_ptr<pb_graph_t> &pgraph) -> void {
                    pm::pb_op *dequant_data
                            = pgraph->append_op(impl::op_kind::Dequantize);
                    pm::pb_op *dequant_weight
                            = pgraph->append_op(impl::op_kind::Dequantize);
                    dequant_weight->append_decision_function(
                            check_input_dtype<impl::data_type::s8>);

                    pm::pb_op *matmul = pgraph->append_op(impl::op_kind::MatMul,
                            in_edges_t {in_edge(0, dequant_data, 0),
                                    in_edge(1, dequant_weight, 0)});
                    matmul->append_decision_function(check_input_num<3>);

                    pm::pb_op *sigmoid
                            = pgraph->append_op(impl::op_kind::Sigmoid,
                                    in_edges_t {in_edge(0, matmul, 0)});
                    pgraph->append_op(impl::op_kind::Quantize,
                            in_edges_t {in_edge(0, sigmoid, 0)});
                })
        .set_attr<FCreateV2FusedOp>(
                "FCreateV2FusedOp", []() -> std::shared_ptr<op_t> {
                    std::shared_ptr<op_t> fused_op = std::make_shared<op_t>(
                            op_kind::int8_matmul_bias_sigmoid);
                    fused_op->set_attr<std::string>("backend", "dnnl");
                    return fused_op;
                });

DNNL_BACKEND_REGISTER_TRANSFORMATION_PASS(
        dnnl, int8_quant_wei_matmul_bias_sigmoid_fusion)
        .set_priority(10.6f)
        .set_attr<FCreateV2Pattern>("FCreateV2Pattern",
                [](const std::shared_ptr<pb_graph_t> &pgraph) -> void {
                    pm::pb_op *dequant_data
                            = pgraph->append_op(impl::op_kind::Dequantize);
                    pm::pb_op *quant_weight
                            = pgraph->append_op(impl::op_kind::Quantize);
                    pm::pb_op *dequant_weight
                            = pgraph->append_op(impl::op_kind::Dequantize,
                                    in_edges_t {in_edge(0, quant_weight, 0)});
                    dequant_weight->append_decision_function(
                            check_input_dtype<impl::data_type::s8>);

                    pm::pb_op *matmul = pgraph->append_op(impl::op_kind::MatMul,
                            in_edges_t {in_edge(0, dequant_data, 0),
                                    in_edge(1, dequant_weight, 0)});
                    matmul->append_decision_function(check_input_num<3>);

                    pm::pb_op *sigmoid
                            = pgraph->append_op(impl::op_kind::Sigmoid,
                                    in_edges_t {in_edge(0, matmul, 0)});
                    pgraph->append_op(impl::op_kind::Quantize,
                            in_edges_t {in_edge(0, sigmoid, 0)});
                })
        .set_attr<FCreateV2Pattern>("FCreateV2Pattern",
                [](const std::shared_ptr<pb_graph_t> &pgraph) -> void {
                    pm::pb_op *dequant_data
                            = pgraph->append_op(impl::op_kind::Dequantize);
                    pm::pb_op *quant_weight
                            = pgraph->append_op(impl::op_kind::Quantize);
                    pm::pb_op *dequant_weight
                            = pgraph->append_op(impl::op_kind::Dequantize,
                                    in_edges_t {in_edge(0, quant_weight, 0)});
                    dequant_weight->append_decision_function(
                            check_input_dtype<impl::data_type::s8>);

                    pm::pb_op *matmul = pgraph->append_op(impl::op_kind::MatMul,
                            in_edges_t {in_edge(0, dequant_data, 0),
                                    in_edge(1, dequant_weight, 0)});
                    matmul->append_decision_function(check_input_num<2>);

                    pm::pb_op *bias = pgraph->append_op(impl::op_kind::BiasAdd,
                            in_edges_t {in_edge(0, matmul, 0)});
                    pm::pb_op *sigmoid
                            = pgraph->append_op(impl::op_kind::Sigmoid,
                                    in_edges_t {in_edge(0, bias, 0)});
                    pgraph->append_op(impl::op_kind::Quantize,
                            in_edges_t {in_edge(0, sigmoid, 0)});
                })
        .set_attr<FCreateV2FusedOp>(
                "FCreateV2FusedOp", []() -> std::shared_ptr<op_t> {
                    std::shared_ptr<op_t> fused_op = std::make_shared<op_t>(
                            op_kind::int8_quant_wei_matmul_bias_sigmoid);
                    fused_op->set_attr<std::string>("backend", "dnnl");
                    return fused_op;
                });

DNNL_BACKEND_REGISTER_TRANSFORMATION_PASS(dnnl, int8_matmul_gelu_fusion)
        .set_priority(9.9f)
        .set_attr<FCreateV2Pattern>("FCreateV2Pattern",
                [](const std::shared_ptr<pb_graph_t> &pgraph) -> void {
                    pm::pb_op *dequant_data
                            = pgraph->append_op(impl::op_kind::Dequantize);

                    pm::pb_op *dequant_weight
                            = pgraph->append_op(impl::op_kind::Dequantize);

                    pm::pb_op *matmul = pgraph->append_op(impl::op_kind::MatMul,
                            in_edges_t {in_edge(0, dequant_data, 0),
                                    in_edge(1, dequant_weight, 0)});
                    matmul->append_decision_function(check_input_num<2>);

                    pm::pb_op *gelu = pgraph->append_op(impl::op_kind::GELU,
                            in_edges_t {in_edge(0, matmul, 0)});

                    pgraph->append_op(impl::op_kind::Quantize,
                            in_edges_t {in_edge(0, gelu, 0)});
                })
        .set_attr<FCreateV2Pattern>("FCreateV2Pattern",
                [](const std::shared_ptr<pb_graph_t> &pgraph) -> void {
                    pm::pb_op *dequant_data
                            = pgraph->append_op(impl::op_kind::Dequantize);
                    pm::pb_op *dequant_weight
                            = pgraph->append_op(impl::op_kind::Dequantize);
                    // this pattern requires the weight should be s8
                    dequant_weight->append_decision_function(
                            check_input_dtype<impl::data_type::s8>);

                    pm::pb_op *typecast_data
                            = pgraph->append_op(impl::op_kind::TypeCast,
                                    in_edges_t {in_edge(0, dequant_data, 0)});
                    typecast_data->append_decision_function(
                            check_output_dtype<impl::data_type::bf16>);

                    pm::pb_op *typecast_weight
                            = pgraph->append_op(impl::op_kind::TypeCast,
                                    in_edges_t {in_edge(0, dequant_weight, 0)});
                    typecast_weight->append_decision_function(
                            check_output_dtype<impl::data_type::bf16>);
                    pm::pb_op *matmul = pgraph->append_op(impl::op_kind::MatMul,
                            in_edges_t {in_edge(0, typecast_data, 0),
                                    in_edge(1, typecast_weight, 0)});
                    matmul->append_decision_function(check_input_num<2>);

                    pm::pb_op *gelu = pgraph->append_op(impl::op_kind::GELU,
                            in_edges_t {in_edge(0, matmul, 0)});
                    pm::pb_op *typecast_gelu
                            = pgraph->append_op(impl::op_kind::TypeCast,
                                    in_edges_t {in_edge(0, gelu, 0)});
                    typecast_gelu->append_decision_function(
                            check_input_dtype<impl::data_type::bf16>);
                    pgraph->append_op(impl::op_kind::Quantize,
                            in_edges_t {in_edge(0, typecast_gelu, 0)});
                })
        .set_attr<FCreateV2FusedOp>(
                "FCreateV2FusedOp", []() -> std::shared_ptr<op_t> {
                    std::shared_ptr<op_t> fused_op
                            = std::make_shared<op_t>(op_kind::int8_matmul_gelu);
                    fused_op->set_attr<std::string>("backend", "dnnl");
                    return fused_op;
                });

DNNL_BACKEND_REGISTER_TRANSFORMATION_PASS(
        dnnl, int8_quant_wei_matmul_gelu_fusion)
        .set_priority(10.6f)
        .set_attr<FCreateV2Pattern>("FCreateV2Pattern",
                [](const std::shared_ptr<pb_graph_t> &pgraph) -> void {
                    pm::pb_op *dequant_data
                            = pgraph->append_op(impl::op_kind::Dequantize);
                    pm::pb_op *quant_weight
                            = pgraph->append_op(impl::op_kind::Quantize);
                    pm::pb_op *dequant_weight
                            = pgraph->append_op(impl::op_kind::Dequantize,
                                    in_edges_t {in_edge(0, quant_weight, 0)});
                    dequant_weight->append_decision_function(
                            check_input_dtype<impl::data_type::s8>);
                    pm::pb_op *matmul = pgraph->append_op(impl::op_kind::MatMul,
                            in_edges_t {in_edge(0, dequant_data, 0),
                                    in_edge(1, dequant_weight, 0)});
                    matmul->append_decision_function(check_input_num<2>);
                    pm::pb_op *gelu = pgraph->append_op(impl::op_kind::GELU,
                            in_edges_t {in_edge(0, matmul, 0)});
                    pgraph->append_op(impl::op_kind::Quantize,
                            in_edges_t {in_edge(0, gelu, 0)});
                })
        .set_attr<FCreateV2FusedOp>(
                "FCreateV2FusedOp", []() -> std::shared_ptr<op_t> {
                    std::shared_ptr<op_t> fused_op = std::make_shared<op_t>(
                            op_kind::int8_quant_wei_matmul_gelu);
                    fused_op->set_attr<std::string>("backend", "dnnl");
                    return fused_op;
                });

DNNL_BACKEND_REGISTER_TRANSFORMATION_PASS(dnnl, int8_matmul_bias_gelu_fusion)
        .set_priority(10.5f)
        .set_attr<FCreateV2Pattern>("FCreateV2Pattern",
                [](const std::shared_ptr<pb_graph_t> &pgraph) -> void {
                    pm::pb_op *dequant_data
                            = pgraph->append_op(impl::op_kind::Dequantize);
                    pm::pb_op *dequant_weight
                            = pgraph->append_op(impl::op_kind::Dequantize);
                    dequant_weight->append_decision_function(
                            check_input_dtype<impl::data_type::s8>);
                    pm::pb_op *matmul = pgraph->append_op(impl::op_kind::MatMul,
                            in_edges_t {in_edge(0, dequant_data, 0),
                                    in_edge(1, dequant_weight, 0)});
                    matmul->append_decision_function(check_input_num<2>);

                    pm::pb_op *bias = pgraph->append_op(impl::op_kind::BiasAdd,
                            in_edges_t {in_edge(0, matmul, 0)});
                    pm::pb_op *gelu = pgraph->append_op(impl::op_kind::GELU,
                            in_edges_t {in_edge(0, bias, 0)});
                    pgraph->append_op(impl::op_kind::Quantize,
                            in_edges_t {in_edge(0, gelu, 0)});
                })
        .set_attr<FCreateV2Pattern>("FCreateV2Pattern",
                [](const std::shared_ptr<pb_graph_t> &pgraph) -> void {
                    pm::pb_op *dequant_data
                            = pgraph->append_op(impl::op_kind::Dequantize);
                    pm::pb_op *dequant_weight
                            = pgraph->append_op(impl::op_kind::Dequantize);
                    dequant_weight->append_decision_function(
                            check_input_dtype<impl::data_type::s8>);

                    pm::pb_op *matmul = pgraph->append_op(impl::op_kind::MatMul,
                            in_edges_t {in_edge(0, dequant_data, 0),
                                    in_edge(1, dequant_weight, 0)});
                    matmul->append_decision_function(check_input_num<3>);

                    pm::pb_op *gelu = pgraph->append_op(impl::op_kind::GELU,
                            in_edges_t {in_edge(0, matmul, 0)});
                    pgraph->append_op(impl::op_kind::Quantize,
                            in_edges_t {in_edge(0, gelu, 0)});
                })
        .set_attr<FCreateV2Pattern>("FCreateV2Pattern",
                [](const std::shared_ptr<pb_graph_t> &pgraph) -> void {
                    pm::pb_op *dequant_data
                            = pgraph->append_op(impl::op_kind::Dequantize);
                    pm::pb_op *dequant_weight
                            = pgraph->append_op(impl::op_kind::Dequantize);
                    // this pattern requires the weight should be s8
                    dequant_weight->append_decision_function(
                            check_input_dtype<impl::data_type::s8>);

                    pm::pb_op *typecast_data
                            = pgraph->append_op(impl::op_kind::TypeCast,
                                    in_edges_t {in_edge(0, dequant_data, 0)});
                    typecast_data->append_decision_function(
                            check_output_dtype<impl::data_type::bf16>);

                    pm::pb_op *typecast_weight
                            = pgraph->append_op(impl::op_kind::TypeCast,
                                    in_edges_t {in_edge(0, dequant_weight, 0)});
                    typecast_weight->append_decision_function(
                            check_output_dtype<impl::data_type::bf16>);
                    pm::pb_op *matmul = pgraph->append_op(impl::op_kind::MatMul,
                            in_edges_t {in_edge(0, typecast_data, 0),
                                    in_edge(1, typecast_weight, 0)});
                    matmul->append_decision_function(check_input_num<3>);

                    pm::pb_op *gelu = pgraph->append_op(impl::op_kind::GELU,
                            in_edges_t {in_edge(0, matmul, 0)});
                    pm::pb_op *typecast_gelu
                            = pgraph->append_op(impl::op_kind::TypeCast,
                                    in_edges_t {in_edge(0, gelu, 0)});
                    typecast_gelu->append_decision_function(
                            check_input_dtype<impl::data_type::bf16>);
                    pgraph->append_op(impl::op_kind::Quantize,
                            in_edges_t {in_edge(0, typecast_gelu, 0)});
                })
        .set_attr<FCreateV2FusedOp>(
                "FCreateV2FusedOp", []() -> std::shared_ptr<op_t> {
                    std::shared_ptr<op_t> fused_op = std::make_shared<op_t>(
                            op_kind::int8_matmul_bias_gelu);
                    fused_op->set_attr<std::string>("backend", "dnnl");
                    return fused_op;
                });

DNNL_BACKEND_REGISTER_TRANSFORMATION_PASS(
        dnnl, int8_quant_wei_matmul_bias_gelu_fusion)
        .set_priority(10.6f)
        .set_attr<FCreateV2Pattern>("FCreateV2Pattern",
                [](const std::shared_ptr<pb_graph_t> &pgraph) -> void {
                    pm::pb_op *dequant_data
                            = pgraph->append_op(impl::op_kind::Dequantize);
                    pm::pb_op *quant_weight
                            = pgraph->append_op(impl::op_kind::Quantize);
                    pm::pb_op *dequant_weight
                            = pgraph->append_op(impl::op_kind::Dequantize,
                                    in_edges_t {in_edge(0, quant_weight, 0)});
                    dequant_weight->append_decision_function(
                            check_input_dtype<impl::data_type::s8>);

                    pm::pb_op *matmul = pgraph->append_op(impl::op_kind::MatMul,
                            in_edges_t {in_edge(0, dequant_data, 0),
                                    in_edge(1, dequant_weight, 0)});
                    matmul->append_decision_function(check_input_num<2>);

                    pm::pb_op *bias = pgraph->append_op(impl::op_kind::BiasAdd,
                            in_edges_t {in_edge(0, matmul, 0)});
                    pm::pb_op *gelu = pgraph->append_op(impl::op_kind::GELU,
                            in_edges_t {in_edge(0, bias, 0)});
                    pgraph->append_op(impl::op_kind::Quantize,
                            in_edges_t {in_edge(0, gelu, 0)});
                })
        .set_attr<FCreateV2Pattern>("FCreateV2Pattern",
                [](const std::shared_ptr<pb_graph_t> &pgraph) -> void {
                    pm::pb_op *dequant_data
                            = pgraph->append_op(impl::op_kind::Dequantize);
                    pm::pb_op *quant_weight
                            = pgraph->append_op(impl::op_kind::Quantize);
                    pm::pb_op *dequant_weight
                            = pgraph->append_op(impl::op_kind::Dequantize,
                                    in_edges_t {in_edge(0, quant_weight, 0)});
                    dequant_weight->append_decision_function(
                            check_input_dtype<impl::data_type::s8>);

                    pm::pb_op *matmul = pgraph->append_op(impl::op_kind::MatMul,
                            in_edges_t {in_edge(0, dequant_data, 0),
                                    in_edge(1, dequant_weight, 0)});
                    matmul->append_decision_function(check_input_num<3>);

                    pm::pb_op *gelu = pgraph->append_op(impl::op_kind::GELU,
                            in_edges_t {in_edge(0, matmul, 0)});
                    pgraph->append_op(impl::op_kind::Quantize,
                            in_edges_t {in_edge(0, gelu, 0)});
                })
        .set_attr<FCreateV2FusedOp>(
                "FCreateV2FusedOp", []() -> std::shared_ptr<op_t> {
                    std::shared_ptr<op_t> fused_op = std::make_shared<op_t>(
                            op_kind::int8_quant_wei_matmul_bias_gelu);
                    fused_op->set_attr<std::string>("backend", "dnnl");
                    return fused_op;
                });

DNNL_BACKEND_REGISTER_TRANSFORMATION_PASS(dnnl, x8x8f32_matmul_fusion)
        .set_priority(9.5f)
        .set_attr<FCreateV2Pattern>("FCreateV2Pattern",
                [](const std::shared_ptr<pb_graph_t> &pgraph) -> void {
                    pm::pb_op *dequant_data
                            = pgraph->append_op(impl::op_kind::Dequantize);
                    pm::pb_op *dequant_weight
                            = pgraph->append_op(impl::op_kind::Dequantize);
                    pm::pb_op *matmul = pgraph->append_op(impl::op_kind::MatMul,
                            in_edges_t {in_edge(0, dequant_data, 0),
                                    in_edge(1, dequant_weight, 0)});
                    matmul->append_decision_function(check_input_num<2>);
                })
        .set_attr<FCreateV2FusedOp>(
                "FCreateV2FusedOp", []() -> std::shared_ptr<op_t> {
                    std::shared_ptr<op_t> fused_op
                            = std::make_shared<op_t>(op_kind::x8x8float_matmul);
                    fused_op->set_attr<std::string>("backend", "dnnl");
                    return fused_op;
                });

DNNL_BACKEND_REGISTER_TRANSFORMATION_PASS(dnnl, x8s8f32_matmul_bias_fusion)
        .set_priority(9.6f)
        .set_attr<FCreateV2Pattern>("FCreateV2Pattern",
                [](const std::shared_ptr<pb_graph_t> &pgraph) -> void {
                    pm::pb_op *dequant_data
                            = pgraph->append_op(impl::op_kind::Dequantize);
                    pm::pb_op *dequant_weight
                            = pgraph->append_op(impl::op_kind::Dequantize);
                    // this pattern requires the weight should be s8
                    dequant_weight->append_decision_function(
                            check_input_dtype<impl::data_type::s8>);

                    pm::pb_op *matmul = pgraph->append_op(impl::op_kind::MatMul,
                            in_edges_t {in_edge(0, dequant_data, 0),
                                    in_edge(1, dequant_weight, 0)});
                    matmul->append_decision_function(check_input_num<3>);
                })
        .set_attr<FCreateV2Pattern>("FCreateV2Pattern",
                [](const std::shared_ptr<pb_graph_t> &pgraph) -> void {
                    pm::pb_op *dequant_data
                            = pgraph->append_op(impl::op_kind::Dequantize);
                    pm::pb_op *dequant_weight
                            = pgraph->append_op(impl::op_kind::Dequantize);
                    // this pattern requires the weight should be s8
                    dequant_weight->append_decision_function(
                            check_input_dtype<impl::data_type::s8>);

                    pm::pb_op *matmul = pgraph->append_op(impl::op_kind::MatMul,
                            in_edges_t {in_edge(0, dequant_data, 0),
                                    in_edge(1, dequant_weight, 0)});
                    matmul->append_decision_function(check_input_num<2>);

                    pgraph->append_op(impl::op_kind::BiasAdd,
                            in_edges_t {in_edge(0, matmul, 0)});
                })
        .set_attr<FCreateV2FusedOp>(
                "FCreateV2FusedOp", []() -> std::shared_ptr<op_t> {
                    std::shared_ptr<op_t> fused_op = std::make_shared<op_t>(
                            op_kind::x8s8float_matmul_bias);
                    fused_op->set_attr<std::string>("backend", "dnnl");
                    return fused_op;
                });

DNNL_BACKEND_REGISTER_TRANSFORMATION_PASS(dnnl, x8s8f32_matmul_relu_fusion)
        .set_priority(9.6f)
        .set_attr<FCreateV2Pattern>("FCreateV2Pattern",
                [](const std::shared_ptr<pb_graph_t> &pgraph) -> void {
                    pm::pb_op *dequant_data
                            = pgraph->append_op(impl::op_kind::Dequantize);
                    pm::pb_op *dequant_weight
                            = pgraph->append_op(impl::op_kind::Dequantize);
                    // this pattern requires the weight should be s8
                    dequant_weight->append_decision_function(
                            check_input_dtype<impl::data_type::s8>);

                    pm::pb_op *matmul = pgraph->append_op(impl::op_kind::MatMul,
                            in_edges_t {in_edge(0, dequant_data, 0),
                                    in_edge(1, dequant_weight, 0)});
                    matmul->append_decision_function(check_input_num<2>);
                    pgraph->append_op(impl::op_kind::ReLU,
                            in_edges_t {in_edge(0, matmul, 0)});
                })
        .set_attr<FCreateV2FusedOp>(
                "FCreateV2FusedOp", []() -> std::shared_ptr<op_t> {
                    std::shared_ptr<op_t> fused_op = std::make_shared<op_t>(
                            op_kind::x8s8f32_matmul_relu);
                    fused_op->set_attr<std::string>("backend", "dnnl");
                    return fused_op;
                });

DNNL_BACKEND_REGISTER_TRANSFORMATION_PASS(dnnl, x8s8f32_matmul_bias_relu_fusion)
        .set_priority(9.7f)
        .set_attr<FCreateV2Pattern>("FCreateV2Pattern",
                [](const std::shared_ptr<pb_graph_t> &pgraph) -> void {
                    pm::pb_op *dequant_data
                            = pgraph->append_op(impl::op_kind::Dequantize);
                    pm::pb_op *dequant_weight
                            = pgraph->append_op(impl::op_kind::Dequantize);
                    // this pattern requires the weight should be s8
                    dequant_weight->append_decision_function(
                            check_input_dtype<impl::data_type::s8>);

                    pm::pb_op *matmul = pgraph->append_op(impl::op_kind::MatMul,
                            in_edges_t {in_edge(0, dequant_data, 0),
                                    in_edge(1, dequant_weight, 0)});
                    matmul->append_decision_function(check_input_num<3>);
                    pgraph->append_op(impl::op_kind::ReLU,
                            in_edges_t {in_edge(0, matmul, 0)});
                })
        .set_attr<FCreateV2Pattern>("FCreateV2Pattern",
                [](const std::shared_ptr<pb_graph_t> &pgraph) -> void {
                    pm::pb_op *dequant_data
                            = pgraph->append_op(impl::op_kind::Dequantize);
                    pm::pb_op *dequant_weight
                            = pgraph->append_op(impl::op_kind::Dequantize);
                    // this pattern requires the weight should be s8
                    dequant_weight->append_decision_function(
                            check_input_dtype<impl::data_type::s8>);

                    pm::pb_op *matmul = pgraph->append_op(impl::op_kind::MatMul,
                            in_edges_t {in_edge(0, dequant_data, 0),
                                    in_edge(1, dequant_weight, 0)});
                    matmul->append_decision_function(check_input_num<2>);

                    pm::pb_op *bias = pgraph->append_op(impl::op_kind::BiasAdd,
                            in_edges_t {in_edge(0, matmul, 0)});
                    pgraph->append_op(impl::op_kind::ReLU,
                            in_edges_t {in_edge(0, bias, 0)});
                })
        .set_attr<FCreateV2FusedOp>(
                "FCreateV2FusedOp", []() -> std::shared_ptr<op_t> {
                    std::shared_ptr<op_t> fused_op = std::make_shared<op_t>(
                            op_kind::x8s8f32_matmul_bias_relu);
                    fused_op->set_attr<std::string>("backend", "dnnl");
                    return fused_op;
                });

DNNL_BACKEND_REGISTER_TRANSFORMATION_PASS(dnnl, x8s8f32_matmul_sigmoid_fusion)
        .set_priority(9.6f)
        .set_attr<FCreateV2Pattern>("FCreateV2Pattern",
                [](const std::shared_ptr<pb_graph_t> &pgraph) -> void {
                    pm::pb_op *dequant_data
                            = pgraph->append_op(impl::op_kind::Dequantize);
                    pm::pb_op *dequant_weight
                            = pgraph->append_op(impl::op_kind::Dequantize);
                    // this pattern requires the weight should be s8
                    dequant_weight->append_decision_function(
                            check_input_dtype<impl::data_type::s8>);
                    pm::pb_op *matmul = pgraph->append_op(impl::op_kind::MatMul,
                            in_edges_t {in_edge(0, dequant_data, 0),
                                    in_edge(1, dequant_weight, 0)});
                    matmul->append_decision_function(check_input_num<2>);
                    pgraph->append_op(impl::op_kind::Sigmoid,
                            in_edges_t {in_edge(0, matmul, 0)});
                })
        .set_attr<FCreateV2FusedOp>(
                "FCreateV2FusedOp", []() -> std::shared_ptr<op_t> {
                    std::shared_ptr<op_t> fused_op = std::make_shared<op_t>(
                            op_kind::x8s8f32_matmul_sigmoid);
                    fused_op->set_attr<std::string>("backend", "dnnl");
                    return fused_op;
                });

DNNL_BACKEND_REGISTER_TRANSFORMATION_PASS(
        dnnl, x8s8f32_matmul_bias_sigmoid_fusion)
        .set_priority(9.7f)
        .set_attr<FCreateV2Pattern>("FCreateV2Pattern",
                [](const std::shared_ptr<pb_graph_t> &pgraph) -> void {
                    pm::pb_op *dequant_data
                            = pgraph->append_op(impl::op_kind::Dequantize);
                    pm::pb_op *dequant_weight
                            = pgraph->append_op(impl::op_kind::Dequantize);
                    // this pattern requires the weight should be s8
                    dequant_weight->append_decision_function(
                            check_input_dtype<impl::data_type::s8>);

                    pm::pb_op *matmul = pgraph->append_op(impl::op_kind::MatMul,
                            in_edges_t {in_edge(0, dequant_data, 0),
                                    in_edge(1, dequant_weight, 0)});
                    matmul->append_decision_function(check_input_num<3>);

                    pgraph->append_op(impl::op_kind::Sigmoid,
                            in_edges_t {in_edge(0, matmul, 0)});
                })
        .set_attr<FCreateV2Pattern>("FCreateV2Pattern",
                [](const std::shared_ptr<pb_graph_t> &pgraph) -> void {
                    pm::pb_op *dequant_data
                            = pgraph->append_op(impl::op_kind::Dequantize);
                    pm::pb_op *dequant_weight
                            = pgraph->append_op(impl::op_kind::Dequantize);
                    // this pattern requires the weight should be s8
                    dequant_weight->append_decision_function(
                            check_input_dtype<impl::data_type::s8>);

                    pm::pb_op *matmul = pgraph->append_op(impl::op_kind::MatMul,
                            in_edges_t {in_edge(0, dequant_data, 0),
                                    in_edge(1, dequant_weight, 0)});
                    matmul->append_decision_function(check_input_num<2>);

                    pm::pb_op *bias = pgraph->append_op(impl::op_kind::BiasAdd,
                            in_edges_t {in_edge(0, matmul, 0)});
                    pgraph->append_op(impl::op_kind::Sigmoid,
                            in_edges_t {in_edge(0, bias, 0)});
                })
        .set_attr<FCreateV2FusedOp>(
                "FCreateV2FusedOp", []() -> std::shared_ptr<op_t> {
                    std::shared_ptr<op_t> fused_op = std::make_shared<op_t>(
                            op_kind::x8s8f32_matmul_bias_sigmoid);
                    fused_op->set_attr<std::string>("backend", "dnnl");
                    return fused_op;
                });

DNNL_BACKEND_REGISTER_TRANSFORMATION_PASS(dnnl, x8s8f32_matmul_gelu_fusion)
        .set_priority(9.6f)
        .set_attr<FCreateV2Pattern>("FCreateV2Pattern",
                [](const std::shared_ptr<pb_graph_t> &pgraph) -> void {
                    pm::pb_op *dequant_data
                            = pgraph->append_op(impl::op_kind::Dequantize);
                    pm::pb_op *dequant_weight
                            = pgraph->append_op(impl::op_kind::Dequantize);
                    // this pattern requires the weight should be s8
                    dequant_weight->append_decision_function(
                            check_input_dtype<impl::data_type::s8>);

                    pm::pb_op *matmul = pgraph->append_op(impl::op_kind::MatMul,
                            in_edges_t {in_edge(0, dequant_data, 0),
                                    in_edge(1, dequant_weight, 0)});
                    matmul->append_decision_function(check_input_num<2>);

                    pgraph->append_op(impl::op_kind::GELU,
                            in_edges_t {in_edge(0, matmul, 0)});
                })
        .set_attr<FCreateV2FusedOp>(
                "FCreateV2FusedOp", []() -> std::shared_ptr<op_t> {
                    std::shared_ptr<op_t> fused_op = std::make_shared<op_t>(
                            op_kind::x8s8f32_matmul_gelu);
                    fused_op->set_attr<std::string>("backend", "dnnl");
                    return fused_op;
                });

DNNL_BACKEND_REGISTER_TRANSFORMATION_PASS(dnnl, x8s8f32_matmul_bias_gelu_fusion)
        .set_priority(9.7f)
        .set_attr<FCreateV2Pattern>("FCreateV2Pattern",
                [](const std::shared_ptr<pb_graph_t> &pgraph) -> void {
                    pm::pb_op *dequant_data
                            = pgraph->append_op(impl::op_kind::Dequantize);
                    pm::pb_op *dequant_weight
                            = pgraph->append_op(impl::op_kind::Dequantize);
                    // this pattern requires the weight should be s8
                    dequant_weight->append_decision_function(
                            check_input_dtype<impl::data_type::s8>);

                    pm::pb_op *matmul = pgraph->append_op(impl::op_kind::MatMul,
                            in_edges_t {in_edge(0, dequant_data, 0),
                                    in_edge(1, dequant_weight, 0)});
                    matmul->append_decision_function(check_input_num<3>);

                    pgraph->append_op(impl::op_kind::GELU,
                            in_edges_t {in_edge(0, matmul, 0)});
                })
        .set_attr<FCreateV2Pattern>("FCreateV2Pattern",
                [](const std::shared_ptr<pb_graph_t> &pgraph) -> void {
                    pm::pb_op *dequant_data
                            = pgraph->append_op(impl::op_kind::Dequantize);
                    pm::pb_op *dequant_weight
                            = pgraph->append_op(impl::op_kind::Dequantize);
                    // this pattern requires the weight should be s8
                    dequant_weight->append_decision_function(
                            check_input_dtype<impl::data_type::s8>);

                    pm::pb_op *matmul = pgraph->append_op(impl::op_kind::MatMul,
                            in_edges_t {in_edge(0, dequant_data, 0),
                                    in_edge(1, dequant_weight, 0)});
                    matmul->append_decision_function(check_input_num<2>);

                    pm::pb_op *bias = pgraph->append_op(impl::op_kind::BiasAdd,
                            in_edges_t {in_edge(0, matmul, 0)});
                    pgraph->append_op(impl::op_kind::GELU,
                            in_edges_t {in_edge(0, bias, 0)});
                })
        .set_attr<FCreateV2FusedOp>(
                "FCreateV2FusedOp", []() -> std::shared_ptr<op_t> {
                    std::shared_ptr<op_t> fused_op = std::make_shared<op_t>(
                            op_kind::x8s8f32_matmul_bias_gelu);
                    fused_op->set_attr<std::string>("backend", "dnnl");
                    return fused_op;
                });

DNNL_BACKEND_REGISTER_TRANSFORMATION_PASS(dnnl, x8s8f32_quant_wei_matmul_fusion)
        .set_priority(9.6f)
        .set_attr<FCreateV2Pattern>("FCreateV2Pattern",
                [](const std::shared_ptr<pb_graph_t> &pgraph) -> void {
                    pm::pb_op *dequant_data
                            = pgraph->append_op(impl::op_kind::Dequantize);
                    pm::pb_op *quant_weight
                            = pgraph->append_op(impl::op_kind::Quantize);
                    pm::pb_op *dequant_weight
                            = pgraph->append_op(impl::op_kind::Dequantize,
                                    in_edges_t {in_edge(0, quant_weight, 0)});
                    dequant_weight->append_decision_function(
                            check_input_dtype<impl::data_type::s8>);

                    pm::pb_op *matmul = pgraph->append_op(impl::op_kind::MatMul,
                            in_edges_t {in_edge(0, dequant_data, 0),
                                    in_edge(1, dequant_weight, 0)});
                    matmul->append_decision_function(check_input_num<2>);
                })
        .set_attr<FCreateV2FusedOp>(
                "FCreateV2FusedOp", []() -> std::shared_ptr<op_t> {
                    std::shared_ptr<op_t> fused_op = std::make_shared<op_t>(
                            op_kind::x8s8f32_quant_wei_matmul);
                    fused_op->set_attr<std::string>("backend", "dnnl");
                    return fused_op;
                });

DNNL_BACKEND_REGISTER_TRANSFORMATION_PASS(
        dnnl, x8s8f32_quant_wei_matmul_bias_fusion)
        .set_priority(9.7f)
        .set_attr<FCreateV2Pattern>("FCreateV2Pattern",
                [](const std::shared_ptr<pb_graph_t> &pgraph) -> void {
                    pm::pb_op *dequant_data
                            = pgraph->append_op(impl::op_kind::Dequantize);
                    pm::pb_op *quant_weight
                            = pgraph->append_op(impl::op_kind::Quantize);
                    pm::pb_op *dequant_weight
                            = pgraph->append_op(impl::op_kind::Dequantize,
                                    in_edges_t {in_edge(0, quant_weight, 0)});
                    dequant_weight->append_decision_function(
                            check_input_dtype<impl::data_type::s8>);

                    pm::pb_op *matmul = pgraph->append_op(impl::op_kind::MatMul,
                            in_edges_t {in_edge(0, dequant_data, 0),
                                    in_edge(1, dequant_weight, 0)});
                    matmul->append_decision_function(check_input_num<3>);
                })
        .set_attr<FCreateV2Pattern>("FCreateV2Pattern",
                [](const std::shared_ptr<pb_graph_t> &pgraph) -> void {
                    pm::pb_op *dequant_data
                            = pgraph->append_op(impl::op_kind::Dequantize);
                    pm::pb_op *quant_weight
                            = pgraph->append_op(impl::op_kind::Quantize);
                    pm::pb_op *dequant_weight
                            = pgraph->append_op(impl::op_kind::Dequantize,
                                    in_edges_t {in_edge(0, quant_weight, 0)});
                    dequant_weight->append_decision_function(
                            check_input_dtype<impl::data_type::s8>);

                    pm::pb_op *matmul = pgraph->append_op(impl::op_kind::MatMul,
                            in_edges_t {in_edge(0, dequant_data, 0),
                                    in_edge(1, dequant_weight, 0)});
                    matmul->append_decision_function(check_input_num<2>);

                    pgraph->append_op(impl::op_kind::BiasAdd,
                            in_edges_t {in_edge(0, matmul, 0)});
                })
        .set_attr<FCreateV2FusedOp>(
                "FCreateV2FusedOp", []() -> std::shared_ptr<op_t> {
                    std::shared_ptr<op_t> fused_op = std::make_shared<op_t>(
                            op_kind::x8s8f32_quant_wei_matmul_bias);
                    fused_op->set_attr<std::string>("backend", "dnnl");
                    return fused_op;
                });

DNNL_BACKEND_REGISTER_TRANSFORMATION_PASS(
        dnnl, x8s8f32_quant_wei_matmul_relu_fusion)
        .set_priority(9.7f)
        .set_attr<FCreateV2Pattern>("FCreateV2Pattern",
                [](const std::shared_ptr<pb_graph_t> &pgraph) -> void {
                    pm::pb_op *dequant_data
                            = pgraph->append_op(impl::op_kind::Dequantize);
                    pm::pb_op *quant_weight
                            = pgraph->append_op(impl::op_kind::Quantize);
                    pm::pb_op *dequant_weight
                            = pgraph->append_op(impl::op_kind::Dequantize,
                                    in_edges_t {in_edge(0, quant_weight, 0)});
                    dequant_weight->append_decision_function(
                            check_input_dtype<impl::data_type::s8>);

                    pm::pb_op *matmul = pgraph->append_op(impl::op_kind::MatMul,
                            in_edges_t {in_edge(0, dequant_data, 0),
                                    in_edge(1, dequant_weight, 0)});
                    matmul->append_decision_function(check_input_num<2>);

                    pgraph->append_op(impl::op_kind::ReLU,
                            in_edges_t {in_edge(0, matmul, 0)});
                })
        .set_attr<FCreateV2FusedOp>(
                "FCreateV2FusedOp", []() -> std::shared_ptr<op_t> {
                    std::shared_ptr<op_t> fused_op = std::make_shared<op_t>(
                            op_kind::x8s8f32_quant_wei_matmul_relu);
                    fused_op->set_attr<std::string>("backend", "dnnl");
                    return fused_op;
                });

DNNL_BACKEND_REGISTER_TRANSFORMATION_PASS(
        dnnl, x8s8f32_quant_wei_matmul_bias_relu_fusion)
        .set_priority(9.8f)
        .set_attr<FCreateV2Pattern>("FCreateV2Pattern",
                [](const std::shared_ptr<pb_graph_t> &pgraph) -> void {
                    pm::pb_op *dequant_data
                            = pgraph->append_op(impl::op_kind::Dequantize);
                    pm::pb_op *quant_weight
                            = pgraph->append_op(impl::op_kind::Quantize);
                    pm::pb_op *dequant_weight
                            = pgraph->append_op(impl::op_kind::Dequantize,
                                    in_edges_t {in_edge(0, quant_weight, 0)});
                    dequant_weight->append_decision_function(
                            check_input_dtype<impl::data_type::s8>);

                    pm::pb_op *matmul = pgraph->append_op(impl::op_kind::MatMul,
                            in_edges_t {in_edge(0, dequant_data, 0),
                                    in_edge(1, dequant_weight, 0)});
                    matmul->append_decision_function(check_input_num<3>);

                    pgraph->append_op(impl::op_kind::ReLU,
                            in_edges_t {in_edge(0, matmul, 0)});
                })
        .set_attr<FCreateV2Pattern>("FCreateV2Pattern",
                [](const std::shared_ptr<pb_graph_t> &pgraph) -> void {
                    pm::pb_op *dequant_data
                            = pgraph->append_op(impl::op_kind::Dequantize);
                    pm::pb_op *quant_weight
                            = pgraph->append_op(impl::op_kind::Quantize);
                    pm::pb_op *dequant_weight
                            = pgraph->append_op(impl::op_kind::Dequantize,
                                    in_edges_t {in_edge(0, quant_weight, 0)});
                    dequant_weight->append_decision_function(
                            check_input_dtype<impl::data_type::s8>);

                    pm::pb_op *matmul = pgraph->append_op(impl::op_kind::MatMul,
                            in_edges_t {in_edge(0, dequant_data, 0),
                                    in_edge(1, dequant_weight, 0)});
                    matmul->append_decision_function(check_input_num<2>);

                    pm::pb_op *bias = pgraph->append_op(impl::op_kind::BiasAdd,
                            in_edges_t {in_edge(0, matmul, 0)});
                    pgraph->append_op(impl::op_kind::ReLU,
                            in_edges_t {in_edge(0, bias, 0)});
                })
        .set_attr<FCreateV2FusedOp>(
                "FCreateV2FusedOp", []() -> std::shared_ptr<op_t> {
                    std::shared_ptr<op_t> fused_op = std::make_shared<op_t>(
                            op_kind::x8s8f32_quant_wei_matmul_bias_relu);
                    fused_op->set_attr<std::string>("backend", "dnnl");
                    return fused_op;
                });

DNNL_BACKEND_REGISTER_TRANSFORMATION_PASS(
        dnnl, x8s8f32_quant_wei_matmul_sigmoid_fusion)
        .set_priority(9.7f)
        .set_attr<FCreateV2Pattern>("FCreateV2Pattern",
                [](const std::shared_ptr<pb_graph_t> &pgraph) -> void {
                    pm::pb_op *dequant_data
                            = pgraph->append_op(impl::op_kind::Dequantize);
                    pm::pb_op *quant_weight
                            = pgraph->append_op(impl::op_kind::Quantize);
                    pm::pb_op *dequant_weight
                            = pgraph->append_op(impl::op_kind::Dequantize,
                                    in_edges_t {in_edge(0, quant_weight, 0)});
                    dequant_weight->append_decision_function(
                            check_input_dtype<impl::data_type::s8>);

                    pm::pb_op *matmul = pgraph->append_op(impl::op_kind::MatMul,
                            in_edges_t {in_edge(0, dequant_data, 0),
                                    in_edge(1, dequant_weight, 0)});
                    matmul->append_decision_function(check_input_num<2>);

                    pgraph->append_op(impl::op_kind::Sigmoid,
                            in_edges_t {in_edge(0, matmul, 0)});
                })
        .set_attr<FCreateV2FusedOp>(
                "FCreateV2FusedOp", []() -> std::shared_ptr<op_t> {
                    std::shared_ptr<op_t> fused_op = std::make_shared<op_t>(
                            op_kind::x8s8f32_quant_wei_matmul_sigmoid);
                    fused_op->set_attr<std::string>("backend", "dnnl");
                    return fused_op;
                });

DNNL_BACKEND_REGISTER_TRANSFORMATION_PASS(
        dnnl, x8s8f32_quant_wei_matmul_bias_sigmoid_fusion)
        .set_priority(9.8f)
        .set_attr<FCreateV2Pattern>("FCreateV2Pattern",
                [](const std::shared_ptr<pb_graph_t> &pgraph) -> void {
                    pm::pb_op *dequant_data
                            = pgraph->append_op(impl::op_kind::Dequantize);
                    pm::pb_op *quant_weight
                            = pgraph->append_op(impl::op_kind::Quantize);
                    pm::pb_op *dequant_weight
                            = pgraph->append_op(impl::op_kind::Dequantize,
                                    in_edges_t {in_edge(0, quant_weight, 0)});
                    dequant_weight->append_decision_function(
                            check_input_dtype<impl::data_type::s8>);

                    pm::pb_op *matmul = pgraph->append_op(impl::op_kind::MatMul,
                            in_edges_t {in_edge(0, dequant_data, 0),
                                    in_edge(1, dequant_weight, 0)});
                    matmul->append_decision_function(check_input_num<3>);

                    pgraph->append_op(impl::op_kind::Sigmoid,
                            in_edges_t {in_edge(0, matmul, 0)});
                })
        .set_attr<FCreateV2Pattern>("FCreateV2Pattern",
                [](const std::shared_ptr<pb_graph_t> &pgraph) -> void {
                    pm::pb_op *dequant_data
                            = pgraph->append_op(impl::op_kind::Dequantize);
                    pm::pb_op *quant_weight
                            = pgraph->append_op(impl::op_kind::Quantize);
                    pm::pb_op *dequant_weight
                            = pgraph->append_op(impl::op_kind::Dequantize,
                                    in_edges_t {in_edge(0, quant_weight, 0)});
                    dequant_weight->append_decision_function(
                            check_input_dtype<impl::data_type::s8>);

                    pm::pb_op *matmul = pgraph->append_op(impl::op_kind::MatMul,
                            in_edges_t {in_edge(0, dequant_data, 0),
                                    in_edge(1, dequant_weight, 0)});
                    matmul->append_decision_function(check_input_num<2>);

                    pm::pb_op *bias = pgraph->append_op(impl::op_kind::BiasAdd,
                            in_edges_t {in_edge(0, matmul, 0)});
                    pgraph->append_op(impl::op_kind::Sigmoid,
                            in_edges_t {in_edge(0, bias, 0)});
                })
        .set_attr<FCreateV2FusedOp>(
                "FCreateV2FusedOp", []() -> std::shared_ptr<op_t> {
                    std::shared_ptr<op_t> fused_op = std::make_shared<op_t>(
                            op_kind::x8s8f32_quant_wei_matmul_bias_sigmoid);
                    fused_op->set_attr<std::string>("backend", "dnnl");
                    return fused_op;
                });

DNNL_BACKEND_REGISTER_TRANSFORMATION_PASS(
        dnnl, x8s8f32_quant_wei_matmul_gelu_fusion)
        .set_priority(9.7f)
        .set_attr<FCreateV2Pattern>("FCreateV2Pattern",
                [](const std::shared_ptr<pb_graph_t> &pgraph) -> void {
                    pm::pb_op *dequant_data
                            = pgraph->append_op(impl::op_kind::Dequantize);
                    pm::pb_op *quant_weight
                            = pgraph->append_op(impl::op_kind::Quantize);
                    pm::pb_op *dequant_weight
                            = pgraph->append_op(impl::op_kind::Dequantize,
                                    in_edges_t {in_edge(0, quant_weight, 0)});
                    dequant_weight->append_decision_function(
                            check_input_dtype<impl::data_type::s8>);

                    pm::pb_op *matmul = pgraph->append_op(impl::op_kind::MatMul,
                            in_edges_t {in_edge(0, dequant_data, 0),
                                    in_edge(1, dequant_weight, 0)});
                    matmul->append_decision_function(check_input_num<2>);

                    pgraph->append_op(impl::op_kind::GELU,
                            in_edges_t {in_edge(0, matmul, 0)});
                })
        .set_attr<FCreateV2FusedOp>(
                "FCreateV2FusedOp", []() -> std::shared_ptr<op_t> {
                    std::shared_ptr<op_t> fused_op = std::make_shared<op_t>(
                            op_kind::x8s8f32_quant_wei_matmul_gelu);
                    fused_op->set_attr<std::string>("backend", "dnnl");
                    return fused_op;
                });

DNNL_BACKEND_REGISTER_TRANSFORMATION_PASS(
        dnnl, x8s8f32_quant_wei_matmul_bias_gelu_fusion)
        .set_priority(9.8f)
        .set_attr<FCreateV2Pattern>("FCreateV2Pattern",
                [](const std::shared_ptr<pb_graph_t> &pgraph) -> void {
                    pm::pb_op *dequant_data
                            = pgraph->append_op(impl::op_kind::Dequantize);
                    pm::pb_op *quant_weight
                            = pgraph->append_op(impl::op_kind::Quantize);
                    pm::pb_op *dequant_weight
                            = pgraph->append_op(impl::op_kind::Dequantize,
                                    in_edges_t {in_edge(0, quant_weight, 0)});
                    dequant_weight->append_decision_function(
                            check_input_dtype<impl::data_type::s8>);

                    pm::pb_op *matmul = pgraph->append_op(impl::op_kind::MatMul,
                            in_edges_t {in_edge(0, dequant_data, 0),
                                    in_edge(1, dequant_weight, 0)});
                    matmul->append_decision_function(check_input_num<3>);

                    pgraph->append_op(impl::op_kind::GELU,
                            in_edges_t {in_edge(0, matmul, 0)});
                })
        .set_attr<FCreateV2Pattern>("FCreateV2Pattern",
                [](const std::shared_ptr<pb_graph_t> &pgraph) -> void {
                    pm::pb_op *dequant_data
                            = pgraph->append_op(impl::op_kind::Dequantize);
                    pm::pb_op *quant_weight
                            = pgraph->append_op(impl::op_kind::Quantize);
                    pm::pb_op *dequant_weight
                            = pgraph->append_op(impl::op_kind::Dequantize,
                                    in_edges_t {in_edge(0, quant_weight, 0)});
                    dequant_weight->append_decision_function(
                            check_input_dtype<impl::data_type::s8>);

                    pm::pb_op *matmul = pgraph->append_op(impl::op_kind::MatMul,
                            in_edges_t {in_edge(0, dequant_data, 0),
                                    in_edge(1, dequant_weight, 0)});
                    matmul->append_decision_function(check_input_num<2>);

                    pm::pb_op *bias = pgraph->append_op(impl::op_kind::BiasAdd,
                            in_edges_t {in_edge(0, matmul, 0)});
                    pgraph->append_op(impl::op_kind::GELU,
                            in_edges_t {in_edge(0, bias, 0)});
                })
        .set_attr<FCreateV2FusedOp>(
                "FCreateV2FusedOp", []() -> std::shared_ptr<op_t> {
                    std::shared_ptr<op_t> fused_op = std::make_shared<op_t>(
                            op_kind::x8s8f32_quant_wei_matmul_bias_gelu);
                    fused_op->set_attr<std::string>("backend", "dnnl");
                    return fused_op;
                });

DNNL_BACKEND_REGISTER_TRANSFORMATION_PASS(dnnl, int8_matmul_add_fusion)
        .set_priority(10.5f)
        .set_attr<FCreateV2Pattern>("FCreateV2Pattern",
                [](const std::shared_ptr<pb_graph_t> &pgraph) -> void {
                    pm::pb_op *dequant_data
                            = pgraph->append_op(impl::op_kind::Dequantize);

                    pm::pb_op *dequant_weight
                            = pgraph->append_op(impl::op_kind::Dequantize);

                    pm::pb_op *dequant_other = pgraph->append_op(
                            impl::op_kind::Dequantize, "dequant_other");

                    pm::pb_op *matmul = pgraph->append_op(impl::op_kind::MatMul,
                            in_edges_t {in_edge(0, dequant_data, 0),
                                    in_edge(1, dequant_weight, 0)});
                    matmul->append_decision_function(check_input_num<2>);

                    pm::pb_op *add = pgraph->append_op(impl::op_kind::Add,
                            in_edges_t {in_edge(0, matmul, 0),
                                    in_edge(1, dequant_other, 0)});

                    pgraph->append_op(impl::op_kind::Quantize,
                            in_edges_t {in_edge(0, add, 0)});
                })
        .set_attr<FCreateV2FusedOp>(
                "FCreateV2FusedOp", []() -> std::shared_ptr<op_t> {
                    std::shared_ptr<op_t> fused_op
                            = std::make_shared<op_t>(op_kind::int8_matmul_add);
                    fused_op->set_attr<std::string>("backend", "dnnl");
                    return fused_op;
                });

DNNL_BACKEND_REGISTER_TRANSFORMATION_PASS(
        dnnl, int8_quant_wei_matmul_add_fusion)
        .set_priority(10.6f)
        .set_attr<FCreateV2Pattern>("FCreateV2Pattern",
                [](const std::shared_ptr<pb_graph_t> &pgraph) -> void {
                    pm::pb_op *dequant_data
                            = pgraph->append_op(impl::op_kind::Dequantize);
                    pm::pb_op *quant_weight
                            = pgraph->append_op(impl::op_kind::Quantize);
                    pm::pb_op *dequant_weight
                            = pgraph->append_op(impl::op_kind::Dequantize,
                                    in_edges_t {in_edge(0, quant_weight, 0)});
                    dequant_weight->append_decision_function(
                            check_input_dtype<impl::data_type::s8>);
                    pm::pb_op *dequant_other = pgraph->append_op(
                            impl::op_kind::Dequantize, "dequant_other");

                    pm::pb_op *matmul = pgraph->append_op(impl::op_kind::MatMul,
                            in_edges_t {in_edge(0, dequant_data, 0),
                                    in_edge(1, dequant_weight, 0)});
                    matmul->append_decision_function(check_input_num<2>);

                    pm::pb_op *add = pgraph->append_op(impl::op_kind::Add,
                            in_edges_t {in_edge(0, matmul, 0),
                                    in_edge(1, dequant_other, 0)});
                    pgraph->append_op(impl::op_kind::Quantize,
                            in_edges_t {in_edge(0, add, 0)});
                })
        .set_attr<FCreateV2FusedOp>(
                "FCreateV2FusedOp", []() -> std::shared_ptr<op_t> {
                    std::shared_ptr<op_t> fused_op = std::make_shared<op_t>(
                            op_kind::int8_quant_wei_matmul_add);
                    fused_op->set_attr<std::string>("backend", "dnnl");
                    return fused_op;
                });

DNNL_BACKEND_REGISTER_TRANSFORMATION_PASS(dnnl, int8_matmul_bias_add_fusion)
        .set_priority(10.5f)
        .set_attr<FCreateV2Pattern>("FCreateV2Pattern",
                [](const std::shared_ptr<pb_graph_t> &pgraph) -> void {
                    pm::pb_op *dequant_data
                            = pgraph->append_op(impl::op_kind::Dequantize);
                    pm::pb_op *dequant_weight
                            = pgraph->append_op(impl::op_kind::Dequantize);
                    dequant_weight->append_decision_function(
                            check_input_dtype<impl::data_type::s8>);
                    pm::pb_op *dequant_other = pgraph->append_op(
                            impl::op_kind::Dequantize, "dequant_other");

                    pm::pb_op *matmul = pgraph->append_op(impl::op_kind::MatMul,
                            in_edges_t {in_edge(0, dequant_data, 0),
                                    in_edge(1, dequant_weight, 0)});
                    matmul->append_decision_function(check_input_num<2>);

                    pm::pb_op *bias = pgraph->append_op(impl::op_kind::BiasAdd,
                            in_edges_t {in_edge(0, matmul, 0)});
                    pm::pb_op *add = pgraph->append_op(impl::op_kind::Add,
                            in_edges_t {in_edge(0, bias, 0),
                                    in_edge(1, dequant_other, 0)});
                    pgraph->append_op(impl::op_kind::Quantize,
                            in_edges_t {in_edge(0, add, 0)});
                })
        .set_attr<FCreateV2Pattern>("FCreateV2Pattern",
                [](const std::shared_ptr<pb_graph_t> &pgraph) -> void {
                    pm::pb_op *dequant_data
                            = pgraph->append_op(impl::op_kind::Dequantize);
                    pm::pb_op *dequant_weight
                            = pgraph->append_op(impl::op_kind::Dequantize);
                    dequant_weight->append_decision_function(
                            check_input_dtype<impl::data_type::s8>);
                    pm::pb_op *dequant_other = pgraph->append_op(
                            impl::op_kind::Dequantize, "dequant_other");

                    pm::pb_op *matmul = pgraph->append_op(impl::op_kind::MatMul,
                            in_edges_t {in_edge(0, dequant_data, 0),
                                    in_edge(1, dequant_weight, 0)});
                    matmul->append_decision_function(check_input_num<3>);

                    pm::pb_op *add = pgraph->append_op(impl::op_kind::Add,
                            in_edges_t {in_edge(0, matmul, 0),
                                    in_edge(1, dequant_other, 0)});
                    pgraph->append_op(impl::op_kind::Quantize,
                            in_edges_t {in_edge(0, add, 0)});
                })
        .set_attr<FCreateV2FusedOp>(
                "FCreateV2FusedOp", []() -> std::shared_ptr<op_t> {
                    std::shared_ptr<op_t> fused_op = std::make_shared<op_t>(
                            op_kind::int8_matmul_bias_add);
                    fused_op->set_attr<std::string>("backend", "dnnl");
                    return fused_op;
                });

DNNL_BACKEND_REGISTER_TRANSFORMATION_PASS(
        dnnl, int8_quant_wei_matmul_bias_add_fusion)
        .set_priority(10.6f)
        .set_attr<FCreateV2Pattern>("FCreateV2Pattern",
                [](const std::shared_ptr<pb_graph_t> &pgraph) -> void {
                    pm::pb_op *dequant_data
                            = pgraph->append_op(impl::op_kind::Dequantize);
                    pm::pb_op *quant_weight
                            = pgraph->append_op(impl::op_kind::Quantize);
                    pm::pb_op *dequant_weight
                            = pgraph->append_op(impl::op_kind::Dequantize,
                                    in_edges_t {in_edge(0, quant_weight, 0)});
                    dequant_weight->append_decision_function(
                            check_input_dtype<impl::data_type::s8>);

                    pm::pb_op *dequant_other = pgraph->append_op(
                            impl::op_kind::Dequantize, "dequant_other");
                    pm::pb_op *matmul = pgraph->append_op(impl::op_kind::MatMul,
                            in_edges_t {in_edge(0, dequant_data, 0),
                                    in_edge(1, dequant_weight, 0)});
                    matmul->append_decision_function(check_input_num<2>);

                    pm::pb_op *bias = pgraph->append_op(impl::op_kind::BiasAdd,
                            in_edges_t {in_edge(0, matmul, 0)});
                    pm::pb_op *add = pgraph->append_op(impl::op_kind::Add,
                            in_edges_t {in_edge(0, bias, 0),
                                    in_edge(1, dequant_other, 0)});

                    pgraph->append_op(impl::op_kind::Quantize,
                            in_edges_t {in_edge(0, add, 0)});
                })
        .set_attr<FCreateV2Pattern>("FCreateV2Pattern",
                [](const std::shared_ptr<pb_graph_t> &pgraph) -> void {
                    pm::pb_op *dequant_data
                            = pgraph->append_op(impl::op_kind::Dequantize);
                    pm::pb_op *quant_weight
                            = pgraph->append_op(impl::op_kind::Quantize);
                    pm::pb_op *dequant_weight
                            = pgraph->append_op(impl::op_kind::Dequantize,
                                    in_edges_t {in_edge(0, quant_weight, 0)});
                    dequant_weight->append_decision_function(
                            check_input_dtype<impl::data_type::s8>);

                    pm::pb_op *dequant_other = pgraph->append_op(
                            impl::op_kind::Dequantize, "dequant_other");

                    pm::pb_op *matmul = pgraph->append_op(impl::op_kind::MatMul,
                            in_edges_t {in_edge(0, dequant_data, 0),
                                    in_edge(1, dequant_weight, 0)});
                    matmul->append_decision_function(check_input_num<3>);

                    pm::pb_op *add = pgraph->append_op(impl::op_kind::Add,
                            in_edges_t {in_edge(0, matmul, 0),
                                    in_edge(1, dequant_other, 0)});

                    pgraph->append_op(impl::op_kind::Quantize,
                            in_edges_t {in_edge(0, add, 0)});
                })
        .set_attr<FCreateV2FusedOp>(
                "FCreateV2FusedOp", []() -> std::shared_ptr<op_t> {
                    std::shared_ptr<op_t> fused_op = std::make_shared<op_t>(
                            op_kind::int8_quant_wei_matmul_bias_add);
                    fused_op->set_attr<std::string>("backend", "dnnl");
                    return fused_op;
                });

DNNL_BACKEND_REGISTER_TRANSFORMATION_PASS(dnnl, x8s8f32_matmul_add_fusion)
        .set_priority(10.4f)
        .set_attr<FCreateV2Pattern>("FCreateV2Pattern",
                [](const std::shared_ptr<pb_graph_t> &pgraph) -> void {
                    pm::pb_op *dequant_data
                            = pgraph->append_op(impl::op_kind::Dequantize);
                    pm::pb_op *dequant_weight
                            = pgraph->append_op(impl::op_kind::Dequantize);
                    dequant_weight->append_decision_function(
                            check_input_dtype<impl::data_type::s8>);

                    pm::pb_op *dequant_other = pgraph->append_op(
                            impl::op_kind::Dequantize, "dequant_other");

                    pm::pb_op *matmul = pgraph->append_op(impl::op_kind::MatMul,
                            in_edges_t {in_edge(0, dequant_data, 0),
                                    in_edge(1, dequant_weight, 0)});
                    matmul->append_decision_function(check_input_num<2>);

                    pgraph->append_op(impl::op_kind::Add,
                            in_edges_t {in_edge(0, matmul, 0),
                                    in_edge(1, dequant_other, 0)});
                })
        .set_attr<FCreateV2FusedOp>(
                "FCreateV2FusedOp", []() -> std::shared_ptr<op_t> {
                    std::shared_ptr<op_t> fused_op = std::make_shared<op_t>(
                            op_kind::x8s8float_matmul_add);
                    fused_op->set_attr<std::string>("backend", "dnnl");
                    return fused_op;
                });

DNNL_BACKEND_REGISTER_TRANSFORMATION_PASS(dnnl, x8x8f32_matmul_div_fusion)
        .set_priority(10.4f)
        .set_attr<FCreateV2Pattern>("FCreateV2Pattern",
                [](const std::shared_ptr<pb_graph_t> &pgraph) -> void {
                    pm::pb_op *dequant_data
                            = pgraph->append_op(impl::op_kind::Dequantize);
                    pm::pb_op *dequant_weight
                            = pgraph->append_op(impl::op_kind::Dequantize);
                    pm::pb_op *matmul = pgraph->append_op(impl::op_kind::MatMul,
                            in_edges_t {in_edge(0, dequant_data, 0),
                                    in_edge(1, dequant_weight, 0)});
                    matmul->append_decision_function(check_input_num<2>);

                    pgraph->append_op(impl::op_kind::Divide,
                            in_edges_t {in_edge(0, matmul, 0)});
                })
        .set_attr<FCreateV2FusedOp>(
                "FCreateV2FusedOp", []() -> std::shared_ptr<op_t> {
                    std::shared_ptr<op_t> fused_op = std::make_shared<op_t>(
                            op_kind::x8x8float_matmul_div);
                    fused_op->set_attr<std::string>("backend", "dnnl");
                    return fused_op;
                });

DNNL_BACKEND_REGISTER_TRANSFORMATION_PASS(dnnl, x8x8f32_matmul_div_add_fusion)
        .set_priority(10.5f)
        .set_attr<FCreateV2Pattern>("FCreateV2Pattern",
                [](const std::shared_ptr<pb_graph_t> &pgraph) -> void {
                    pm::pb_op *dequant_data
                            = pgraph->append_op(impl::op_kind::Dequantize);
                    pm::pb_op *dequant_weight
                            = pgraph->append_op(impl::op_kind::Dequantize);
                    pm::pb_op *matmul = pgraph->append_op(impl::op_kind::MatMul,
                            in_edges_t {in_edge(0, dequant_data, 0),
                                    in_edge(1, dequant_weight, 0)});
                    matmul->append_decision_function(check_input_num<2>);

                    pm::pb_op *div = pgraph->append_op(impl::op_kind::Divide,
                            in_edges_t {in_edge(0, matmul, 0)});
                    pgraph->append_op(impl::op_kind::Add,
                            in_edges_t {in_edge(0, div, 0)});
                })
        .set_attr<FCreateV2FusedOp>(
                "FCreateV2FusedOp", []() -> std::shared_ptr<op_t> {
                    std::shared_ptr<op_t> fused_op = std::make_shared<op_t>(
                            op_kind::x8x8float_matmul_div_add);
                    fused_op->set_attr<std::string>("backend", "dnnl");
                    return fused_op;
                });

DNNL_BACKEND_REGISTER_TRANSFORMATION_PASS(dnnl, x8s8f32_matmul_bias_add_fusion)
        .set_priority(10.4f)
        .set_attr<FCreateV2Pattern>("FCreateV2Pattern",
                [](const std::shared_ptr<pb_graph_t> &pgraph) -> void {
                    pm::pb_op *dequant_data
                            = pgraph->append_op(impl::op_kind::Dequantize);
                    pm::pb_op *dequant_weight
                            = pgraph->append_op(impl::op_kind::Dequantize);
                    dequant_weight->append_decision_function(
                            check_input_dtype<impl::data_type::s8>);

                    pm::pb_op *dequant_other = pgraph->append_op(
                            impl::op_kind::Dequantize, "dequant_other");

                    pm::pb_op *matmul = pgraph->append_op(impl::op_kind::MatMul,
                            in_edges_t {in_edge(0, dequant_data, 0),
                                    in_edge(1, dequant_weight, 0)});
                    matmul->append_decision_function(check_input_num<3>);

                    pgraph->append_op(impl::op_kind::Add,
                            in_edges_t {in_edge(0, matmul, 0),
                                    in_edge(1, dequant_other, 0)});
                })
        .set_attr<FCreateV2Pattern>("FCreateV2Pattern",
                [](const std::shared_ptr<pb_graph_t> &pgraph) -> void {
                    pm::pb_op *dequant_data
                            = pgraph->append_op(impl::op_kind::Dequantize);
                    pm::pb_op *dequant_weight
                            = pgraph->append_op(impl::op_kind::Dequantize);
                    dequant_weight->append_decision_function(
                            check_input_dtype<impl::data_type::s8>);

                    pm::pb_op *dequant_other = pgraph->append_op(
                            impl::op_kind::Dequantize, "dequant_other");

                    pm::pb_op *matmul = pgraph->append_op(impl::op_kind::MatMul,
                            in_edges_t {in_edge(0, dequant_data, 0),
                                    in_edge(1, dequant_weight, 0)});
                    matmul->append_decision_function(check_input_num<2>);
                    pm::pb_op *bias = pgraph->append_op(impl::op_kind::BiasAdd,
                            in_edges_t {in_edge(0, matmul, 0)});

                    pgraph->append_op(impl::op_kind::Add,
                            in_edges_t {in_edge(0, bias, 0),
                                    in_edge(1, dequant_other, 0)});
                })
        .set_attr<FCreateV2FusedOp>(
                "FCreateV2FusedOp", []() -> std::shared_ptr<op_t> {
                    std::shared_ptr<op_t> fused_op = std::make_shared<op_t>(
                            op_kind::x8s8float_matmul_bias_add);
                    fused_op->set_attr<std::string>("backend", "dnnl");
                    return fused_op;
                });

DNNL_BACKEND_REGISTER_TRANSFORMATION_PASS(
        dnnl, x8s8f32_quant_wei_matmul_add_fusion)
        .set_priority(10.5f)
        .set_attr<FCreateV2Pattern>("FCreateV2Pattern",
                [](const std::shared_ptr<pb_graph_t> &pgraph) -> void {
                    pm::pb_op *dequant_data
                            = pgraph->append_op(impl::op_kind::Dequantize);
                    pm::pb_op *quant_weight
                            = pgraph->append_op(impl::op_kind::Quantize);
                    pm::pb_op *dequant_weight
                            = pgraph->append_op(impl::op_kind::Dequantize,
                                    in_edges_t {in_edge(0, quant_weight, 0)});
                    dequant_weight->append_decision_function(
                            check_input_dtype<impl::data_type::s8>);

                    pm::pb_op *dequant_other = pgraph->append_op(
                            impl::op_kind::Dequantize, "dequant_other");
                    pm::pb_op *matmul = pgraph->append_op(impl::op_kind::MatMul,
                            in_edges_t {in_edge(0, dequant_data, 0),
                                    in_edge(1, dequant_weight, 0)});
                    matmul->append_decision_function(check_input_num<2>);

                    pgraph->append_op(impl::op_kind::Add,
                            in_edges_t {in_edge(0, matmul, 0),
                                    in_edge(1, dequant_other, 0)});
                })
        .set_attr<FCreateV2FusedOp>(
                "FCreateV2FusedOp", []() -> std::shared_ptr<op_t> {
                    std::shared_ptr<op_t> fused_op = std::make_shared<op_t>(
                            op_kind::x8s8f32_quant_wei_matmul_add);
                    fused_op->set_attr<std::string>("backend", "dnnl");
                    return fused_op;
                });

DNNL_BACKEND_REGISTER_TRANSFORMATION_PASS(
        dnnl, x8s8f32_quant_wei_matmul_bias_add_fusion)
        .set_priority(10.5f)
        .set_attr<FCreateV2Pattern>("FCreateV2Pattern",
                [](const std::shared_ptr<pb_graph_t> &pgraph) -> void {
                    pm::pb_op *dequant_data
                            = pgraph->append_op(impl::op_kind::Dequantize);
                    pm::pb_op *quant_weight
                            = pgraph->append_op(impl::op_kind::Quantize);
                    pm::pb_op *dequant_weight
                            = pgraph->append_op(impl::op_kind::Dequantize,
                                    in_edges_t {in_edge(0, quant_weight, 0)});
                    dequant_weight->append_decision_function(
                            check_input_dtype<impl::data_type::s8>);

                    pm::pb_op *dequant_other = pgraph->append_op(
                            impl::op_kind::Dequantize, "dequant_other");
                    pm::pb_op *matmul = pgraph->append_op(impl::op_kind::MatMul,
                            in_edges_t {in_edge(0, dequant_data, 0),
                                    in_edge(1, dequant_weight, 0)});
                    matmul->append_decision_function(check_input_num<3>);

                    pgraph->append_op(impl::op_kind::Add,
                            in_edges_t {in_edge(0, matmul, 0),
                                    in_edge(1, dequant_other, 0)});
                })
        .set_attr<FCreateV2Pattern>("FCreateV2Pattern",
                [](const std::shared_ptr<pb_graph_t> &pgraph) -> void {
                    pm::pb_op *dequant_data
                            = pgraph->append_op(impl::op_kind::Dequantize);
                    pm::pb_op *quant_weight
                            = pgraph->append_op(impl::op_kind::Quantize);
                    pm::pb_op *dequant_weight
                            = pgraph->append_op(impl::op_kind::Dequantize,
                                    in_edges_t {in_edge(0, quant_weight, 0)});
                    dequant_weight->append_decision_function(
                            check_input_dtype<impl::data_type::s8>);

                    pm::pb_op *dequant_other = pgraph->append_op(
                            impl::op_kind::Dequantize, "dequant_other");
                    pm::pb_op *matmul = pgraph->append_op(impl::op_kind::MatMul,
                            in_edges_t {in_edge(0, dequant_data, 0),
                                    in_edge(1, dequant_weight, 0)});
                    matmul->append_decision_function(check_input_num<2>);

                    pm::pb_op *bias = pgraph->append_op(impl::op_kind::BiasAdd,
                            in_edges_t {in_edge(0, matmul, 0)});
                    pgraph->append_op(impl::op_kind::Add,
                            in_edges_t {in_edge(0, bias, 0),
                                    in_edge(1, dequant_other, 0)});
                })
        .set_attr<FCreateV2FusedOp>(
                "FCreateV2FusedOp", []() -> std::shared_ptr<op_t> {
                    std::shared_ptr<op_t> fused_op = std::make_shared<op_t>(
                            op_kind::x8s8f32_quant_wei_matmul_bias_add);
                    fused_op->set_attr<std::string>("backend", "dnnl");
                    return fused_op;
                });

DNNL_BACKEND_REGISTER_TRANSFORMATION_PASS(dnnl, x8x8bf16_matmul_fusion)
        .set_priority(9.8f)
        .set_attr<FCreateV2Pattern>("FCreateV2Pattern",
                [](const std::shared_ptr<pb_graph_t> &pgraph) -> void {
                    pm::pb_op *dequant_data
                            = pgraph->append_op(impl::op_kind::Dequantize);
                    pm::pb_op *dequant_weight
                            = pgraph->append_op(impl::op_kind::Dequantize);
                    pm::pb_op *typecast_data
                            = pgraph->append_op(impl::op_kind::TypeCast,
                                    in_edges_t {in_edge(0, dequant_data, 0)});
                    typecast_data->append_decision_function(
                            check_output_dtype<impl::data_type::bf16>);

                    pm::pb_op *typecast_weight
                            = pgraph->append_op(impl::op_kind::TypeCast,
                                    in_edges_t {in_edge(0, dequant_weight, 0)});
                    typecast_weight->append_decision_function(
                            check_output_dtype<impl::data_type::bf16>);

                    pm::pb_op *matmul = pgraph->append_op(impl::op_kind::MatMul,
                            in_edges_t {in_edge(0, typecast_data, 0),
                                    in_edge(1, typecast_weight, 0)});
                    matmul->append_decision_function(check_input_num<2>);
                })
        .set_attr<FCreateV2FusedOp>(
                "FCreateV2FusedOp", []() -> std::shared_ptr<op_t> {
                    std::shared_ptr<op_t> fused_op
                            = std::make_shared<op_t>(op_kind::x8x8float_matmul);
                    fused_op->set_attr<std::string>("backend", "dnnl");
                    return fused_op;
                });

DNNL_BACKEND_REGISTER_TRANSFORMATION_PASS(dnnl, x8x8bf16_matmul_div_fusion)
        .set_priority(10.4f)
        .set_attr<FCreateV2Pattern>("FCreateV2Pattern",
                [](const std::shared_ptr<pb_graph_t> &pgraph) -> void {
                    pm::pb_op *dequant_data
                            = pgraph->append_op(impl::op_kind::Dequantize);
                    pm::pb_op *dequant_weight
                            = pgraph->append_op(impl::op_kind::Dequantize);
                    pm::pb_op *typecast_data
                            = pgraph->append_op(impl::op_kind::TypeCast,
                                    in_edges_t {in_edge(0, dequant_data, 0)});
                    typecast_data->append_decision_function(
                            check_output_dtype<impl::data_type::bf16>);

                    pm::pb_op *typecast_weight
                            = pgraph->append_op(impl::op_kind::TypeCast,
                                    in_edges_t {in_edge(0, dequant_weight, 0)});
                    typecast_weight->append_decision_function(
                            check_output_dtype<impl::data_type::bf16>);

                    pm::pb_op *matmul = pgraph->append_op(impl::op_kind::MatMul,
                            in_edges_t {in_edge(0, typecast_data, 0),
                                    in_edge(1, typecast_weight, 0)});
                    matmul->append_decision_function(check_input_num<2>);

                    pgraph->append_op(impl::op_kind::Divide,
                            in_edges_t {in_edge(0, matmul, 0)});
                })
        .set_attr<FCreateV2FusedOp>(
                "FCreateV2FusedOp", []() -> std::shared_ptr<op_t> {
                    std::shared_ptr<op_t> fused_op = std::make_shared<op_t>(
                            op_kind::x8x8float_matmul_div);
                    fused_op->set_attr<std::string>("backend", "dnnl");
                    return fused_op;
                });

DNNL_BACKEND_REGISTER_TRANSFORMATION_PASS(dnnl, x8x8bf16_matmul_div_add_fusion)
        .set_priority(10.5f)
        .set_attr<FCreateV2Pattern>("FCreateV2Pattern",
                [](const std::shared_ptr<pb_graph_t> &pgraph) -> void {
                    pm::pb_op *dequant_data
                            = pgraph->append_op(impl::op_kind::Dequantize);
                    pm::pb_op *dequant_weight
                            = pgraph->append_op(impl::op_kind::Dequantize);
                    pm::pb_op *typecast_data
                            = pgraph->append_op(impl::op_kind::TypeCast,
                                    in_edges_t {in_edge(0, dequant_data, 0)});
                    typecast_data->append_decision_function(
                            check_output_dtype<impl::data_type::bf16>);

                    pm::pb_op *typecast_weight
                            = pgraph->append_op(impl::op_kind::TypeCast,
                                    in_edges_t {in_edge(0, dequant_weight, 0)});
                    typecast_weight->append_decision_function(
                            check_output_dtype<impl::data_type::bf16>);

                    pm::pb_op *matmul = pgraph->append_op(impl::op_kind::MatMul,
                            in_edges_t {in_edge(0, typecast_data, 0),
                                    in_edge(1, typecast_weight, 0)});
                    matmul->append_decision_function(check_input_num<2>);

                    pm::pb_op *div = pgraph->append_op(impl::op_kind::Divide,
                            in_edges_t {in_edge(0, matmul, 0)});
                    pgraph->append_op(impl::op_kind::Add,
                            in_edges_t {in_edge(0, div, 0)});
                })
        .set_attr<FCreateV2FusedOp>(
                "FCreateV2FusedOp", []() -> std::shared_ptr<op_t> {
                    std::shared_ptr<op_t> fused_op = std::make_shared<op_t>(
                            op_kind::x8x8float_matmul_div_add);
                    fused_op->set_attr<std::string>("backend", "dnnl");
                    return fused_op;
                });

DNNL_BACKEND_REGISTER_TRANSFORMATION_PASS(dnnl, x8s8bf16_matmul_bias_fusion)
        .set_priority(10.4f)
        .set_attr<FCreateV2Pattern>("FCreateV2Pattern",
                [](const std::shared_ptr<pb_graph_t> &pgraph) -> void {
                    pm::pb_op *dequant_data
                            = pgraph->append_op(impl::op_kind::Dequantize);
                    pm::pb_op *dequant_weight
                            = pgraph->append_op(impl::op_kind::Dequantize);
                    dequant_weight->append_decision_function(
                            check_input_dtype<impl::data_type::s8>);
                    pm::pb_op *typecast_data
                            = pgraph->append_op(impl::op_kind::TypeCast,
                                    in_edges_t {in_edge(0, dequant_data, 0)});
                    typecast_data->append_decision_function(
                            check_output_dtype<impl::data_type::bf16>);

                    pm::pb_op *typecast_weight
                            = pgraph->append_op(impl::op_kind::TypeCast,
                                    in_edges_t {in_edge(0, dequant_weight, 0)});
                    typecast_weight->append_decision_function(
                            check_output_dtype<impl::data_type::bf16>);

                    pm::pb_op *matmul = pgraph->append_op(impl::op_kind::MatMul,
                            in_edges_t {in_edge(0, typecast_data, 0),
                                    in_edge(1, typecast_weight, 0)});
                    matmul->append_decision_function(check_input_num<3>);
                })
        .set_attr<FCreateV2FusedOp>(
                "FCreateV2FusedOp", []() -> std::shared_ptr<op_t> {
                    std::shared_ptr<op_t> fused_op = std::make_shared<op_t>(
                            op_kind::x8s8float_matmul_bias);
                    fused_op->set_attr<std::string>("backend", "dnnl");
                    return fused_op;
                });

DNNL_BACKEND_REGISTER_TRANSFORMATION_PASS(dnnl, x8s8bf16_matmul_bias_add_fusion)
        .set_priority(10.5f)
        .set_attr<FCreateV2Pattern>("FCreateV2Pattern",
                [](const std::shared_ptr<pb_graph_t> &pgraph) -> void {
                    pm::pb_op *dequant_data
                            = pgraph->append_op(impl::op_kind::Dequantize);
                    pm::pb_op *dequant_weight
                            = pgraph->append_op(impl::op_kind::Dequantize);
                    dequant_weight->append_decision_function(
                            check_input_dtype<impl::data_type::s8>);
                    pm::pb_op *dequant_other
                            = pgraph->append_op(impl::op_kind::Dequantize);
                    pm::pb_op *typecast_data
                            = pgraph->append_op(impl::op_kind::TypeCast,
                                    in_edges_t {in_edge(0, dequant_data, 0)});
                    typecast_data->append_decision_function(
                            check_output_dtype<impl::data_type::bf16>);

                    pm::pb_op *typecast_weight
                            = pgraph->append_op(impl::op_kind::TypeCast,
                                    in_edges_t {in_edge(0, dequant_weight, 0)});
                    typecast_weight->append_decision_function(
                            check_output_dtype<impl::data_type::bf16>);

                    pm::pb_op *typecast_other
                            = pgraph->append_op(impl::op_kind::TypeCast,
                                    in_edges_t {in_edge(0, dequant_other, 0)});
                    typecast_other->append_decision_function(
                            check_output_dtype<impl::data_type::bf16>);

                    pm::pb_op *matmul = pgraph->append_op(impl::op_kind::MatMul,
                            in_edges_t {in_edge(0, typecast_data, 0),
                                    in_edge(1, typecast_weight, 0)});
                    matmul->append_decision_function(check_input_num<3>);

                    pgraph->append_op(impl::op_kind::Add,
                            in_edges_t {in_edge(0, matmul, 0),
                                    in_edge(1, typecast_other, 0)});
                })
        .set_attr<FCreateV2FusedOp>(
                "FCreateV2FusedOp", []() -> std::shared_ptr<op_t> {
                    std::shared_ptr<op_t> fused_op = std::make_shared<op_t>(
                            op_kind::x8s8float_matmul_bias_add);
                    fused_op->set_attr<std::string>("backend", "dnnl");
                    return fused_op;
                });

DNNL_BACKEND_REGISTER_TRANSFORMATION_PASS(
        dnnl, x8s8bf16_matmul_bias_add_bf16_fusion)
        .set_priority(10.49f)
        .set_attr<FCreateV2Pattern>("FCreateV2Pattern",
                [](const std::shared_ptr<pb_graph_t> &pgraph) -> void {
                    pm::pb_op *dequant_data
                            = pgraph->append_op(impl::op_kind::Dequantize);
                    pm::pb_op *dequant_weight
                            = pgraph->append_op(impl::op_kind::Dequantize);
                    dequant_weight->append_decision_function(
                            check_input_dtype<impl::data_type::s8>);

                    pm::pb_op *typecast_data
                            = pgraph->append_op(impl::op_kind::TypeCast,
                                    in_edges_t {in_edge(0, dequant_data, 0)});
                    typecast_data->append_decision_function(
                            check_output_dtype<impl::data_type::bf16>);

                    pm::pb_op *typecast_weight
                            = pgraph->append_op(impl::op_kind::TypeCast,
                                    in_edges_t {in_edge(0, dequant_weight, 0)});
                    typecast_weight->append_decision_function(
                            check_output_dtype<impl::data_type::bf16>);

                    pm::pb_op *matmul = pgraph->append_op(impl::op_kind::MatMul,
                            in_edges_t {in_edge(0, typecast_data, 0),
                                    in_edge(1, typecast_weight, 0)});
                    matmul->append_decision_function(check_input_num<3>);

                    pgraph->append_op(impl::op_kind::Add,
                            in_edges_t {in_edge(0, matmul, 0)});
                })
        .set_attr<FCreateV2FusedOp>(
                "FCreateV2FusedOp", []() -> std::shared_ptr<op_t> {
                    std::shared_ptr<op_t> fused_op = std::make_shared<op_t>(
                            op_kind::x8s8float_matmul_bias_add);
                    fused_op->set_attr<std::string>("backend", "dnnl");
                    return fused_op;
                });

DNNL_BACKEND_REGISTER_TRANSFORMATION_PASS(dnnl, x8s8bf16_matmul_add_fusion)
        .set_priority(10.3f)
        .set_attr<FCreateV2Pattern>("FCreateV2Pattern",
                [](const std::shared_ptr<pb_graph_t> &pgraph) -> void {
                    pm::pb_op *dequant_data
                            = pgraph->append_op(impl::op_kind::Dequantize);
                    pm::pb_op *dequant_weight
                            = pgraph->append_op(impl::op_kind::Dequantize);
                    dequant_weight->append_decision_function(
                            check_input_dtype<impl::data_type::s8>);
                    pm::pb_op *dequant_other
                            = pgraph->append_op(impl::op_kind::Dequantize);
                    pm::pb_op *typecast_data
                            = pgraph->append_op(impl::op_kind::TypeCast,
                                    in_edges_t {in_edge(0, dequant_data, 0)});
                    typecast_data->append_decision_function(
                            check_output_dtype<impl::data_type::bf16>);

                    pm::pb_op *typecast_weight
                            = pgraph->append_op(impl::op_kind::TypeCast,
                                    in_edges_t {in_edge(0, dequant_weight, 0)});
                    typecast_weight->append_decision_function(
                            check_output_dtype<impl::data_type::bf16>);

                    pm::pb_op *typecast_other
                            = pgraph->append_op(impl::op_kind::TypeCast,
                                    in_edges_t {in_edge(0, dequant_other, 0)});
                    typecast_other->append_decision_function(
                            check_output_dtype<impl::data_type::bf16>);

                    pm::pb_op *matmul = pgraph->append_op(impl::op_kind::MatMul,
                            in_edges_t {in_edge(0, typecast_data, 0),
                                    in_edge(1, typecast_weight, 0)});
                    matmul->append_decision_function(check_input_num<2>);

                    pgraph->append_op(impl::op_kind::Add,
                            in_edges_t {in_edge(0, matmul, 0),
                                    in_edge(0, typecast_other, 0)});
                })
        .set_attr<FCreateV2FusedOp>(
                "FCreateV2FusedOp", []() -> std::shared_ptr<op_t> {
                    std::shared_ptr<op_t> fused_op = std::make_shared<op_t>(
                            op_kind::x8s8float_matmul_add);
                    fused_op->set_attr<std::string>("backend", "dnnl");
                    return fused_op;
                });

DNNL_BACKEND_REGISTER_TRANSFORMATION_PASS(dnnl, int8_MHA_fusion)
        .set_priority(5.0f)
        .set_attr<FCreateV2Pattern>("FCreateV2Pattern",
                [](const std::shared_ptr<pb_graph_t> &pgraph) -> void {
                    auto query_reshape = pgraph->append_op(
                            impl::op_kind::StaticReshape, "query_reshape");
                    auto query_transpose
                            = pgraph->append_op(impl::op_kind::StaticTranspose,
                                    in_edges_t {in_edge(0, query_reshape, 0)},
                                    "query_transpose");
                    auto quantize_query
                            = pgraph->append_op(impl::op_kind::Quantize,
                                    in_edges_t {in_edge(0, query_transpose, 0)},
                                    "quantize_query");
                    auto dequantize_query
                            = pgraph->append_op(impl::op_kind::Dequantize,
                                    in_edges_t {in_edge(0, quantize_query, 0)},
                                    "dequantize_query");

                    auto key_reshape = pgraph->append_op(
                            impl::op_kind::StaticReshape, "key_reshape");
                    auto key_transpose
                            = pgraph->append_op(impl::op_kind::StaticTranspose,
                                    in_edges_t {in_edge(0, key_reshape, 0)},
                                    "key_transpose");
                    auto key_transpose2
                            = pgraph->append_op(impl::op_kind::StaticTranspose,
                                    in_edges_t {in_edge(0, key_transpose, 0)},
                                    "key_transpose2");
                    auto quantize_key
                            = pgraph->append_op(impl::op_kind::Quantize,
                                    in_edges_t {in_edge(0, key_transpose2, 0)},
                                    "quantize_key");
                    auto dequantize_key
                            = pgraph->append_op(impl::op_kind::Dequantize,
                                    in_edges_t {in_edge(0, quantize_key, 0)},
                                    "dequantize_key");
                    auto matmul_qk = pgraph->append_op(impl::op_kind::MatMul,
                            in_edges_t {in_edge(0, dequantize_query, 0),
                                    in_edge(1, dequantize_key, 0)},
                            "matmul_qk");

                    auto fscore_scale = pgraph->append_op(impl::op_kind::Divide,
                            in_edges_t {in_edge(0, matmul_qk, 0)},
                            "fscore_scale");
                    auto fscore_add = pgraph->append_op(impl::op_kind::Add,
                            in_edges_t {in_edge(0, fscore_scale, 0)},
                            "fscore_add");
                    auto softmax = pgraph->append_op(impl::op_kind::SoftMax,
                            in_edges_t {in_edge(0, fscore_add, 0)}, "softmax");
                    auto quantize_softmax
                            = pgraph->append_op(impl::op_kind::Quantize,
                                    in_edges_t {in_edge(0, softmax, 0)},
                                    "quantize_softmax");
                    auto dequantize_softmax = pgraph->append_op(
                            impl::op_kind::Dequantize,
                            in_edges_t {in_edge(0, quantize_softmax, 0)},
                            "dequantize_softmax");

                    auto value_reshape = pgraph->append_op(
                            impl::op_kind::StaticReshape, "value_reshape");
                    auto value_transpose
                            = pgraph->append_op(impl::op_kind::StaticTranspose,
                                    in_edges_t {in_edge(0, value_reshape, 0)},
                                    "value_transpose");
                    auto quantize_value
                            = pgraph->append_op(impl::op_kind::Quantize,
                                    in_edges_t {in_edge(0, value_transpose, 0)},
                                    "quantize_value");
                    auto dequantize_value
                            = pgraph->append_op(impl::op_kind::Dequantize,
                                    in_edges_t {in_edge(0, quantize_value, 0)},
                                    "dequantize_value");
                    auto matmul_v = pgraph->append_op(impl::op_kind::MatMul,
                            in_edges_t {in_edge(0, dequantize_softmax, 0),
                                    in_edge(1, dequantize_value, 0)},
                            "matmul_v");
                    pgraph->append_op(impl::op_kind::StaticTranspose,
                            in_edges_t {in_edge(0, matmul_v, 0)},
                            "transpose_output");
                })
        .set_attr<FCreateV2FusedOp>(
                "FCreateV2FusedOp", []() -> std::shared_ptr<op_t> {
                    std::shared_ptr<op_t> fused_op
                            = std::make_shared<op_t>(op_kind::int8_MHA);
                    fused_op->set_attr<std::string>("backend", "dnnl");
                    return fused_op;
                });

DNNL_BACKEND_REGISTER_TRANSFORMATION_PASS(dnnl, f32_MHA_fusion)
        .set_priority(5.0f)
        .set_attr<FCreateV2Pattern>("FCreateV2Pattern",
                [](const std::shared_ptr<pb_graph_t> &pgraph) -> void {
                    auto query_reshape = pgraph->append_op(
                            impl::op_kind::StaticReshape, "query_reshape");
                    auto query_transpose
                            = pgraph->append_op(impl::op_kind::StaticTranspose,
                                    in_edges_t {in_edge(0, query_reshape, 0)},
                                    "query_transpose");

                    auto key_reshape = pgraph->append_op(
                            impl::op_kind::StaticReshape, "key_reshape");
                    auto key_transpose
                            = pgraph->append_op(impl::op_kind::StaticTranspose,
                                    in_edges_t {in_edge(0, key_reshape, 0)},
                                    "key_transpose");
                    auto key_transpose2
                            = pgraph->append_op(impl::op_kind::StaticTranspose,
                                    in_edges_t {in_edge(0, key_transpose, 0)},
                                    "key_transpose2");
                    auto matmul_qk = pgraph->append_op(impl::op_kind::MatMul,
                            in_edges_t {in_edge(0, query_transpose, 0),
                                    in_edge(1, key_transpose2, 0)},
                            "matmul_qk");

                    auto fscore_scale = pgraph->append_op(impl::op_kind::Divide,
                            in_edges_t {in_edge(0, matmul_qk, 0)},
                            "fscore_scale");
                    auto fscore_add = pgraph->append_op(impl::op_kind::Add,
                            in_edges_t {in_edge(0, fscore_scale, 0)},
                            "fscore_add");
                    auto softmax = pgraph->append_op(impl::op_kind::SoftMax,
                            in_edges_t {in_edge(0, fscore_add, 0)}, "softmax");

                    auto value_reshape = pgraph->append_op(
                            impl::op_kind::StaticReshape, "value_reshape");
                    auto value_transpose
                            = pgraph->append_op(impl::op_kind::StaticTranspose,
                                    in_edges_t {in_edge(0, value_reshape, 0)},
                                    "value_transpose");

                    auto matmul_v = pgraph->append_op(impl::op_kind::MatMul,
                            in_edges_t {in_edge(0, softmax, 0),
                                    in_edge(1, value_transpose, 0)},
                            "matmul_v");
                    pgraph->append_op(impl::op_kind::StaticTranspose,
                            in_edges_t {in_edge(0, matmul_v, 0)},
                            "transpose_output");
                })
        .set_attr<FCreateV2FusedOp>(
                "FCreateV2FusedOp", []() -> std::shared_ptr<op_t> {
                    std::shared_ptr<op_t> fused_op
                            = std::make_shared<op_t>(op_kind::f32_MHA);
                    fused_op->set_attr<std::string>("backend", "dnnl");
                    return fused_op;
                });

DNNL_BACKEND_REGISTER_TRANSFORMATION_PASS(dnnl, int8_bf16_MHA_fusion)
        .set_priority(5.0f)
        .set_attr<FCreateV2Pattern>("FCreateV2Pattern",
                [](const std::shared_ptr<pb_graph_t> &pgraph) -> void {
                    auto query_reshape = pgraph->append_op(
                            impl::op_kind::StaticReshape, "query_reshape");
                    auto query_transpose
                            = pgraph->append_op(impl::op_kind::StaticTranspose,
                                    in_edges_t {in_edge(0, query_reshape, 0)},
                                    "query_transpose");
                    auto bf16_to_f32_query
                            = pgraph->append_op(impl::op_kind::TypeCast,
                                    in_edges_t {in_edge(0, query_transpose, 0)},
                                    "bf16_to_f32_query");
                    auto quantize_query = pgraph->append_op(
                            impl::op_kind::Quantize,
                            in_edges_t {in_edge(0, bf16_to_f32_query, 0)},
                            "quantize_query");
                    auto dequantize_query
                            = pgraph->append_op(impl::op_kind::Dequantize,
                                    in_edges_t {in_edge(0, quantize_query, 0)},
                                    "dequantize_query");
                    auto f32_to_bf16_query = pgraph->append_op(
                            impl::op_kind::TypeCast,
                            in_edges_t {in_edge(0, dequantize_query, 0)},
                            "f32_to_bf16_query");

                    auto key_reshape = pgraph->append_op(
                            impl::op_kind::StaticReshape, "key_reshape");
                    auto key_transpose
                            = pgraph->append_op(impl::op_kind::StaticTranspose,
                                    in_edges_t {in_edge(0, key_reshape, 0)},
                                    "key_transpose");
                    auto key_transpose2
                            = pgraph->append_op(impl::op_kind::StaticTranspose,
                                    in_edges_t {in_edge(0, key_transpose, 0)},
                                    "key_transpose2");
                    auto bf16_to_f32_key
                            = pgraph->append_op(impl::op_kind::TypeCast,
                                    in_edges_t {in_edge(0, key_transpose2, 0)},
                                    "bf16_to_f32_key");
                    auto quantize_key
                            = pgraph->append_op(impl::op_kind::Quantize,
                                    in_edges_t {in_edge(0, bf16_to_f32_key, 0)},
                                    "quantize_key");
                    auto dequantize_key
                            = pgraph->append_op(impl::op_kind::Dequantize,
                                    in_edges_t {in_edge(0, quantize_key, 0)},
                                    "dequantize_key");
                    auto f32_to_bf16_key
                            = pgraph->append_op(impl::op_kind::TypeCast,
                                    in_edges_t {in_edge(0, dequantize_key, 0)},
                                    "f32_to_bf16_key");
                    auto matmul_qk = pgraph->append_op(impl::op_kind::MatMul,
                            in_edges_t {in_edge(0, f32_to_bf16_query, 0),
                                    in_edge(1, f32_to_bf16_key, 0)},
                            "matmul_qk");

                    auto fscore_scale = pgraph->append_op(impl::op_kind::Divide,
                            in_edges_t {in_edge(0, matmul_qk, 0)},
                            "fscore_scale");
                    auto fscore_add = pgraph->append_op(impl::op_kind::Add,
                            in_edges_t {in_edge(0, fscore_scale, 0)},
                            "fscore_add");
                    auto softmax = pgraph->append_op(impl::op_kind::SoftMax,
                            in_edges_t {in_edge(0, fscore_add, 0)}, "softmax");
                    auto bf16_to_f32_softmax
                            = pgraph->append_op(impl::op_kind::TypeCast,
                                    in_edges_t {in_edge(0, softmax, 0)},
                                    "bf16_to_f32_softmax");
                    auto quantize_softmax = pgraph->append_op(
                            impl::op_kind::Quantize,
                            in_edges_t {in_edge(0, bf16_to_f32_softmax, 0)},
                            "quantize_softmax");
                    auto dequantize_softmax = pgraph->append_op(
                            impl::op_kind::Dequantize,
                            in_edges_t {in_edge(0, quantize_softmax, 0)},
                            "dequantize_softmax");
                    auto f32_to_bf16_softmax = pgraph->append_op(
                            impl::op_kind::TypeCast,
                            in_edges_t {in_edge(0, dequantize_softmax, 0)},
                            "f32_to_bf16_softmax");

                    auto value_reshape = pgraph->append_op(
                            impl::op_kind::StaticReshape, "value_reshape");
                    auto value_transpose
                            = pgraph->append_op(impl::op_kind::StaticTranspose,
                                    in_edges_t {in_edge(0, value_reshape, 0)},
                                    "value_transpose");
                    auto bf16_to_f32_value
                            = pgraph->append_op(impl::op_kind::TypeCast,
                                    in_edges_t {in_edge(0, value_transpose, 0)},
                                    "bf16_to_f32_value");
                    auto quantize_value = pgraph->append_op(
                            impl::op_kind::Quantize,
                            in_edges_t {in_edge(0, bf16_to_f32_value, 0)},
                            "quantize_value");
                    auto dequantize_value
                            = pgraph->append_op(impl::op_kind::Dequantize,
                                    in_edges_t {in_edge(0, quantize_value, 0)},
                                    "dequantize_value");
                    auto f32_to_bf16_value = pgraph->append_op(
                            impl::op_kind::TypeCast,
                            in_edges_t {in_edge(0, dequantize_value, 0)},
                            "f32_to_bf16_value");
                    auto matmul_v = pgraph->append_op(impl::op_kind::MatMul,
                            in_edges_t {in_edge(0, f32_to_bf16_softmax, 0),
                                    in_edge(1, f32_to_bf16_value, 0)},
                            "matmul_v");
                    pgraph->append_op(impl::op_kind::StaticTranspose,
                            in_edges_t {in_edge(0, matmul_v, 0)},
                            "transpose_output");
                })
        .set_attr<FCreateV2FusedOp>(
                "FCreateV2FusedOp", []() -> std::shared_ptr<op_t> {
                    std::shared_ptr<op_t> fused_op
                            = std::make_shared<op_t>(op_kind::int8_MHA);
                    fused_op->set_attr<std::string>("backend", "dnnl");
                    return fused_op;
                });

DNNL_BACKEND_REGISTER_PASSES_DEF_END

} // namespace pass
} // namespace dnnl_impl
} // namespace impl
} // namespace graph
} // namespace dnnl