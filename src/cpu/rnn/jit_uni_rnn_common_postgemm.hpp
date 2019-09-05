/*******************************************************************************
* Copyright 2019 Intel Corporation
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

#ifndef CPU_JIT_RNN_POSTGEMM
#define CPU_JIT_RNN_POSTGEMM

#include "dnnl_thread.hpp"

#include "c_types_map.hpp"
#include "utils.hpp"

#include "../jit_generator.hpp"
#include "../jit_uni_eltwise.hpp"

#include "rnn_pd.hpp"
#include "rnn_utils.hpp"

namespace dnnl {
namespace impl {
namespace cpu {

struct jit_uni_rnn_postgemm : public jit_generator {

    typedef void (*kernel_t)(void *param1_, void *param2_, const void *param3_,
            void *param4_, void *param5_, void *param6_, void *param7_);

    jit_uni_rnn_postgemm(const rnn_utils::rnn_conf_t &rnn, const rnn_pd_t *pd)
        : rnn_(rnn), pd_(pd) {}

    virtual void init() = 0;

    template <typename src_data_t, typename acc_data_t, typename scratch_data_t>
    rnn_postgemm_sig(execute) {
        rnn_utils::ws_gates_aoc<src_data_t> ws_gates(rnn, ws_gates_);
        rnn_utils::ws_gates_aoc<scratch_data_t> scratch_gates(
                rnn, scratch_gates_);
        rnn_utils::bias_aoc_t bias(rnn, bias_);
        rnn_utils::ws_states_aoc<src_data_t> states_t_l(rnn, states_t_l_);
        rnn_utils::ws_states_aoc<src_data_t> states_tm1_l(rnn, states_tm1_l_);
        rnn_utils::ws_states_aoc<float> c_states_t_l(rnn, c_states_t_l_);
        rnn_utils::ws_states_aoc<float> c_states_tm1_l(rnn, c_states_tm1_l_);
        rnn_utils::ws_gates_aoc<scratch_data_t> scratch_cell(
                rnn, scratch_cell_);
        utils::array_offset_calculator<src_data_t, 2> ws_Wh_b(
                ws_grid_, rnn.mb, rnn.dic);

        // Todo: add parallelization on dic for the batch 1 case
        // Assumption: the kernel runs a loop on dic elements
        parallel_nd(rnn.mb, [&](int i) {
            void *param1_ = &ws_gates(i, 0, 0); // RNN, LSTM, GRU
            void *param2_ = &scratch_gates(i, 0, 0); // RNN, LSTM, GRU
            const void *param3_ = &bias(0, 0); // RNN, LSTM, GRU
            void *param4_ = &states_t_l(i, 0); // RNN, LSTM, GRU
            void *param5_, *param6_, *param7_;
            switch (pd_->cell_kind()) {
                case alg_kind::vanilla_lstm:
                    param5_ = &c_states_tm1_l(i, 0);
                    param6_ = &c_states_t_l(i, 0);
                    param7_ = nullptr;
                    break;
                case alg_kind::lbr_gru:
                    param5_ = &states_tm1_l(i, 0);
                    param6_ = &scratch_cell(i, 0, 0);
                    param7_ = &ws_Wh_b(i, 0);
                    break;
                case alg_kind::vanilla_gru:
                    param5_ = &states_tm1_l(i, 0);
                    param6_ = nullptr;
                    param7_ = nullptr;
                    break;
                default:
                    param5_ = nullptr;
                    param6_ = nullptr;
                    param7_ = nullptr;
                    break;
            }
            kernel_(param1_, param2_, param3_, param4_, param5_, param6_,
                    param7_);
        });
    }

protected:
    kernel_t kernel_;
    const rnn_utils::rnn_conf_t &rnn_;
    const rnn_pd_t *pd_;
};

} // namespace cpu
} // namespace impl
} // namespace dnnl

#endif
