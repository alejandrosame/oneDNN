/*******************************************************************************
* Copyright 2020-2021 Intel Corporation
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

#include <gtest/gtest.h>

#include "interface/c_types_map.hpp"
#include "interface/graph.hpp"
#include "interface/op_def.hpp"
#include "interface/op_schema.hpp"

#include "cpp/unit/utils.hpp"

using namespace dnnl::graph::impl;
using namespace dnnl::graph::tests::unit::utils;

#ifndef NDEBUG

TEST(OpSchema, DuplicateAttribute) {
    EXPECT_DEATH(op_schema_t()
                         .set_attr("kernel", "size of each filter", true,
                                 attribute_kind::b)
                         .set_attr("kernel", "size of each filter", true,
                                 attribute_kind::b),
            "provided attribute has already been set");
}

TEST(OpSchema, DuplicatedInput) {
    EXPECT_DEATH(op_schema_t()
                         .set_num_inputs(5)
                         .set_input(3, "mean", "value for mean normalization")
                         .set_input(3, "mean", "value for mean normalization"),
            "provided `in_offset` has already been set");
}

TEST(OpSchema, DuplicatedOutput) {
    EXPECT_DEATH(op_schema_t()
                         .set_num_outputs(1)
                         .set_output(0, "output", "output tensor")
                         .set_output(0, "output", "output tensor"),
            "provided `out_offset` has already been set");
}

TEST(OpSchema, SetInputBeforeSetNumInputs) {
    EXPECT_DEATH(op_schema_t()
                         .set_input(0, "a", "first input tensor")
                         .set_num_inputs(2),
            "input set before setting num_inputs_");
}

TEST(OpSchema, SetOutputBeforeSetNumOutputs) {
    EXPECT_DEATH(op_schema_t()
                         .set_output(0, "output", "output tensor")
                         .set_num_outputs(1),
            "output set before setting num_outputs_");
}

TEST(OpSchema, ExceededNumInputs) {
    EXPECT_DEATH(op_schema_t()
                         .set_num_inputs(1)
                         .set_input(0, "a", "first input tensor")
                         .set_input(1, "b", "second input tensor"),
            "input offset exceeds declared num of inputs");
}

TEST(OpSchema, ExceededNumOutputs) {
    EXPECT_DEATH(op_schema_t()
                         .set_num_outputs(1)
                         .set_output(0, "a", "first output tensor")
                         .set_output(1, "b", "second output tensor"),
            "output offset exceeds declared num of outputs");
}

#endif

TEST(OpSchema, Convolution) {
    const op_kind_t op_kind_ = op_kind::Convolution;
    const std::set<size_t> expected_in_sizes = {2, 3};
    const size_t expected_out_size = 1;
    const size_t expected_attr_size = 8;
    const std::map<std::string, bool> attrs_data
            = {{"strides", true}, {"pads_begin", true}, {"pads_end", true},
                    {"dilations", true}, {"auto_pad", false}, {"groups", false},
                    {"data_format", false}, {"filter_format", false}};
    for (auto expected_in_size : expected_in_sizes) {
        verify_op_schema(op_kind_, expected_in_size, expected_out_size,
                expected_attr_size, attrs_data);
    }
}

TEST(OpSchema, ConvTranspose) {
    const op_kind_t op_kind_ = op_kind::ConvTranspose;
    const std::set<size_t> expected_in_sizes = {2, 3};
    const size_t expected_out_size = 1;
    const size_t expected_attr_size = 9;
    const std::map<std::string, bool> attrs_data = {{"strides", true},
            {"pads_begin", true}, {"pads_end", true}, {"dilations", true},
            {"auto_pad", false}, {"groups", false}, {"data_format", false},
            {"filter_format", false}, {"output_padding", false}};
    for (auto expected_in_size : expected_in_sizes) {
        verify_op_schema(op_kind_, expected_in_size, expected_out_size,
                expected_attr_size, attrs_data);
    }
}

TEST(OpSchema, InferConvtransposeBiasAutoPad) {
    const op_schema_t *a_op_schema
            = op_schema_registry_t::get_op_schema(op_kind::ConvTranspose);
    EXPECT_TRUE(nullptr != a_op_schema);
    op_t a_op {op_kind::ConvTranspose, op_t::kind2str(op_kind::ConvTranspose)};
    std::vector<int64_t> strides = {1, 1};
    std::vector<int64_t> pads_begin = {}; // empty pads_begin
    std::vector<int64_t> pads_end = {}; // empty pads_end
    std::vector<int64_t> dilations = {1, 1};
    std::string data_format = "NCX";
    std::string filter_format = "OIX";
    int64_t groups = 1;
    // according to the convtranspose semantic, output_padding is bigger
    // than 0 only if stride is greater than 1.
    std::vector<int64_t> output_padding = {0, 0};

    set_convtranspose_common_attr(a_op, strides, pads_begin, pads_end,
            dilations, "SAME_UPPER", data_format, filter_format, groups,
            output_padding);

    auto lt_data = logical_tensor_init(0, {1, 1, 5, 5}, data_type::f32);
    auto lt_weight = logical_tensor_init(1, {1, 1, 3, 3}, data_type::f32);
    auto lt_o = logical_tensor_init(2, data_type::f32, layout_type::strided);
    std::vector<logical_tensor_t *> lt_in {&lt_data, &lt_weight};
    std::vector<logical_tensor_t *> lt_out {&lt_o};
    a_op_schema->shape_infer(&a_op, lt_in, lt_out);

    const auto infered_out_shape = logical_tensor_wrapper_t(lt_o).vdims();
    const std::vector<int64_t> expected_out_shape {1, 1, 5, 5};
    ASSERT_EQ(infered_out_shape, expected_out_shape);
}

TEST(OpSchema, InferConvtransposeBiasOutputShape) {
    const op_schema_t *a_op_schema
            = op_schema_registry_t::get_op_schema(op_kind::ConvTranspose);
    EXPECT_TRUE(nullptr != a_op_schema);
    op_t a_op {op_kind::ConvTranspose, op_t::kind2str(op_kind::ConvTranspose)};
    std::vector<int64_t> strides = {1, 1};
    std::vector<int64_t> pads_begin = {}; // empty pads_begin
    std::vector<int64_t> pads_end = {}; // empty pads_end
    std::vector<int64_t> dilations = {1, 1};
    std::string data_format = "NCX";
    std::string filter_format = "OIX";
    int64_t groups = 1;
    std::vector<int64_t> output_padding = {0, 0};

    set_convtranspose_common_attr(a_op, strides, pads_begin, pads_end,
            dilations, "SAME_UPPER", data_format, filter_format, groups,
            output_padding);

    auto lt_data = logical_tensor_init(0, {1, 1, 5, 5}, data_type::f32);
    auto lt_weight = logical_tensor_init(1, {1, 1, 3, 3}, data_type::f32);
    auto lt_o = logical_tensor_init(
            2, {1, 1, 5, 5}, data_type::f32, layout_type::strided);
    std::vector<logical_tensor_t *> lt_in {&lt_data, &lt_weight};
    std::vector<logical_tensor_t *> lt_out {&lt_o};
    a_op_schema->shape_infer(&a_op, lt_in, lt_out);

    pads_begin = a_op.get_attr<std::vector<int64_t>>("pads_begin");
    pads_end = a_op.get_attr<std::vector<int64_t>>("pads_end");
    std::vector<int64_t> expected_pads_begin = {1, 1};
    std::vector<int64_t> expected_pads_end = {1, 1};
    EXPECT_EQ(pads_begin, expected_pads_begin);
    EXPECT_EQ(pads_end, expected_pads_end);
}

TEST(OpSchema, GenerateDefaultAttribute) {
    const op_schema_t *matmul_op_schema
            = op_schema_registry_t::get_op_schema(op_kind::MatMul);
    op_t matmul_op {0, kMatMul, std::string("matmul")};
    logical_tensor_t lt_data_a = logical_tensor_init(0, data_type::f32);
    logical_tensor_t lt_data_b = logical_tensor_init(1, data_type::f32);
    logical_tensor_t lt_out = logical_tensor_init(2, data_type::f32);

    matmul_op.add_input(lt_data_a);
    matmul_op.add_input(lt_data_b);
    matmul_op.add_output(lt_out);
    EXPECT_TRUE(matmul_op_schema->verify(&matmul_op));

    logical_tensor_t lt_bias = logical_tensor_init(3, data_type::f32);
    matmul_op.add_input(lt_bias);
    EXPECT_TRUE(matmul_op_schema->verify(&matmul_op));

    matmul_op.set_attr("transpose_a", true);
    const bool *flag;
    const bool **ret_flag = &flag;
    matmul_op.get_attr<bool>("transpose_a", ret_flag);
    EXPECT_TRUE(ret_flag);
    EXPECT_EQ(matmul_op.get_attr<bool>("transpose_b", ret_flag),
            status::invalid_argument);

    graph_t agraph;
    ASSERT_EQ(agraph.add_op(&matmul_op), status::success);
    agraph.build_graph();
    ASSERT_EQ(agraph.num_ops(), 1);

    const auto &graph_matmul_op = agraph.get_ops()[0];
    EXPECT_TRUE(graph_matmul_op->get_attr<bool>("transpose_a"));
    EXPECT_FALSE(graph_matmul_op->get_attr<bool>("transpose_b"));
}

TEST(OpSchema, TestVerifyFunction) {
    const op_schema_t *conv_op_schema
            = op_schema_registry_t::get_op_schema(op_kind::Convolution);

    op_t conv_op {0, kConvolution, std::string("convolution")};
    logical_tensor_t lt_data = logical_tensor_init(0, data_type::f32);
    logical_tensor_t lt_weight = logical_tensor_init(1, data_type::f32);
    logical_tensor_t lt_out = logical_tensor_init(2, data_type::f32);

    conv_op.add_input(lt_data);
    conv_op.add_input(lt_weight);
    EXPECT_FALSE(conv_op_schema->verify(&conv_op));

    conv_op.add_output(lt_out);
    EXPECT_FALSE(conv_op_schema->verify(&conv_op));

    std::vector<int64_t> strides = {2, 2};
    std::vector<int64_t> pads_begin = {1, 1};
    std::vector<int64_t> pads_end = {2, 2};
    std::vector<int64_t> dilations = {1, 1};
    std::string data_format = "NCX";
    std::string filter_format = "OIX";
    conv_op.set_attr("strides", strides);
    conv_op.set_attr("pads_begin", pads_begin);
    conv_op.set_attr("pads_end", pads_end);
    conv_op.set_attr("dilations", dilations);
    conv_op.set_attr("data_format", data_format);
    conv_op.set_attr("filter_format", filter_format);

    EXPECT_TRUE(conv_op_schema->verify(&conv_op));

    conv_op.set_attr("auto_pad", false);
    EXPECT_FALSE(conv_op_schema->verify(&conv_op));

    std::string auto_pad = "VALID";
    conv_op.set_attr("auto_pad", auto_pad);

    EXPECT_TRUE(conv_op_schema->verify(&conv_op));

    float arbitrary_value = 123.0;
    const std::string arbitrary_name {"arbitrary"};
    conv_op.set_attr("arbitrary", arbitrary_value);

    EXPECT_TRUE(conv_op_schema->verify(&conv_op));
}

TEST(OpSchema, InferConvOutputShape) {
    const op_kind_t op_kind_ = op_kind::Convolution;

    std::string data_format = "NCX";
    std::string filter_format = "OIX";

    std::vector<int64_t> groups_vec {1, 2, 4};

    for (auto groups : groups_vec) {
        // data shape {N, IC, H, W}
        const std::vector<int64_t> &in_data = {1, 32, 224, 224};
        // weight shape {OC, IC, KH, KW}
        const std::vector<int64_t> &in_weight = {16, 32 / groups, 3, 3};
        const std::vector<int64_t> &expected_out_shape = {1, 16, 111, 111};

        verify_shape_infer_for_conv(op_kind_, data_format, filter_format,
                groups, in_data, in_weight, expected_out_shape);

        const std::vector<int64_t> &in_bias = {16};
        verify_shape_infer_for_conv(op_kind_, data_format, filter_format,
                groups, in_data, in_weight, in_bias, expected_out_shape);
    }
}

TEST(OpSchema, InferConvtransposeOutputShape) {
    const op_kind_t op_kind_ = op_kind::ConvTranspose;

    std::string data_format = "NCX";
    std::string filter_format = "OIX";
    std::vector<int64_t> groups_vec {1, 2, 4};

    // data shape {N, IC, H, W}
    const std::vector<int64_t> &in_data = {1, 16, 111, 111};
    const std::vector<int64_t> &expected_out_shape = {1, 32, 224, 224};
    for (auto groups : groups_vec) {
        // weight shape {OC, IC, KH, KW}
        const std::vector<int64_t> &in_weight = {32 / groups, 16, 3, 3};

        verify_shape_infer_for_convtranspose(op_kind_, data_format,
                filter_format, groups, in_data, in_weight, expected_out_shape);
    }
}

TEST(OpSchema, InferConv3dOutputShape) {
    const op_kind_t op_kind_ = op_kind::Convolution;

    std::string data_format = "NCX";
    std::string filter_format = "OIX";

    std::vector<int64_t> groups_vec {1, 2, 4};

    for (auto groups : groups_vec) {
        // data shape {N, IC, D, H, W}
        const std::vector<int64_t> &in_data = {1, 32, 224, 224, 224};
        // weight shape {OC, IC, KD, KH, KW}
        const std::vector<int64_t> &in_weight = {16, 32 / groups, 3, 3, 3};
        const std::vector<int64_t> &expected_out_shape = {1, 16, 111, 111, 111};

        verify_shape_infer_for_conv(op_kind_, data_format, filter_format,
                groups, in_data, in_weight, expected_out_shape);

        const std::vector<int64_t> &in_bias = {16};
        verify_shape_infer_for_conv(op_kind_, data_format, filter_format,
                groups, in_data, in_weight, in_bias, expected_out_shape);
    }
}

TEST(OpSchema, InferConvtransposeOutputShapeWithNxcFormat) {
    const op_kind_t op_kind_ = op_kind::ConvTranspose;

    std::string data_format = "NXC";
    std::string filter_format = "OIX";
    std::vector<int64_t> groups_vec {1, 2, 4};

    // data shape {N, H, W, IC}
    const std::vector<int64_t> &in_data = {1, 111, 111, 16};
    const std::vector<int64_t> &expected_out_shape = {1, 224, 224, 32};
    for (auto groups : groups_vec) {
        // weight shape {OC, IC, KH, KW}
        const std::vector<int64_t> &in_weight = {32 / groups, 16, 3, 3};

        verify_shape_infer_for_convtranspose(op_kind_, data_format,
                filter_format, groups, in_data, in_weight, expected_out_shape);
    }
}

TEST(OpSchema, InferConvOutputShapeWithNxcFormat) {
    const op_kind_t op_kind_ = op_kind::Convolution;

    std::string data_format = "NXC";
    std::string filter_format = "OIX";

    std::vector<int64_t> groups_vec {1, 2, 4};

    for (auto groups : groups_vec) {
        // data shape {N, H, W, IC}
        const std::vector<int64_t> &in_data = {1, 224, 224, 32};
        // weight shape {OC, IC, KH, KW}
        const std::vector<int64_t> &in_weight = {16, 32 / groups, 3, 3};
        const std::vector<int64_t> &expected_out_shape = {1, 111, 111, 16};

        verify_shape_infer_for_conv(op_kind_, data_format, filter_format,
                groups, in_data, in_weight, expected_out_shape);

        const std::vector<int64_t> &in_bias = {16};
        verify_shape_infer_for_conv(op_kind_, data_format, filter_format,
                groups, in_data, in_weight, in_bias, expected_out_shape);
    }
}

TEST(OpSchema, InferConv3dOutputShapeWithNxcFormat) {
    const op_kind_t op_kind_ = op_kind::Convolution;

    std::string data_format = "NXC";
    std::string filter_format = "OIX";

    std::vector<int64_t> groups_vec {1, 2, 4};

    for (auto groups : groups_vec) {
        // data shape {N, D, H, W, IC}
        const std::vector<int64_t> &in_data = {1, 224, 224, 224, 32};
        // weight shape {OC, IC, KD, KH, KW}
        const std::vector<int64_t> &in_weight = {16, 32 / groups, 3, 3, 3};
        const std::vector<int64_t> &expected_out_shape = {1, 111, 111, 111, 16};

        verify_shape_infer_for_conv(op_kind_, data_format, filter_format,
                groups, in_data, in_weight, expected_out_shape);

        const std::vector<int64_t> &in_bias = {16};
        verify_shape_infer_for_conv(op_kind_, data_format, filter_format,
                groups, in_data, in_weight, in_bias, expected_out_shape);
    }
}

TEST(OpSchema, InferConvOutputShapeWithNxcXioFormat) {
    const op_kind_t op_kind_ = op_kind::Convolution;

    std::string data_format = "NXC";
    std::string filter_format = "XIO";

    std::vector<int64_t> groups_vec {1, 2, 4};

    for (auto groups : groups_vec) {
        // data shape {N, H, W, IC}
        const std::vector<int64_t> &in_data = {1, 224, 224, 32};
        // weight shape {KH, KW, IC, OC}
        const std::vector<int64_t> &in_weight = {3, 3, 32 / groups, 16};
        const std::vector<int64_t> &expected_out_shape = {1, 111, 111, 16};

        verify_shape_infer_for_conv(op_kind_, data_format, filter_format,
                groups, in_data, in_weight, expected_out_shape);

        const std::vector<int64_t> &in_bias = {16};
        verify_shape_infer_for_conv(op_kind_, data_format, filter_format,
                groups, in_data, in_weight, in_bias, expected_out_shape);
    }
}

TEST(OpSchema, InferConvtransposeOutputShapeWithNxcXioFormat) {
    const op_kind_t op_kind_ = op_kind::ConvTranspose;

    std::string data_format = "NXC";
    std::string filter_format = "XIO";
    std::vector<int64_t> groups_vec {1, 2, 4};

    // data shape {N, H, W, IC}
    const std::vector<int64_t> &in_data = {1, 111, 111, 16};
    const std::vector<int64_t> &expected_out_shape = {1, 224, 224, 32};
    for (auto groups : groups_vec) {
        // weight shape {KH, KW, IC, OC}
        const std::vector<int64_t> &in_weight = {3, 3, 16, 32 / groups};

        verify_shape_infer_for_convtranspose(op_kind_, data_format,
                filter_format, groups, in_data, in_weight, expected_out_shape);
    }
}

TEST(OpSchema, InferConv3dOutputShapeWithNxcXioFormat) {
    const op_kind_t op_kind_ = op_kind::Convolution;

    std::string data_format = "NXC";
    std::string filter_format = "XIO";

    std::vector<int64_t> groups_vec {1, 2, 4};

    for (auto groups : groups_vec) {
        // data shape {N, D, H, W, IC}
        const std::vector<int64_t> &in_data = {1, 224, 224, 224, 32};
        // weight shape {KD, KH, KW, IC, OC}
        const std::vector<int64_t> &in_weight = {3, 3, 3, 32 / groups, 16};
        const std::vector<int64_t> &expected_out_shape = {1, 111, 111, 111, 16};

        verify_shape_infer_for_conv(op_kind_, data_format, filter_format,
                groups, in_data, in_weight, expected_out_shape);

        const std::vector<int64_t> &in_bias = {16};
        verify_shape_infer_for_conv(op_kind_, data_format, filter_format,
                groups, in_data, in_weight, in_bias, expected_out_shape);
    }
}

TEST(OpSchema, InferConvOutputShapeWithXioFormat) {
    const op_kind_t op_kind_ = op_kind::Convolution;

    std::string data_format = "NCX";
    std::string filter_format = "XIO";

    std::vector<int64_t> groups_vec {1, 2, 4};

    for (auto groups : groups_vec) {
        // data shape {N, IC, H, W}
        const std::vector<int64_t> &in_data = {1, 32, 224, 224};
        // weight shape {KH, KW, IC, OC}
        const std::vector<int64_t> &in_weight = {3, 3, 32 / groups, 16};
        const std::vector<int64_t> &expected_out_shape = {1, 16, 111, 111};

        verify_shape_infer_for_conv(op_kind_, data_format, filter_format,
                groups, in_data, in_weight, expected_out_shape);

        const std::vector<int64_t> &in_bias = {16};
        verify_shape_infer_for_conv(op_kind_, data_format, filter_format,
                groups, in_data, in_weight, in_bias, expected_out_shape);
    }
}

TEST(OpSchema, InferConvtransposeOutputShapeWithXioFormat) {
    const op_kind_t op_kind_ = op_kind::ConvTranspose;

    std::string data_format = "NCX";
    std::string filter_format = "XIO";
    std::vector<int64_t> groups_vec {1, 2, 4};

    // data shape {N, IC, H, W}
    const std::vector<int64_t> &in_data = {1, 16, 111, 111};
    const std::vector<int64_t> &expected_out_shape = {1, 32, 224, 224};
    for (auto groups : groups_vec) {
        // weight shape {KH, KW, IC, OC}
        const std::vector<int64_t> &in_weight = {3, 3, 16, 32 / groups};

        verify_shape_infer_for_convtranspose(op_kind_, data_format,
                filter_format, groups, in_data, in_weight, expected_out_shape);
    }
}

TEST(OpSchema, InferConv3dOutputShapeWithXioFormat) {
    const op_kind_t op_kind_ = op_kind::Convolution;

    std::string data_format = "NCX";
    std::string filter_format = "XIO";

    std::vector<int64_t> groups_vec {1, 2, 4};

    for (auto groups : groups_vec) {
        // data shape {N, IC, D, H, W}
        const std::vector<int64_t> &in_data = {1, 32, 224, 224, 224};
        // weight shape {KD, KH, KW, IC, OC}
        const std::vector<int64_t> &in_weight = {3, 3, 3, 32 / groups, 16};
        const std::vector<int64_t> &expected_out_shape = {1, 16, 111, 111, 111};

        verify_shape_infer_for_conv(op_kind_, data_format, filter_format,
                groups, in_data, in_weight, expected_out_shape);

        const std::vector<int64_t> &in_bias = {16};
        verify_shape_infer_for_conv(op_kind_, data_format, filter_format,
                groups, in_data, in_weight, in_bias, expected_out_shape);
    }
}

TEST(OpSchema, PowBackpropExponent) {
    const op_kind_t op_kind_ = op_kind::PowBackpropExponent;
    const size_t expected_in_size = 4;
    const size_t expected_out_size = 1;
    const size_t expected_attr_size = 0;
    // clang3 requires user-provided default constructor
    const std::map<std::string, bool> attrs_data = {};

    verify_op_schema(op_kind_, expected_in_size, expected_out_size,
            expected_attr_size, attrs_data);
}

TEST(OpSchema, InferExponentOutputShape) {
    const op_schema_t *exponent_op_schema
            = op_schema_registry_t::get_op_schema(op_kind::PowBackpropExponent);

    op_t exponent_op {op_kind::PowBackpropExponent,
            op_t::kind2str(op_kind::PowBackpropExponent)};

    logical_tensor_t lt_in1
            = logical_tensor_init(0, {64, 1024, 64}, data_type::f32);
    logical_tensor_t lt_in2
            = logical_tensor_init(1, {64, 1024, 64}, data_type::f32);
    logical_tensor_t lt_in3
            = logical_tensor_init(2, {64, 1024, 64}, data_type::f32);
    logical_tensor_t lt_in4 = logical_tensor_init(3, {64}, data_type::f32);
    std::vector<logical_tensor_t *> lt_in {&lt_in1, &lt_in2, &lt_in3, &lt_in4};
    logical_tensor_t lt_o1
            = logical_tensor_init(4, data_type::f32, layout_type::strided);
    std::vector<logical_tensor_t *> lt_out1 {&lt_o1};

    exponent_op_schema->shape_infer(&exponent_op, lt_in, lt_out1);

    const std::vector<int64_t> infered_out_shape1
            = logical_tensor_wrapper_t(lt_o1).vdims();
    const std::vector<int64_t> expected_out_shape1 = {64};
    EXPECT_EQ(infered_out_shape1, expected_out_shape1);

    const std::vector<int64_t> infered_out_strides1
            = logical_tensor_wrapper_t(lt_o1).vstrides();
    const std::vector<int64_t> expected_out_strides1
            = compute_dense_strides(expected_out_shape1);
    EXPECT_EQ(infered_out_strides1, expected_out_strides1);
}

TEST(OpSchema, MaxPool) {
    const op_kind_t op_kind_ = op_kind::MaxPool;
    const size_t expected_in_size = 1;
    const size_t expected_out_size = 1;
    const size_t expected_attr_size = 8;
    const std::map<std::string, bool> attrs_data = {{"strides", true},
            {"kernel", true}, {"pads_begin", true}, {"pads_end", true},
            {"dilations", false}, {"data_format", false}, {"auto_pad", false},
            {"rounding_type", false}};

    verify_op_schema(op_kind_, expected_in_size, expected_out_size,
            expected_attr_size, attrs_data);
}

TEST(OpSchema, InferMaxpoolOutputShape) {
    const op_schema_t *pool_op_schema
            = op_schema_registry_t::get_op_schema(op_kind::MaxPool);

    op_t pool_op {op_kind::MaxPool, op_t::kind2str(op_kind::MaxPool)};
    std::vector<int64_t> strides = {2, 2};
    std::vector<int64_t> kernel = {3, 3};
    std::vector<int64_t> pads_begin = {1, 1};
    std::vector<int64_t> pads_end = {2, 2};
    std::vector<int64_t> dilations = {1, 1};
    std::string auto_pad = "SAME_UPPER";
    std::string data_format = "NCX";

    pool_op.set_attr("strides", strides);
    pool_op.set_attr("pads_begin", pads_begin);
    pool_op.set_attr("pads_end", pads_end);
    pool_op.set_attr("kernel", kernel);
    pool_op.set_attr("dilations", dilations);
    pool_op.set_attr("auto_pad", auto_pad);
    pool_op.set_attr("data_format", data_format);

    logical_tensor_t lt_data
            = logical_tensor_init(0, {1, 3, 224, 224}, data_type::f32);
    std::vector<logical_tensor_t *> lt_in {&lt_data};
    logical_tensor_t lt_o
            = logical_tensor_init(2, data_type::f32, layout_type::strided);
    std::vector<logical_tensor_t *> lt_out {&lt_o};

    // if output shape is unknown, infer output shape
    pool_op_schema->shape_infer(&pool_op, lt_in, lt_out);
    const std::vector<int64_t> infered_out_shape
            = logical_tensor_wrapper_t(lt_o).vdims();
    const std::vector<int64_t> expected_out_shape = {1, 3, 112, 112};
    EXPECT_EQ(infered_out_shape, expected_out_shape);

    const std::vector<int64_t> infered_out_strides
            = logical_tensor_wrapper_t(lt_o).vstrides();
    const std::vector<int64_t> expected_out_strides
            = compute_dense_strides(expected_out_shape);
    EXPECT_EQ(infered_out_strides, expected_out_strides);

    // if output shape is known, infer auto pad
    pool_op_schema->shape_infer(&pool_op, lt_in, lt_out);
    auto infered_pads_begin
            = pool_op.get_attr<std::vector<int64_t>>("pads_begin");
    auto infered_pads_end = pool_op.get_attr<std::vector<int64_t>>("pads_end");
    const std::vector<int64_t> expected_pads_begin = {0, 0};
    const std::vector<int64_t> expected_pads_end = {1, 1};
    EXPECT_EQ(infered_pads_begin, expected_pads_begin);
    EXPECT_EQ(infered_pads_end, expected_pads_end);
}

TEST(OpSchema, InferMaxpoolOutputShapeWithCeilMode) {
    const op_schema_t *pool_op_schema
            = op_schema_registry_t::get_op_schema(op_kind::MaxPool);

    std::set<std::string> rounding_types = {"ceil", "floor"};
    for (auto &rounding_type : rounding_types) {
        op_t pool_op {op_kind::MaxPool, op_t::kind2str(op_kind::MaxPool)};
        std::vector<int64_t> strides = {2, 2};
        std::vector<int64_t> kernel = {3, 3};
        std::vector<int64_t> pads_begin = {0, 0};
        std::vector<int64_t> pads_end = {0, 0};
        std::vector<int64_t> dilations = {1, 1};
        std::string data_format = "NCX";
        pool_op.set_attr("strides", strides);
        pool_op.set_attr("pads_begin", pads_begin);
        pool_op.set_attr("pads_end", pads_end);
        pool_op.set_attr("kernel", kernel);
        pool_op.set_attr("dilations", dilations);
        pool_op.set_attr("data_format", data_format);
        pool_op.set_attr("rounding_type", rounding_type);

        logical_tensor_t lt_data
                = logical_tensor_init(0, {1, 3, 224, 224}, data_type::f32);
        std::vector<logical_tensor_t *> lt_in {&lt_data};
        logical_tensor_t lt_o
                = logical_tensor_init(2, data_type::f32, layout_type::strided);
        std::vector<logical_tensor_t *> lt_out {&lt_o};

        // if output shape is unknown, infer output shape
        pool_op_schema->shape_infer(&pool_op, lt_in, lt_out);
        const std::vector<int64_t> infered_out_shape
                = logical_tensor_wrapper_t(lt_o).vdims();
        const std::vector<int64_t> infered_out_strides
                = logical_tensor_wrapper_t(lt_o).vstrides();
        if (rounding_type == "ceil") {
            const std::vector<int64_t> expected_out_shape = {1, 3, 112, 112};
            EXPECT_EQ(infered_out_shape, expected_out_shape);
            const std::vector<int64_t> expected_out_strides
                    = compute_dense_strides(expected_out_shape);
            EXPECT_EQ(infered_out_strides, expected_out_strides);
        } else { // rounding_type = floor
            const std::vector<int64_t> expected_out_shape = {1, 3, 111, 111};
            EXPECT_EQ(infered_out_shape, expected_out_shape);
            const std::vector<int64_t> expected_out_strides
                    = compute_dense_strides(expected_out_shape);
            EXPECT_EQ(infered_out_strides, expected_out_strides);
        }
    }
}

TEST(OpSchema, InferMatmulOutputShape) {
    const op_schema_t *matmul_op_schema
            = op_schema_registry_t::get_op_schema(op_kind::MatMul);

    op_t matmul_op {op_kind::MatMul, op_t::kind2str(op_kind::MatMul)};
    bool transpose_a = true;
    matmul_op.set_attr("transpose_a", transpose_a);

    // test 2 dims matmul
    logical_tensor_t lt_in1
            = logical_tensor_init(0, {1024, 64}, data_type::f32);
    logical_tensor_t lt_in2
            = logical_tensor_init(1, {1024, 1000}, data_type::f32);
    std::vector<logical_tensor_t *> lt_in {&lt_in1, &lt_in2};
    logical_tensor_t lt_o1
            = logical_tensor_init(2, data_type::f32, layout_type::strided);
    std::vector<logical_tensor_t *> lt_out1 {&lt_o1};

    matmul_op_schema->shape_infer(&matmul_op, lt_in, lt_out1);

    const std::vector<int64_t> infered_out_shape1
            = logical_tensor_wrapper_t(lt_o1).vdims();
    const std::vector<int64_t> expected_out_shape1 = {64, 1000};
    EXPECT_EQ(infered_out_shape1, expected_out_shape1);

    const std::vector<int64_t> infered_out_strides1
            = logical_tensor_wrapper_t(lt_o1).vstrides();
    const std::vector<int64_t> expected_out_strides1
            = compute_dense_strides(expected_out_shape1);
    EXPECT_EQ(infered_out_strides1, expected_out_strides1);

    // test 1 dims matmul
    lt_in1 = logical_tensor_init(0, {1024}, data_type::f32);
    logical_tensor_t lt_o2
            = logical_tensor_init(2, data_type::f32, layout_type::strided);
    std::vector<logical_tensor_t *> lt_out2 {&lt_o2};
    matmul_op_schema->shape_infer(&matmul_op, lt_in, lt_out2);

    auto &infered_out_shape2 = lt_o2.dims;
    EXPECT_EQ(lt_o2.ndims, 1);
    EXPECT_EQ(infered_out_shape2[0], 1000);
    auto &infered_out_strides2 = lt_o2.layout.strides;
    EXPECT_EQ(infered_out_strides2[0], 1);

    // test >2 dims matmul
    lt_in1 = logical_tensor_init(0, {3, 1, 10, 1024, 64}, data_type::f32);
    lt_in2 = logical_tensor_init(1, {5, 1, 1024, 1000}, data_type::f32);
    logical_tensor_t lt_o3
            = logical_tensor_init(2, data_type::f32, layout_type::strided);
    std::vector<logical_tensor_t *> lt_out3 {&lt_o3};
    matmul_op_schema->shape_infer(&matmul_op, lt_in, lt_out3);

    const std::vector<int64_t> infered_out_shape3
            = logical_tensor_wrapper_t(lt_o3).vdims();
    const std::vector<int64_t> expected_out_shape3 = {3, 5, 10, 64, 1000};
    EXPECT_EQ(infered_out_shape3, expected_out_shape3);

    const std::vector<int64_t> infered_out_strides3
            = logical_tensor_wrapper_t(lt_o3).vstrides();
    const std::vector<int64_t> expected_out_strides3
            = compute_dense_strides(expected_out_shape3);
    EXPECT_EQ(infered_out_strides3, expected_out_strides3);
}

TEST(OpSchema, Abs) {
    const op_kind_t op_kind_ = op_kind::Abs;
    const size_t expected_in_size = 1;
    const size_t expected_out_size = 1;
    const size_t expected_attr_size = 0;
    const std::map<std::string, bool> attrs_data {};

    verify_op_schema(op_kind_, expected_in_size, expected_out_size,
            expected_attr_size, attrs_data);
}

TEST(OpSchema, Add) {
    const op_kind_t op_kind_ = op_kind::Add;
    const size_t expected_in_size = 2;
    const size_t expected_out_size = 1;
    const size_t expected_attr_size = 1;
    const std::map<std::string, bool> attrs_data = {{"auto_broadcast", false}};

    verify_op_schema(op_kind_, expected_in_size, expected_out_size,
            expected_attr_size, attrs_data);
}

TEST(OpSchema, InferAddOutputShapeWithoutBroadcast) {
    const op_kind_t op_kind_ = op_kind::Add;

    verify_shape_infer_for_arithmetic_op_no_broadcast(op_kind_);
}

TEST(OpSchema, InferAddOutputShapeWithBroadcast) {
    const op_kind_t op_kind_ = op_kind::Add;

    verify_shape_infer_for_arithmetic_op_with_broadcast(op_kind_);
}

TEST(OpSchema, BatchNormForwardTraining) {
    const op_kind_t op_kind_ = op_kind::BatchNormForwardTraining;
    const size_t expected_in_size = 5;
    const size_t expected_out_size = 5;
    const size_t expected_attr_size = 3;
    const std::map<std::string, bool> attrs_data
            = {{"epsilon", true}, {"momentum", false}, {"data_format", false}};

    verify_op_schema(op_kind_, expected_in_size, expected_out_size,
            expected_attr_size, attrs_data);
}

TEST(OpSchema, InferBatchNormForwardTrainingOutputShape) {
    const op_kind_t op_kind_ = op_kind::BatchNormForwardTraining;
    const op_schema_t *op_schema_
            = op_schema_registry_t::get_op_schema(op_kind_);
    op_t op_ {op_kind_, op_t::kind2str(op_kind_)};

    logical_tensor_t lt_in
            = logical_tensor_init(0, {1, 3, 224, 224}, data_type::f32);
    logical_tensor_t lt_mean = logical_tensor_init(1, {224}, data_type::f32);
    logical_tensor_t lt_variance
            = logical_tensor_init(2, {224}, data_type::f32);
    std::vector<logical_tensor_t *> in {&lt_in, &lt_mean, &lt_variance};
    logical_tensor_t lt_out = logical_tensor_init(3, data_type::f32);
    logical_tensor_t lt_r_mean = logical_tensor_init(4, data_type::f32);
    logical_tensor_t lt_r_var = logical_tensor_init(5, data_type::f32);
    logical_tensor_t lt_b_mean = logical_tensor_init(6, data_type::f32);
    logical_tensor_t lt_b_var = logical_tensor_init(7, data_type::f32);
    std::vector<logical_tensor_t *> out {
            &lt_out, &lt_r_mean, &lt_r_var, &lt_b_mean, &lt_b_var};

    op_.set_attr<float>("epsilon", 0.01f);
    op_schema_->shape_infer(&op_, in, out);
    const std::vector<int64_t> infered_out_shape0
            = logical_tensor_wrapper_t(lt_out).vdims();
    const std::vector<int64_t> expected_out_shape0 = {1, 3, 224, 224};
    EXPECT_EQ(infered_out_shape0, expected_out_shape0);

    const std::vector<int64_t> infered_out_shape1
            = logical_tensor_wrapper_t(lt_r_mean).vdims();
    const std::vector<int64_t> infered_out_shape2
            = logical_tensor_wrapper_t(lt_r_var).vdims();
    const std::vector<int64_t> infered_out_shape3
            = logical_tensor_wrapper_t(lt_b_mean).vdims();
    const std::vector<int64_t> infered_out_shape4
            = logical_tensor_wrapper_t(lt_b_var).vdims();
    const std::vector<int64_t> expected_out_shape_1D = {224};
    EXPECT_EQ(infered_out_shape1, expected_out_shape_1D);
    EXPECT_EQ(infered_out_shape2, expected_out_shape_1D);
    EXPECT_EQ(infered_out_shape3, expected_out_shape_1D);
    EXPECT_EQ(infered_out_shape4, expected_out_shape_1D);

    logical_tensor_t lt_out_not_filled = logical_tensor_init(8, data_type::f32);
    std::vector<logical_tensor_t *> out_partially_not_filled {
            &lt_out_not_filled, &lt_r_mean, &lt_r_var, &lt_b_mean, &lt_b_var};
    const std::string data_f = "NCX";
    op_.set_attr("data_format", data_f);
    auto result = op_schema_->shape_infer(&op_, in, out_partially_not_filled);
    EXPECT_EQ(result, status::invalid_shape);
}

TEST(OpSchema, BatchNormTrainingBackprop) {
    const op_kind_t op_kind_ = op_kind::BatchNormTrainingBackprop;
    const size_t expected_in_size = 6;
    const size_t expected_out_size = 3;
    const size_t expected_attr_size = 3;
    const std::map<std::string, bool> attrs_data = {
            {"epsilon", true}, {"is_training", false}, {"data_format", false}};

    verify_op_schema(op_kind_, expected_in_size, expected_out_size,
            expected_attr_size, attrs_data);
}

TEST(OpSchema, InferBatchNormTrainingBackpropOutputShape) {
    const op_kind_t op_kind_ = op_kind::BatchNormTrainingBackprop;
    const op_schema_t *op_schema_
            = op_schema_registry_t::get_op_schema(op_kind_);
    op_t op_ {op_kind_, op_t::kind2str(op_kind_)};

    logical_tensor_t lt_in
            = logical_tensor_init(0, {1, 3, 224, 224}, data_type::f32);
    logical_tensor_t lt_output_delta
            = logical_tensor_init(1, {1, 2, 224, 224}, data_type::f32);
    logical_tensor_t lt_mean = logical_tensor_init(2, {224}, data_type::f32);
    logical_tensor_t lt_variance
            = logical_tensor_init(3, {224}, data_type::f32);
    std::vector<logical_tensor_t *> in {
            &lt_in, &lt_output_delta, &lt_mean, &lt_variance};
    logical_tensor_t lt_input_delta = logical_tensor_init(4, data_type::f32);
    std::vector<logical_tensor_t *> out {&lt_input_delta};

    op_.set_attr<float>("epsilon", 0.01f);
    op_schema_->shape_infer(&op_, in, out);
    const std::vector<int64_t> infered_out_shape0
            = logical_tensor_wrapper_t(lt_input_delta).vdims();
    const std::vector<int64_t> expected_out_shape0 = {1, 3, 224, 224};
    EXPECT_EQ(infered_out_shape0, expected_out_shape0);

    logical_tensor_t lt_gamma = logical_tensor_init(5, {224}, data_type::f32);
    logical_tensor_t lt_beta = logical_tensor_init(6, {224}, data_type::f32);
    std::vector<logical_tensor_t *> in_with_options {&lt_in, &lt_output_delta,
            &lt_gamma, &lt_beta, &lt_mean, &lt_variance};
    logical_tensor_t lt_gamma_delta = logical_tensor_init(7, data_type::f32);
    logical_tensor_t lt_beta_delta = logical_tensor_init(8, data_type::f32);
    std::vector<logical_tensor_t *> out_with_options {
            &lt_input_delta, &lt_gamma_delta, &lt_beta_delta};

    op_schema_->shape_infer(&op_, in_with_options, out_with_options);
    const std::vector<int64_t> expected_out_shape_1D = {224};
    const std::vector<int64_t> infered_out_shape1
            = logical_tensor_wrapper_t(lt_gamma_delta).vdims();
    const std::vector<int64_t> infered_out_shape2
            = logical_tensor_wrapper_t(lt_beta_delta).vdims();
    EXPECT_EQ(infered_out_shape1, expected_out_shape_1D);
    EXPECT_EQ(infered_out_shape2, expected_out_shape_1D);
}

TEST(OpSchema, BiasAdd) {
    const op_kind_t op_kind_ = op_kind::BiasAdd;
    const size_t expected_in_size = 2;
    const size_t expected_out_size = 1;
    const size_t expected_attr_size = 1;
    const std::map<std::string, bool> attrs_data = {{"data_format", false}};

    verify_op_schema(op_kind_, expected_in_size, expected_out_size,
            expected_attr_size, attrs_data);
}

TEST(OpSchema, InferBiasAddOutputShape) {
    const op_kind_t op_kind_ = op_kind::BiasAdd;
    const op_schema_t *op_schema_
            = op_schema_registry_t::get_op_schema(op_kind_);
    op_t op_ {op_kind_, op_t::kind2str(op_kind_)};

    logical_tensor_t lt_in
            = logical_tensor_init(0, {1, 3, 224, 224}, data_type::f32);
    logical_tensor_t lt_bias = logical_tensor_init(1, {224}, data_type::f32);
    std::vector<logical_tensor_t *> in {&lt_in, &lt_bias};
    logical_tensor_t lt_out = logical_tensor_init(2, data_type::f32);
    std::vector<logical_tensor_t *> out {&lt_out};

    op_schema_->shape_infer(&op_, in, out);
    const std::vector<int64_t> infered_out_shape
            = logical_tensor_wrapper_t(lt_out).vdims();
    const std::vector<int64_t> expected_out_shape = {1, 3, 224, 224};
    EXPECT_EQ(infered_out_shape, expected_out_shape);

    logical_tensor_t lt_out_not_filled = logical_tensor_init(2, data_type::f32);
    std::vector<logical_tensor_t *> out_not_filled {&lt_out_not_filled};
    const std::string data_f = "NCX";
    op_.set_attr("data_format", data_f);
    auto result = op_schema_->shape_infer(&op_, in, out_not_filled);
    EXPECT_EQ(result, status::invalid_shape);
}

TEST(OpSchema, BiasAddBackprop) {
    const op_kind_t op_kind_ = op_kind::BiasAddBackprop;
    const size_t expected_in_size = 1;
    const size_t expected_out_size = 1;
    const size_t expected_attr_size = 1;
    const std::map<std::string, bool> attrs_data = {{"data_format", false}};

    verify_op_schema(op_kind_, expected_in_size, expected_out_size,
            expected_attr_size, attrs_data);
}

TEST(OpSchema, InferBiasAddBackpropOutputShapeWithNxcFormat) {
    const op_kind_t op_kind_ = op_kind::BiasAddBackprop;
    const op_schema_t *op_schema_
            = op_schema_registry_t::get_op_schema(op_kind_);
    op_t op_ {op_kind_, op_t::kind2str(op_kind_)};

    const int64_t channels = 16;
    logical_tensor_t lt_in
            = logical_tensor_init(0, {3, 64, 64, channels}, data_type::f32);
    std::vector<logical_tensor_t *> in {&lt_in};
    logical_tensor_t lt_out = logical_tensor_init(1, data_type::f32);
    std::vector<logical_tensor_t *> out {&lt_out};

    // no need to set attribute, NXC value should be default
    op_schema_->shape_infer(&op_, in, out);
    const std::vector<int64_t> infered_out_shape
            = logical_tensor_wrapper_t(lt_out).vdims();
    const std::vector<int64_t> expected_out_shape = {channels};
    EXPECT_EQ(infered_out_shape, expected_out_shape);

    // explicitly setting data_format to NXC
    const std::string default_data_f = "NXC";
    op_.set_attr("data_format", default_data_f);
    logical_tensor_t lt_out_expl = logical_tensor_init(2, data_type::f32);
    std::vector<logical_tensor_t *> out_expl {&lt_out_expl};

    op_schema_->shape_infer(&op_, in, out_expl);
    const std::vector<int64_t> infered_out_shape_expl
            = logical_tensor_wrapper_t(lt_out_expl).vdims();
    EXPECT_EQ(infered_out_shape_expl, expected_out_shape);
}

TEST(OpSchema, InferBiasAddBackpropOutputShape) {
    const op_kind_t op_kind_ = op_kind::BiasAddBackprop;
    const op_schema_t *op_schema_
            = op_schema_registry_t::get_op_schema(op_kind_);
    op_t op_ {op_kind_, op_t::kind2str(op_kind_)};

    const int64_t channels = 16;
    logical_tensor_t lt_in
            = logical_tensor_init(0, {3, channels, 64, 64}, data_type::f32);
    std::vector<logical_tensor_t *> in {&lt_in};
    logical_tensor_t lt_out = logical_tensor_init(1, data_type::f32);
    std::vector<logical_tensor_t *> out {&lt_out};
    const std::string data_f = "NCX";
    op_.set_attr("data_format", data_f);

    op_schema_->shape_infer(&op_, in, out);
    const std::vector<int64_t> infered_out_shape
            = logical_tensor_wrapper_t(lt_out).vdims();
    const std::vector<int64_t> expected_out_shape = {channels};
    EXPECT_EQ(infered_out_shape, expected_out_shape);
}

TEST(OpSchema, Clamp) {
    const op_kind_t op_kind_ = op_kind::Clamp;
    const size_t expected_in_size = 1;
    const size_t expected_out_size = 1;
    const size_t expected_attr_size = 2;
    const std::map<std::string, bool> attrs_data
            = {{"min", true}, {"max", true}};

    verify_op_schema(op_kind_, expected_in_size, expected_out_size,
            expected_attr_size, attrs_data);
}

TEST(OpSchema, InferClampOutputShape) {
    const op_kind_t op_kind_ = op_kind::Clamp;

    verify_single_in_identity_shape_infer(op_kind_);
}

TEST(OpSchema, ClampBackprop) {
    const op_kind_t op_kind_ = op_kind::ClampBackprop;
    const size_t expected_in_size = 2;
    const size_t expected_out_size = 1;
    const size_t expected_attr_size = 2;
    const std::map<std::string, bool> attrs_data
            = {{"min", true}, {"max", true}};

    verify_op_schema(op_kind_, expected_in_size, expected_out_size,
            expected_attr_size, attrs_data);
}

TEST(OpSchema, InferClampBackpropOutputShape) {
    const op_kind_t op_kind_ = op_kind::ClampBackprop;

    verify_two_ins_identity_shape_infer(op_kind_);
}

TEST(OpSchema, ConvolutionBackpropData) {
    const op_kind_t op_kind_ = op_kind::ConvolutionBackpropData;
    const size_t expected_in_size = 3;
    const size_t expected_out_size = 1;
    const size_t expected_attr_size = 9;
    const std::map<std::string, bool> attrs_data = {{"strides", true},
            {"pads_begin", true}, {"pads_end", true}, {"dilations", true},
            {"auto_pad", false}, {"output_padding", false}, {"groups", false},
            {"data_format", false}, {"filter_format", false}};

    verify_op_schema(op_kind_, expected_in_size, expected_out_size,
            expected_attr_size, attrs_data);
}

TEST(OpSchema, InferConvolutionBackpropDataOutputShape) {
    const op_kind_t op_kind_ = op_kind::ConvolutionBackpropData;

    std::string data_format = "NCX";
    std::string filter_format = "OIX";

    std::vector<int64_t> groups_vec {1, 2, 4};
    // data shape {N, IC, H, W}
    std::vector<int64_t> in_data {1, 16, 224, 224};
    std::vector<int64_t> in_output_shape {};
    std::vector<int64_t> expected_out_shape {1, 32, 452, 452};

    for (auto groups : groups_vec) {
        // weight shape {OC, IC, KH, KW}
        std::vector<int64_t> in_weight = {16, 32 / groups, 3, 3};

        verify_shape_infer_for_conv_bprop_data(op_kind_, data_format,
                filter_format, groups, in_data, in_weight, in_output_shape,
                expected_out_shape);
    }
}

TEST(OpSchema, InferConvolutionBackpropDataOutputShapeWithNxcFormat) {
    const op_kind_t op_kind_ = op_kind::ConvolutionBackpropData;

    std::string data_format = "NXC";
    std::string filter_format = "OIX";

    std::vector<int64_t> groups_vec {1, 2, 4};
    // data shape {N, H, W, IC}
    std::vector<int64_t> in_data {1, 224, 224, 16};
    std::vector<int64_t> in_output_shape {};
    std::vector<int64_t> expected_out_shape {1, 452, 452, 32};

    for (auto groups : groups_vec) {
        // weight shape {OC, IC, KH, KW}
        std::vector<int64_t> in_weight = {16, 32 / groups, 3, 3};

        verify_shape_infer_for_conv_bprop_data(op_kind_, data_format,
                filter_format, groups, in_data, in_weight, in_output_shape,
                expected_out_shape);
    }
}

TEST(OpSchema, InferConvolutionBackpropDataOutputShapeWithNxcXioFormat) {
    const op_kind_t op_kind_ = op_kind::ConvolutionBackpropData;

    std::string data_format = "NXC";
    std::string filter_format = "XIO";

    std::vector<int64_t> groups_vec {1, 2, 4};
    // data shape {N, H, W, IC}
    std::vector<int64_t> in_data {1, 224, 224, 16};
    std::vector<int64_t> in_output_shape {};
    std::vector<int64_t> expected_out_shape {1, 452, 452, 32};

    for (auto groups : groups_vec) {
        // weight shape {KH, KW, IC, OC}
        std::vector<int64_t> in_weight = {3, 3, 32 / groups, 16};

        verify_shape_infer_for_conv_bprop_data(op_kind_, data_format,
                filter_format, groups, in_data, in_weight, in_output_shape,
                expected_out_shape);
    }
}

TEST(OpSchema, InferConvolutionBackpropDataOutputShapeWithXioFormat) {
    const op_kind_t op_kind_ = op_kind::ConvolutionBackpropData;

    std::string data_format = "NCX";
    std::string filter_format = "XIO";

    // data shape {N, IC, H, W}
    std::vector<int64_t> in_data {1, 16, 224, 224};
    std::vector<int64_t> in_output_shape {};
    std::vector<int64_t> expected_out_shape {1, 32, 452, 452};

    std::vector<int64_t> groups_vec {1, 2, 4};
    for (auto groups : groups_vec) {
        // weight shape {KH, KW, IC, OC}
        std::vector<int64_t> in_weight {3, 3, 32 / groups, 16};

        verify_shape_infer_for_conv_bprop_data(op_kind_, data_format,
                filter_format, groups, in_data, in_weight, in_output_shape,
                expected_out_shape);
    }
}

TEST(OpSchema, ConvolutionBackpropFilters) {
    const op_kind_t op_kind_ = op_kind::ConvolutionBackpropFilters;
    const size_t expected_in_size = 3;
    const size_t expected_out_size = 1;
    const size_t expected_attr_size = 8;
    const std::map<std::string, bool> attrs_data
            = {{"strides", true}, {"pads_begin", true}, {"pads_end", true},
                    {"dilations", true}, {"auto_pad", false}, {"groups", false},
                    {"data_format", false}, {"filter_format", false}};

    verify_op_schema(op_kind_, expected_in_size, expected_out_size,
            expected_attr_size, attrs_data);
}

TEST(OpSchema, InferConvolutionBackpropFiltersOutputShape) {
    const op_kind_t op_kind_ = op_kind::ConvolutionBackpropFilters;

    std::vector<int64_t> strides = {2, 2};
    std::vector<int64_t> pads_begin = {1, 1};
    std::vector<int64_t> pads_end = {2, 2};
    std::vector<int64_t> dilations = {1, 1};
    std::string auto_pad = "VALID";
    std::string data_format = "NCX";
    std::string filter_format = "XIO";
    int64_t groups = 1;

    // data shape {N, IC, H, W}
    const std::vector<int64_t> &in_data = {1, 3, 224, 224};
    const std::vector<int64_t> &expected_out_shape = {4, 4, 3, 16};
    const std::vector<int64_t> &in_output_delta = {1, 16, 111, 111};

    const op_schema_t *op_schema_
            = op_schema_registry_t::get_op_schema(op_kind_);
    op_t op_ {op_kind_, op_t::kind2str(op_kind_)};

    set_conv_common_attr(op_, strides, pads_begin, pads_end, dilations,
            auto_pad, data_format, filter_format, groups);

    logical_tensor_t lt_data = logical_tensor_init(0, in_data, data_type::f32);
    logical_tensor_t lt_weight_spatial_dims
            = logical_tensor_init(1, data_type::f32);
    logical_tensor_t lt_output_delta
            = logical_tensor_init(2, in_output_delta, data_type::f32);
    std::vector<logical_tensor_t *> in {
            &lt_data, &lt_weight_spatial_dims, &lt_output_delta};
    logical_tensor_t lt_out = logical_tensor_init(3, data_type::f32);
    std::vector<logical_tensor_t *> out {&lt_out};

    op_schema_->shape_infer(&op_, in, out);
    const std::vector<int64_t> infered_out_shape
            = logical_tensor_wrapper_t(lt_out).vdims();
    EXPECT_EQ(infered_out_shape, expected_out_shape);
}

TEST(OpSchema, Divide) {
    const op_kind_t op_kind_ = op_kind::Divide;
    const size_t expected_in_size = 2;
    const size_t expected_out_size = 1;
    const size_t expected_attr_size = 1;
    const std::map<std::string, bool> attrs_data = {{"auto_broadcast", false}};

    verify_op_schema(op_kind_, expected_in_size, expected_out_size,
            expected_attr_size, attrs_data);
}

TEST(OpSchema, InferDivideOutputShapeWithoutBroadcast) {
    const op_kind_t op_kind_ = op_kind::Divide;

    verify_shape_infer_for_arithmetic_op_no_broadcast(op_kind_);
}

TEST(OpSchema, InferDivideOutputShapeWithBroadcast) {
    const op_kind_t op_kind_ = op_kind::Divide;

    verify_shape_infer_for_arithmetic_op_with_broadcast(op_kind_);
}

TEST(OpSchema, Elu) {
    const op_kind_t op_kind_ = op_kind::Elu;
    const size_t expected_in_size = 1;
    const size_t expected_out_size = 1;
    const size_t expected_attr_size = 1;
    const std::map<std::string, bool> attrs_data = {{"alpha", true}};

    verify_op_schema(op_kind_, expected_in_size, expected_out_size,
            expected_attr_size, attrs_data);
}

TEST(OpSchema, InferEluOutputShape) {
    const op_kind_t op_kind_ = op_kind::Elu;

    verify_single_in_identity_shape_infer(op_kind_);
}

TEST(OpSchema, EluBackprop) {
    const op_kind_t op_kind_ = op_kind::EluBackprop;
    const size_t expected_in_size = 2;
    const size_t expected_out_size = 1;
    const size_t expected_attr_size = 1;
    const std::map<std::string, bool> attrs_data = {{"alpha", true}};

    verify_op_schema(op_kind_, expected_in_size, expected_out_size,
            expected_attr_size, attrs_data);
}

TEST(OpSchema, InferEluBackpropOutputShape) {
    const op_kind_t op_kind_ = op_kind::EluBackprop;

    verify_two_ins_identity_shape_infer(op_kind_);
}

TEST(OpSchema, End) {
    const op_schema_t *op_schema
            = op_schema_registry_t::get_op_schema(op_kind::End);

    op_t end_op {0, op_kind::End, "end"};
    logical_tensor_t lt_in_0 = logical_tensor_init(0, data_type::f32);

    end_op.add_input(lt_in_0);
    EXPECT_TRUE(op_schema->verify(&end_op));
}

TEST(OpSchema, Reorder) {
    const op_kind_t op_kind_ = op_kind::Reorder;
    const size_t expected_in_size = 1;
    const size_t expected_out_size = 1;
    const size_t expected_attr_size = 0;
    const std::map<std::string, bool> attrs_data {};

    verify_op_schema(op_kind_, expected_in_size, expected_out_size,
            expected_attr_size, attrs_data);
}

TEST(OpSchema, InferReorderOutputShape) {
    const op_kind_t op_kind_ = op_kind::Reorder;

    verify_single_in_identity_shape_infer(op_kind_);
}

TEST(OpSchema, Erf) {
    const op_kind_t op_kind_ = op_kind::Erf;
    const size_t expected_in_size = 1;
    const size_t expected_out_size = 1;
    const size_t expected_attr_size = 0;
    const std::map<std::string, bool> attrs_data = {};

    verify_op_schema(op_kind_, expected_in_size, expected_out_size,
            expected_attr_size, attrs_data);
}

TEST(OpSchema, InferErfOutputShape) {
    const op_kind_t op_kind_ = op_kind::Erf;

    verify_single_in_identity_shape_infer(op_kind_);
}

TEST(OpSchema, Exp) {
    const op_kind_t op_kind_ = op_kind::Exp;
    const size_t expected_in_size = 1;
    const size_t expected_out_size = 1;
    const size_t expected_attr_size = 0;
    const std::map<std::string, bool> attrs_data = {};

    verify_op_schema(op_kind_, expected_in_size, expected_out_size,
            expected_attr_size, attrs_data);
}

TEST(OpSchema, InferExpOutputShape) {
    const op_kind_t op_kind_ = op_kind::Exp;

    verify_single_in_identity_shape_infer(op_kind_);
}

TEST(OpSchema, Gelu) {
    const op_kind_t op_kind_ = op_kind::GELU;
    const size_t expected_in_size = 1;
    const size_t expected_out_size = 1;
    const size_t expected_attr_size = 0;
    const std::map<std::string, bool> attrs_data = {};

    verify_op_schema(op_kind_, expected_in_size, expected_out_size,
            expected_attr_size, attrs_data);
}

TEST(OpSchema, InferGeluOutputShape) {
    const op_kind_t op_kind_ = op_kind::GELU;

    verify_single_in_identity_shape_infer(op_kind_);
}

TEST(OpSchema, GELUBackprop) {
    const op_kind_t op_kind_ = op_kind::GELUBackprop;
    const size_t expected_in_size = 2;
    const size_t expected_out_size = 1;
    const size_t expected_attr_size = 0;
    const std::map<std::string, bool> attrs_data = {};

    verify_op_schema(op_kind_, expected_in_size, expected_out_size,
            expected_attr_size, attrs_data);
}

TEST(OpSchema, InferGeluBackpropOutputShape) {
    const op_kind_t op_kind_ = op_kind::GELUBackprop;

    verify_two_ins_identity_shape_infer(op_kind_);
}

TEST(OpSchema, HardTanh) {
    const op_kind_t op_kind_ = op_kind::HardTanh;
    const size_t expected_in_size = 1;
    const size_t expected_out_size = 1;
    const size_t expected_attr_size = 2;
    const std::map<std::string, bool> attrs_data
            = {{"min", true}, {"max", true}};

    verify_op_schema(op_kind_, expected_in_size, expected_out_size,
            expected_attr_size, attrs_data);
}

TEST(OpSchema, InferHardTanhOutputShape) {
    const op_kind_t op_kind_ = op_kind::HardTanh;

    verify_single_in_identity_shape_infer(op_kind_);
}

TEST(OpSchema, HardTanhBackprop) {
    const op_kind_t op_kind_ = op_kind::HardTanhBackprop;
    const size_t expected_in_size = 1;
    const size_t expected_out_size = 1;
    const size_t expected_attr_size = 2;
    const std::map<std::string, bool> attrs_data
            = {{"min", true}, {"max", true}};

    verify_op_schema(op_kind_, expected_in_size, expected_out_size,
            expected_attr_size, attrs_data);
}

TEST(OpSchema, InferHardTanhBackpropOutputShape) {
    const op_kind_t op_kind_ = op_kind::HardTanhBackprop;

    verify_single_in_identity_shape_infer(op_kind_);
}

TEST(OpSchema, Index) {
    const op_kind_t op_kind_ = op_kind::Index;
    const size_t expected_in_size = 2;
    const size_t expected_out_size = 1;
    const size_t expected_attr_size = 0;
    // clang3 requires user-provided default constructor
    const std::map<std::string, bool> attrs_data = {};

    verify_op_schema(op_kind_, expected_in_size, expected_out_size,
            expected_attr_size, attrs_data);
}

TEST(OpSchema, InferIndexOutputShapeUnsupported) {
    const op_kind_t op_kind_ = op_kind::Index;
    const op_schema_t *op_schema_
            = op_schema_registry_t::get_op_schema(op_kind_);
    op_t op_ {op_kind_, op_t::kind2str(op_kind_)};

    logical_tensor_t lt_in
            = logical_tensor_init(0, {3, 64, 64, 64}, data_type::f32);
    logical_tensor_t lt_indices
            = logical_tensor_init(1, {2, 4}, data_type::s32);
    std::vector<logical_tensor_t *> in {&lt_in, &lt_indices};
    logical_tensor_t lt_out = logical_tensor_init(2, data_type::f32);
    std::vector<logical_tensor_t *> out {&lt_out};

    auto status = op_schema_->shape_infer(&op_, in, out);
    EXPECT_EQ(status, status::unsupported);
}

TEST(OpSchema, LayerNorm) {
    const op_kind_t op_kind_ = op_kind::LayerNorm;
    const size_t expected_in_size = 3;
    const size_t expected_out_size = 3;
    const size_t expected_attr_size = 4;
    const std::map<std::string, bool> attrs_data
            = {{"keep_stats", false}, {"begin_norm_axis", false},
                    {"use_affine", false}, {"epsilon", false}};

    verify_op_schema(op_kind_, expected_in_size, expected_out_size,
            expected_attr_size, attrs_data);
}

TEST(OpSchema, InferLayerNormOutputShape) {
    const op_schema_t *op_schema_
            = op_schema_registry_t::get_op_schema(op_kind::LayerNorm);
    op_t op_ {op_kind::LayerNorm, op_t::kind2str(op_kind::LayerNorm)};

    const std::vector<layout_type_t> layout_types
            = {layout_type::strided, layout_type::opaque};

    // We test all available cases
    const std::vector<bool> keep_statses = {true, false};

    // TODO(qun) we should test multi begin_norm_axis attrs
    const int64_t begin_norm_axis = -1;

    op_.set_attr("begin_norm_axis", begin_norm_axis);

    for (auto keep_stats : keep_statses) {
        op_.set_attr("keep_stats", static_cast<bool>(keep_stats));
        for (const auto &ltype : layout_types) {
            logical_tensor_t lt_in1 = logical_tensor_init(
                    0, {1, 3, 416, 416}, data_type::f32, ltype);
            logical_tensor_t lt_out = logical_tensor_init(
                    1, data_type::f32, layout_type::strided);
            logical_tensor_t lt_mean = logical_tensor_init(
                    2, data_type::f32, layout_type::strided);
            logical_tensor_t lt_var = logical_tensor_init(
                    3, data_type::f32, layout_type::strided);

            std::vector<logical_tensor_t *> in {&lt_in1};
            std::vector<logical_tensor_t *> out {&lt_out, &lt_mean, &lt_var};

            op_schema_->shape_infer(&op_, in, out);

            const std::vector<int64_t> infered_out_shape
                    = logical_tensor_wrapper_t(lt_out).vdims();
            const std::vector<int64_t> expected_out_shape = {1, 3, 416, 416};
            EXPECT_EQ(infered_out_shape, expected_out_shape);

            const std::vector<int64_t> infered_out_strides
                    = logical_tensor_wrapper_t(lt_out).vstrides();
            const std::vector<int64_t> expected_out_strides
                    = compute_dense_strides(expected_out_shape);
            EXPECT_EQ(infered_out_strides, expected_out_strides);

            if (keep_stats) {
                // check infered shape and strides for mean
                const std::vector<int64_t> infered_mean_shape
                        = logical_tensor_wrapper_t(lt_mean).vdims();
                const std::vector<int64_t> expected_mean_shape = {1, 3, 416};
                EXPECT_EQ(infered_mean_shape, expected_mean_shape);

                const std::vector<int64_t> infered_mean_strides
                        = logical_tensor_wrapper_t(lt_mean).vstrides();
                const std::vector<int64_t> expected_mean_strides
                        = compute_dense_strides(expected_mean_shape);
                EXPECT_EQ(infered_mean_strides, expected_mean_strides);

                // check infered shape and strides for var
                const std::vector<int64_t> infered_var_shape
                        = logical_tensor_wrapper_t(lt_var).vdims();
                const std::vector<int64_t> expected_var_shape = {1, 3, 416};
                EXPECT_EQ(infered_var_shape, expected_var_shape);

                const std::vector<int64_t> infered_var_strides
                        = logical_tensor_wrapper_t(lt_var).vstrides();
                const std::vector<int64_t> expected_var_strides
                        = compute_dense_strides(expected_var_shape);
                EXPECT_EQ(infered_var_strides, expected_var_strides);
            }
        }
    }
}

TEST(OpSchema, LayerNormBackprop) {
    const op_kind_t op_kind_ = op_kind::LayerNormBackprop;
    const size_t expected_in_size = 5;
    const size_t expected_out_size = 3;
    const size_t expected_attr_size = 4;
    const std::map<std::string, bool> attrs_data = {{"begin_norm_axis", false},
            {"use_affine", false}, {"epsilon", false}, {"use_stats", false}};

    verify_op_schema(op_kind_, expected_in_size, expected_out_size,
            expected_attr_size, attrs_data);
}

TEST(OpSchema, InferLayerNormBackpropOutputShape) {
    const op_schema_t *op_schema_
            = op_schema_registry_t::get_op_schema(op_kind::LayerNormBackprop);
    op_t op_ {op_kind::LayerNormBackprop,
            op_t::kind2str(op_kind::LayerNormBackprop)};

    const std::vector<layout_type_t> layout_types
            = {layout_type::strided, layout_type::opaque};

    // We test all available cases
    const std::vector<bool> use_affines = {true, false};

    for (auto use_affine : use_affines) {
        op_.set_attr("use_affine", static_cast<bool>(use_affine));
        for (const auto &ltype : layout_types) {
            logical_tensor_t lt_data = logical_tensor_init(
                    0, {1, 256, 64, 64}, data_type::f32, ltype);
            logical_tensor_t lt_gamma
                    = logical_tensor_init(1, {1, 256}, data_type::f32, ltype);
            logical_tensor_t lt_beta
                    = logical_tensor_init(2, {1, 256}, data_type::f32, ltype);
            logical_tensor_t lt_mean
                    = logical_tensor_init(3, {1, 256}, data_type::f32, ltype);
            logical_tensor_t lt_variance
                    = logical_tensor_init(4, {1, 256}, data_type::f32, ltype);
            logical_tensor_t lt_in_delta = logical_tensor_init(
                    5, data_type::f32, layout_type::strided);
            logical_tensor_t lt_gamma_delta = logical_tensor_init(
                    6, data_type::f32, layout_type::strided);
            logical_tensor_t lt_beta_delta = logical_tensor_init(
                    7, data_type::f32, layout_type::strided);
            std::vector<logical_tensor_t *> lt_in {
                    &lt_data, &lt_gamma, &lt_beta, &lt_mean, &lt_variance};
            std::vector<logical_tensor_t *> lt_out {
                    &lt_in_delta, &lt_gamma_delta, &lt_beta_delta};

            op_schema_->shape_infer(&op_, lt_in, lt_out);

            const std::vector<int64_t> infered_in_delta_shape
                    = logical_tensor_wrapper_t(lt_in_delta).vdims();
            const std::vector<int64_t> expected_in_delta_shape
                    = {1, 256, 64, 64};
            EXPECT_EQ(infered_in_delta_shape, expected_in_delta_shape);

            const std::vector<int64_t> infered_in_delta_strides
                    = logical_tensor_wrapper_t(lt_in_delta).vstrides();
            const std::vector<int64_t> expected_in_delta_strides
                    = compute_dense_strides(expected_in_delta_shape);
            EXPECT_EQ(infered_in_delta_strides, expected_in_delta_strides);

            if (use_affine) {
                const std::vector<int64_t> infered_gamma_delta_shape
                        = logical_tensor_wrapper_t(lt_gamma_delta).vdims();
                const std::vector<int64_t> expected_gamma_delta_shape
                        = {1, 256};
                EXPECT_EQ(
                        infered_gamma_delta_shape, expected_gamma_delta_shape);

                const std::vector<int64_t> infered_gamma_delta_strides
                        = logical_tensor_wrapper_t(lt_gamma_delta).vstrides();
                const std::vector<int64_t> expected_gamma_delta_strides
                        = compute_dense_strides(expected_gamma_delta_shape);
                EXPECT_EQ(infered_gamma_delta_strides,
                        expected_gamma_delta_strides);

                const std::vector<int64_t> infered_beta_delta_shape
                        = logical_tensor_wrapper_t(lt_beta_delta).vdims();
                const std::vector<int64_t> expected_beta_delta_shape = {1, 256};
                EXPECT_EQ(infered_beta_delta_shape, expected_beta_delta_shape);

                const std::vector<int64_t> infered_beta_delta_strides
                        = logical_tensor_wrapper_t(lt_beta_delta).vstrides();
                const std::vector<int64_t> expected_beta_delta_strides
                        = compute_dense_strides(expected_beta_delta_shape);
                EXPECT_EQ(infered_beta_delta_strides,
                        expected_beta_delta_strides);
            }
        }
    }
}

TEST(OpSchema, Log) {
    const op_kind_t op_kind_ = op_kind::Log;
    const size_t expected_in_size = 1;
    const size_t expected_out_size = 1;
    const size_t expected_attr_size = 0;
    const std::map<std::string, bool> attrs_data = {};

    verify_op_schema(op_kind_, expected_in_size, expected_out_size,
            expected_attr_size, attrs_data);
}

TEST(OpSchema, InferLogOutputShape) {
    const op_kind_t op_kind_ = op_kind::Log;

    verify_single_in_identity_shape_infer(op_kind_);
}

TEST(OpSchema, LogSoftmax) {
    const op_kind_t op_kind_ = op_kind::LogSoftmax;
    const size_t expected_in_size = 1;
    const size_t expected_out_size = 1;
    const size_t expected_attr_size = 1;
    const std::map<std::string, bool> attrs_data = {{"axis", false}};

    verify_op_schema(op_kind_, expected_in_size, expected_out_size,
            expected_attr_size, attrs_data);
}

TEST(OpSchema, InferLogSoftmaxOutputShape) {
    const op_kind_t op_kind_ = op_kind::LogSoftmax;

    verify_single_in_identity_shape_infer(op_kind_);
}

TEST(OpSchema, LogSoftmaxBackprop) {
    const op_kind_t op_kind_ = op_kind::LogSoftmaxBackprop;
    const size_t expected_in_size = 2;
    const size_t expected_out_size = 1;
    const size_t expected_attr_size = 1;
    const std::map<std::string, bool> attrs_data = {{"axis", false}};

    verify_op_schema(op_kind_, expected_in_size, expected_out_size,
            expected_attr_size, attrs_data);
}

TEST(OpSchema, InferLogSoftmaxBackpropOutputShape) {
    const op_kind_t op_kind_ = op_kind::LogSoftmaxBackprop;

    verify_two_ins_identity_shape_infer(op_kind_);
}

TEST(OpSchema, Maximum) {
    const op_kind_t op_kind_ = op_kind::Maximum;
    const size_t expected_in_size = 2;
    const size_t expected_out_size = 1;
    const size_t expected_attr_size = 1;
    const std::map<std::string, bool> attrs_data = {{"auto_broadcast", false}};

    verify_op_schema(op_kind_, expected_in_size, expected_out_size,
            expected_attr_size, attrs_data);
}

TEST(OpSchema, InferMaximumOutputShapeWithoutBroadcst) {
    const op_kind_t op_kind_ = op_kind::Maximum;

    verify_shape_infer_for_arithmetic_op_no_broadcast(op_kind_);
}

TEST(OpSchema, InferMaximumOutputShapeWihBroadcast) {
    const op_kind_t op_kind_ = op_kind::Maximum;

    verify_shape_infer_for_arithmetic_op_with_broadcast(op_kind_);
}

TEST(OpSchema, MaxPoolBackprop) {
    const op_kind_t op_kind_ = op_kind::MaxPoolBackprop;

    const std::set<size_t> expected_in_sizes = {2, 3};
    const size_t expected_out_size = 1;
    const size_t expected_attr_size = 7;
    const std::map<std::string, bool> attrs_data = {{"strides", true},
            {"pads_begin", true}, {"pads_end", true}, {"kernel", true},
            {"auto_pad", false}, {"dilations", false}, {"data_format", false}};
    for (auto expected_in_size : expected_in_sizes) {
        verify_op_schema(op_kind_, expected_in_size, expected_out_size,
                expected_attr_size, attrs_data);
    }
}

TEST(OpSchema, InferMaxPoolBackpropOutputShape) {
    const op_kind_t op_kind_ = op_kind::MaxPoolBackprop;

    verify_two_ins_identity_shape_infer(op_kind_);
}
TEST(OpSchema, Minimum) {
    const op_kind_t op_kind_ = op_kind::Minimum;
    const size_t expected_in_size = 2;
    const size_t expected_out_size = 1;
    const size_t expected_attr_size = 1;
    const std::map<std::string, bool> attrs_data = {{"auto_broadcast", false}};

    verify_op_schema(op_kind_, expected_in_size, expected_out_size,
            expected_attr_size, attrs_data);
}

TEST(OpSchema, InferMinimumOutputShapeWithoutBroadcast) {
    const op_kind_t op_kind_ = op_kind::Minimum;

    verify_shape_infer_for_arithmetic_op_no_broadcast(op_kind_);
}

TEST(OpSchema, InferMinimumOutputShapeWithBroadcast) {
    const op_kind_t op_kind_ = op_kind::Minimum;

    verify_shape_infer_for_arithmetic_op_with_broadcast(op_kind_);
}

TEST(OpSchema, Multiply) {
    const op_kind_t op_kind_ = op_kind::Multiply;
    const size_t expected_in_size = 2;
    const size_t expected_out_size = 1;
    const size_t expected_attr_size = 1;
    const std::map<std::string, bool> attrs_data = {{"auto_broadcast", false}};

    verify_op_schema(op_kind_, expected_in_size, expected_out_size,
            expected_attr_size, attrs_data);
}

TEST(OpSchema, InferMultiplyOutputShapeWithoutBroadcast) {
    const op_kind_t op_kind_ = op_kind::Multiply;

    verify_shape_infer_for_arithmetic_op_no_broadcast(op_kind_);
}

TEST(OpSchema, InferMultiplyOutputShapeWithBroadcast) {
    const op_kind_t op_kind_ = op_kind::Multiply;

    verify_shape_infer_for_arithmetic_op_with_broadcast(op_kind_);
}

TEST(OpSchema, Pow) {
    const op_kind_t op_kind_ = op_kind::Pow;
    const size_t expected_in_size = 2;
    const size_t expected_out_size = 1;
    const size_t expected_attr_size = 1;
    const std::map<std::string, bool> attrs_data = {{"auto_broadcast", false}};

    verify_op_schema(op_kind_, expected_in_size, expected_out_size,
            expected_attr_size, attrs_data);
}

TEST(OpSchema, InferPowOutputShapeWithoutBroadcast) {
    const op_kind_t op_kind_ = op_kind::Pow;

    verify_shape_infer_for_arithmetic_op_no_broadcast(op_kind_);
}

TEST(OpSchema, InferPowOutputShapeWithBroadcast) {
    const op_kind_t op_kind_ = op_kind::Pow;

    verify_shape_infer_for_arithmetic_op_with_broadcast(op_kind_);
}

TEST(OpSchema, PowBackprop) {
    const op_kind_t op_kind_ = op_kind::PowBackprop;
    const size_t expected_in_size = 3;
    const size_t expected_out_size = 1;
    const size_t expected_attr_size = 0;
    const std::map<std::string, bool> attrs_data = {};

    verify_op_schema(op_kind_, expected_in_size, expected_out_size,
            expected_attr_size, attrs_data);
}

TEST(OpSchema, InferPowBackpropOutputShape) {
    const op_kind_t op_kind_ = op_kind::PowBackprop;
    const op_schema_t *op_schema_
            = op_schema_registry_t::get_op_schema(op_kind_);
    op_t op_ {op_kind_, op_t::kind2str(op_kind_)};

    logical_tensor_t lt_in
            = logical_tensor_init(0, {1, 3, 224, 224}, data_type::f32);
    logical_tensor_t lt_out_delta
            = logical_tensor_init(1, {1, 3, 224, 224}, data_type::f32);
    logical_tensor_t lt_exp
            = logical_tensor_init(2, {1, 1, 224}, data_type::f32);
    std::vector<logical_tensor_t *> in {&lt_in, &lt_out_delta, &lt_exp};
    logical_tensor_t lt_in_delta = logical_tensor_init(3, data_type::f32);
    std::vector<logical_tensor_t *> out {&lt_in_delta};

    op_schema_->shape_infer(&op_, in, out);
    const std::vector<int64_t> infered_out_shape
            = logical_tensor_wrapper_t(lt_in_delta).vdims();
    const std::vector<int64_t> expected_out_shape = {1, 3, 224, 224};
    EXPECT_EQ(infered_out_shape, expected_out_shape);
}

TEST(OpSchema, PReLU) {
    const op_kind_t op_kind_ = op_kind::PReLU;
    const size_t expected_in_size = 2;
    const size_t expected_out_size = 1;
    const size_t expected_attr_size = 2;
    const std::map<std::string, bool> attrs_data
            = {{"data_format", false}, {"per_channel_broadcast", false}};

    verify_op_schema(op_kind_, expected_in_size, expected_out_size,
            expected_attr_size, attrs_data);
}

TEST(OpSchema, PReLUOutputShape) {
    const op_kind_t op_kind_ = op_kind::PReLU;

    verify_two_ins_identity_shape_infer(op_kind_);
}

TEST(OpSchema, ReduceSum) {
    const op_kind_t op_kind_ = op_kind::ReduceSum;
    const size_t expected_in_size = 2;
    const size_t expected_out_size = 1;
    const size_t expected_attr_size = 1;
    const std::map<std::string, bool> attrs_data = {{"keep_dims", false}};

    verify_op_schema(op_kind_, expected_in_size, expected_out_size,
            expected_attr_size, attrs_data);
}

TEST(OpSchema, InferReduceSumOutputShape) {
    const op_kind_t op_kind_ = op_kind::ReduceSum;
    const op_schema_t *op_schema_
            = op_schema_registry_t::get_op_schema(op_kind_);
    op_t op_ {op_kind_, op_t::kind2str(op_kind_)};

    logical_tensor_t lt_in
            = logical_tensor_init(0, {1, 3, 224, 224}, data_type::f32);
    logical_tensor_t lt_axis_indices = logical_tensor_init(1, data_type::f32);
    std::vector<logical_tensor_t *> in {&lt_in, &lt_axis_indices};
    logical_tensor_t lt_out = logical_tensor_init(2, data_type::f32);
    std::vector<logical_tensor_t *> out {&lt_out};

    auto ret = op_schema_->shape_infer(&op_, in, out);
    EXPECT_EQ(ret, status::unsupported);
}

TEST(OpSchema, ReLU) {
    const op_kind_t op_kind_ = op_kind::ReLU;
    const size_t expected_in_size = 1;
    const size_t expected_out_size = 1;
    const size_t expected_attr_size = 0;
    const std::map<std::string, bool> attrs_data = {};

    verify_op_schema(op_kind_, expected_in_size, expected_out_size,
            expected_attr_size, attrs_data);
}

TEST(OpSchema, InferReluOutputShape) {
    const op_kind_t op_kind_ = op_kind::ReLU;

    verify_single_in_identity_shape_infer(op_kind_);
}

TEST(OpSchema, ReLUBackprop) {
    const op_kind_t op_kind_ = op_kind::ReLUBackprop;
    const size_t expected_in_size = 2;
    const size_t expected_out_size = 1;
    const size_t expected_attr_size = 0;
    const std::map<std::string, bool> attrs_data = {};

    verify_op_schema(op_kind_, expected_in_size, expected_out_size,
            expected_attr_size, attrs_data);
}

TEST(OpSchema, InferReluBackpropOutputShape) {
    const op_kind_t op_kind_ = op_kind::ReLUBackprop;

    verify_two_ins_identity_shape_infer(op_kind_);
}

TEST(OpSchema, Round) {
    const op_kind_t op_kind_ = op_kind::Round;
    const size_t expected_in_size = 1;
    const size_t expected_out_size = 1;
    const size_t expected_attr_size = 0;
    const std::map<std::string, bool> attrs_data = {};

    verify_op_schema(op_kind_, expected_in_size, expected_out_size,
            expected_attr_size, attrs_data);
}

TEST(OpSchema, InferRoundOutputShape) {
    const op_kind_t op_kind_ = op_kind::Round;

    verify_single_in_identity_shape_infer(op_kind_);
}

TEST(OpSchema, Sigmoid) {
    const op_kind_t op_kind_ = op_kind::Sigmoid;
    const size_t expected_in_size = 1;
    const size_t expected_out_size = 1;
    const size_t expected_attr_size = 0;
    const std::map<std::string, bool> attrs_data = {};

    verify_op_schema(op_kind_, expected_in_size, expected_out_size,
            expected_attr_size, attrs_data);
}

TEST(OpSchema, InferSigmoidOutputShape) {
    const op_kind_t op_kind_ = op_kind::Sigmoid;

    verify_single_in_identity_shape_infer(op_kind_);
}

TEST(OpSchema, SigmoidBackprop) {
    const op_kind_t op_kind_ = op_kind::SigmoidBackprop;
    const size_t expected_in_size = 2;
    const size_t expected_out_size = 1;
    const size_t expected_attr_size = 1;
    const std::map<std::string, bool> attrs_data = {{"use_dst", false}};

    verify_op_schema(op_kind_, expected_in_size, expected_out_size,
            expected_attr_size, attrs_data);
}

TEST(OpSchema, InferSigmoidBackpropOutputShape) {
    const op_kind_t op_kind_ = op_kind::SigmoidBackprop;

    verify_two_ins_identity_shape_infer(op_kind_);
}

TEST(OpSchema, SoftMax) {
    const op_kind_t op_kind_ = op_kind::SoftMax;
    const size_t expected_in_size = 1;
    const size_t expected_out_size = 1;
    const size_t expected_attr_size = 1;
    const std::map<std::string, bool> attrs_data = {{"axis", false}};

    verify_op_schema(op_kind_, expected_in_size, expected_out_size,
            expected_attr_size, attrs_data);
}

TEST(OpSchema, InferSoftMaxOutputShape) {
    const op_kind_t op_kind_ = op_kind::SoftMax;

    verify_single_in_identity_shape_infer(op_kind_);
}

TEST(OpSchema, SoftMaxBackprop) {
    const op_kind_t op_kind_ = op_kind::SoftMaxBackprop;
    const size_t expected_in_size = 2;
    const size_t expected_out_size = 1;
    const size_t expected_attr_size = 1;
    const std::map<std::string, bool> attrs_data = {{"axis", false}};

    verify_op_schema(op_kind_, expected_in_size, expected_out_size,
            expected_attr_size, attrs_data);
}

TEST(OpSchema, InferSoftMaxBackpropOutputShape) {
    const op_kind_t op_kind_ = op_kind::SoftMaxBackprop;

    verify_two_ins_identity_shape_infer(op_kind_);
}

TEST(OpSchema, InferSqrtOutputShape) {
    const op_kind_t op_kind_ = op_kind::Sqrt;

    verify_single_in_identity_shape_infer(op_kind_);
}

TEST(OpSchema, Sqrt) {
    const op_kind_t op_kind_ = op_kind::Sqrt;
    const size_t expected_in_size = 1;
    const size_t expected_out_size = 1;
    const size_t expected_attr_size = 0;
    const std::map<std::string, bool> attrs_data = {};

    verify_op_schema(op_kind_, expected_in_size, expected_out_size,
            expected_attr_size, attrs_data);
}

TEST(OpSchema, SoftPlus) {
    const op_kind_t op_kind_ = op_kind::SoftPlus;
    const size_t expected_in_size = 1;
    const size_t expected_out_size = 1;
    const size_t expected_attr_size = 1;
    const std::map<std::string, bool> attrs_data = {{"beta", false}};

    verify_op_schema(op_kind_, expected_in_size, expected_out_size,
            expected_attr_size, attrs_data);
}

TEST(OpSchema, InferSoftPlusOutputShape) {
    const op_kind_t op_kind_ = op_kind::SoftPlus;

    verify_single_in_identity_shape_infer(op_kind_);
}

TEST(OpSchema, SoftPlusBackprop) {
    const op_kind_t op_kind_ = op_kind::SoftPlusBackprop;
    const size_t expected_in_size = 2;
    const size_t expected_out_size = 1;
    const size_t expected_attr_size = 1;
    const std::map<std::string, bool> attrs_data = {{"beta", false}};

    verify_op_schema(op_kind_, expected_in_size, expected_out_size,
            expected_attr_size, attrs_data);
}

TEST(OpSchema, InferSoftPlusBackpropOutputShape) {
    const op_kind_t op_kind_ = op_kind::SoftPlusBackprop;

    verify_two_ins_identity_shape_infer(op_kind_);
}

TEST(OpSchema, SqrtBackprop) {
    const op_kind_t op_kind_ = op_kind::SqrtBackprop;
    const size_t expected_in_size = 2;
    const size_t expected_out_size = 1;
    const size_t expected_attr_size = 1;
    const std::map<std::string, bool> attrs_data = {{"use_dst", false}};

    verify_op_schema(op_kind_, expected_in_size, expected_out_size,
            expected_attr_size, attrs_data);
}

TEST(OpSchema, Tanh) {
    const op_kind_t op_kind_ = op_kind::Tanh;
    const size_t expected_in_size = 1;
    const size_t expected_out_size = 1;
    const size_t expected_attr_size = 0;
    const std::map<std::string, bool> attrs_data = {};

    verify_op_schema(op_kind_, expected_in_size, expected_out_size,
            expected_attr_size, attrs_data);
}
TEST(OpSchema, InferSqrtBackpropOutputShape) {
    const op_kind_t op_kind_ = op_kind::SqrtBackprop;

    verify_two_ins_identity_shape_infer(op_kind_);
}

TEST(OpSchema, Square) {
    const op_kind_t op_kind_ = op_kind::Square;
    const size_t expected_in_size = 1;
    const size_t expected_out_size = 1;
    const size_t expected_attr_size = 0;
    const std::map<std::string, bool> attrs_data = {};

    verify_op_schema(op_kind_, expected_in_size, expected_out_size,
            expected_attr_size, attrs_data);
}

TEST(OpSchema, InferSquareOutputShape) {
    const op_kind_t op_kind_ = op_kind::Square;

    verify_single_in_identity_shape_infer(op_kind_);
}

TEST(OpSchema, InferTanhOutputShape) {
    const op_kind_t op_kind_ = op_kind::Tanh;

    verify_single_in_identity_shape_infer(op_kind_);
}

TEST(OpSchema, TanhBackprop) {
    const op_kind_t op_kind_ = op_kind::TanhBackprop;
    const size_t expected_in_size = 2;
    const size_t expected_out_size = 1;
    const size_t expected_attr_size = 1;
    const std::map<std::string, bool> attrs_data = {{"use_dst", false}};

    verify_op_schema(op_kind_, expected_in_size, expected_out_size,
            expected_attr_size, attrs_data);
}

TEST(OpSchema, InferTanhBackpropOutputShape) {
    const op_kind_t op_kind_ = op_kind::TanhBackprop;

    verify_single_in_identity_shape_infer(op_kind_);
}

TEST(OpSchema, Wildcard) {
    const op_schema_t *op_schema
            = op_schema_registry_t::get_op_schema(kWildcard);
    auto inputs_option = op_schema->get_inputs_option();
    auto outputs_option = op_schema->get_outputs_option();
    EXPECT_TRUE(inputs_option == op_schema_t::param_num_option::variadic);
    EXPECT_TRUE(outputs_option == op_schema_t::param_num_option::variadic);

    op_t wildcard_op {0, kWildcard, std::string("wildcard")};
    logical_tensor_t lt_in_0 = logical_tensor_init(0, data_type::f32);
    logical_tensor_t lt_in_1 = logical_tensor_init(1, data_type::f32);
    logical_tensor_t lt_out_0 = logical_tensor_init(2, data_type::f32);
    logical_tensor_t lt_out_1 = logical_tensor_init(3, data_type::f32);
    logical_tensor_t lt_out_2 = logical_tensor_init(4, data_type::f32);

    wildcard_op.add_input(lt_in_0);
    wildcard_op.add_input(lt_in_1);
    wildcard_op.add_output(lt_out_0);
    wildcard_op.add_output(lt_out_1);
    wildcard_op.add_output(lt_out_2);

    EXPECT_TRUE(op_schema->verify(&wildcard_op));
}

TEST(OpSchema, InferWildcardOutputShape) {
    const op_kind_t op_kind_ = op_kind::Wildcard;
    const op_schema_t *op_schema_
            = op_schema_registry_t::get_op_schema(op_kind_);
    op_t op_ {op_kind_, op_t::kind2str(op_kind_)};

    logical_tensor_t lt_in = logical_tensor_init(0, data_type::f32);
    std::vector<logical_tensor_t *> in {&lt_in};
    logical_tensor_t lt_out = logical_tensor_init(1, data_type::f32);
    std::vector<logical_tensor_t *> out {&lt_out};

    auto ret = op_schema_->shape_infer(&op_, in, out);
    EXPECT_EQ(ret, status::unsupported);
}

TEST(OpSchema, BatchNormOptionalInput) {
    const op_schema_t *bn_op_schema
            = op_schema_registry_t::get_op_schema(kBatchNormForwardTraining);
    auto inputs_option = bn_op_schema->get_inputs_option();
    auto outputs_option = bn_op_schema->get_outputs_option();
    EXPECT_TRUE(inputs_option == op_schema_t::param_num_option::optional);
    EXPECT_TRUE(outputs_option == op_schema_t::param_num_option::fixed);

    op_t bn_op {0, kBatchNormForwardTraining, std::string("bn")};
    logical_tensor_t lt_data = logical_tensor_init(0, data_type::f32);
    logical_tensor_t lt_mean = logical_tensor_init(1, data_type::f32);
    logical_tensor_t lt_viance = logical_tensor_init(2, data_type::f32);
    logical_tensor_t lt_output = logical_tensor_init(3, data_type::f32);
    logical_tensor_t lt_running_mean = logical_tensor_init(4, data_type::f32);
    logical_tensor_t lt_running_viance = logical_tensor_init(5, data_type::f32);
    logical_tensor_t lt_batch_mean = logical_tensor_init(6, data_type::f32);
    logical_tensor_t lt_batch_viance = logical_tensor_init(7, data_type::f32);

    bn_op.add_input(lt_data);
    bn_op.add_input(lt_mean);
    bn_op.add_input(lt_viance);
    bn_op.add_output(lt_output);
    bn_op.add_output(lt_running_mean);
    bn_op.add_output(lt_running_viance);
    bn_op.add_output(lt_batch_mean);
    bn_op.add_output(lt_batch_viance);

    bn_op.set_attr<float>("epsilon", 0.001f);
    EXPECT_TRUE(bn_op_schema->verify(&bn_op));

    logical_tensor_t lt_gamma = logical_tensor_init(8, data_type::f32);
    bn_op.add_input(lt_gamma);
    EXPECT_TRUE(bn_op_schema->verify(&bn_op));
    logical_tensor_t lt_beta = logical_tensor_init(9, data_type::f32);
    bn_op.add_input(lt_beta);
    EXPECT_TRUE(bn_op_schema->verify(&bn_op));

    logical_tensor_t lt_false = logical_tensor_init(10, data_type::f32);
    bn_op.add_input(lt_false);
    EXPECT_FALSE(bn_op_schema->verify(&bn_op));
}

TEST(OpSchema, ConcatVariadicInput) {
    const op_schema_t *concat_op_schema
            = op_schema_registry_t::get_op_schema(kConcat);
    auto inputs_option = concat_op_schema->get_inputs_option();
    auto outputs_option = concat_op_schema->get_outputs_option();
    EXPECT_TRUE(inputs_option == op_schema_t::param_num_option::variadic);
    EXPECT_TRUE(outputs_option == op_schema_t::param_num_option::fixed);

    op_t concat_op {0, kConcat, std::string("concat")};
    logical_tensor_t lt_data_0 = logical_tensor_init(0, data_type::f32);
    logical_tensor_t lt_data_1 = logical_tensor_init(1, data_type::f32);
    logical_tensor_t lt_data_2 = logical_tensor_init(2, data_type::f32);
    logical_tensor_t lt_output = logical_tensor_init(3, data_type::f32);

    concat_op.add_input(lt_data_0);
    concat_op.add_input(lt_data_1);
    concat_op.add_input(lt_data_2);
    concat_op.add_output(lt_output);

    concat_op.set_attr("axis", int64_t(0));
    EXPECT_TRUE(concat_op_schema->verify(&concat_op));
}

TEST(OpSchema, ConcatVariadicInputNegative) {
    const op_schema_t *concat_op_schema
            = op_schema_registry_t::get_op_schema(kConcat);

    op_t concat_op {0, kConcat, std::string("concat")};
    logical_tensor_t lt_output = logical_tensor_init(3, data_type::f32);

    concat_op.add_output(lt_output);

    concat_op.set_attr("axis", int64_t(0));
    EXPECT_FALSE(concat_op_schema->verify(&concat_op));
}

TEST(OpSchema, LayerNormOptionalInputs) {
    const op_schema_t *ln_op_schema
            = op_schema_registry_t::get_op_schema(kLayerNorm);
    auto inputs_option = ln_op_schema->get_inputs_option();
    auto outputs_option = ln_op_schema->get_outputs_option();
    EXPECT_TRUE(inputs_option == op_schema_t::param_num_option::optional);
    EXPECT_TRUE(outputs_option == op_schema_t::param_num_option::optional);

    op_t ln_op {0, kLayerNorm, std::string("ln")};
    logical_tensor_t lt_data = logical_tensor_init(0, data_type::f32);

    logical_tensor_t lt_output = logical_tensor_init(1, data_type::f32);

    ln_op.add_input(lt_data);

    ln_op.add_output(lt_output);

    ln_op.set_attr("keep_stats", true);
    EXPECT_TRUE(ln_op_schema->verify(&ln_op));

    logical_tensor_t lt_beta = logical_tensor_init(2, data_type::f32);
    ln_op.add_input(lt_beta);
    EXPECT_FALSE(ln_op_schema->verify(&ln_op));

    logical_tensor_t lt_gamma = logical_tensor_init(3, data_type::f32);
    ln_op.add_input(lt_gamma);
    EXPECT_TRUE(ln_op_schema->verify(&ln_op));

    logical_tensor_t lt_mean = logical_tensor_init(4, data_type::f32);
    ln_op.add_output(lt_mean);
    EXPECT_FALSE(ln_op_schema->verify(&ln_op));

    logical_tensor_t lt_variance = logical_tensor_init(5, data_type::f32);
    ln_op.add_output(lt_variance);
    EXPECT_TRUE(ln_op_schema->verify(&ln_op));

    logical_tensor_t lt_false = logical_tensor_init(6, data_type::f32);
    ln_op.add_input(lt_false);
    EXPECT_FALSE(ln_op_schema->verify(&ln_op));
}

TEST(OpSchema, AddDefaultAttribute) {
    op_kind_t tmp_op_kind = kAdd;
    op_t tmp_op {0, tmp_op_kind, std::string("add")};

    const op_schema_t *opm = op_schema_registry_t::get_op_schema(tmp_op_kind);
    EXPECT_TRUE(opm != nullptr);
    opm->set_default_attribute(&tmp_op);

    const std::string *sval {nullptr};
    tmp_op.get_attr<std::string>("auto_broadcast", &sval);
    EXPECT_EQ(*sval, "numpy");
}

TEST(OpSchema, AvgpoolDefaultAttribute) {
    op_kind_t tmp_op_kind = kAvgPool;
    op_t tmp_op {0, tmp_op_kind, std::string("avgpool")};

    const op_schema_t *opm = op_schema_registry_t::get_op_schema(tmp_op_kind);
    EXPECT_TRUE(opm != nullptr);
    opm->set_default_attribute(&tmp_op);

    const std::string *sval {nullptr};
    tmp_op.get_attr<std::string>("data_format", &sval);
    EXPECT_EQ(*sval, "NXC");

    tmp_op.get_attr<std::string>("rounding_type", &sval);
    EXPECT_EQ(*sval, "floor");

    tmp_op.get_attr<std::string>("auto_pad", &sval);
    EXPECT_EQ(*sval, "None");

    tmp_op.get_attr<std::string>("exclude_pad", &sval);
    EXPECT_EQ(*sval, "None");

    logical_tensor_t lt_data = logical_tensor_init(0, data_type::f32);
    logical_tensor_t lt_out = logical_tensor_init(1, data_type::f32);
    tmp_op.add_input(lt_data);
    tmp_op.add_output(lt_out);
    std::vector<int64_t> strides = {2, 2};
    std::vector<int64_t> kernel = {3, 3};
    std::vector<int64_t> pads_begin = {1, 1};
    std::vector<int64_t> pads_end = {2, 2};
    std::vector<int64_t> dilations = {1, 1};
    bool exclude_pad = false;

    tmp_op.set_attr("strides", strides);
    tmp_op.set_attr("pads_begin", pads_begin);
    tmp_op.set_attr("pads_end", pads_end);
    tmp_op.set_attr("kernel", kernel);
    tmp_op.set_attr("exclude_pad", exclude_pad);

    EXPECT_TRUE(opm->verify(&tmp_op));
}

TEST(OpSchema, AvgBackpropDefaultAttribute) {
    op_kind_t tmp_op_kind = kAvgPoolBackprop;
    op_t tmp_op {0, tmp_op_kind, std::string("avgpool_bp")};

    const op_schema_t *opm = op_schema_registry_t::get_op_schema(tmp_op_kind);
    EXPECT_TRUE(opm != nullptr);
    opm->set_default_attribute(&tmp_op);

    const std::string *sval {nullptr};
    tmp_op.get_attr<std::string>("data_format", &sval);
    EXPECT_EQ(*sval, "NXC");

    tmp_op.get_attr<std::string>("auto_pad", &sval);
    EXPECT_EQ(*sval, "None");
}

TEST(OpSchema, BatchNormInferenceDefaultAttribute) {
    op_kind_t tmp_op_kind = kBatchNormInference;
    op_t tmp_op {0, tmp_op_kind, std::string("bn_inference")};

    const op_schema_t *opm = op_schema_registry_t::get_op_schema(tmp_op_kind);
    EXPECT_TRUE(opm != nullptr);
    opm->set_default_attribute(&tmp_op);

    const std::string *sval {nullptr};
    tmp_op.get_attr<std::string>("data_format", &sval);
    EXPECT_EQ(*sval, "NXC");
}

TEST(OpSchema, BatchNormForwardTrainingDefaultAttribute) {
    op_kind_t tmp_op_kind = kBatchNormForwardTraining;
    op_t tmp_op {0, tmp_op_kind, std::string("bn_fwd_training")};

    const op_schema_t *opm = op_schema_registry_t::get_op_schema(tmp_op_kind);
    EXPECT_TRUE(opm != nullptr);
    opm->set_default_attribute(&tmp_op);

    const std::string *sval {nullptr};
    tmp_op.get_attr<std::string>("data_format", &sval);
    EXPECT_EQ(*sval, "NXC");
}

TEST(OpSchema, BatchNormTrainingBackpropDefaultAttribute) {
    op_kind_t tmp_op_kind = kBatchNormTrainingBackprop;
    op_t tmp_op {0, tmp_op_kind, std::string("bn_bp")};

    const op_schema_t *opm = op_schema_registry_t::get_op_schema(tmp_op_kind);
    EXPECT_TRUE(opm != nullptr);
    opm->set_default_attribute(&tmp_op);

    const std::string *sval {nullptr};
    tmp_op.get_attr<std::string>("data_format", &sval);
    EXPECT_EQ(*sval, "NXC");

    const bool *bval {nullptr};
    tmp_op.get_attr<bool>("is_training", &bval);
    EXPECT_TRUE(*bval);
}

TEST(OpSchema, BiasaddDefaultAttribute) {
    op_kind_t tmp_op_kind = kBiasAdd;
    op_t tmp_op {0, tmp_op_kind, std::string("bias_add")};

    const op_schema_t *opm = op_schema_registry_t::get_op_schema(tmp_op_kind);
    EXPECT_TRUE(opm != nullptr);
    opm->set_default_attribute(&tmp_op);

    const std::string *sval {nullptr};
    tmp_op.get_attr<std::string>("data_format", &sval);
    EXPECT_EQ(*sval, "NXC");
}

TEST(OpSchema, BiasaddBackpropDefaultAttribute) {
    op_kind_t tmp_op_kind = kBiasAddBackprop;
    op_t tmp_op {0, tmp_op_kind, std::string("bias_add_bp")};

    const op_schema_t *opm = op_schema_registry_t::get_op_schema(tmp_op_kind);
    EXPECT_TRUE(opm != nullptr);
    opm->set_default_attribute(&tmp_op);

    const std::string *sval {nullptr};
    tmp_op.get_attr<std::string>("data_format", &sval);
    EXPECT_EQ(*sval, "NXC");
}

TEST(OpSchema, ConvolutionDefaultAttribute) {
    op_kind_t tmp_op_kind = kConvolution;
    op_t tmp_op {0, tmp_op_kind, std::string("conv")};

    const op_schema_t *opm = op_schema_registry_t::get_op_schema(tmp_op_kind);
    EXPECT_TRUE(opm != nullptr);
    opm->set_default_attribute(&tmp_op);

    const std::string *sval {nullptr};
    tmp_op.get_attr<std::string>("data_format", &sval);
    EXPECT_EQ(*sval, "NXC");

    tmp_op.get_attr<std::string>("filter_format", &sval);
    EXPECT_EQ(*sval, "XIO");

    tmp_op.get_attr<std::string>("auto_pad", &sval);
    EXPECT_EQ(*sval, "None");

    const int64_t *ival {nullptr};
    tmp_op.get_attr<int64_t>("groups", &ival);
    int64_t int_value {1};
    EXPECT_EQ(*ival, int_value);
}

TEST(OpSchema, ConvolutionBackpropDataDefaultAttribute) {
    op_kind_t tmp_op_kind = kConvolutionBackpropData;
    op_t tmp_op {0, tmp_op_kind, std::string("conv_bpd")};

    const op_schema_t *opm = op_schema_registry_t::get_op_schema(tmp_op_kind);
    EXPECT_TRUE(opm != nullptr);
    opm->set_default_attribute(&tmp_op);

    const std::vector<int64_t> *vval {nullptr};
    tmp_op.get_attr<std::vector<int64_t>>("output_padding", &vval);
    std::vector<int64_t> vector_value(DNNL_GRAPH_MAX_NDIMS, 0);
    EXPECT_EQ(*vval, vector_value);

    const std::string *sval {nullptr};
    tmp_op.get_attr<std::string>("data_format", &sval);
    EXPECT_EQ(*sval, "NXC");

    tmp_op.get_attr<std::string>("filter_format", &sval);
    EXPECT_EQ(*sval, "XIO");

    tmp_op.get_attr<std::string>("auto_pad", &sval);
    EXPECT_EQ(*sval, "None");

    const int64_t *ival {nullptr};
    tmp_op.get_attr<int64_t>("groups", &ival);
    int64_t int_value {1};
    EXPECT_EQ(*ival, int_value);
}

TEST(OpSchema, ConvolutionBackpropFiltersDefaultAttribute) {
    op_kind_t tmp_op_kind = kConvolutionBackpropFilters;
    op_t tmp_op {0, tmp_op_kind, std::string("conv_bpf")};

    const op_schema_t *opm = op_schema_registry_t::get_op_schema(tmp_op_kind);
    EXPECT_TRUE(opm != nullptr);
    opm->set_default_attribute(&tmp_op);

    const std::string *sval {nullptr};
    tmp_op.get_attr<std::string>("data_format", &sval);
    EXPECT_EQ(*sval, "NXC");

    tmp_op.get_attr<std::string>("filter_format", &sval);
    EXPECT_EQ(*sval, "XIO");

    tmp_op.get_attr<std::string>("auto_pad", &sval);
    EXPECT_EQ(*sval, "None");

    const int64_t *ival {nullptr};
    tmp_op.get_attr<int64_t>("groups", &ival);
    int64_t int_value {1};
    EXPECT_EQ(*ival, int_value);
}

TEST(OpSchema, DivideDefaultAttribute) {
    op_kind_t tmp_op_kind = kDivide;
    op_t tmp_op {0, tmp_op_kind, std::string("divide")};

    const op_schema_t *opm = op_schema_registry_t::get_op_schema(tmp_op_kind);
    EXPECT_TRUE(opm != nullptr);
    opm->set_default_attribute(&tmp_op);

    const std::string *sval {nullptr};
    tmp_op.get_attr<std::string>("auto_broadcast", &sval);
    EXPECT_EQ(*sval, "numpy");
}

TEST(OpSchema, InterpolateDefaultAttribute) {
    op_kind_t tmp_op_kind = kInterpolate;
    op_t tmp_op {0, tmp_op_kind, std::string("interpolate")};

    const op_schema_t *opm = op_schema_registry_t::get_op_schema(tmp_op_kind);
    EXPECT_TRUE(opm != nullptr);
    opm->set_default_attribute(&tmp_op);

    const std::string *sval {nullptr};
    tmp_op.get_attr<std::string>("coordinate_transformation_mode", &sval);
    EXPECT_EQ(*sval, "half_pixel");

    tmp_op.get_attr<std::string>("nearest_mode", &sval);
    EXPECT_EQ(*sval, "round_prefer_floor");

    const bool *bval {nullptr};
    tmp_op.get_attr<bool>("antialias", &bval);
    EXPECT_FALSE(*bval);

    const std::vector<int64_t> *vval {nullptr};
    tmp_op.get_attr<std::vector<int64_t>>("pads_begin", &vval);
    std::vector<int64_t> vector_value(0, DNNL_GRAPH_MAX_NDIMS);
    EXPECT_EQ(*vval, vector_value);

    tmp_op.get_attr<std::vector<int64_t>>("pads_end", &vval);
    EXPECT_EQ(*vval, vector_value);

    const float *fval {nullptr};
    tmp_op.get_attr<float>("cube_coeff", &fval);
    float float_value {-0.75};
    EXPECT_FLOAT_EQ(*fval, float_value);
}

TEST(OpSchema, InterpolateBackpropDefaultAttribute) {
    op_kind_t tmp_op_kind = kInterpolateBackprop;
    op_t tmp_op {0, tmp_op_kind, std::string("interpolate_bp")};

    const op_schema_t *opm = op_schema_registry_t::get_op_schema(tmp_op_kind);
    EXPECT_TRUE(opm != nullptr);
    opm->set_default_attribute(&tmp_op);

    const std::string *sval {nullptr};
    tmp_op.get_attr<std::string>("coordinate_transformation_mode", &sval);
    EXPECT_EQ(*sval, "half_pixel");

    tmp_op.get_attr<std::string>("nearest_mode", &sval);
    EXPECT_EQ(*sval, "round_prefer_floor");

    const bool *bval {nullptr};
    tmp_op.get_attr<bool>("antialias", &bval);
    EXPECT_FALSE(*bval);

    const std::vector<int64_t> *vval {nullptr};
    tmp_op.get_attr<std::vector<int64_t>>("pads_begin", &vval);
    std::vector<int64_t> vector_value(0, DNNL_GRAPH_MAX_NDIMS);
    EXPECT_EQ(*vval, vector_value);

    tmp_op.get_attr<std::vector<int64_t>>("pads_end", &vval);
    EXPECT_EQ(*vval, vector_value);

    const float *fval {nullptr};
    tmp_op.get_attr<float>("cube_coeff", &fval);
    float float_value {-0.75};
    EXPECT_FLOAT_EQ(*fval, float_value);
}

TEST(OpSchema, LayerNormDefaultAttribute) {
    op_kind_t tmp_op_kind = kLayerNorm;
    op_t tmp_op {0, tmp_op_kind, std::string("ln")};

    const op_schema_t *opm = op_schema_registry_t::get_op_schema(tmp_op_kind);
    EXPECT_TRUE(opm != nullptr);
    opm->set_default_attribute(&tmp_op);

    const bool *bval {nullptr};
    tmp_op.get_attr<bool>("keep_stats", &bval);
    EXPECT_TRUE(bval);

    const int64_t *ival {nullptr};
    tmp_op.get_attr<int64_t>("begin_norm_axis", &ival);
    int64_t int_value {-1};
    EXPECT_EQ(*ival, int_value);

    tmp_op.get_attr<bool>("use_affine", &bval);
    EXPECT_TRUE(bval);

    const float *fval {nullptr};
    tmp_op.get_attr<float>("epsilon", &fval);
    float float_value {1e-5f};
    EXPECT_FLOAT_EQ(*fval, float_value);
}

TEST(OpSchema, LayerNormBackpropDefaultAttribute) {
    op_kind_t tmp_op_kind = kLayerNormBackprop;
    op_t tmp_op {0, tmp_op_kind, std::string("ln_bp")};

    const op_schema_t *opm = op_schema_registry_t::get_op_schema(tmp_op_kind);
    EXPECT_TRUE(opm != nullptr);
    opm->set_default_attribute(&tmp_op);

    const bool *bval {nullptr};
    tmp_op.get_attr<bool>("use_affine", &bval);
    EXPECT_TRUE(bval);

    const int64_t *ival {nullptr};
    tmp_op.get_attr<int64_t>("begin_norm_axis", &ival);
    int64_t int_value {-1};
    EXPECT_EQ(*ival, int_value);

    tmp_op.get_attr<bool>("use_stats", &bval);
    EXPECT_TRUE(bval);

    const float *fval {nullptr};
    tmp_op.get_attr<float>("epsilon", &fval);
    float float_value {1e-5f};
    EXPECT_FLOAT_EQ(*fval, float_value);
}

TEST(OpSchema, LogSoftmaxDefaultAttribute) {
    op_kind_t tmp_op_kind = kLogSoftmax;
    op_t tmp_op {0, tmp_op_kind, std::string("log_softmax")};

    const op_schema_t *opm = op_schema_registry_t::get_op_schema(tmp_op_kind);
    EXPECT_TRUE(opm != nullptr);
    opm->set_default_attribute(&tmp_op);

    const int64_t *ival {nullptr};
    tmp_op.get_attr<int64_t>("axis", &ival);
    int64_t int_value {-1};
    EXPECT_EQ(*ival, int_value);
}

TEST(OpSchema, LogSoftmaxBackpropDefaultAttribute) {
    op_kind_t tmp_op_kind = kLogSoftmaxBackprop;
    op_t tmp_op {0, tmp_op_kind, std::string("log_softmax_bp")};

    const op_schema_t *opm = op_schema_registry_t::get_op_schema(tmp_op_kind);
    EXPECT_TRUE(opm != nullptr);
    opm->set_default_attribute(&tmp_op);

    const int64_t *ival {nullptr};
    tmp_op.get_attr<int64_t>("axis", &ival);
    int64_t int_value {-1};
    EXPECT_EQ(*ival, int_value);
}

TEST(OpSchema, MatmulDefaultAttribute) {
    op_kind_t tmp_op_kind = kMatMul;
    op_t tmp_op {0, tmp_op_kind, std::string("matmul")};

    const op_schema_t *opm = op_schema_registry_t::get_op_schema(tmp_op_kind);
    EXPECT_TRUE(opm != nullptr);
    opm->set_default_attribute(&tmp_op);

    const bool *bval {nullptr};
    tmp_op.get_attr<bool>("transpose_a", &bval);
    EXPECT_FALSE(*bval);

    tmp_op.get_attr<bool>("transpose_b", &bval);
    EXPECT_FALSE(*bval);
}

TEST(OpSchema, MaxPoolDefaultAttribute) {
    op_kind_t tmp_op_kind = kMaxPool;
    op_t tmp_op {0, tmp_op_kind, std::string("max_pool")};

    const op_schema_t *opm = op_schema_registry_t::get_op_schema(tmp_op_kind);
    EXPECT_TRUE(opm != nullptr);
    opm->set_default_attribute(&tmp_op);

    const std::string *sval {nullptr};
    tmp_op.get_attr<std::string>("data_format", &sval);
    EXPECT_EQ(*sval, "NXC");

    tmp_op.get_attr<std::string>("rounding_type", &sval);
    EXPECT_EQ(*sval, "floor");

    tmp_op.get_attr<std::string>("auto_pad", &sval);
    EXPECT_EQ(*sval, "None");

    const std::vector<int64_t> *vval {nullptr};
    tmp_op.get_attr<std::vector<int64_t>>("dilations", &vval);
    std::vector<int64_t> vector_value(1, DNNL_GRAPH_MAX_NDIMS);
    EXPECT_EQ(*vval, vector_value);
}

TEST(OpSchema, MaxPoolBackpropDefaultAttribute) {
    op_kind_t tmp_op_kind = kMaxPoolBackprop;
    op_t tmp_op {0, tmp_op_kind, std::string("max_pool_bp")};

    const op_schema_t *opm = op_schema_registry_t::get_op_schema(tmp_op_kind);
    EXPECT_TRUE(opm != nullptr);
    opm->set_default_attribute(&tmp_op);

    const std::string *sval {nullptr};
    tmp_op.get_attr<std::string>("data_format", &sval);
    EXPECT_EQ(*sval, "NXC");

    const std::vector<int64_t> *vval {nullptr};
    tmp_op.get_attr<std::vector<int64_t>>("dilations", &vval);
    std::vector<int64_t> vector_value(1, DNNL_GRAPH_MAX_NDIMS);
    EXPECT_EQ(*vval, vector_value);

    tmp_op.get_attr<std::string>("auto_pad", &sval);
    EXPECT_EQ(*sval, "None");
}

TEST(OpSchema, MaximumDefaultAttribute) {
    op_kind_t tmp_op_kind = kMaximum;
    op_t tmp_op {0, tmp_op_kind, std::string("max")};

    const op_schema_t *opm = op_schema_registry_t::get_op_schema(tmp_op_kind);
    EXPECT_TRUE(opm != nullptr);
    opm->set_default_attribute(&tmp_op);

    const std::string *sval {nullptr};
    tmp_op.get_attr<std::string>("auto_broadcast", &sval);
    EXPECT_EQ(*sval, "numpy");
}

TEST(OpSchema, MinimumDefaultAttribute) {
    op_kind_t tmp_op_kind = kMinimum;
    op_t tmp_op {0, tmp_op_kind, std::string("min")};

    const op_schema_t *opm = op_schema_registry_t::get_op_schema(tmp_op_kind);
    EXPECT_TRUE(opm != nullptr);
    opm->set_default_attribute(&tmp_op);

    const std::string *sval {nullptr};
    tmp_op.get_attr<std::string>("auto_broadcast", &sval);
    EXPECT_EQ(*sval, "numpy");
}

TEST(OpSchema, MultiplyDefaultAttribute) {
    op_kind_t tmp_op_kind = kMultiply;
    op_t tmp_op {0, tmp_op_kind, std::string("mul")};

    const op_schema_t *opm = op_schema_registry_t::get_op_schema(tmp_op_kind);
    EXPECT_TRUE(opm != nullptr);
    opm->set_default_attribute(&tmp_op);

    const std::string *sval {nullptr};
    tmp_op.get_attr<std::string>("auto_broadcast", &sval);
    EXPECT_EQ(*sval, "numpy");
}

TEST(OpSchema, PowDefaultAttribute) {
    op_kind_t tmp_op_kind = kPow;
    op_t tmp_op {0, tmp_op_kind, std::string("pow")};

    const op_schema_t *opm = op_schema_registry_t::get_op_schema(tmp_op_kind);
    EXPECT_TRUE(opm != nullptr);
    opm->set_default_attribute(&tmp_op);

    const std::string *sval {nullptr};
    tmp_op.get_attr<std::string>("auto_broadcast", &sval);
    EXPECT_EQ(*sval, "numpy");
}

TEST(OpSchema, ReduceSumDefaultAttribute) {
    op_kind_t tmp_op_kind = kReduceSum;
    op_t tmp_op {0, tmp_op_kind, std::string("reduce_sum")};

    const op_schema_t *opm = op_schema_registry_t::get_op_schema(tmp_op_kind);
    EXPECT_TRUE(opm != nullptr);
    opm->set_default_attribute(&tmp_op);

    const bool *bval {nullptr};
    tmp_op.get_attr<bool>("keep_dims", &bval);
    EXPECT_FALSE(*bval);
}

TEST(OpSchema, SigmoidBackpropDefaultAttribute) {
    op_kind_t tmp_op_kind = kSigmoidBackprop;
    op_t tmp_op {0, tmp_op_kind, std::string("sig_bp")};

    const op_schema_t *opm = op_schema_registry_t::get_op_schema(tmp_op_kind);
    EXPECT_TRUE(opm != nullptr);
    opm->set_default_attribute(&tmp_op);

    const bool *bval {nullptr};
    tmp_op.get_attr<bool>("use_dst", &bval);
    EXPECT_TRUE(bval);
}

TEST(OpSchema, SoftMaxDefaultAttribute) {
    op_kind_t tmp_op_kind = kSoftMax;
    op_t tmp_op {0, tmp_op_kind, std::string("softmax")};

    const op_schema_t *opm = op_schema_registry_t::get_op_schema(tmp_op_kind);
    EXPECT_TRUE(opm != nullptr);
    opm->set_default_attribute(&tmp_op);

    const int64_t *ival {nullptr};
    tmp_op.get_attr<int64_t>("axis", &ival);
    int64_t int_value {1};
    EXPECT_EQ(*ival, int_value);
}

TEST(OpSchema, SoftMaxBackpropDefaultAttribute) {
    op_kind_t tmp_op_kind = kSoftMaxBackprop;
    op_t tmp_op {0, tmp_op_kind, std::string("softmax_bp")};

    const op_schema_t *opm = op_schema_registry_t::get_op_schema(tmp_op_kind);
    EXPECT_TRUE(opm != nullptr);
    opm->set_default_attribute(&tmp_op);

    const int64_t *ival {nullptr};
    tmp_op.get_attr<int64_t>("axis", &ival);
    int64_t int_value {1};
    EXPECT_EQ(*ival, int_value);
}

TEST(OpSchema, SoftPlusDefaultAttribute) {
    op_kind_t tmp_op_kind = kSoftPlus;
    op_t tmp_op {0, tmp_op_kind, std::string("softplus")};

    const op_schema_t *opm = op_schema_registry_t::get_op_schema(tmp_op_kind);
    EXPECT_TRUE(opm != nullptr);
    opm->set_default_attribute(&tmp_op);

    const int64_t *ival {nullptr};
    tmp_op.get_attr<int64_t>("beta", &ival);
    int64_t int_value {1};
    EXPECT_EQ(*ival, int_value);
}

TEST(OpSchema, SoftPlusBackpropDefaultAttribute) {
    op_kind_t tmp_op_kind = kSoftPlusBackprop;
    op_t tmp_op {0, tmp_op_kind, std::string("softplus_bp")};

    const op_schema_t *opm = op_schema_registry_t::get_op_schema(tmp_op_kind);
    EXPECT_TRUE(opm != nullptr);
    opm->set_default_attribute(&tmp_op);

    const int64_t *ival {nullptr};
    tmp_op.get_attr<int64_t>("beta", &ival);
    int64_t int_value {1};
    EXPECT_EQ(*ival, int_value);
}

TEST(OpSchema, SqrtBackpropDefaultAttribute) {
    op_kind_t tmp_op_kind = kSqrtBackprop;
    op_t tmp_op {0, tmp_op_kind, std::string("sqrt_bp")};

    const op_schema_t *opm = op_schema_registry_t::get_op_schema(tmp_op_kind);
    EXPECT_TRUE(opm != nullptr);
    opm->set_default_attribute(&tmp_op);

    const bool *bval {nullptr};
    tmp_op.get_attr<bool>("use_dst", &bval);
    EXPECT_TRUE(bval);
}

TEST(OpSchema, TanhBackpropDefaultAttribute) {
    op_kind_t tmp_op_kind = kTanhBackprop;
    op_t tmp_op {0, tmp_op_kind, std::string("tanh_bp")};

    const op_schema_t *opm = op_schema_registry_t::get_op_schema(tmp_op_kind);
    EXPECT_TRUE(opm != nullptr);
    opm->set_default_attribute(&tmp_op);

    const bool *bval {nullptr};
    tmp_op.get_attr<bool>("use_dst", &bval);
    EXPECT_TRUE(bval);
}

TEST(OpSchema, MatmulTypeConstraints) {
    const op_schema_t *matmul_op_schema
            = op_schema_registry_t::get_op_schema(op_kind::MatMul);
    op_t matmul_op {0, kMatMul, std::string("matmul")};
    logical_tensor_t lt_data_a = logical_tensor_init(0, data_type::f32);
    logical_tensor_t lt_data_b = logical_tensor_init(1, data_type::f32);
    logical_tensor_t lt_out = logical_tensor_init(2, data_type::s8);

    matmul_op.add_input(lt_data_a);
    matmul_op.add_input(lt_data_b);
    matmul_op.add_output(lt_out);
    // MatMul op doesn't support s8 output
    EXPECT_FALSE(matmul_op_schema->verify(&matmul_op));
}

TEST(OpSchema, Quantize) {
    const op_schema_t *quant_op_schema
            = op_schema_registry_t::get_op_schema(op_kind::Quantize);
    op_t quant_op {0, kQuantize, std::string("quantize")};
    logical_tensor_t lt_data = logical_tensor_init(0, data_type::f32);
    logical_tensor_t lt_out = logical_tensor_init(1, data_type::s8);

    quant_op.add_input(lt_data);
    quant_op.add_output(lt_out);
    quant_op.set_attr("zps", std::vector<int64_t> {1});
    quant_op.set_attr("scales", std::vector<float> {0.1});
    EXPECT_TRUE(quant_op_schema->verify(&quant_op));
}

TEST(OpSchema, QuantizeWithFloatZps) {
    const op_schema_t *quant_op_schema
            = op_schema_registry_t::get_op_schema(op_kind::Quantize);
    op_t quant_op {0, kQuantize, std::string("quantize")};
    logical_tensor_t lt_data = logical_tensor_init(0, data_type::f32);
    logical_tensor_t lt_out = logical_tensor_init(1, data_type::s8);

    quant_op.add_input(lt_data);
    quant_op.add_output(lt_out);
    quant_op.set_attr("scales", std::vector<int64_t> {1});
    quant_op.set_attr("zps", std::vector<float> {0.1});

    //Quantize op does not support float zps and int64 scales
    EXPECT_FALSE(quant_op_schema->verify(&quant_op));
}

TEST(OpSchema, Dequantize) {
    const op_schema_t *dequant_op_schema
            = op_schema_registry_t::get_op_schema(op_kind::Dequantize);
    op_t dequant_op {0, kDequantize, std::string("dequantize")};
    logical_tensor_t lt_data = logical_tensor_init(0, data_type::u8);
    logical_tensor_t lt_out = logical_tensor_init(1, data_type::f32);

    dequant_op.add_input(lt_data);
    dequant_op.add_output(lt_out);
    dequant_op.set_attr("zps", std::vector<int64_t> {1});
    dequant_op.set_attr("scales", std::vector<float> {0.1});
    EXPECT_TRUE(dequant_op_schema->verify(&dequant_op));
}

TEST(OpSchema, LayerNormBf16) {
    const op_schema_t *schema = op_schema_registry_t::get_op_schema(kLayerNorm);

    op_t lnorm {0, kLayerNorm, std::string("layer_norm")};
    logical_tensor_t lt_data = logical_tensor_init(0, data_type::bf16);
    logical_tensor_t lt_gamma = logical_tensor_init(1, data_type::f32);
    logical_tensor_t lt_beta = logical_tensor_init(2, data_type::f32);
    logical_tensor_t lt_output = logical_tensor_init(5, data_type::bf16);

    lnorm.add_input(lt_data);
    lnorm.add_input(lt_gamma);
    lnorm.add_input(lt_beta);
    lnorm.add_output(lt_output);

    EXPECT_TRUE(schema->verify(&lnorm));
}

TEST(OpSchema, LayerNormBf16WithGamma) {
    const op_schema_t *schema = op_schema_registry_t::get_op_schema(kLayerNorm);

    op_t lnorm {0, kLayerNorm, std::string("layer_norm")};
    logical_tensor_t lt_data = logical_tensor_init(0, data_type::bf16);
    logical_tensor_t lt_gamma = logical_tensor_init(1, data_type::bf16);
    logical_tensor_t lt_beta = logical_tensor_init(2, data_type::bf16);
    logical_tensor_t lt_output = logical_tensor_init(5, data_type::bf16);

    lnorm.add_input(lt_data);
    lnorm.add_input(lt_gamma);
    lnorm.add_input(lt_beta);
    lnorm.add_output(lt_output);

    EXPECT_TRUE(schema->verify(&lnorm));
}

TEST(OpSchema, SoftmaxBf16) {
    const op_schema_t *schema = op_schema_registry_t::get_op_schema(kSoftMax);

    op_t softmax {0, kSoftMax, std::string("softmax")};
    logical_tensor_t lt_data = logical_tensor_init(0, data_type::bf16);
    logical_tensor_t lt_output = logical_tensor_init(1, data_type::bf16);

    softmax.add_input(lt_data);
    softmax.add_output(lt_output);

    softmax.set_attr<int64_t>("axis", 1);
    EXPECT_TRUE(schema->verify(&softmax));
}

TEST(OpSchema, LogSoftmaxBf16) {
    const op_schema_t *schema
            = op_schema_registry_t::get_op_schema(kLogSoftmax);

    op_t logsoftmax {0, kLogSoftmax, std::string("logsoftmax")};
    logical_tensor_t lt_data = logical_tensor_init(0, data_type::bf16);
    logical_tensor_t lt_output = logical_tensor_init(1, data_type::bf16);

    logsoftmax.add_input(lt_data);
    logsoftmax.add_output(lt_output);

    logsoftmax.set_attr<int64_t>("axis", 1);
    EXPECT_TRUE(schema->verify(&logsoftmax));
}

TEST(OpSchema, TypeCast) {
    const op_schema_t *schema
            = op_schema_registry_t::get_op_schema(op_kind::TypeCast);
    EXPECT_TRUE(schema != nullptr);

    op_t typecast {0, op_kind::TypeCast, std::string("typecast")};
    logical_tensor_t lt_data = logical_tensor_init(0, data_type::bf16);
    logical_tensor_t lt_output = logical_tensor_init(1, data_type::f32);

    typecast.add_input(lt_data);
    typecast.add_output(lt_output);

    EXPECT_TRUE(schema->verify(&typecast));
}

TEST(OpSchema, BatchNormInferenceWithBf16Data) {
    const op_schema_t *schema
            = op_schema_registry_t::get_op_schema(op_kind::BatchNormInference);

    op_t bn {0, op_kind::BatchNormInference, std::string("bn")};
    bn.set_attr("epsilon", 0.001f);
    logical_tensor_t lt_data = logical_tensor_init(0, data_type::bf16);
    logical_tensor_t lt_gamma = logical_tensor_init(1, data_type::f32);
    logical_tensor_t lt_beta = logical_tensor_init(2, data_type::f32);
    logical_tensor_t lt_mean = logical_tensor_init(3, data_type::f32);
    logical_tensor_t lt_var = logical_tensor_init(4, data_type::f32);
    logical_tensor_t lt_output = logical_tensor_init(5, data_type::bf16);

    bn.add_input(lt_data);
    bn.add_input(lt_gamma);
    bn.add_input(lt_beta);
    bn.add_input(lt_mean);
    bn.add_input(lt_var);
    bn.add_output(lt_output);

    EXPECT_TRUE(schema->verify(&bn));
}

TEST(OpSchema, BatchNormInferenceWithBf16Inputs) {
    const op_schema_t *schema
            = op_schema_registry_t::get_op_schema(op_kind::BatchNormInference);

    op_t bn {0, op_kind::BatchNormInference, std::string("bn")};
    bn.set_attr("epsilon", 0.001f);
    logical_tensor_t lt_data = logical_tensor_init(0, data_type::bf16);
    logical_tensor_t lt_gamma = logical_tensor_init(1, data_type::bf16);
    logical_tensor_t lt_beta = logical_tensor_init(2, data_type::bf16);
    logical_tensor_t lt_mean = logical_tensor_init(3, data_type::bf16);
    logical_tensor_t lt_var = logical_tensor_init(4, data_type::bf16);
    logical_tensor_t lt_output = logical_tensor_init(5, data_type::bf16);

    bn.add_input(lt_data);
    bn.add_input(lt_gamma);
    bn.add_input(lt_beta);
    bn.add_input(lt_mean);
    bn.add_input(lt_var);
    bn.add_output(lt_output);

    EXPECT_TRUE(schema->verify(&bn));
}

TEST(OpSchema, DynamicTranspose) {
    const op_kind_t op_kind_ = op_kind::DynamicTranspose;
    const size_t expected_in_size = 2;
    const size_t expected_out_size = 1;
    const size_t expected_attr_size = 0;
    const std::map<std::string, bool> attrs_data {};
    verify_op_schema(op_kind_, expected_in_size, expected_out_size,
            expected_attr_size, attrs_data);
}

TEST(OpSchema, DynamicReshape) {
    const op_kind_t op_kind_ = op_kind::DynamicReshape;
    const size_t expected_in_size = 2;
    const size_t expected_out_size = 1;
    const size_t expected_attr_size = 1;
    const std::map<std::string, bool> attrs_data = {{"special_zero", true}};
    verify_op_schema(op_kind_, expected_in_size, expected_out_size,
            expected_attr_size, attrs_data);
}

TEST(OpSchema, StaticTranspose) {
    const op_kind_t op_kind_ = op_kind::StaticTranspose;
    const size_t expected_in_size = 1;
    const size_t expected_out_size = 1;
    const size_t expected_attr_size = 1;
    const std::map<std::string, bool> attrs_data = {{"order", true}};
    verify_op_schema(op_kind_, expected_in_size, expected_out_size,
            expected_attr_size, attrs_data);
}

TEST(OpSchema, StaticReshape) {
    const op_kind_t op_kind_ = op_kind::StaticReshape;
    const size_t expected_in_size = 1;
    const size_t expected_out_size = 1;
    const size_t expected_attr_size = 2;
    const std::map<std::string, bool> attrs_data
            = {{"shape", true}, {"special_zero", true}};
    verify_op_schema(op_kind_, expected_in_size, expected_out_size,
            expected_attr_size, attrs_data);
}

TEST(OpSchema, InferStaticTransposeShape) {
    const op_schema_t *static_transpose_op_schema
            = op_schema_registry_t::get_op_schema(op_kind::StaticTranspose);

    op_t static_transpose_op {
            op_kind::StaticTranspose, op_t::kind2str(op_kind::StaticTranspose)};
    static_transpose_op.set_attr("order", std::vector<int64_t> {2, 0, 1});

    logical_tensor_t lt_in1
            = logical_tensor_init(0, {1024, 64, 32}, data_type::f32);
    std::vector<logical_tensor_t *> lt_in {&lt_in1};
    logical_tensor_t lt_o1
            = logical_tensor_init(1, data_type::f32, layout_type::strided);
    std::vector<logical_tensor_t *> lt_out1 {&lt_o1};

    static_transpose_op_schema->shape_infer(
            &static_transpose_op, lt_in, lt_out1);

    const std::vector<int64_t> infered_out_shape1
            = logical_tensor_wrapper_t(lt_o1).vdims();
    const std::vector<int64_t> expected_out_shape1 = {32, 1024, 64};
    EXPECT_EQ(infered_out_shape1, expected_out_shape1);

    // negative order
    static_transpose_op.set_attr("order", std::vector<int64_t> {-2, 2, 0});
    lt_in1 = logical_tensor_init(0, {2, 4, 1024}, data_type::f32);
    logical_tensor_t lt_o2
            = logical_tensor_init(2, data_type::f32, layout_type::strided);
    std::vector<logical_tensor_t *> lt_out2 {&lt_o2};
    static_transpose_op_schema->shape_infer(
            &static_transpose_op, lt_in, lt_out2);

    const std::vector<int64_t> infered_out_shape2
            = logical_tensor_wrapper_t(lt_o2).vdims();
    const std::vector<int64_t> expected_out_shape2 = {4, 1024, 2};
    EXPECT_EQ(infered_out_shape2, expected_out_shape2);

    // repeat order
    static_transpose_op.set_attr("order", std::vector<int64_t> {1, 1, 0});
    logical_tensor_t lt_o3
            = logical_tensor_init(2, data_type::f32, layout_type::strided);
    std::vector<logical_tensor_t *> lt_out3 {&lt_o3};
    status_t infer_status = static_transpose_op_schema->shape_infer(
            &static_transpose_op, lt_in, lt_out3);
    EXPECT_EQ(infer_status, status::invalid_shape);

    // order not cover all input axis
    static_transpose_op.set_attr("order", std::vector<int64_t> {1, 0});
    logical_tensor_t lt_o4
            = logical_tensor_init(2, data_type::f32, layout_type::strided);
    std::vector<logical_tensor_t *> lt_out4 {&lt_o4};
    infer_status = static_transpose_op_schema->shape_infer(
            &static_transpose_op, lt_in, lt_out4);
    EXPECT_EQ(infer_status, status::invalid_shape);

    // order out of range
    static_transpose_op.set_attr("order", std::vector<int64_t> {1, 3, 0});
    logical_tensor_t lt_o5
            = logical_tensor_init(2, data_type::f32, layout_type::strided);
    std::vector<logical_tensor_t *> lt_out5 {&lt_o5};
    infer_status = static_transpose_op_schema->shape_infer(
            &static_transpose_op, lt_in, lt_out5);
    EXPECT_EQ(infer_status, status::invalid_shape);

    // order is empty
    static_transpose_op.set_attr("order", std::vector<int64_t> {});
    logical_tensor_t lt_o6
            = logical_tensor_init(2, data_type::f32, layout_type::strided);
    std::vector<logical_tensor_t *> lt_out6 {&lt_o6};
    infer_status = static_transpose_op_schema->shape_infer(
            &static_transpose_op, lt_in, lt_out6);

    const std::vector<int64_t> infered_out_shape6
            = logical_tensor_wrapper_t(lt_o6).vdims();
    const std::vector<int64_t> expected_out_shape6 = {1024, 4, 2};
    EXPECT_EQ(infered_out_shape6, expected_out_shape6);
}

TEST(OpSchema, InferStaticReshapeShape) {
    const op_schema_t *static_reshape_op_schema
            = op_schema_registry_t::get_op_schema(op_kind::StaticReshape);

    op_t static_reshape_op {
            op_kind::StaticReshape, op_t::kind2str(op_kind::StaticReshape)};

    std::vector<int64_t> out_shape {16, 4, 8};
    static_reshape_op.set_attr("shape", out_shape);
    static_reshape_op.set_attr("special_zero", false);

    logical_tensor_t lt_in1
            = logical_tensor_init(0, {2, 8, 32}, data_type::f32);
    std::vector<logical_tensor_t *> lt_in {&lt_in1};
    logical_tensor_t lt_o1
            = logical_tensor_init(1, data_type::f32, layout_type::strided);
    std::vector<logical_tensor_t *> lt_out1 {&lt_o1};

    static_reshape_op_schema->shape_infer(&static_reshape_op, lt_in, lt_out1);

    std::vector<int64_t> infered_out_shape
            = logical_tensor_wrapper_t(lt_o1).vdims();
    EXPECT_EQ(infered_out_shape, out_shape);

    // test special zero true
    out_shape = {4, 0, 16};
    static_reshape_op.set_attr("shape", out_shape);
    static_reshape_op.set_attr("special_zero", true);
    lt_o1 = logical_tensor_init(2, data_type::f32, layout_type::strided);

    static_reshape_op_schema->shape_infer(&static_reshape_op, lt_in, lt_out1);

    infered_out_shape = logical_tensor_wrapper_t(lt_o1).vdims();
    std::vector<int64_t> expected_out_shape = {4, 8, 16};
    EXPECT_EQ(infered_out_shape, expected_out_shape);

    // test special zero false
    out_shape = {4, 0, 16};
    static_reshape_op.set_attr("shape", out_shape);
    static_reshape_op.set_attr("special_zero", false);
    lt_o1 = logical_tensor_init(3, data_type::f32, layout_type::strided);

    status_t infer_status = static_reshape_op_schema->shape_infer(
            &static_reshape_op, lt_in, lt_out1);
    EXPECT_EQ(infer_status, status::invalid_shape);

    // test -1 in shape
    out_shape = {8, 0, -1};
    static_reshape_op.set_attr("shape", out_shape);
    static_reshape_op.set_attr("special_zero", true);
    lt_o1 = logical_tensor_init(4, data_type::f32, layout_type::strided);

    static_reshape_op_schema->shape_infer(&static_reshape_op, lt_in, lt_out1);

    infered_out_shape = logical_tensor_wrapper_t(lt_o1).vdims();
    expected_out_shape = {8, 8, 8};
    EXPECT_EQ(infered_out_shape, expected_out_shape);

    // test input/output with different shape size
    out_shape = {4, 6, 16};
    static_reshape_op.set_attr("shape", out_shape);
    lt_o1 = logical_tensor_init(5, data_type::f32, layout_type::strided);

    infer_status = static_reshape_op_schema->shape_infer(
            &static_reshape_op, lt_in, lt_out1);
    EXPECT_EQ(infer_status, status::invalid_shape);

    // test invalid shape: more than one -1
    out_shape = {-1, -1, 16};
    static_reshape_op.set_attr("shape", out_shape);
    lt_o1 = logical_tensor_init(6, data_type::f32, layout_type::strided);

    infer_status = static_reshape_op_schema->shape_infer(
            &static_reshape_op, lt_in, lt_out1);
    EXPECT_EQ(infer_status, status::invalid_shape);

    // test invalid shape: < -1
    out_shape = {4, -6, 16};
    static_reshape_op.set_attr("shape", out_shape);
    lt_o1 = logical_tensor_init(7, data_type::f32, layout_type::strided);

    infer_status = static_reshape_op_schema->shape_infer(
            &static_reshape_op, lt_in, lt_out1);
    EXPECT_EQ(infer_status, status::invalid_shape);

    // test shape contains 0-D
    out_shape = {0, 4, 7};
    static_reshape_op.set_attr("shape", out_shape);
    static_reshape_op.set_attr("special_zero", false);

    logical_tensor_t lt_in8
            = logical_tensor_init(8, {0, 2, 8, 6}, data_type::f32);
    lt_in = {&lt_in8};
    logical_tensor_t lt_o9
            = logical_tensor_init(9, data_type::f32, layout_type::strided);
    std::vector<logical_tensor_t *> lt_out9 {&lt_o9};

    static_reshape_op_schema->shape_infer(&static_reshape_op, lt_in, lt_out9);

    infered_out_shape = logical_tensor_wrapper_t(lt_o9).vdims();
    EXPECT_EQ(infered_out_shape, out_shape);

    // test invalid shape case
    out_shape = {-1, 0};
    static_reshape_op.set_attr("shape", out_shape);
    lt_o9 = logical_tensor_init(10, data_type::f32, layout_type::strided);

    infer_status = static_reshape_op_schema->shape_infer(
            &static_reshape_op, lt_in, lt_out9);
    EXPECT_EQ(infer_status, status::invalid_shape);

    // test input/output with different shape size
    out_shape = {-1, 0};
    static_reshape_op.set_attr("shape", out_shape);
    lt_o9 = logical_tensor_init(11, data_type::f32, layout_type::strided);

    infer_status = static_reshape_op_schema->shape_infer(
            &static_reshape_op, lt_in, lt_out9);
    EXPECT_EQ(infer_status, status::invalid_shape);
}

TEST(OpSchema, FailToAddWildcard) {
    const op_schema_t *wildcard_op_schema
            = op_schema_registry_t::get_op_schema(op_kind::Wildcard);
    op_t wildcard_op {0, kWildcard, std::string("wildcard")};
    logical_tensor_t lt_data_a = logical_tensor_init(0, data_type::f32);
    logical_tensor_t lt_data_b = logical_tensor_init(1, data_type::f16);
    logical_tensor_t lt_out = logical_tensor_init(2, data_type::bf16);

    wildcard_op.add_input(lt_data_a);
    wildcard_op.add_input(lt_data_b);
    wildcard_op.add_output(lt_out);
    EXPECT_TRUE(wildcard_op_schema->verify(&wildcard_op));
}