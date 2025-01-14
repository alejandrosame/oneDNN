/*******************************************************************************
* Copyright 2019-2022 Intel Corporation
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

#ifndef CPU_MATMUL_GEMM_BASED_COMMON_HPP
#define CPU_MATMUL_GEMM_BASED_COMMON_HPP

#include <assert.h>

#include "common/c_types_map.hpp"
#include "common/dnnl_thread.hpp"
#include "common/primitive_attr.hpp"
#include "common/type_helpers.hpp"

#include "cpu/matmul/cpu_matmul_pd.hpp"

namespace dnnl {
namespace impl {
namespace cpu {
namespace matmul {
namespace gemm_based {

struct params_t {
    // indicates if an auxiliary array for intermediate computations is not
    // required
    bool dst_is_acc_;

    // indicates if output scales from attributes are applied
    // by gemm (alpha parameter) or post-op kernel (pp_kernel_)
    bool gemm_applies_output_scales_ = false;

    // sum post-op scaling factor that is fused into gemm
    float gemm_beta_ = 0.f;

    // indicates if a special post processing kernel
    // should be invoked after gemm
    bool has_pp_kernel_ = false;

    // indicates if src batch dims can be fused into M, so that a single
    // GeMM call can be made
    bool can_fuse_src_batch_dims_ = false;

    float default_pp_scales_ = 1.0f;

    // an attribute for post processing kernel
    primitive_attr_t pp_attr_;

    // auxiliary functions

    // returns gemm alpha parameter (a single value for now)
    float get_gemm_alpha(const float *primitive_scales) const {
        return gemm_applies_output_scales_ ? primitive_scales[0] : 1.f;
    }

    // returns scaling factors for post processing kernel
    const float *get_post_processing_scales(
            const float *primitive_scales) const {
        return gemm_applies_output_scales_ ? &default_pp_scales_
                                           : primitive_scales;
    }
};

inline bool check_gemm_compatible_formats(const matmul_pd_t &pd) {

    const memory_desc_wrapper dst_d(pd.dst_md());
    const int ndims = dst_d.ndims();

    auto check_input_format = [=](const memory_desc_t *md) {
        memory_desc_wrapper mdw(md);

        if (!mdw.is_plain()) return false;

        const dims_t &strides = mdw.blocking_desc().strides;

        // disable md with zero stride for a particular dimension
        for (int dim = 0; dim < ndims; ++dim)
            if (strides[dim] == 0) return false;

        // for GeMM atleast one of the two innermost axes must be contiguous
        return utils::one_of(1, strides[ndims - 1], strides[ndims - 2]);
    };

    bool ok = check_input_format(pd.src_md())
            && check_input_format(pd.weights_md()) && dst_d.is_plain()
            && dst_d.blocking_desc().strides[ndims - 1] == 1;

    return ok;
}

inline bool check_gemm_binary_per_oc_compatible_formats(const matmul_pd_t &pd) {
    const memory_desc_wrapper dst_d(pd.dst_md());
    const dims_t &strides = dst_d.blocking_desc().strides;
    const dims_t &dims = dst_d.dims();
    const int ndims = dst_d.ndims();

    // check d, h, w... (b2, m, n... for matmul) dimensions are continuous
    bool ok = true;
    for (int i = 2; i < ndims - 1; i++)
        ok = ok && strides[i] == strides[i + 1] * dims[i + 1];
    // only allowed for nchw and nhwc (b0xb1xMxN or b0xMxNxb1 for matmul)
    return ok && strides[0] == utils::array_product(dims + 1, ndims - 1);
}

inline size_t get_scratchpad_size(const dim_t batch, dim_t M, const dim_t N,
        const bool can_fuse_src_batch_dims, const int nthr) {
    assert(batch > 0);
    assert(M > 0);
    assert(N > 0);
    size_t buffer_size;
    if (can_fuse_src_batch_dims || batch == 1) {
        buffer_size = (size_t)batch * M * N;
    } else {
        const size_t work_per_thr = utils::div_up((size_t)batch * M * N, nthr);
        if (work_per_thr >= (size_t)N) {
            buffer_size = nstl::min<size_t>(
                    (size_t)M * N, utils::rnd_dn(work_per_thr, N));
        } else {
            buffer_size = work_per_thr;
        }
    }
    return utils::rnd_up(buffer_size, 64);
}

inline void book_acc_scratchpad(matmul_pd_t &pd, const params_t &params,
        size_t sizeof_acc_data, const int nthr) {

    if (!params.dst_is_acc_
            && !memory_desc_wrapper(pd.dst_md()).has_runtime_dims()) {
        const size_t buffer_size = get_scratchpad_size(pd.batch(), pd.M(),
                pd.N(), params.can_fuse_src_batch_dims_, nthr);
        const size_t sp_size = params.can_fuse_src_batch_dims_
                ? buffer_size
                : buffer_size * nthr;
        auto scratchpad = pd.scratchpad_registry().registrar();
        scratchpad.book(memory_tracking::names::key_matmul_dst_in_acc_dt,
                sp_size, sizeof_acc_data);
    }
}

} // namespace gemm_based
} // namespace matmul
} // namespace cpu
} // namespace impl
} // namespace dnnl

#endif
