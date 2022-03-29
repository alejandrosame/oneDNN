/*******************************************************************************
* Copyright 2021-2022 Intel Corporation
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

using pattern = impl::pass::pattern;
using FCreatePattern = impl::pass::FCreatePattern;
using in_edges_t = pm::in_edges_t;
using pb_graph_t = pm::pb_graph_t;
using FCreateOptPattern = impl::pass::FCreateOptPattern;
using FCreateV2FusedOp = impl::pass::FCreateV2FusedOp;
using FCreateV2Pattern = impl::pass::FCreateV2Pattern;

/*!
 * \brief This provides eltwise-related fusion, i.e.
 *        relu-add fusion
 *        The process includes follow steps:
 *          1. look for fusion pattern on the graph
 *          2. If found, verify if this transformation is safe / correct
 *          3. replace the pattern with a fused op, update the graph
 */

DNNL_BACKEND_REGISTER_PASSES_DEF_BEGIN(eltwise_fusion)

DNNL_BACKEND_REGISTER_TRANSFORMATION_PASS(dnnl, eltwise_binary_fusion)
        .set_priority(8.2f)
        .set_attr<FCreateV2Pattern>("FCreateV2Pattern",
                [](const std::shared_ptr<pb_graph_t> &pgraph) -> void {
                    pm::pb_op *peltwise = pgraph->append_alternation(
                            {impl::op_kind::Abs, impl::op_kind::Clamp,
                                    impl::op_kind::Elu, impl::op_kind::Exp,
                                    impl::op_kind::GELU,
                                    impl::op_kind::HardTanh,
                                    impl::op_kind::HardSwish,
                                    impl::op_kind::Log, impl::op_kind::Sigmoid,
                                    impl::op_kind::SoftPlus, impl::op_kind::Pow,
                                    impl::op_kind::ReLU, impl::op_kind::Round,
                                    impl::op_kind::Sqrt, impl::op_kind::Square,
                                    impl::op_kind::Tanh},
                            "peltwise");
                    pgraph->append_alternation(
                            {impl::op_kind::Add, impl::op_kind::Multiply,
                                    impl::op_kind::Maximum,
                                    impl::op_kind::Minimum,
                                    impl::op_kind::Divide,
                                    impl::op_kind::Subtract},
                            {in_edge(0, peltwise, 0)}, "pbinary");
                })
        .set_attr<FCreateV2FusedOp>(
                "FCreateV2FusedOp", []() -> std::shared_ptr<op_t> {
                    std::shared_ptr<op_t> fused_op
                            = std::make_shared<op_t>(op_kind::eltwise_binary);
                    fused_op->set_attr<std::string>("backend", "dnnl");
                    return fused_op;
                });

DNNL_BACKEND_REGISTER_TRANSFORMATION_PASS(dnnl, chained_relu_fusion)
        .set_priority(5.0f)
        .set_attr<FCreateV2Pattern>("FCreateV2Pattern",
                [](const std::shared_ptr<pb_graph_t> &pgraph) -> void {
                    auto chained_relu = std::make_shared<pb_graph_t>();
                    pm::pb_op *relu
                            = chained_relu->append_op(impl::op_kind::ReLU);
                    chained_relu->create_input_port(0, relu, 0);
                    chained_relu->create_output_port(0, relu, 0);

                    pgraph->append_repetition(
                            chained_relu, {0, 0}, 1, MAX_REPETITION);
                })
        .set_attr<FCreateV2FusedOp>(
                "FCreateV2FusedOp", []() -> std::shared_ptr<op_t> {
                    std::shared_ptr<op_t> fused_op
                            = std::make_shared<op_t>(op_kind::chained_relu);
                    fused_op->set_attr<std::string>("backend", "dnnl");
                    return fused_op;
                });

DNNL_BACKEND_REGISTER_TRANSFORMATION_PASS(dnnl, int8_relu_fusion)
        .set_priority(9.9f)
        .set_attr<FCreatePattern>("FCreatePattern",
                [](pattern *apattern) -> void {
                    op_t *dequant_data
                            = apattern->create_op(impl::op_kind::Dequantize);
                    op_t *relu = apattern->create_op(impl::op_kind::ReLU);
                    op_t *quant = apattern->create_op(impl::op_kind::Quantize);
                    relu->fill_and_connect_input(0, *dequant_data, 0);
                    quant->fill_and_connect_input(0, *relu, 0);
                })
        .set_attr<FCreateOptPattern>(
                "FCreateOptPattern", [](pattern *optimized_pattern) -> void {
                    op_t *fused_op
                            = optimized_pattern->create_op(op_kind::int8_relu);
                    fused_op->set_attr<std::string>("backend", "dnnl");
                });

DNNL_BACKEND_REGISTER_TRANSFORMATION_PASS(dnnl, int8_relu_add_fusion)
        .set_priority(10.0f)
        .set_attr<FCreatePattern>("FCreatePattern",
                [](pattern *apattern) -> void {
                    op_t *dequant_data
                            = apattern->create_op(impl::op_kind::Dequantize);
                    op_t *dequant_other
                            = apattern->create_op(impl::op_kind::Dequantize);
                    op_t *relu = apattern->create_op(impl::op_kind::ReLU);
                    op_t *add = apattern->create_op(impl::op_kind::Add);
                    op_t *quant = apattern->create_op(impl::op_kind::Quantize);
                    relu->fill_and_connect_input(0, *dequant_data, 0);
                    add->fill_and_connect_input(0, *relu, 0);
                    add->fill_and_connect_input(1, *dequant_other, 0);
                    quant->fill_and_connect_input(0, *add, 0);
                })
        .set_attr<FCreateOptPattern>(
                "FCreateOptPattern", [](pattern *optimized_pattern) -> void {
                    op_t *fused_op = optimized_pattern->create_op(
                            op_kind::int8_relu_add);
                    fused_op->set_attr<std::string>("backend", "dnnl");
                });

DNNL_BACKEND_REGISTER_PASSES_DEF_END

} // namespace pass
} // namespace dnnl_impl
} // namespace impl
} // namespace graph
} // namespace dnnl