# Standard DSA benchmark results

Every row is a capacity-free, single-pool standard DSA problem. Public MiniMalloc instances are used directly. PyPTO rows are per-pool projections that retain buffer sizes and lifetimes but remove compiler-specific constraints, alignment, capacity, and architecture resources; they are not device-valid PyPTO placements.

Raw per-run records are in `results.jsonl`; `summary.csv` is the authoritative long-form aggregation. Peak values are bytes. A dagger (`†`) marks a feasible MiniMalloc result whose optimality was not proved before the timeout.

Configuration: run label `standard-v1`; seeds `0,1,2`; search budget `2000`; local-search restarts `4`; deterministic repetitions `5`; MiniMalloc timeout `5000 ms` per instance. Peak is the best valid result. Runtime is the median across deterministic repetitions or stochastic seeds; MiniMalloc is one bounded run. All returned placements are independently validated.

Regenerate from the repository root:

```bash
./build/dsa-suite \
  --standard 'third_party/minimalloc/benchmarks/challenging' \
  --pypto 'benchmarks/pypto' \
  --pypto 'benchmarks/pypto-lib' \
  --output-dir 'benchmarks/results/standard-v1' \
  --run-label 'standard-v1' \
  --seeds '0,1,2' \
  --iterations '2000' \
  --restarts '4' \
  --stagnation '250' \
  --deterministic-repetitions '5' \
  --minimalloc-timeout-ms '5000' \
  --standard-only
```

## Peak memory (bytes)

| Instance | Origin | Buffers | MiniMalloc exact | First fit | XLA heap | TVM hill climb | Local search |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: |
| A.1048576.csv | MiniMalloc | 154 | 1063936† | 1352704 | 1374208 | 1234944 | 1284096 |
| B.1048576.csv | MiniMalloc | 170 | 1081344† | 1412096 | 1412096 | 1177600 | 1250304 |
| C.1048576.csv | MiniMalloc | 203 | 1039360 | 1417216 | 1417216 | 1222656 | 1234944 |
| D.1048576.csv | MiniMalloc | 213 | 1041408† | 1291264 | 1304576 | 1215488 | 1188864 |
| E.1048576.csv | MiniMalloc | 215 | 1471488† | 1435648 | 1469440 | 1252352 | 1314816 |
| F.1048576.csv | MiniMalloc | 296 | 1165312† | 1441792 | 1433600 | 1243136 | 1262592 |
| G.1048576.csv | MiniMalloc | 308 | 1119232† | 1396736 | 1428480 | 1282048 | 1280000 |
| H.1048576.csv | MiniMalloc | 316 | 1152000† | 1421312 | 1405952 | 1227776 | 1219584 |
| I.1048576.csv | MiniMalloc | 374 | 1464320† | 1478656 | 1478656 | 1303552 | 1292288 |
| J.1048576.csv | MiniMalloc | 409 | 1137664† | 1303552 | 1333248 | 1157120 | 1230848 |
| K.1048576.csv | MiniMalloc | 454 | 1256448† | 1352704 | 1397760 | 1285120 | 1316864 |
| issue_1908_fragmentation::Vec | PyPTO/Vec | 4 | 65536 | 65536 | 65536 | 65536 | 65536 |
| pypto-lib::examples::advanced::allreduce::pypto_reduce_step::Vec | PyPTO-Lib/examples/Vec | 3 | 2048 | 2048 | 2048 | 2048 | 2048 |
| pypto-lib::examples::advanced::multi_proj::pypto_proj::Left | PyPTO-Lib/examples/Left | 4 | 4096 | 4096 | 4096 | 4096 | 4096 |
| pypto-lib::examples::advanced::topk::pypto_topk_block::Vec | PyPTO-Lib/examples/Vec | 7 | 6208 | 6208 | 6208 | 6208 | 6208 |
| pypto-lib::examples::intermediate::layer_norm::pypto_layer_norm_rows::Vec | PyPTO-Lib/examples/Vec | 19 | 131072 | 131072 | 131072 | 131072 | 131072 |
| pypto-lib::examples::intermediate::rms_norm::pypto_rms_norm_rows::Vec | PyPTO-Lib/examples/Vec | 12 | 49408 | 49408 | 49408 | 49408 | 49408 |
| pypto-lib::examples::intermediate::rope::pypto_rope_rotate::Vec | PyPTO-Lib/examples/Vec | 12 | 8704 | 8704 | 8704 | 8704 | 8704 |
| pypto-lib::examples::intermediate::softmax::pypto_softmax_rows::Vec | PyPTO-Lib/examples/Vec | 8 | 131072 | 131072 | 131072 | 131072 | 131072 |
| pypto-lib::models::deepseek::v3_2::deepseek_v3_2_decode_back::pypto_deepseek_v3_2_decode_back_layer_incore_0::Mat | PyPTO-Lib/deepseek-v3_2/Mat | 4 | 20480 | 20480 | 20480 | 20480 | 20480 |
| pypto-lib::models::deepseek::v3_2::deepseek_v3_2_decode_back::pypto_deepseek_v3_2_decode_back_layer_incore_1::Vec | PyPTO-Lib/deepseek-v3_2/Vec | 4 | 8192 | 8192 | 8192 | 8192 | 8192 |
| pypto-lib::models::deepseek::v3_2::deepseek_v3_2_decode_back::pypto_deepseek_v3_2_decode_back_layer_incore_2::Vec | PyPTO-Lib/deepseek-v3_2/Vec | 14 | 16448 | 16448 | 16448 | 16448 | 16448 |
| pypto-lib::models::deepseek::v3_2::deepseek_v3_2_decode_back::pypto_deepseek_v3_2_decode_back_layer_incore_3::Mat | PyPTO-Lib/deepseek-v3_2/Mat | 4 | 69632 | 69632 | 69632 | 69632 | 69632 |
| pypto-lib::models::deepseek::v3_2::deepseek_v3_2_decode_back::pypto_deepseek_v3_2_decode_back_layer_incore_5::Vec | PyPTO-Lib/deepseek-v3_2/Vec | 9 | 49152 | 49152 | 49152 | 49152 | 49152 |
| pypto-lib::models::deepseek::v3_2::deepseek_v3_2_decode_back::pypto_deepseek_v3_2_decode_back_layer_incore_6::Left | PyPTO-Lib/deepseek-v3_2/Left | 4 | 8192 | 8192 | 8192 | 8192 | 8192 |
| pypto-lib::models::deepseek::v3_2::deepseek_v3_2_decode_back::pypto_deepseek_v3_2_decode_back_layer_incore_6::Mat | PyPTO-Lib/deepseek-v3_2/Mat | 4 | 73728 | 73728 | 73728 | 73728 | 73728 |
| pypto-lib::models::deepseek::v3_2::deepseek_v3_2_decode_back::pypto_deepseek_v3_2_decode_back_layer_incore_7::Vec | PyPTO-Lib/deepseek-v3_2/Vec | 4 | 16384 | 16384 | 16384 | 16384 | 16384 |
| pypto-lib::models::deepseek::v3_2::deepseek_v3_2_decode_front::pypto_decode_cache_write::Vec | PyPTO-Lib/deepseek-v3_2/Vec | 17 | 1792 | 1792 | 1792 | 1792 | 1792 |
| pypto-lib::models::deepseek::v3_2::deepseek_v3_2_decode_front::pypto_input_rmsnorm::Vec | PyPTO-Lib/deepseek-v3_2/Vec | 16 | 65600 | 65600 | 65600 | 65600 | 65600 |
| pypto-lib::models::deepseek::v3_2::deepseek_v3_2_decode_front::pypto_kv_a_proj::Mat | PyPTO-Lib/deepseek-v3_2/Mat | 4 | 81920 | 81920 | 81920 | 81920 | 81920 |
| pypto-lib::models::deepseek::v3_2::deepseek_v3_2_decode_front::pypto_kv_rmsnorm::Vec | PyPTO-Lib/deepseek-v3_2/Vec | 13 | 98304 | 98304 | 98304 | 98304 | 98304 |
| pypto-lib::models::deepseek::v3_2::deepseek_v3_2_decode_front::pypto_q_head_proj::Mat | PyPTO-Lib/deepseek-v3_2/Mat | 4 | 10240 | 10240 | 10240 | 10240 | 10240 |
| pypto-lib::models::deepseek::v3_2::deepseek_v3_2_decode_front::pypto_q_lora_rmsnorm::Vec | PyPTO-Lib/deepseek-v3_2/Vec | 16 | 12352 | 12352 | 12352 | 12352 | 12352 |
| pypto-lib::models::deepseek::v3_2::deepseek_v3_2_decode_front::pypto_q_rope::Vec | PyPTO-Lib/deepseek-v3_2/Vec | 16 | 1024 | 1024 | 1024 | 1024 | 1024 |
| pypto-lib::models::deepseek::v3_2::deepseek_v3_2_decode_front::pypto_s2_idx_rope::Vec | PyPTO-Lib/deepseek-v3_2/Vec | 28 | 1024 | 1024 | 1024 | 1024 | 1024 |
| pypto-lib::models::deepseek::v3_2::deepseek_v3_2_decode_front::pypto_s2_k_idx_layernorm::Vec | PyPTO-Lib/deepseek-v3_2/Vec | 22 | 33792 | 33792 | 33792 | 33792 | 33792 |
| pypto-lib::models::deepseek::v3_2::deepseek_v3_2_decode_front::pypto_s2_q_reduce::Vec | PyPTO-Lib/deepseek-v3_2/Vec | 5 | 512 | 512 | 512 | 512 | 512 |
| pypto-lib::models::deepseek::v3_2::deepseek_v3_2_decode_front::pypto_s3_sort::Vec | PyPTO-Lib/deepseek-v3_2/Vec | 7 | 65536 | 65536 | 65536 | 65536 | 65536 |
| pypto-lib::models::deepseek::v3_2::deepseek_v3_2_decode_front::pypto_s4_q_pe_load::Vec | PyPTO-Lib/deepseek-v3_2/Vec | 9 | 40960 | 40960 | 40960 | 40960 | 40960 |
| pypto-lib::models::deepseek::v3_2::deepseek_v3_2_decode_front::pypto_s4_softmax::Vec | PyPTO-Lib/deepseek-v3_2/Vec | 46 | 172160 | 172160 | 172160 | 172160 | 172160 |
| pypto-lib::models::deepseek::v3_2::deepseek_v3_2_prefill_back::pypto_deepseek_v3_2_prefill_back_layer_incore_2::Vec | PyPTO-Lib/deepseek-v3_2/Vec | 4 | 32768 | 32768 | 32768 | 32768 | 32768 |
| pypto-lib::models::deepseek::v3_2::deepseek_v3_2_prefill_back::pypto_deepseek_v3_2_prefill_back_layer_incore_3::Vec | PyPTO-Lib/deepseek-v3_2/Vec | 8 | 65792 | 65792 | 65792 | 65792 | 65792 |
| pypto-lib::models::deepseek::v3_2::deepseek_v3_2_prefill_back::pypto_deepseek_v3_2_prefill_back_layer_incore_4::Vec | PyPTO-Lib/deepseek-v3_2/Vec | 6 | 33536 | 33536 | 33536 | 33536 | 33536 |
| pypto-lib::models::deepseek::v3_2::deepseek_v3_2_prefill_back::pypto_deepseek_v3_2_prefill_back_layer_incore_5::Mat | PyPTO-Lib/deepseek-v3_2/Mat | 4 | 49152 | 49152 | 49152 | 49152 | 49152 |
| pypto-lib::models::deepseek::v3_2::deepseek_v3_2_prefill_back::pypto_deepseek_v3_2_prefill_back_layer_incore_7::Vec | PyPTO-Lib/deepseek-v3_2/Vec | 9 | 98304 | 98304 | 98304 | 98304 | 98304 |
| pypto-lib::models::deepseek::v3_2::deepseek_v3_2_prefill_back::pypto_deepseek_v3_2_prefill_back_layer_incore_9::Vec | PyPTO-Lib/deepseek-v3_2/Vec | 4 | 65536 | 65536 | 65536 | 65536 | 65536 |
| pypto-lib::models::deepseek::v4::decode_attention_csa::pypto_comb_sinkhorn::Vec | PyPTO-Lib/deepseek-v4/Vec | 123 | 2848 | 2848 | 2848 | 2848 | 2848 |
| pypto-lib::models::deepseek::v4::decode_attention_csa::pypto_csa_rope_step::Vec | PyPTO-Lib/deepseek-v4/Vec | 10 | 512 | 512 | 512 | 512 | 512 |
| pypto-lib::models::deepseek::v4::decode_attention_csa::pypto_csa_slots_build_valid_qk_plan::Vec | PyPTO-Lib/deepseek-v4/Vec | 32 | 49152 | 49152 | 49152 | 49152 | 49152 |
| pypto-lib::models::deepseek::v4::decode_attention_csa::pypto_hc_post::Vec | PyPTO-Lib/deepseek-v4/Vec | 42 | 90112 | 90112 | 90112 | 90112 | 90112 |
| pypto-lib::models::deepseek::v4::decode_attention_csa::pypto_hc_pre_rms::Vec | PyPTO-Lib/deepseek-v4/Vec | 24 | 81952 | 81952 | 81952 | 81952 | 81952 |
| pypto-lib::models::deepseek::v4::decode_attention_csa::pypto_idx_qr_proj_dequant::Vec | PyPTO-Lib/deepseek-v4/Vec | 6 | 36896 | 36896 | 36896 | 36896 | 36896 |
| pypto-lib::models::deepseek::v4::decode_attention_csa::pypto_kv_and_cache_write::Vec | PyPTO-Lib/deepseek-v4/Vec | 16 | 24576 | 24576 | 24576 | 24576 | 24576 |
| pypto-lib::models::deepseek::v4::decode_attention_csa::pypto_kv_rms_norm_rope::Vec | PyPTO-Lib/deepseek-v4/Vec | 71 | 14592 | 14592 | 14592 | 14592 | 14592 |
| pypto-lib::models::deepseek::v4::decode_attention_csa::pypto_kv_score_proj::Left | PyPTO-Lib/deepseek-v4/Left | 16 | 16384 | 16384 | 16384 | 16384 | 16384 |
| pypto-lib::models::deepseek::v4::decode_attention_csa::pypto_kv_score_proj::Right | PyPTO-Lib/deepseek-v4/Right | 16 | 65536 | 65536 | 65536 | 65536 | 65536 |
| pypto-lib::models::deepseek::v4::decode_attention_csa::pypto_merge_norm::Vec | PyPTO-Lib/deepseek-v4/Vec | 47 | 98624 | 98624 | 98624 | 98624 | 98624 |
| pypto-lib::models::deepseek::v4::decode_attention_csa::pypto_mix_x::Vec | PyPTO-Lib/deepseek-v4/Vec | 27 | 65792 | 65792 | 65792 | 65792 | 65792 |
| pypto-lib::models::deepseek::v4::decode_attention_csa::pypto_proj_a_mm::Mat | PyPTO-Lib/deepseek-v4/Mat | 8 | 147456 | 147456 | 147456 | 147456 | 147456 |
| pypto-lib::models::deepseek::v4::decode_attention_csa::pypto_proj_b_act::Vec | PyPTO-Lib/deepseek-v4/Vec | 13 | 51264 | 51264 | 51264 | 51264 | 51264 |
| pypto-lib::models::deepseek::v4::decode_attention_csa::pypto_proj_b_mm::Left | PyPTO-Lib/deepseek-v4/Left | 8 | 4096 | 4096 | 4096 | 4096 | 4096 |
| pypto-lib::models::deepseek::v4::decode_attention_csa::pypto_proj_b_mm::Mat | PyPTO-Lib/deepseek-v4/Mat | 8 | 69632 | 69632 | 69632 | 69632 | 69632 |
| pypto-lib::models::deepseek::v4::decode_attention_csa::pypto_q_rope_prepare::Vec | PyPTO-Lib/deepseek-v4/Vec | 27 | 12544 | 12544 | 12544 | 12544 | 12544 |
| pypto-lib::models::deepseek::v4::decode_attention_csa::pypto_qk_pv_aic::Left | PyPTO-Lib/deepseek-v4/Left | 8 | 16384 | 16384 | 16384 | 16384 | 16384 |
| pypto-lib::models::deepseek::v4::decode_attention_csa::pypto_qk_pv_aic::Mat | PyPTO-Lib/deepseek-v4/Mat | 3 | 163840 | 163840 | 163840 | 163840 | 163840 |
| pypto-lib::models::deepseek::v4::decode_attention_csa::pypto_qk_pv_aiv::Vec | PyPTO-Lib/deepseek-v4/Vec | 26 | 33408 | 33408 | 33408 | 33408 | 33408 |
| pypto-lib::models::deepseek::v4::decode_attention_csa::pypto_qproj_dequant_rms_nope_rope::Vec | PyPTO-Lib/deepseek-v4/Vec | 48 | 73760 | 73760 | 73760 | 73760 | 73760 |
| pypto-lib::models::deepseek::v4::decode_attention_csa::pypto_qproj_matmul::Left | PyPTO-Lib/deepseek-v4/Left | 8 | 1024 | 1024 | 1024 | 1024 | 1024 |
| pypto-lib::models::deepseek::v4::decode_attention_csa::pypto_qr_hadamard_quant::Vec | PyPTO-Lib/deepseek-v4/Vec | 14 | 49408 | 49408 | 49408 | 49408 | 49408 |
| pypto-lib::models::deepseek::v4::decode_attention_csa::pypto_qr_rms_norm_quant::Vec | PyPTO-Lib/deepseek-v4/Vec | 52 | 33856 | 33856 | 33856 | 33856 | 33856 |
| pypto-lib::models::deepseek::v4::decode_attention_csa::pypto_qr_rope::Vec | PyPTO-Lib/deepseek-v4/Vec | 39 | 45312 | 45312 | 45312 | 45312 | 45312 |
| pypto-lib::models::deepseek::v4::decode_attention_csa::pypto_quant::Vec | PyPTO-Lib/deepseek-v4/Vec | 16 | 65536 | 65536 | 65536 | 65536 | 65536 |
| pypto-lib::models::deepseek::v4::decode_attention_csa::pypto_rms_norm::Vec | PyPTO-Lib/deepseek-v4/Vec | 30 | 10272 | 10304 | 10304 | 10272 | 10272 |
| pypto-lib::models::deepseek::v4::decode_attention_csa::pypto_rmsnorm_rope::Vec | PyPTO-Lib/deepseek-v4/Vec | 54 | 26880 | 26880 | 26880 | 26880 | 26880 |
| pypto-lib::models::deepseek::v4::decode_attention_csa::pypto_rmsnorm_rope_cache_write::Vec | PyPTO-Lib/deepseek-v4/Vec | 54 | 26880 | 26880 | 26880 | 26880 | 26880 |
| pypto-lib::models::deepseek::v4::decode_attention_csa::pypto_rope_cs::Vec | PyPTO-Lib/deepseek-v4/Vec | 24 | 8448 | 8448 | 8448 | 8448 | 8448 |
| pypto-lib::models::deepseek::v4::decode_attention_csa::pypto_scatter_softmax_pool::Vec | PyPTO-Lib/deepseek-v4/Vec | 35 | 16384 | 16384 | 16384 | 16384 | 16384 |
| pypto-lib::models::deepseek::v4::decode_attention_csa::pypto_scatter_softmax_pool_0::Vec | PyPTO-Lib/deepseek-v4/Vec | 35 | 3072 | 3072 | 3072 | 3072 | 3072 |
| pypto-lib::models::deepseek::v4::decode_attention_csa::pypto_score_mat::Mat | PyPTO-Lib/deepseek-v4/Mat | 4 | 40960 | 40960 | 40960 | 40960 | 40960 |
| pypto-lib::models::deepseek::v4::decode_attention_csa::pypto_score_reduce::Vec | PyPTO-Lib/deepseek-v4/Vec | 41 | 132608 | 132608 | 132608 | 132608 | 132608 |
| pypto-lib::models::deepseek::v4::decode_attention_csa::pypto_split_pre_post::Vec | PyPTO-Lib/deepseek-v4/Vec | 23 | 544 | 544 | 544 | 544 | 544 |
| pypto-lib::models::deepseek::v4::decode_attention_csa::pypto_topk::Vec | PyPTO-Lib/deepseek-v4/Vec | 14 | 40960 | 40960 | 40960 | 40960 | 40960 |
| pypto-lib::models::deepseek::v4::decode_attention_csa::pypto_weights_proj_reduce::Vec | PyPTO-Lib/deepseek-v4/Vec | 8 | 8192 | 8192 | 8192 | 8192 | 8192 |
| pypto-lib::models::deepseek::v4::decode_attention_hca::pypto_build_valid::Vec | PyPTO-Lib/deepseek-v4/Vec | 14 | 8192 | 8192 | 8192 | 8192 | 8192 |
| pypto-lib::models::deepseek::v4::decode_attention_hca::pypto_hca_rope::Vec | PyPTO-Lib/deepseek-v4/Vec | 10 | 512 | 512 | 512 | 512 | 512 |
| pypto-lib::models::deepseek::v4::decode_attention_hca::pypto_merge_norm::Vec | PyPTO-Lib/deepseek-v4/Vec | 48 | 65856 | 65856 | 65856 | 65856 | 65856 |
| pypto-lib::models::deepseek::v4::decode_attention_hca::pypto_proj_b_mm::Mat | PyPTO-Lib/deepseek-v4/Mat | 8 | 139264 | 139264 | 139264 | 139264 | 139264 |
| pypto-lib::models::deepseek::v4::decode_attention_hca::pypto_qk_pv_aiv::Vec | PyPTO-Lib/deepseek-v4/Vec | 28 | 33408 | 33408 | 33408 | 33408 | 33408 |
| pypto-lib::models::deepseek::v4::decode_attention_hca::pypto_rmsnorm_rope_cache_write::Vec | PyPTO-Lib/deepseek-v4/Vec | 69 | 26880 | 26880 | 26880 | 26880 | 26880 |
| pypto-lib::models::deepseek::v4::decode_attention_hca::pypto_rope_cs::Vec | PyPTO-Lib/deepseek-v4/Vec | 24 | 4224 | 4224 | 4224 | 4224 | 4224 |
| pypto-lib::models::deepseek::v4::decode_attention_hca::pypto_scatter_softmax_pool::Vec | PyPTO-Lib/deepseek-v4/Vec | 29 | 147456 | 147456 | 147456 | 147456 | 147456 |
| pypto-lib::models::deepseek::v4::decode_attention_swa::pypto_merge_norm::Vec | PyPTO-Lib/deepseek-v4/Vec | 20 | 57600 | 57600 | 57600 | 57600 | 57600 |
| pypto-lib::models::deepseek::v4::decode_attention_swa::pypto_qk_pv_aiv::Vec | PyPTO-Lib/deepseek-v4/Vec | 24 | 33408 | 33408 | 33408 | 33408 | 33408 |
| pypto-lib::models::deepseek::v4::decode_attention_swa::pypto_quant::Vec | PyPTO-Lib/deepseek-v4/Vec | 16 | 65536 | 65536 | 65536 | 65536 | 65536 |
| pypto-lib::models::deepseek::v4::decode_attention_swa::pypto_rope_cs::Vec | PyPTO-Lib/deepseek-v4/Vec | 37 | 8192 | 8192 | 8192 | 8192 | 8192 |
| pypto-lib::models::deepseek::v4::decode_attention_swa::pypto_swa_cache_insert_valid_bias::Vec | PyPTO-Lib/deepseek-v4/Vec | 13 | 4608 | 4608 | 4608 | 4608 | 4608 |
| pypto-lib::models::deepseek::v4::decode_attention_swa::pypto_swa_rope_step::Vec | PyPTO-Lib/deepseek-v4/Vec | 6 | 512 | 512 | 512 | 512 | 512 |
| pypto-lib::models::deepseek::v4::decode_fwd::pypto_exp_gate_mm::Mat | PyPTO-Lib/deepseek-v4/Mat | 4 | 139264 | 139264 | 139264 | 139264 | 139264 |
| pypto-lib::models::deepseek::v4::decode_fwd::pypto_exp_gate_up_act::Vec | PyPTO-Lib/deepseek-v4/Vec | 42 | 42048 | 42048 | 43136 | 42048 | 42048 |
| pypto-lib::models::deepseek::v4::decode_fwd::pypto_exp_h_q::Vec | PyPTO-Lib/deepseek-v4/Vec | 25 | 98368 | 98368 | 98368 | 98368 | 98368 |
| pypto-lib::models::deepseek::v4::decode_fwd::pypto_exp_w2_act::Vec | PyPTO-Lib/deepseek-v4/Vec | 15 | 69696 | 69696 | 69696 | 69696 | 69696 |
| pypto-lib::models::deepseek::v4::decode_fwd::pypto_exp_w2_mm::Mat | PyPTO-Lib/deepseek-v4/Mat | 4 | 147456 | 147456 | 147456 | 147456 | 147456 |
| pypto-lib::models::deepseek::v4::decode_fwd::pypto_ffn_norm::Vec | PyPTO-Lib/deepseek-v4/Vec | 32 | 20512 | 20544 | 20544 | 20512 | 20512 |
| pypto-lib::models::deepseek::v4::decode_fwd::pypto_gate_0_aiv::Vec | PyPTO-Lib/deepseek-v4/Vec | 34 | 3072 | 3072 | 3072 | 3072 | 3072 |
| pypto-lib::models::deepseek::v4::decode_fwd::pypto_gate_1_aiv::Vec | PyPTO-Lib/deepseek-v4/Vec | 42 | 3136 | 3136 | 3136 | 3136 | 3136 |
| pypto-lib::models::deepseek::v4::decode_fwd::pypto_hc_head_pre_fused::Vec | PyPTO-Lib/deepseek-v4/Vec | 40 | 1632 | 1632 | 1632 | 1632 | 1632 |
| pypto-lib::models::deepseek::v4::decode_fwd::pypto_hc_head_reduce::Vec | PyPTO-Lib/deepseek-v4/Vec | 28 | 131200 | 131200 | 131200 | 131200 | 131200 |
| pypto-lib::models::deepseek::v4::decode_fwd::pypto_route_hash::Vec | PyPTO-Lib/deepseek-v4/Vec | 6 | 4608 | 4608 | 4608 | 4608 | 4608 |
| pypto-lib::models::deepseek::v4::decode_fwd::pypto_route_sort::Vec | PyPTO-Lib/deepseek-v4/Vec | 19 | 16640 | 16640 | 16640 | 16640 | 16640 |
| pypto-lib::models::deepseek::v4::decode_fwd::pypto_sh_gate_up_act_q::Vec | PyPTO-Lib/deepseek-v4/Vec | 34 | 98368 | 98368 | 106560 | 98368 | 98368 |
| pypto-lib::models::deepseek::v4::decode_fwd::pypto_sh_w2_act::Vec | PyPTO-Lib/deepseek-v4/Vec | 15 | 69696 | 69696 | 69696 | 69696 | 69696 |
| pypto-lib::models::deepseek::v4::decode_fwd::pypto_shared_routed::Vec | PyPTO-Lib/deepseek-v4/Vec | 6 | 32768 | 32768 | 32768 | 32768 | 32768 |
| pypto-lib::models::deepseek::v4::decode_fwd::pypto_x_norm_quant::Vec | PyPTO-Lib/deepseek-v4/Vec | 25 | 24608 | 24608 | 24608 | 24608 | 24608 |
| pypto-lib::models::deepseek::v4::decode_mtp::pypto_mtp_projection_linear_aic::Mat | PyPTO-Lib/deepseek-v4/Mat | 16 | 53248 | 53248 | 53248 | 53248 | 53248 |
| pypto-lib::models::deepseek::v4::decode_mtp::pypto_mtp_projection_linear_aiv::Vec | PyPTO-Lib/deepseek-v4/Vec | 22 | 16960 | 16960 | 16960 | 16960 | 16960 |
| pypto-lib::models::deepseek::v4::decode_mtp::pypto_mtp_projection_norm::Vec | PyPTO-Lib/deepseek-v4/Vec | 23 | 9248 | 9248 | 9248 | 9248 | 9248 |
| pypto-lib::models::deepseek::v4::decode_mtp::pypto_mtp_projection_quant::Vec | PyPTO-Lib/deepseek-v4/Vec | 20 | 4128 | 4128 | 4128 | 4128 | 4128 |
| pypto-lib::models::deepseek::v4::decode_mtp::pypto_mtp_projection_rms::Vec | PyPTO-Lib/deepseek-v4/Vec | 30 | 12320 | 12320 | 12320 | 12320 | 12320 |
| pypto-lib::models::deepseek::v4::decode_sparse_attn_swa::pypto_swa_valid_bias::Vec | PyPTO-Lib/deepseek-v4/Vec | 12 | 4608 | 4608 | 4608 | 4608 | 4608 |
| pypto-lib::models::deepseek::v4::lm_head::pypto_lm_head_output::Vec | PyPTO-Lib/deepseek-v4/Vec | 3 | 16384 | 16384 | 16384 | 16384 | 16384 |
| pypto-lib::models::deepseek::v4::prefill_attention_csa::pypto_gather_kv::Vec | PyPTO-Lib/deepseek-v4/Vec | 3 | 132096 | 132096 | 132096 | 132096 | 132096 |
| pypto-lib::models::deepseek::v4::prefill_attention_csa::pypto_hc_post_prefill::Vec | PyPTO-Lib/deepseek-v4/Vec | 42 | 81920 | 81920 | 81920 | 81920 | 81920 |
| pypto-lib::models::deepseek::v4::prefill_attention_csa::pypto_merge_norm::Vec | PyPTO-Lib/deepseek-v4/Vec | 26 | 98624 | 98624 | 98624 | 98624 | 98624 |
| pypto-lib::models::deepseek::v4::prefill_attention_csa::pypto_prefill_c4_rmsnorm_rope::Vec | PyPTO-Lib/deepseek-v4/Vec | 54 | 26880 | 26880 | 26880 | 26880 | 26880 |
| pypto-lib::models::deepseek::v4::prefill_attention_csa::pypto_prefill_c4_softmax_pool::Vec | PyPTO-Lib/deepseek-v4/Vec | 31 | 21504 | 21504 | 21504 | 21504 | 21504 |
| pypto-lib::models::deepseek::v4::prefill_attention_csa::pypto_prefill_c4_state_update::Vec | PyPTO-Lib/deepseek-v4/Vec | 8 | 384 | 384 | 384 | 384 | 384 |
| pypto-lib::models::deepseek::v4::prefill_attention_csa::pypto_prefill_idx_c4_cache_write::Vec | PyPTO-Lib/deepseek-v4/Vec | 16 | 24576 | 24576 | 24576 | 24576 | 24576 |
| pypto-lib::models::deepseek::v4::prefill_attention_csa::pypto_prefill_idx_c4_kv_score_proj::Right | PyPTO-Lib/deepseek-v4/Right | 16 | 32768 | 32768 | 32768 | 32768 | 32768 |
| pypto-lib::models::deepseek::v4::prefill_attention_csa::pypto_prefill_idx_c4_rmsnorm_rope::Vec | PyPTO-Lib/deepseek-v4/Vec | 56 | 26880 | 26880 | 26880 | 26880 | 26880 |
| pypto-lib::models::deepseek::v4::prefill_attention_csa::pypto_prefill_idx_c4_softmax_pool::Vec | PyPTO-Lib/deepseek-v4/Vec | 35 | 2688 | 2688 | 2688 | 2688 | 2688 |
| pypto-lib::models::deepseek::v4::prefill_attention_csa::pypto_prefill_idx_c4_state_update::Vec | PyPTO-Lib/deepseek-v4/Vec | 8 | 768 | 768 | 768 | 768 | 768 |
| pypto-lib::models::deepseek::v4::prefill_attention_csa::pypto_prefill_idx_qr_hadamard_quant_aic::Mat | PyPTO-Lib/deepseek-v4/Mat | 3 | 24576 | 24576 | 24576 | 24576 | 24576 |
| pypto-lib::models::deepseek::v4::prefill_attention_csa::pypto_prefill_idx_qr_hadamard_quant_aiv::Vec | PyPTO-Lib/deepseek-v4/Vec | 28 | 41216 | 41216 | 41216 | 41216 | 41216 |
| pypto-lib::models::deepseek::v4::prefill_attention_csa::pypto_prefill_idx_qr_proj_aiv::Vec | PyPTO-Lib/deepseek-v4/Vec | 10 | 17472 | 17472 | 17472 | 17472 | 17472 |
| pypto-lib::models::deepseek::v4::prefill_attention_csa::pypto_prefill_idx_qr_rope::Vec | PyPTO-Lib/deepseek-v4/Vec | 37 | 49408 | 49408 | 49408 | 49408 | 49408 |
| pypto-lib::models::deepseek::v4::prefill_attention_csa::pypto_prefill_idx_score_aiv::Vec | PyPTO-Lib/deepseek-v4/Vec | 28 | 24704 | 24704 | 24704 | 24704 | 24704 |
| pypto-lib::models::deepseek::v4::prefill_attention_csa::pypto_prefill_idx_topk::Vec | PyPTO-Lib/deepseek-v4/Vec | 8 | 16384 | 16384 | 16384 | 16384 | 16384 |
| pypto-lib::models::deepseek::v4::prefill_attention_csa::pypto_proj_a_mm::Mat | PyPTO-Lib/deepseek-v4/Mat | 8 | 262144 | 262144 | 262144 | 262144 | 262144 |
| pypto-lib::models::deepseek::v4::prefill_attention_csa::pypto_proj_b_act::Vec | PyPTO-Lib/deepseek-v4/Vec | 8 | 67648 | 67648 | 67648 | 67648 | 67648 |
| pypto-lib::models::deepseek::v4::prefill_attention_csa::pypto_proj_b_mm::Mat | PyPTO-Lib/deepseek-v4/Mat | 8 | 196608 | 196608 | 196608 | 196608 | 196608 |
| pypto-lib::models::deepseek::v4::prefill_attention_csa::pypto_qk_pv_aic::Mat | PyPTO-Lib/deepseek-v4/Mat | 4 | 294912 | 294912 | 294912 | 294912 | 294912 |
| pypto-lib::models::deepseek::v4::prefill_attention_csa::pypto_quant::Vec | PyPTO-Lib/deepseek-v4/Vec | 14 | 32800 | 32800 | 32800 | 32800 | 32800 |
| pypto-lib::models::deepseek::v4::prefill_attention_csa::pypto_rope::Vec | PyPTO-Lib/deepseek-v4/Vec | 23 | 41088 | 41088 | 41088 | 41088 | 41088 |
| pypto-lib::models::deepseek::v4::prefill_attention_csa::pypto_rope_cs::Vec | PyPTO-Lib/deepseek-v4/Vec | 24 | 65664 | 65664 | 65664 | 65664 | 65664 |
| pypto-lib::models::deepseek::v4::prefill_attention_hca::pypto_prefill_hca_c128_rmsnorm_rope::Vec | PyPTO-Lib/deepseek-v4/Vec | 69 | 13568 | 13568 | 13568 | 13568 | 13568 |
| pypto-lib::models::deepseek::v4::prefill_attention_hca::pypto_prefill_hca_c128_softmax_pool::Vec | PyPTO-Lib/deepseek-v4/Vec | 21 | 98304 | 98304 | 98304 | 98304 | 98304 |
| pypto-lib::models::deepseek::v4::prefill_attention_hca::pypto_prefill_hca_c128_state_scatter_pre::Vec | PyPTO-Lib/deepseek-v4/Vec | 4 | 4096 | 4096 | 4096 | 4096 | 4096 |
| pypto-lib::models::qwen3::14b::decode_fwd::pypto_dcr_xgamma::Vec | PyPTO-Lib/qwen3-14b/Vec | 6 | 131072 | 131072 | 131072 | 131072 | 131072 |
| pypto-lib::models::qwen3::14b::decode_fwd::pypto_down_proj::Mat | PyPTO-Lib/qwen3-14b/Mat | 8 | 266240 | 266240 | 266240 | 266240 | 266240 |
| pypto-lib::models::qwen3::14b::decode_fwd::pypto_k_proj::Mat | PyPTO-Lib/qwen3-14b/Mat | 8 | 278528 | 278528 | 278528 | 278528 | 278528 |
| pypto-lib::models::qwen3::14b::decode_fwd::pypto_out_proj::Mat | PyPTO-Lib/qwen3-14b/Mat | 8 | 135168 | 135168 | 135168 | 135168 | 135168 |
| pypto-lib::models::qwen3::14b::decode_fwd::pypto_post_rms_reduce::Vec | PyPTO-Lib/qwen3-14b/Vec | 18 | 65600 | 65664 | 65600 | 65600 | 65600 |
| pypto-lib::models::qwen3::14b::decode_fwd::pypto_residual_rms_cast::Vec | PyPTO-Lib/qwen3-14b/Vec | 12 | 67584 | 67584 | 67584 | 67584 | 67584 |
| pypto-lib::models::qwen3::14b::decode_fwd::pypto_rms_recip::Vec | PyPTO-Lib/qwen3-14b/Vec | 24 | 81984 | 81984 | 81984 | 81984 | 81984 |
| pypto-lib::models::qwen3::14b::decode_fwd::pypto_rope_qkv::Vec | PyPTO-Lib/qwen3-14b/Vec | 106 | 36352 | 36352 | 36352 | 36352 | 36352 |
| pypto-lib::models::qwen3::14b::decode_fwd::pypto_silu::Vec | PyPTO-Lib/qwen3-14b/Vec | 23 | 81984 | 81984 | 81984 | 81984 | 81984 |
| pypto-lib::models::qwen3::14b::decode_fwd::pypto_x_gamma0::Vec | PyPTO-Lib/qwen3-14b/Vec | 8 | 34816 | 34816 | 34816 | 34816 | 34816 |
| pypto-lib::models::qwen3::14b::decode_layer_a8w8::pypto_act_quant::Vec | PyPTO-Lib/qwen3-14b/Vec | 17 | 32832 | 32832 | 32832 | 32832 | 32832 |
| pypto-lib::models::qwen3::14b::decode_layer_a8w8::pypto_down_cast_residual::Vec | PyPTO-Lib/qwen3-14b/Vec | 7 | 32768 | 32768 | 32768 | 32768 | 32768 |
| pypto-lib::models::qwen3::14b::decode_layer_a8w8::pypto_down_dual_proj::Mat | PyPTO-Lib/qwen3-14b/Mat | 12 | 135168 | 135168 | 135168 | 135168 | 135168 |
| pypto-lib::models::qwen3::14b::decode_layer_a8w8::pypto_fa_fused_aiv::Vec | PyPTO-Lib/qwen3-14b/Vec | 44 | 9792 | 9792 | 9792 | 9792 | 9792 |
| pypto-lib::models::qwen3::14b::decode_layer_a8w8::pypto_gate_up_dual_proj::Mat | PyPTO-Lib/qwen3-14b/Mat | 12 | 135168 | 135168 | 135168 | 135168 | 135168 |
| pypto-lib::models::qwen3::14b::decode_layer_a8w8::pypto_gate_up_dual_proj::Right | PyPTO-Lib/qwen3-14b/Right | 8 | 65536 | 65536 | 65536 | 65536 | 65536 |
| pypto-lib::models::qwen3::14b::decode_layer_a8w8::pypto_k_proj_fused_dequant_aic::Left | PyPTO-Lib/qwen3-14b/Left | 6 | 4096 | 4096 | 4096 | 4096 | 4096 |
| pypto-lib::models::qwen3::14b::decode_layer_a8w8::pypto_k_proj_fused_dequant_aic::Right | PyPTO-Lib/qwen3-14b/Right | 6 | 65536 | 65536 | 65536 | 65536 | 65536 |
| pypto-lib::models::qwen3::14b::decode_layer_a8w8::pypto_k_proj_fused_dequant_aiv::Vec | PyPTO-Lib/qwen3-14b/Vec | 16 | 17472 | 17472 | 17472 | 17472 | 17472 |
| pypto-lib::models::qwen3::14b::decode_layer_a8w8::pypto_online_softmax::Vec | PyPTO-Lib/qwen3-14b/Vec | 20 | 24896 | 24896 | 24896 | 24896 | 24896 |
| pypto-lib::models::qwen3::14b::decode_layer_a8w8::pypto_out_act_quant::Vec | PyPTO-Lib/qwen3-14b/Vec | 17 | 65600 | 65600 | 65600 | 65600 | 65600 |
| pypto-lib::models::qwen3::14b::decode_layer_a8w8::pypto_out_proj_aiv::Vec | PyPTO-Lib/qwen3-14b/Vec | 16 | 16448 | 16448 | 16448 | 16448 | 16448 |
| pypto-lib::models::qwen3::14b::decode_layer_a8w8::pypto_post_rms_reduce::Vec | PyPTO-Lib/qwen3-14b/Vec | 20 | 114752 | 114752 | 114752 | 114752 | 114752 |
| pypto-lib::models::qwen3::14b::decode_layer_a8w8::pypto_qk_norm::Vec | PyPTO-Lib/qwen3-14b/Vec | 25 | 123456 | 123456 | 123456 | 123456 | 123456 |
| pypto-lib::models::qwen3::14b::decode_layer_a8w8::pypto_residual_rms_cast::Vec | PyPTO-Lib/qwen3-14b/Vec | 16 | 118784 | 118784 | 118784 | 118784 | 118784 |
| pypto-lib::models::qwen3::14b::decode_layer_a8w8::pypto_rms_recip::Vec | PyPTO-Lib/qwen3-14b/Vec | 28 | 57408 | 57472 | 57472 | 57408 | 57408 |
| pypto-lib::models::qwen3::14b::decode_layer_a8w8::pypto_rope_qkv::Vec | PyPTO-Lib/qwen3-14b/Vec | 126 | 17536 | 17536 | 17536 | 17536 | 17536 |
| pypto-lib::models::qwen3::14b::decode_layer_a8w8::pypto_silu::Vec | PyPTO-Lib/qwen3-14b/Vec | 12 | 49152 | 49152 | 49152 | 49152 | 49152 |
| pypto-lib::models::qwen3::14b::decode_layer_a8w8::pypto_v_norm::Vec | PyPTO-Lib/qwen3-14b/Vec | 3 | 8256 | 8256 | 8256 | 8256 | 8256 |
| pypto-lib::models::qwen3::14b::decode_layer_a8w8::pypto_x_gamma::Vec | PyPTO-Lib/qwen3-14b/Vec | 15 | 26624 | 26624 | 26624 | 26624 | 26624 |
| pypto-lib::models::qwen3::14b::greedy_sample::pypto_greedy_sample::Vec | PyPTO-Lib/qwen3-14b/Vec | 18 | 12288 | 12288 | 12288 | 12288 | 12288 |
| pypto-lib::models::qwen3::14b::prefill_fwd::pypto_attention_finalize_phase::Vec | PyPTO-Lib/qwen3-14b/Vec | 4 | 8256 | 8256 | 8256 | 8256 | 8256 |
| pypto-lib::models::qwen3::14b::prefill_fwd::pypto_down_proj::Left | PyPTO-Lib/qwen3-14b/Left | 8 | 32768 | 32768 | 32768 | 32768 | 32768 |
| pypto-lib::models::qwen3::14b::prefill_fwd::pypto_down_proj::Mat | PyPTO-Lib/qwen3-14b/Mat | 8 | 393216 | 393216 | 393216 | 393216 | 393216 |
| pypto-lib::models::qwen3::14b::prefill_fwd::pypto_final_rmsnorm::Vec | PyPTO-Lib/qwen3-14b/Vec | 15 | 16448 | 16448 | 16448 | 16448 | 16448 |
| pypto-lib::models::qwen3::14b::prefill_fwd::pypto_gate_up_proj::Mat | PyPTO-Lib/qwen3-14b/Mat | 16 | 393216 | 393216 | 393216 | 393216 | 393216 |
| pypto-lib::models::qwen3::14b::prefill_fwd::pypto_kv_proj::Mat | PyPTO-Lib/qwen3-14b/Mat | 16 | 327680 | 327680 | 327680 | 327680 | 327680 |
| pypto-lib::models::qwen3::14b::prefill_fwd::pypto_lm_head::Mat | PyPTO-Lib/qwen3-14b/Mat | 6 | 425984 | 425984 | 425984 | 425984 | 425984 |
| pypto-lib::models::qwen3::14b::prefill_fwd::pypto_lm_head::Right | PyPTO-Lib/qwen3-14b/Right | 8 | 49152 | 49152 | 49152 | 49152 | 49152 |
| pypto-lib::models::qwen3::14b::prefill_fwd::pypto_out_proj_aic::Left | PyPTO-Lib/qwen3-14b/Left | 8 | 16384 | 16384 | 16384 | 16384 | 16384 |
| pypto-lib::models::qwen3::14b::prefill_fwd::pypto_out_proj_aic::Mat | PyPTO-Lib/qwen3-14b/Mat | 8 | 327680 | 327680 | 327680 | 327680 | 327680 |
| pypto-lib::models::qwen3::14b::prefill_fwd::pypto_out_proj_aiv::Vec | PyPTO-Lib/qwen3-14b/Vec | 4 | 131072 | 131072 | 131072 | 131072 | 131072 |
| pypto-lib::models::qwen3::14b::prefill_fwd::pypto_post_rmsnorm::Vec | PyPTO-Lib/qwen3-14b/Vec | 14 | 16416 | 16416 | 16416 | 16416 | 16416 |
| pypto-lib::models::qwen3::14b::prefill_fwd::pypto_qk_pv_online_phase_0_aic::Mat | PyPTO-Lib/qwen3-14b/Mat | 7 | 69632 | 69632 | 69632 | 69632 | 69632 |
| pypto-lib::models::qwen3::14b::prefill_fwd::pypto_qk_pv_online_phase_0_aiv::Vec | PyPTO-Lib/qwen3-14b/Vec | 250 | 20544 | 20544 | 20544 | 20544 | 20544 |
| pypto-lib::models::qwen3::14b::prefill_fwd::pypto_qk_pv_skew_probe_aiv::Vec | PyPTO-Lib/qwen3-14b/Vec | 65 | 81920 | 81920 | 81920 | 81920 | 81920 |
| pypto-lib::models::qwen3::14b::prefill_fwd::pypto_rmsnorm::Vec | PyPTO-Lib/qwen3-14b/Vec | 16 | 16416 | 16416 | 16416 | 16416 | 16416 |
| pypto-lib::models::qwen3::14b::prefill_fwd::pypto_rope_kv_cache::Vec | PyPTO-Lib/qwen3-14b/Vec | 52 | 25600 | 25600 | 25600 | 25600 | 25600 |
| pypto-lib::models::qwen3::14b::qwen3_14b_decode_tq_draft::pypto_codebook_expand::Vec | PyPTO-Lib/qwen3-14b/Vec | 3 | 2112 | 2112 | 2112 | 2112 | 2112 |
| pypto-lib::models::qwen3::14b::qwen3_14b_decode_tq_draft::pypto_gate_up_silu_aic::Mat | PyPTO-Lib/qwen3-14b/Mat | 6 | 270336 | 270336 | 270336 | 270336 | 270336 |
| pypto-lib::models::qwen3::14b::qwen3_14b_decode_tq_draft::pypto_k_rotate_quant_aiv::Vec | PyPTO-Lib/qwen3-14b/Vec | 194 | 25120 | 25120 | 25120 | 25120 | 25120 |
| pypto-lib::models::qwen3::14b::qwen3_14b_decode_tq_draft::pypto_kv_pad::Vec | PyPTO-Lib/qwen3-14b/Vec | 11 | 8960 | 8960 | 8960 | 8960 | 8960 |
| pypto-lib::models::qwen3::14b::qwen3_14b_decode_tq_draft::pypto_online_softmax::Vec | PyPTO-Lib/qwen3-14b/Vec | 17 | 16704 | 16704 | 16704 | 16704 | 16704 |
| pypto-lib::models::qwen3::14b::qwen3_14b_decode_tq_draft::pypto_post_rmsnorm::Vec | PyPTO-Lib/qwen3-14b/Vec | 14 | 32832 | 32832 | 32832 | 32832 | 32832 |
| pypto-lib::models::qwen3::14b::qwen3_14b_decode_tq_draft::pypto_qk_dequant_aiv::Vec | PyPTO-Lib/qwen3-14b/Vec | 71 | 49792 | 49792 | 49792 | 49792 | 49792 |
| pypto-lib::models::qwen3::14b::qwen3_14b_decode_tq_draft::pypto_qk_norm::Vec | PyPTO-Lib/qwen3-14b/Vec | 20 | 122880 | 122880 | 122880 | 122880 | 122880 |
| pypto-lib::models::qwen3::14b::qwen3_14b_decode_tq_draft::pypto_rope_k_norm::Vec | PyPTO-Lib/qwen3-14b/Vec | 26 | 36928 | 36928 | 36928 | 36928 | 36928 |
| pypto-lib::models::qwen3::14b::qwen3_14b_decode_tq_draft::pypto_scatter_q_rope::Vec | PyPTO-Lib/qwen3-14b/Vec | 15 | 2304 | 2304 | 2304 | 2304 | 2304 |
| pypto-lib::models::qwen3::14b::qwen3_14b_decode_tq_draft::pypto_softmax::Vec | PyPTO-Lib/qwen3-14b/Vec | 11 | 20544 | 20544 | 20544 | 20544 | 20544 |
| pypto-lib::models::qwen3::14b::qwen3_14b_decode_tq_draft::pypto_sv_dequant_aiv::Vec | PyPTO-Lib/qwen3-14b/Vec | 70 | 49408 | 49408 | 49408 | 49408 | 49408 |
| pypto-lib::models::qwen3::14b::qwen3_14b_decode_tq_draft::pypto_v_norm::Vec | PyPTO-Lib/qwen3-14b/Vec | 9 | 24576 | 24576 | 24576 | 24576 | 24576 |
| pypto-lib::models::qwen3::14b::qwen3_14b_prefill_tq_draft::pypto_k_norm::Vec | PyPTO-Lib/qwen3-14b/Vec | 11 | 24576 | 24576 | 24576 | 24576 | 24576 |
| pypto-lib::models::qwen3::14b::qwen3_14b_prefill_tq_draft::pypto_kv_proj::Mat | PyPTO-Lib/qwen3-14b/Mat | 8 | 20480 | 20480 | 20480 | 20480 | 20480 |
| pypto-lib::models::qwen3::14b::qwen3_14b_prefill_tq_draft::pypto_online_softmax::Vec | PyPTO-Lib/qwen3-14b/Vec | 16 | 33152 | 33152 | 33152 | 33152 | 33152 |
| pypto-lib::models::qwen3::14b::qwen3_14b_prefill_tq_draft::pypto_out_proj_residual::Vec | PyPTO-Lib/qwen3-14b/Vec | 4 | 8192 | 8192 | 8192 | 8192 | 8192 |
| pypto-lib::models::qwen3::14b::qwen3_14b_prefill_tq_draft::pypto_q_rope::Vec | PyPTO-Lib/qwen3-14b/Vec | 14 | 2048 | 2048 | 2048 | 2048 | 2048 |
| pypto-lib::models::qwen3::14b::qwen3_14b_prefill_tq_draft::pypto_rmsnorm::Vec | PyPTO-Lib/qwen3-14b/Vec | 16 | 16448 | 16448 | 16448 | 16448 | 16448 |
| pypto-lib::models::qwen3::14b::qwen3_14b_prefill_tq_draft::pypto_silu::Vec | PyPTO-Lib/qwen3-14b/Vec | 9 | 24576 | 24576 | 24576 | 24576 | 24576 |
| pypto-lib::models::qwen3::14b::topk_select::pypto_greedy_select::Vec | PyPTO-Lib/qwen3-14b/Vec | 18 | 12288 | 12288 | 12288 | 12288 | 12288 |
| pypto-lib::models::qwen3::14b::topk_select::pypto_topk_select::Vec | PyPTO-Lib/qwen3-14b/Vec | 20 | 24704 | 24704 | 24704 | 24704 | 24704 |
| pypto-lib::models::qwen3::32b::qwen3_32b_decode::pypto_gate_proj::Mat | PyPTO-Lib/qwen3-32b/Mat | 8 | 139264 | 139264 | 139264 | 139264 | 139264 |
| pypto-lib::models::qwen3::32b::qwen3_32b_decode::pypto_kv_proj::Left | PyPTO-Lib/qwen3-32b/Left | 16 | 4096 | 4096 | 4096 | 4096 | 4096 |
| pypto-lib::models::qwen3::32b::qwen3_32b_decode::pypto_online_softmax::Vec | PyPTO-Lib/qwen3-32b/Vec | 36 | 16672 | 16672 | 16672 | 16672 | 16672 |
| pypto-lib::models::qwen3::32b::qwen3_32b_decode::pypto_post_rmsnorm::Vec | PyPTO-Lib/qwen3-32b/Vec | 24 | 24640 | 24640 | 24640 | 24640 | 24640 |
| pypto-lib::models::qwen3::32b::qwen3_32b_decode::pypto_qk_matmul::Mat | PyPTO-Lib/qwen3-32b/Mat | 4 | 73728 | 73728 | 73728 | 73728 | 73728 |
| pypto-lib::models::qwen3::32b::qwen3_32b_decode::pypto_rmsnorm::Vec | PyPTO-Lib/qwen3-32b/Vec | 52 | 114752 | 114816 | 114816 | 114752 | 114752 |
| pypto-lib::models::qwen3::32b::qwen3_32b_decode::pypto_rope_kv_cache::Vec | PyPTO-Lib/qwen3-32b/Vec | 31 | 9216 | 9216 | 9216 | 9216 | 9216 |
| pypto-lib::models::qwen3::32b::qwen3_32b_decode::pypto_softmax::Vec | PyPTO-Lib/qwen3-32b/Vec | 22 | 20512 | 20512 | 20512 | 20512 | 20512 |
| pypto-lib::models::qwen3::32b::qwen3_32b_decode_4d::pypto_down_proj::Mat | PyPTO-Lib/qwen3-32b/Mat | 8 | 67584 | 67584 | 67584 | 67584 | 67584 |
| pypto-lib::models::qwen3::32b::qwen3_32b_decode_4d::pypto_out_proj::Mat | PyPTO-Lib/qwen3-32b/Mat | 12 | 69632 | 69632 | 69632 | 69632 | 69632 |
| pypto-lib::models::qwen3::32b::qwen3_32b_decode_4d::pypto_out_proj_residual::Vec | PyPTO-Lib/qwen3-32b/Vec | 16 | 16384 | 16384 | 16384 | 16384 | 16384 |
| pypto-lib::models::qwen3::32b::qwen3_32b_decode_4d::pypto_qk_matmul::Mat | PyPTO-Lib/qwen3-32b/Mat | 4 | 69632 | 69632 | 69632 | 69632 | 69632 |
| pypto-lib::models::qwen3::32b::qwen3_32b_decode_4d::pypto_rmsnorm::Vec | PyPTO-Lib/qwen3-32b/Vec | 52 | 28736 | 28800 | 28800 | 28736 | 28736 |
| pypto-lib::models::qwen3::32b::qwen3_32b_decode_4d::pypto_rope_kv_cache::Vec | PyPTO-Lib/qwen3-32b/Vec | 28 | 5120 | 5120 | 5120 | 5120 | 5120 |
| pypto::tests::st::examples::01_beginner::basic::test_basic_ops::pypto_fused_add_scale_incore_0::Vec | PyPTO/pypto-examples-01_beginner-basic/Vec | 4 | 131072 | 131072 | 131072 | 131072 | 131072 |
| pypto::tests::st::examples::02_intermediate::test_activation::pypto_silu_incore_0::Vec | PyPTO/pypto-examples-02_intermediate/Vec | 6 | 32768 | 32768 | 32768 | 32768 | 32768 |
| pypto::tests::st::examples::02_intermediate::test_ffn_activations::pypto_gelu_kernel::Vec | PyPTO/pypto-examples-02_intermediate/Vec | 7 | 32768 | 32768 | 32768 | 32768 | 32768 |
| pypto::tests::st::examples::02_intermediate::test_layer_norm::pypto_layer_norm_incore_0::Vec | PyPTO/pypto-examples-02_intermediate/Vec | 17 | 25088 | 25088 | 25088 | 25088 | 25088 |
| pypto::tests::st::examples::02_intermediate::test_rms_norm::pypto_rms_norm_incore_0::Vec | PyPTO/pypto-examples-02_intermediate/Vec | 10 | 24832 | 24832 | 24832 | 24832 | 24832 |
| pypto::tests::st::examples::02_intermediate::test_softmax::pypto_tile_softmax_incore_0::Vec | PyPTO/pypto-examples-02_intermediate/Vec | 8 | 16640 | 16640 | 16640 | 16640 | 16640 |
| pypto::tests::st::examples::03_llm_models::test_llama_7b_mini_1h::pypto_kernel_matmul_trans_b::Mat | PyPTO/pypto-examples-03_llm_models/Mat | 8 | 2048 | 2048 | 2048 | 2048 | 2048 |
| pypto::tests::st::examples::03_llm_models::test_llama_7b_mini_1h::pypto_kernel_rms_norm::Vec | PyPTO/pypto-examples-03_llm_models/Vec | 8 | 12288 | 12288 | 12288 | 12288 | 12288 |
| pypto::tests::st::examples::03_llm_models::test_llama_7b_mini_1h::pypto_kernel_rope::Vec | PyPTO/pypto-examples-03_llm_models/Vec | 10 | 12288 | 12288 | 12288 | 12288 | 12288 |
| pypto::tests::st::examples::03_llm_models::test_llama_7b_mini_1h::pypto_kernel_softmax::Vec | PyPTO/pypto-examples-03_llm_models/Vec | 8 | 2048 | 2048 | 2048 | 2048 | 2048 |
| pypto::tests::st::examples::03_llm_models::test_llama_7b_mini_1h::pypto_kernel_swiglu::Vec | PyPTO/pypto-examples-03_llm_models/Vec | 8 | 12288 | 12288 | 12288 | 12288 | 12288 |
| pypto::tests::st::runtime::control_flow::test_ctrl_flow::pypto_kernel_for_if_else::Vec | PyPTO/pypto-runtime-control_flow/Vec | 5 | 49152 | 49152 | 49152 | 49152 | 49152 |
| pypto::tests::st::runtime::control_flow::test_ctrl_flow::pypto_kernel_if_yield::Vec | PyPTO/pypto-runtime-control_flow/Vec | 3 | 32768 | 32768 | 32768 | 32768 | 32768 |
| pypto::tests::st::runtime::control_flow::test_dyn_orch_shape::pypto_kernel_online_update::Vec | PyPTO/pypto-runtime-control_flow/Vec | 21 | 24832 | 24832 | 24832 | 24832 | 24832 |
| pypto::tests::st::runtime::control_flow::test_dyn_orch_shape::pypto_kernel_softmax_prepare::Vec | PyPTO/pypto-runtime-control_flow/Vec | 9 | 20544 | 20544 | 20544 | 20544 | 20544 |
| pypto::tests::st::runtime::cross_core::test_cross_core::pypto_bidirect_aiv::Vec | PyPTO/pypto-runtime-cross_core/Vec | 10 | 4096 | 4096 | 4096 | 4096 | 4096 |
| pypto::tests::st::runtime::cross_core::test_cross_core::pypto_main_incore_0_aiv-0ff59980e9b21d94::Vec | PyPTO/pypto-runtime-cross_core/Vec | 8 | 24576 | 24576 | 24576 | 24576 | 24576 |
| pypto::tests::st::runtime::cross_core::test_cross_core::pypto_main_incore_0_aiv-82edcd9ef10f9f6b::Vec | PyPTO/pypto-runtime-cross_core/Vec | 4 | 6144 | 6144 | 6144 | 6144 | 6144 |
| pypto::tests::st::runtime::cross_core::test_cross_core::pypto_main_incore_0_aiv-f24b408956592e02::Vec | PyPTO/pypto-runtime-cross_core/Vec | 8 | 12288 | 12288 | 12288 | 12288 | 12288 |
| pypto::tests::st::runtime::cross_core::test_cross_core::pypto_main_incore_0_aiv::Vec | PyPTO/pypto-runtime-cross_core/Vec | 4 | 12288 | 12288 | 12288 | 12288 | 12288 |
| pypto::tests::st::runtime::cross_core::test_cross_core::pypto_vector_slotnum::Vec | PyPTO/pypto-runtime-cross_core/Vec | 8 | 3072 | 3072 | 3072 | 3072 | 3072 |
| pypto::tests::st::runtime::cross_core::test_spmd_multi_output_subview::pypto_fused::Vec | PyPTO/pypto-runtime-cross_core/Vec | 19 | 131584 | 131584 | 131584 | 131584 | 131584 |
| pypto::tests::st::runtime::cross_core::test_syncall::pypto_aic_syncall::Mat | PyPTO/pypto-runtime-cross_core/Mat | 3 | 12288 | 12288 | 12288 | 12288 | 12288 |
| pypto::tests::st::runtime::cross_core::test_syncall::pypto_mix_syncall_aiv::Vec | PyPTO/pypto-runtime-cross_core/Vec | 8 | 4288 | 4288 | 4288 | 4288 | 4288 |
| pypto::tests::st::runtime::cross_core::test_syncall::pypto_spmd_syncall_soft_add::Vec | PyPTO/pypto-runtime-cross_core/Vec | 4 | 131200 | 131200 | 131200 | 131200 | 131200 |
| pypto::tests::st::runtime::framework_and_models::test_batch_paged_attention::pypto_KernelSoftmaxPrepare::Vec | PyPTO/pypto-runtime-framework_and_models/Vec | 10 | 2624 | 2624 | 2624 | 2624 | 2624 |
| pypto::tests::st::runtime::framework_and_models::test_ci::pypto_repro_kernel_incore_0::Vec | PyPTO/pypto-runtime-framework_and_models/Vec | 3 | 65536 | 65536 | 65536 | 65536 | 65536 |
| pypto::tests::st::runtime::framework_and_models::test_dynamic_paged_attention::pypto_dyn_kernel_online_update::Vec | PyPTO/pypto-runtime-framework_and_models/Vec | 21 | 24832 | 24832 | 24832 | 24832 | 24832 |
| pypto::tests::st::runtime::framework_and_models::test_dynamic_paged_attention::pypto_dyn_kernel_softmax_prepare::Vec | PyPTO/pypto-runtime-framework_and_models/Vec | 10 | 20544 | 20544 | 20544 | 20544 | 20544 |
| pypto::tests::st::runtime::framework_and_models::test_paged_attention_multi_config::pypto_kernel_online_update-16584c844d7b63d4::Vec | PyPTO/pypto-runtime-framework_and_models/Vec | 21 | 98816 | 98816 | 98816 | 98816 | 98816 |
| pypto::tests::st::runtime::framework_and_models::test_paged_attention_multi_config::pypto_kernel_online_update::Vec | PyPTO/pypto-runtime-framework_and_models/Vec | 21 | 99328 | 99328 | 99328 | 99328 | 99328 |
| pypto::tests::st::runtime::framework_and_models::test_paged_attention_multi_config::pypto_kernel_pv_matmul-1a88d3f94e04dc21::Mat | PyPTO/pypto-runtime-framework_and_models/Mat | 4 | 36864 | 36864 | 36864 | 36864 | 36864 |
| pypto::tests::st::runtime::framework_and_models::test_paged_attention_multi_config::pypto_kernel_pv_matmul-62044ce5003e2a01::Mat | PyPTO/pypto-runtime-framework_and_models/Mat | 4 | 24576 | 24576 | 24576 | 24576 | 24576 |
| pypto::tests::st::runtime::framework_and_models::test_paged_attention_multi_config::pypto_kernel_pv_matmul::Mat | PyPTO/pypto-runtime-framework_and_models/Mat | 4 | 18432 | 18432 | 18432 | 18432 | 18432 |
| pypto::tests::st::runtime::framework_and_models::test_paged_attention_multi_config::pypto_kernel_softmax_prepare-38ccd3bce855159c::Vec | PyPTO/pypto-runtime-framework_and_models/Vec | 19 | 16384 | 16384 | 16384 | 16384 | 16384 |
| pypto::tests::st::runtime::framework_and_models::test_paged_attention_multi_config::pypto_kernel_softmax_prepare-689a89f5726e2b2e::Vec | PyPTO/pypto-runtime-framework_and_models/Vec | 19 | 32768 | 32768 | 32768 | 32768 | 32768 |
| pypto::tests::st::runtime::framework_and_models::test_paged_attention_multi_config::pypto_kernel_softmax_prepare-f7feddcaf73fe379::Vec | PyPTO/pypto-runtime-framework_and_models/Vec | 19 | 16384 | 16384 | 16384 | 16384 | 16384 |
| pypto::tests::st::runtime::framework_and_models::test_paged_attention_multi_config::pypto_kernel_softmax_prepare::Vec | PyPTO/pypto-runtime-framework_and_models/Vec | 19 | 8192 | 8192 | 8192 | 8192 | 8192 |
| pypto::tests::st::runtime::framework_and_models::test_paged_attention_spmd::pypto_SpmdNormalize::Vec | PyPTO/pypto-runtime-framework_and_models/Vec | 3 | 8256 | 8256 | 8256 | 8256 | 8256 |
| pypto::tests::st::runtime::framework_and_models::test_paged_attention_spmd::pypto_SpmdOnlineUpdate::Vec | PyPTO/pypto-runtime-framework_and_models/Vec | 20 | 16704 | 16704 | 16704 | 16704 | 16704 |
| pypto::tests::st::runtime::framework_and_models::test_paged_attention_spmd::pypto_SpmdSoftmaxPrepare::Vec | PyPTO/pypto-runtime-framework_and_models/Vec | 17 | 20544 | 20544 | 20544 | 20544 | 20544 |
| pypto::tests::st::runtime::framework_and_models::test_qwen3_decode_scope3_mixed::pypto_mlp_block_aic::Mat | PyPTO/pypto-runtime-framework_and_models/Mat | 4 | 36864 | 36864 | 36864 | 36864 | 36864 |
| pypto::tests::st::runtime::framework_and_models::test_qwen3_decode_scope3_mixed::pypto_mlp_block_aiv::Vec | PyPTO/pypto-runtime-framework_and_models/Vec | 22 | 12288 | 12288 | 12288 | 12288 | 12288 |
| pypto::tests::st::runtime::framework_and_models::test_qwen3_decode_scope3_mixed::pypto_oproj_block_aiv::Vec | PyPTO/pypto-runtime-framework_and_models/Vec | 8 | 8192 | 8192 | 8192 | 8192 | 8192 |
| pypto::tests::st::runtime::framework_and_models::test_qwen3_decode_scope3_mixed::pypto_postnorm_block::Vec | PyPTO/pypto-runtime-framework_and_models/Vec | 6 | 8768 | 8768 | 8768 | 8768 | 8768 |
| pypto::tests::st::runtime::framework_and_models::test_qwen3_decode_scope3_mixed::pypto_rmsnorm::Vec | PyPTO/pypto-runtime-framework_and_models/Vec | 8 | 16448 | 16448 | 16448 | 16448 | 16448 |
| pypto::tests::st::runtime::ops::test_argmax_reduction::pypto_kernel-12d3a8ac8fdb9bef::Vec | PyPTO/pypto-runtime-ops/Vec | 3 | 8192 | 8192 | 8192 | 8192 | 8192 |
| pypto::tests::st::runtime::ops::test_argmax_reduction::pypto_kernel-1f2102186a086376::Vec | PyPTO/pypto-runtime-ops/Vec | 3 | 2048 | 2048 | 2048 | 2048 | 2048 |
| pypto::tests::st::runtime::ops::test_argmax_reduction::pypto_kernel-2e6b8480b173f393::Vec | PyPTO/pypto-runtime-ops/Vec | 3 | 8192 | 8192 | 8192 | 8192 | 8192 |
| pypto::tests::st::runtime::ops::test_argmax_reduction::pypto_kernel-571bf7c158183d2d::Vec | PyPTO/pypto-runtime-ops/Vec | 3 | 16384 | 16384 | 16384 | 16384 | 16384 |
| pypto::tests::st::runtime::ops::test_argmax_reduction::pypto_kernel-6f5288c34616e1aa::Vec | PyPTO/pypto-runtime-ops/Vec | 3 | 4096 | 4096 | 4096 | 4096 | 4096 |
| pypto::tests::st::runtime::ops::test_argmax_reduction::pypto_kernel-7c07d54356c739b3::Vec | PyPTO/pypto-runtime-ops/Vec | 3 | 8192 | 8192 | 8192 | 8192 | 8192 |
| pypto::tests::st::runtime::ops::test_argmax_reduction::pypto_kernel-b3bf3649a4296c61::Vec | PyPTO/pypto-runtime-ops/Vec | 3 | 4096 | 4096 | 4096 | 4096 | 4096 |
| pypto::tests::st::runtime::ops::test_argmax_reduction::pypto_kernel-c4c3b4056d9b5264::Vec | PyPTO/pypto-runtime-ops/Vec | 3 | 8192 | 8192 | 8192 | 8192 | 8192 |
| pypto::tests::st::runtime::ops::test_argmax_reduction::pypto_kernel-f5f95a5f00667ded::Vec | PyPTO/pypto-runtime-ops/Vec | 3 | 16384 | 16384 | 16384 | 16384 | 16384 |
| pypto::tests::st::runtime::ops::test_argmax_reduction::pypto_kernel::Vec | PyPTO/pypto-runtime-ops/Vec | 3 | 1024 | 1024 | 1024 | 1024 | 1024 |
| pypto::tests::st::runtime::ops::test_auto_tile_matmul::pypto_ddr_split_k_ddr_split_k::Left | PyPTO/pypto-runtime-ops/Left | 4 | 32768 | 32768 | 32768 | 32768 | 32768 |
| pypto::tests::st::runtime::ops::test_auto_tile_matmul::pypto_ddr_split_k_ddr_split_k::Right | PyPTO/pypto-runtime-ops/Right | 4 | 65536 | 65536 | 65536 | 65536 | 65536 |
| pypto::tests::st::runtime::ops::test_broadcast::pypto_col_expand_kernel-9864bc0d6384c627::Vec | PyPTO/pypto-runtime-ops/Vec | 3 | 576 | 576 | 576 | 576 | 576 |
| pypto::tests::st::runtime::ops::test_broadcast::pypto_col_expand_kernel-bc2c27b8495cd9a2::Vec | PyPTO/pypto-runtime-ops/Vec | 3 | 544 | 544 | 544 | 544 | 544 |
| pypto::tests::st::runtime::ops::test_broadcast::pypto_col_expand_kernel::Vec | PyPTO/pypto-runtime-ops/Vec | 3 | 1088 | 1088 | 1088 | 1088 | 1088 |
| pypto::tests::st::runtime::ops::test_broadcast::pypto_col_expand_mul_column_slice_kernel-376055b7125096bb::Vec | PyPTO/pypto-runtime-ops/Vec | 4 | 1024 | 1024 | 1024 | 1024 | 1024 |
| pypto::tests::st::runtime::ops::test_broadcast::pypto_col_expand_mul_column_slice_kernel-63de932d0dec9e63::Vec | PyPTO/pypto-runtime-ops/Vec | 6 | 1792 | 1792 | 1792 | 1792 | 1792 |
| pypto::tests::st::runtime::ops::test_broadcast::pypto_col_expand_mul_column_slice_kernel::Vec | PyPTO/pypto-runtime-ops/Vec | 6 | 12544 | 12544 | 12544 | 12544 | 12544 |
| pypto::tests::st::runtime::ops::test_broadcast::pypto_col_expand_mul_slice_kernel::Vec | PyPTO/pypto-runtime-ops/Vec | 4 | 18432 | 18432 | 18432 | 18432 | 18432 |
| pypto::tests::st::runtime::ops::test_broadcast::pypto_expand_clone_kernel-78c57111f850e5bc::Vec | PyPTO/pypto-runtime-ops/Vec | 3 | 288 | 288 | 288 | 288 | 288 |
| pypto::tests::st::runtime::ops::test_broadcast::pypto_expand_clone_kernel-c687f471d8dddc6a::Vec | PyPTO/pypto-runtime-ops/Vec | 3 | 2304 | 2304 | 2304 | 2304 | 2304 |
| pypto::tests::st::runtime::ops::test_concat::pypto_tile_concat_32x32_incore_0::Vec | PyPTO/pypto-runtime-ops/Vec | 3 | 4096 | 4096 | 4096 | 4096 | 4096 |
| pypto::tests::st::runtime::ops::test_expand_ops::pypto_kernel-0fc36c2fbfe21fb5::Vec | PyPTO/pypto-runtime-ops/Vec | 3 | 8320 | 8320 | 8320 | 8320 | 8320 |
| pypto::tests::st::runtime::ops::test_expand_ops::pypto_kernel::Vec | PyPTO/pypto-runtime-ops/Vec | 3 | 8448 | 8448 | 8448 | 8448 | 8448 |
| pypto::tests::st::runtime::ops::test_gather::pypto_main_incore_0-1dca644ab783d55c::Vec | PyPTO/pypto-runtime-ops/Vec | 5 | 192 | 192 | 192 | 192 | 192 |
| pypto::tests::st::runtime::ops::test_gather::pypto_main_incore_0-5c52315cd917b17d::Vec | PyPTO/pypto-runtime-ops/Vec | 5 | 256 | 256 | 256 | 256 | 256 |
| pypto::tests::st::runtime::ops::test_gather::pypto_main_incore_0-5e94c22947b68ae8::Vec | PyPTO/pypto-runtime-ops/Vec | 6 | 416 | 416 | 416 | 416 | 416 |
| pypto::tests::st::runtime::ops::test_gather::pypto_main_incore_0-81e880e62d4c56b9::Vec | PyPTO/pypto-runtime-ops/Vec | 8 | 544 | 544 | 544 | 544 | 544 |
| pypto::tests::st::runtime::ops::test_gather::pypto_main_incore_0-885a88b6ad40ec8f::Vec | PyPTO/pypto-runtime-ops/Vec | 6 | 8192 | 8192 | 8192 | 8192 | 8192 |
| pypto::tests::st::runtime::ops::test_mat_slice_to_left::pypto_mat_slice_to_left::Mat | PyPTO/pypto-runtime-ops/Mat | 3 | 32768 | 32768 | 32768 | 32768 | 32768 |
| pypto::tests::st::runtime::ops::test_matmul::pypto_matmul_acc_64_incore_0::Mat | PyPTO/pypto-runtime-ops/Mat | 4 | 16384 | 16384 | 16384 | 16384 | 16384 |
| pypto::tests::st::runtime::ops::test_matmul::pypto_mixed_add_btrans_aiv::Vec | PyPTO/pypto-runtime-ops/Vec | 6 | 65536 | 65536 | 65536 | 65536 | 65536 |
| pypto::tests::st::runtime::ops::test_memory_planner_ptoas::pypto_kernel::Vec | PyPTO/pypto-runtime-ops/Vec | 14 | 448 | 448 | 448 | 448 | 448 |
| pypto::tests::st::runtime::ops::test_memory_reuse_acc_coalesce::pypto_mm_512x512x192_acc_coalesce::Acc | PyPTO/pypto-runtime-ops/Acc | 16 | 262144 | 262144 | 262144 | 262144 | 262144 |
| pypto::tests::st::runtime::ops::test_memory_reuse_acc_coalesce::pypto_mm_512x512x192_acc_coalesce::Left | PyPTO/pypto-runtime-ops/Left | 24 | 32768 | 32768 | 32768 | 32768 | 32768 |
| pypto::tests::st::runtime::ops::test_memory_reuse_acc_coalesce::pypto_mm_512x512x192_acc_coalesce::Right | PyPTO/pypto-runtime-ops/Right | 24 | 65536 | 65536 | 65536 | 65536 | 65536 |
| pypto::tests::st::runtime::ops::test_prod_reduction::pypto_kernel-588a14b69424b5ef::Vec | PyPTO/pypto-runtime-ops/Vec | 3 | 24576 | 24576 | 24576 | 24576 | 24576 |
| pypto::tests::st::runtime::ops::test_prod_reduction::pypto_kernel::Vec | PyPTO/pypto-runtime-ops/Vec | 3 | 8192 | 8192 | 8192 | 8192 | 8192 |
| pypto::tests::st::runtime::ops::test_rsqrt::pypto_main_incore_0-e0989979105661a9::Vec | PyPTO/pypto-runtime-ops/Vec | 3 | 2048 | 2048 | 2048 | 2048 | 2048 |
| pypto::tests::st::runtime::ops::test_scatter::pypto_main_incore_0-a649dfc91e66db6c::Vec | PyPTO/pypto-runtime-ops/Vec | 12 | 4096 | 4096 | 4096 | 4096 | 4096 |
| pypto::tests::st::runtime::ops::test_scatter::pypto_main_incore_0-af62fc156f029d10::Vec | PyPTO/pypto-runtime-ops/Vec | 15 | 14336 | 14336 | 14336 | 14336 | 14336 |
| pypto::tests::st::runtime::ops::test_scatter::pypto_main_incore_0-d12679089a454e18::Vec | PyPTO/pypto-runtime-ops/Vec | 8 | 1792 | 1792 | 1792 | 1792 | 1792 |
| pypto::tests::st::runtime::ops::test_scatter::pypto_main_incore_0-d5d3f1dd092aa73f::Vec | PyPTO/pypto-runtime-ops/Vec | 12 | 2048 | 2048 | 2048 | 2048 | 2048 |
| pypto::tests::st::runtime::ops::test_scatter::pypto_main_incore_0::Vec | PyPTO/pypto-runtime-ops/Vec | 9 | 256 | 256 | 256 | 256 | 256 |
| pypto::tests::st::runtime::ops::test_scatter_update::pypto_kernel-a359420001b9111f::Vec | PyPTO/pypto-runtime-ops/Vec | 14 | 14336 | 14336 | 14336 | 14336 | 14336 |
| pypto::tests::st::runtime::ops::test_scatter_update::pypto_kernel-cb9c4a9226d73b99::Vec | PyPTO/pypto-runtime-ops/Vec | 15 | 8192 | 8192 | 8192 | 8192 | 8192 |
| pypto::tests::st::runtime::ops::test_scatter_update::pypto_main_incore_0::Vec | PyPTO/pypto-runtime-ops/Vec | 14 | 16384 | 16384 | 16384 | 16384 | 16384 |
| pypto::tests::st::runtime::ops::test_tensor_batch_matmul::pypto_batch_matmul_3d_btrans-a796aec773083fe1::Left | PyPTO/pypto-runtime-ops/Left | 8 | 8192 | 8192 | 8192 | 8192 | 8192 |
| pypto::tests::st::runtime::ops::test_tensor_batch_matmul::pypto_batch_matmul_3d_btrans-a796aec773083fe1::Mat | PyPTO/pypto-runtime-ops/Mat | 8 | 294912 | 294912 | 294912 | 294912 | 294912 |
| pypto::tests::st::runtime::ops::test_tensor_batch_matmul::pypto_batch_matmul_3d_btrans-a796aec773083fe1::Right | PyPTO/pypto-runtime-ops/Right | 8 | 65536 | 65536 | 65536 | 65536 | 65536 |
| pypto::tests::st::runtime::ops::test_tensor_batch_matmul::pypto_mixed_add_btrans_nd_aiv::Vec | PyPTO/pypto-runtime-ops/Vec | 6 | 61440 | 61440 | 61440 | 61440 | 61440 |
| pypto::tests::st::runtime::ops::test_trans::pypto_kernel-17c839e7de54a6cd::Vec | PyPTO/pypto-runtime-ops/Vec | 3 | 512 | 512 | 512 | 512 | 512 |
| pypto::tests::st::runtime::ops::test_trans::pypto_kernel-2b8511e5bd60e571::Vec | PyPTO/pypto-runtime-ops/Vec | 3 | 4608 | 4608 | 4608 | 4608 | 4608 |
| pypto::tests::st::runtime::ops::test_trans::pypto_kernel-bdc41f704d68a63e::Vec | PyPTO/pypto-runtime-ops/Vec | 7 | 9984 | 9984 | 9984 | 9984 | 9984 |
| pypto::tests::st::runtime::ops::test_trans::pypto_kernel-ff09810cf359506a::Vec | PyPTO/pypto-runtime-ops/Vec | 3 | 1024 | 1024 | 1024 | 1024 | 1024 |
| pypto::tests::st::runtime::ops::test_trans::pypto_kernel::Vec | PyPTO/pypto-runtime-ops/Vec | 6 | 4160 | 4160 | 4160 | 4160 | 4160 |
| target_hazard::Vec | PyPTO/Vec | 3 | 8192 | 8192 | 8192 | 8192 | 8192 |

## Runtime

| Instance | Origin | Buffers | MiniMalloc exact | First fit | XLA heap | TVM hill climb | Local search |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: |
| A.1048576.csv | MiniMalloc | 154 | 5.00 s† | 532 us | 500 us | 2.23 s | 1.10 s |
| B.1048576.csv | MiniMalloc | 170 | 5.00 s† | 582 us | 580 us | 2.31 s | 1.22 s |
| C.1048576.csv | MiniMalloc | 203 | 83.05 ms | 766 us | 774 us | 3.24 s | 1.59 s |
| D.1048576.csv | MiniMalloc | 213 | 5.00 s† | 1.16 ms | 1.10 ms | 5.36 s | 2.31 s |
| E.1048576.csv | MiniMalloc | 215 | 5.00 s† | 652 us | 649 us | 1.84 s | 1.36 s |
| F.1048576.csv | MiniMalloc | 296 | 5.00 s† | 942 us | 995 us | 2.06 s | 2.11 s |
| G.1048576.csv | MiniMalloc | 308 | 5.00 s† | 1.10 ms | 1.09 ms | 2.31 s | 2.25 s |
| H.1048576.csv | MiniMalloc | 316 | 5.00 s† | 1.13 ms | 1.13 ms | 2.54 s | 2.66 s |
| I.1048576.csv | MiniMalloc | 374 | 5.00 s† | 2.20 ms | 2.23 ms | 7.40 s | 4.33 s |
| J.1048576.csv | MiniMalloc | 409 | 5.00 s† | 3.89 ms | 3.98 ms | 17.65 s | 9.53 s |
| K.1048576.csv | MiniMalloc | 454 | 5.00 s† | 2.25 ms | 2.25 ms | 5.55 s | 4.47 s |
| issue_1908_fragmentation::Vec | PyPTO/Vec | 4 | 16 us | 2 us | 2 us | 208 us | 3.18 ms |
| pypto-lib::examples::advanced::allreduce::pypto_reduce_step::Vec | PyPTO-Lib/examples/Vec | 3 | 10 us | 2 us | 2 us | 181 us | 2.29 ms |
| pypto-lib::examples::advanced::multi_proj::pypto_proj::Left | PyPTO-Lib/examples/Left | 4 | 9 us | 2 us | 2 us | 212 us | 2.99 ms |
| pypto-lib::examples::advanced::topk::pypto_topk_block::Vec | PyPTO-Lib/examples/Vec | 7 | 33 us | 4 us | 3 us | 7.10 ms | 6.47 ms |
| pypto-lib::examples::intermediate::layer_norm::pypto_layer_norm_rows::Vec | PyPTO-Lib/examples/Vec | 19 | 135 us | 13 us | 14 us | 24.23 ms | 25.42 ms |
| pypto-lib::examples::intermediate::rms_norm::pypto_rms_norm_rows::Vec | PyPTO-Lib/examples/Vec | 12 | 31 us | 7 us | 7 us | 13.70 ms | 14.36 ms |
| pypto-lib::examples::intermediate::rope::pypto_rope_rotate::Vec | PyPTO-Lib/examples/Vec | 12 | 69 us | 7 us | 8 us | 16.06 ms | 15.37 ms |
| pypto-lib::examples::intermediate::softmax::pypto_softmax_rows::Vec | PyPTO-Lib/examples/Vec | 8 | 15 us | 4 us | 4 us | 383 us | 8.44 ms |
| pypto-lib::models::deepseek::v3_2::deepseek_v3_2_decode_back::pypto_deepseek_v3_2_decode_back_layer_incore_0::Mat | PyPTO-Lib/deepseek-v3_2/Mat | 4 | 9 us | 1 us | 1 us | 221 us | 3.02 ms |
| pypto-lib::models::deepseek::v3_2::deepseek_v3_2_decode_back::pypto_deepseek_v3_2_decode_back_layer_incore_1::Vec | PyPTO-Lib/deepseek-v3_2/Vec | 4 | 9 us | 1 us | 1 us | 205 us | 3.02 ms |
| pypto-lib::models::deepseek::v3_2::deepseek_v3_2_decode_back::pypto_deepseek_v3_2_decode_back_layer_incore_2::Vec | PyPTO-Lib/deepseek-v3_2/Vec | 14 | 32 us | 9 us | 8 us | 16.92 ms | 17.57 ms |
| pypto-lib::models::deepseek::v3_2::deepseek_v3_2_decode_back::pypto_deepseek_v3_2_decode_back_layer_incore_3::Mat | PyPTO-Lib/deepseek-v3_2/Mat | 4 | 8 us | 1 us | 1 us | 220 us | 2.96 ms |
| pypto-lib::models::deepseek::v3_2::deepseek_v3_2_decode_back::pypto_deepseek_v3_2_decode_back_layer_incore_5::Vec | PyPTO-Lib/deepseek-v3_2/Vec | 9 | 36 us | 5 us | 5 us | 10.02 ms | 10.04 ms |
| pypto-lib::models::deepseek::v3_2::deepseek_v3_2_decode_back::pypto_deepseek_v3_2_decode_back_layer_incore_6::Left | PyPTO-Lib/deepseek-v3_2/Left | 4 | 9 us | 1 us | 1 us | 219 us | 2.98 ms |
| pypto-lib::models::deepseek::v3_2::deepseek_v3_2_decode_back::pypto_deepseek_v3_2_decode_back_layer_incore_6::Mat | PyPTO-Lib/deepseek-v3_2/Mat | 4 | 7 us | 1 us | 1 us | 218 us | 2.95 ms |
| pypto-lib::models::deepseek::v3_2::deepseek_v3_2_decode_back::pypto_deepseek_v3_2_decode_back_layer_incore_7::Vec | PyPTO-Lib/deepseek-v3_2/Vec | 4 | 6 us | 1 us | 1 us | 201 us | 2.94 ms |
| pypto-lib::models::deepseek::v3_2::deepseek_v3_2_decode_front::pypto_decode_cache_write::Vec | PyPTO-Lib/deepseek-v3_2/Vec | 17 | 110 us | 14 us | 14 us | 24.58 ms | 24.66 ms |
| pypto-lib::models::deepseek::v3_2::deepseek_v3_2_decode_front::pypto_input_rmsnorm::Vec | PyPTO-Lib/deepseek-v3_2/Vec | 16 | 44 us | 11 us | 12 us | 20.39 ms | 21.89 ms |
| pypto-lib::models::deepseek::v3_2::deepseek_v3_2_decode_front::pypto_kv_a_proj::Mat | PyPTO-Lib/deepseek-v3_2/Mat | 4 | 8 us | 1 us | 1 us | 221 us | 3.08 ms |
| pypto-lib::models::deepseek::v3_2::deepseek_v3_2_decode_front::pypto_kv_rmsnorm::Vec | PyPTO-Lib/deepseek-v3_2/Vec | 13 | 60 us | 8 us | 9 us | 15.51 ms | 16.01 ms |
| pypto-lib::models::deepseek::v3_2::deepseek_v3_2_decode_front::pypto_q_head_proj::Mat | PyPTO-Lib/deepseek-v3_2/Mat | 4 | 9 us | 1 us | 1 us | 222 us | 3.03 ms |
| pypto-lib::models::deepseek::v3_2::deepseek_v3_2_decode_front::pypto_q_lora_rmsnorm::Vec | PyPTO-Lib/deepseek-v3_2/Vec | 16 | 35 us | 10 us | 10 us | 20.14 ms | 21.16 ms |
| pypto-lib::models::deepseek::v3_2::deepseek_v3_2_decode_front::pypto_q_rope::Vec | PyPTO-Lib/deepseek-v3_2/Vec | 16 | 104 us | 10 us | 11 us | 23.76 ms | 23.70 ms |
| pypto-lib::models::deepseek::v3_2::deepseek_v3_2_decode_front::pypto_s2_idx_rope::Vec | PyPTO-Lib/deepseek-v3_2/Vec | 28 | 286 us | 22 us | 23 us | 48.42 ms | 48.30 ms |
| pypto-lib::models::deepseek::v3_2::deepseek_v3_2_decode_front::pypto_s2_k_idx_layernorm::Vec | PyPTO-Lib/deepseek-v3_2/Vec | 22 | 192 us | 16 us | 18 us | 35.44 ms | 32.75 ms |
| pypto-lib::models::deepseek::v3_2::deepseek_v3_2_decode_front::pypto_s2_q_reduce::Vec | PyPTO-Lib/deepseek-v3_2/Vec | 5 | 15 us | 2 us | 2 us | 255 us | 4.96 ms |
| pypto-lib::models::deepseek::v3_2::deepseek_v3_2_decode_front::pypto_s3_sort::Vec | PyPTO-Lib/deepseek-v3_2/Vec | 7 | 9 us | 3 us | 3 us | 334 us | 7.07 ms |
| pypto-lib::models::deepseek::v3_2::deepseek_v3_2_decode_front::pypto_s4_q_pe_load::Vec | PyPTO-Lib/deepseek-v3_2/Vec | 9 | 34 us | 5 us | 5 us | 10.70 ms | 10.94 ms |
| pypto-lib::models::deepseek::v3_2::deepseek_v3_2_decode_front::pypto_s4_softmax::Vec | PyPTO-Lib/deepseek-v3_2/Vec | 46 | 1.27 ms | 50 us | 51 us | 111.23 ms | 100.07 ms |
| pypto-lib::models::deepseek::v3_2::deepseek_v3_2_prefill_back::pypto_deepseek_v3_2_prefill_back_layer_incore_2::Vec | PyPTO-Lib/deepseek-v3_2/Vec | 4 | 10 us | 2 us | 2 us | 213 us | 3.18 ms |
| pypto-lib::models::deepseek::v3_2::deepseek_v3_2_prefill_back::pypto_deepseek_v3_2_prefill_back_layer_incore_3::Vec | PyPTO-Lib/deepseek-v3_2/Vec | 8 | 25 us | 5 us | 4 us | 9.16 ms | 9.29 ms |
| pypto-lib::models::deepseek::v3_2::deepseek_v3_2_prefill_back::pypto_deepseek_v3_2_prefill_back_layer_incore_4::Vec | PyPTO-Lib/deepseek-v3_2/Vec | 6 | 32 us | 3 us | 3 us | 7.13 ms | 6.54 ms |
| pypto-lib::models::deepseek::v3_2::deepseek_v3_2_prefill_back::pypto_deepseek_v3_2_prefill_back_layer_incore_5::Mat | PyPTO-Lib/deepseek-v3_2/Mat | 4 | 12 us | 1 us | 1 us | 229 us | 3.22 ms |
| pypto-lib::models::deepseek::v3_2::deepseek_v3_2_prefill_back::pypto_deepseek_v3_2_prefill_back_layer_incore_7::Vec | PyPTO-Lib/deepseek-v3_2/Vec | 9 | 43 us | 5 us | 5 us | 10.77 ms | 11.39 ms |
| pypto-lib::models::deepseek::v3_2::deepseek_v3_2_prefill_back::pypto_deepseek_v3_2_prefill_back_layer_incore_9::Vec | PyPTO-Lib/deepseek-v3_2/Vec | 4 | 7 us | 1 us | 2 us | 218 us | 3.24 ms |
| pypto-lib::models::deepseek::v4::decode_attention_csa::pypto_comb_sinkhorn::Vec | PyPTO-Lib/deepseek-v4/Vec | 123 | 2.91 ms | 184 us | 205 us | 439.52 ms | 388.35 ms |
| pypto-lib::models::deepseek::v4::decode_attention_csa::pypto_csa_rope_step::Vec | PyPTO-Lib/deepseek-v4/Vec | 10 | 21 us | 7 us | 7 us | 453 us | 11.99 ms |
| pypto-lib::models::deepseek::v4::decode_attention_csa::pypto_csa_slots_build_valid_qk_plan::Vec | PyPTO-Lib/deepseek-v4/Vec | 32 | 246 us | 27 us | 27 us | 48.90 ms | 52.49 ms |
| pypto-lib::models::deepseek::v4::decode_attention_csa::pypto_hc_post::Vec | PyPTO-Lib/deepseek-v4/Vec | 42 | 206 us | 37 us | 38 us | 82.67 ms | 81.68 ms |
| pypto-lib::models::deepseek::v4::decode_attention_csa::pypto_hc_pre_rms::Vec | PyPTO-Lib/deepseek-v4/Vec | 24 | 224 us | 19 us | 20 us | 40.94 ms | 40.35 ms |
| pypto-lib::models::deepseek::v4::decode_attention_csa::pypto_idx_qr_proj_dequant::Vec | PyPTO-Lib/deepseek-v4/Vec | 6 | 21 us | 3 us | 3 us | 6.50 ms | 6.36 ms |
| pypto-lib::models::deepseek::v4::decode_attention_csa::pypto_kv_and_cache_write::Vec | PyPTO-Lib/deepseek-v4/Vec | 16 | 79 us | 11 us | 10 us | 22.11 ms | 23.71 ms |
| pypto-lib::models::deepseek::v4::decode_attention_csa::pypto_kv_rms_norm_rope::Vec | PyPTO-Lib/deepseek-v4/Vec | 71 | 706 us | 80 us | 84 us | 163.20 ms | 174.70 ms |
| pypto-lib::models::deepseek::v4::decode_attention_csa::pypto_kv_score_proj::Left | PyPTO-Lib/deepseek-v4/Left | 16 | 19 us | 11 us | 12 us | 732 us | 22.99 ms |
| pypto-lib::models::deepseek::v4::decode_attention_csa::pypto_kv_score_proj::Right | PyPTO-Lib/deepseek-v4/Right | 16 | 22 us | 10 us | 11 us | 734 us | 23.26 ms |
| pypto-lib::models::deepseek::v4::decode_attention_csa::pypto_merge_norm::Vec | PyPTO-Lib/deepseek-v4/Vec | 47 | 415 us | 50 us | 48 us | 97.03 ms | 96.61 ms |
| pypto-lib::models::deepseek::v4::decode_attention_csa::pypto_mix_x::Vec | PyPTO-Lib/deepseek-v4/Vec | 27 | 454 us | 24 us | 25 us | 58.55 ms | 48.48 ms |
| pypto-lib::models::deepseek::v4::decode_attention_csa::pypto_proj_a_mm::Mat | PyPTO-Lib/deepseek-v4/Mat | 8 | 27 us | 5 us | 5 us | 9.44 ms | 9.22 ms |
| pypto-lib::models::deepseek::v4::decode_attention_csa::pypto_proj_b_act::Vec | PyPTO-Lib/deepseek-v4/Vec | 13 | 86 us | 9 us | 9 us | 19.37 ms | 18.22 ms |
| pypto-lib::models::deepseek::v4::decode_attention_csa::pypto_proj_b_mm::Left | PyPTO-Lib/deepseek-v4/Left | 8 | 20 us | 5 us | 5 us | 436 us | 9.61 ms |
| pypto-lib::models::deepseek::v4::decode_attention_csa::pypto_proj_b_mm::Mat | PyPTO-Lib/deepseek-v4/Mat | 8 | 12 us | 4 us | 4 us | 434 us | 9.84 ms |
| pypto-lib::models::deepseek::v4::decode_attention_csa::pypto_q_rope_prepare::Vec | PyPTO-Lib/deepseek-v4/Vec | 27 | 373 us | 26 us | 24 us | 47.19 ms | 48.64 ms |
| pypto-lib::models::deepseek::v4::decode_attention_csa::pypto_qk_pv_aic::Left | PyPTO-Lib/deepseek-v4/Left | 8 | 13 us | 5 us | 4 us | 428 us | 9.60 ms |
| pypto-lib::models::deepseek::v4::decode_attention_csa::pypto_qk_pv_aic::Mat | PyPTO-Lib/deepseek-v4/Mat | 3 | 15 us | 1 us | 1 us | 227 us | 2.66 ms |
| pypto-lib::models::deepseek::v4::decode_attention_csa::pypto_qk_pv_aiv::Vec | PyPTO-Lib/deepseek-v4/Vec | 26 | 113 us | 25 us | 24 us | 43.49 ms | 44.08 ms |
| pypto-lib::models::deepseek::v4::decode_attention_csa::pypto_qproj_dequant_rms_nope_rope::Vec | PyPTO-Lib/deepseek-v4/Vec | 48 | 1.44 ms | 60 us | 68 us | 141.95 ms | 111.58 ms |
| pypto-lib::models::deepseek::v4::decode_attention_csa::pypto_qproj_matmul::Left | PyPTO-Lib/deepseek-v4/Left | 8 | 13 us | 5 us | 5 us | 421 us | 9.23 ms |
| pypto-lib::models::deepseek::v4::decode_attention_csa::pypto_qr_hadamard_quant::Vec | PyPTO-Lib/deepseek-v4/Vec | 14 | 42 us | 10 us | 9 us | 18.66 ms | 19.28 ms |
| pypto-lib::models::deepseek::v4::decode_attention_csa::pypto_qr_rms_norm_quant::Vec | PyPTO-Lib/deepseek-v4/Vec | 52 | 765 us | 56 us | 57 us | 109.85 ms | 108.08 ms |
| pypto-lib::models::deepseek::v4::decode_attention_csa::pypto_qr_rope::Vec | PyPTO-Lib/deepseek-v4/Vec | 39 | 529 us | 39 us | 42 us | 71.14 ms | 72.96 ms |
| pypto-lib::models::deepseek::v4::decode_attention_csa::pypto_quant::Vec | PyPTO-Lib/deepseek-v4/Vec | 16 | 19 us | 11 us | 10 us | 616 us | 21.16 ms |
| pypto-lib::models::deepseek::v4::decode_attention_csa::pypto_rms_norm::Vec | PyPTO-Lib/deepseek-v4/Vec | 30 | 110 us | 27 us | 27 us | 43.89 ms | 48.94 ms |
| pypto-lib::models::deepseek::v4::decode_attention_csa::pypto_rmsnorm_rope::Vec | PyPTO-Lib/deepseek-v4/Vec | 54 | 479 us | 60 us | 61 us | 117.00 ms | 114.76 ms |
| pypto-lib::models::deepseek::v4::decode_attention_csa::pypto_rmsnorm_rope_cache_write::Vec | PyPTO-Lib/deepseek-v4/Vec | 54 | 473 us | 56 us | 58 us | 116.18 ms | 114.48 ms |
| pypto-lib::models::deepseek::v4::decode_attention_csa::pypto_rope_cs::Vec | PyPTO-Lib/deepseek-v4/Vec | 24 | 233 us | 22 us | 23 us | 37.42 ms | 37.25 ms |
| pypto-lib::models::deepseek::v4::decode_attention_csa::pypto_scatter_softmax_pool::Vec | PyPTO-Lib/deepseek-v4/Vec | 35 | 406 us | 33 us | 31 us | 66.40 ms | 66.52 ms |
| pypto-lib::models::deepseek::v4::decode_attention_csa::pypto_scatter_softmax_pool_0::Vec | PyPTO-Lib/deepseek-v4/Vec | 35 | 86 us | 33 us | 35 us | 53.86 ms | 63.73 ms |
| pypto-lib::models::deepseek::v4::decode_attention_csa::pypto_score_mat::Mat | PyPTO-Lib/deepseek-v4/Mat | 4 | 24 us | 2 us | 2 us | 3.60 ms | 3.23 ms |
| pypto-lib::models::deepseek::v4::decode_attention_csa::pypto_score_reduce::Vec | PyPTO-Lib/deepseek-v4/Vec | 41 | 621 us | 42 us | 47 us | 89.76 ms | 77.83 ms |
| pypto-lib::models::deepseek::v4::decode_attention_csa::pypto_split_pre_post::Vec | PyPTO-Lib/deepseek-v4/Vec | 23 | 95 us | 18 us | 19 us | 31.40 ms | 33.62 ms |
| pypto-lib::models::deepseek::v4::decode_attention_csa::pypto_topk::Vec | PyPTO-Lib/deepseek-v4/Vec | 14 | 26 us | 10 us | 11 us | 597 us | 18.42 ms |
| pypto-lib::models::deepseek::v4::decode_attention_csa::pypto_weights_proj_reduce::Vec | PyPTO-Lib/deepseek-v4/Vec | 8 | 10 us | 5 us | 5 us | 408 us | 8.93 ms |
| pypto-lib::models::deepseek::v4::decode_attention_hca::pypto_build_valid::Vec | PyPTO-Lib/deepseek-v4/Vec | 14 | 50 us | 10 us | 12 us | 4.06 ms | 18.90 ms |
| pypto-lib::models::deepseek::v4::decode_attention_hca::pypto_hca_rope::Vec | PyPTO-Lib/deepseek-v4/Vec | 10 | 24 us | 7 us | 8 us | 446 us | 11.85 ms |
| pypto-lib::models::deepseek::v4::decode_attention_hca::pypto_merge_norm::Vec | PyPTO-Lib/deepseek-v4/Vec | 48 | 323 us | 53 us | 56 us | 87.83 ms | 92.74 ms |
| pypto-lib::models::deepseek::v4::decode_attention_hca::pypto_proj_b_mm::Mat | PyPTO-Lib/deepseek-v4/Mat | 8 | 25 us | 5 us | 4 us | 8.80 ms | 8.53 ms |
| pypto-lib::models::deepseek::v4::decode_attention_hca::pypto_qk_pv_aiv::Vec | PyPTO-Lib/deepseek-v4/Vec | 28 | 120 us | 26 us | 27 us | 41.42 ms | 42.61 ms |
| pypto-lib::models::deepseek::v4::decode_attention_hca::pypto_rmsnorm_rope_cache_write::Vec | PyPTO-Lib/deepseek-v4/Vec | 69 | 698 us | 85 us | 86 us | 164.68 ms | 158.08 ms |
| pypto-lib::models::deepseek::v4::decode_attention_hca::pypto_rope_cs::Vec | PyPTO-Lib/deepseek-v4/Vec | 24 | 209 us | 21 us | 21 us | 35.38 ms | 36.43 ms |
| pypto-lib::models::deepseek::v4::decode_attention_hca::pypto_scatter_softmax_pool::Vec | PyPTO-Lib/deepseek-v4/Vec | 29 | 206 us | 26 us | 27 us | 43.86 ms | 45.47 ms |
| pypto-lib::models::deepseek::v4::decode_attention_swa::pypto_merge_norm::Vec | PyPTO-Lib/deepseek-v4/Vec | 20 | 123 us | 15 us | 17 us | 29.62 ms | 31.05 ms |
| pypto-lib::models::deepseek::v4::decode_attention_swa::pypto_qk_pv_aiv::Vec | PyPTO-Lib/deepseek-v4/Vec | 24 | 105 us | 22 us | 22 us | 35.32 ms | 35.54 ms |
| pypto-lib::models::deepseek::v4::decode_attention_swa::pypto_quant::Vec | PyPTO-Lib/deepseek-v4/Vec | 16 | 22 us | 12 us | 13 us | 679 us | 22.00 ms |
| pypto-lib::models::deepseek::v4::decode_attention_swa::pypto_rope_cs::Vec | PyPTO-Lib/deepseek-v4/Vec | 37 | 97 us | 32 us | 31 us | 11.51 ms | 64.06 ms |
| pypto-lib::models::deepseek::v4::decode_attention_swa::pypto_swa_cache_insert_valid_bias::Vec | PyPTO-Lib/deepseek-v4/Vec | 13 | 22 us | 10 us | 11 us | 494 us | 16.77 ms |
| pypto-lib::models::deepseek::v4::decode_attention_swa::pypto_swa_rope_step::Vec | PyPTO-Lib/deepseek-v4/Vec | 6 | 18 us | 4 us | 4 us | 348 us | 6.28 ms |
| pypto-lib::models::deepseek::v4::decode_fwd::pypto_exp_gate_mm::Mat | PyPTO-Lib/deepseek-v4/Mat | 4 | 9 us | 3 us | 2 us | 233 us | 3.19 ms |
| pypto-lib::models::deepseek::v4::decode_fwd::pypto_exp_gate_up_act::Vec | PyPTO-Lib/deepseek-v4/Vec | 42 | 615 us | 41 us | 54 us | 102.16 ms | 85.05 ms |
| pypto-lib::models::deepseek::v4::decode_fwd::pypto_exp_h_q::Vec | PyPTO-Lib/deepseek-v4/Vec | 25 | 99 us | 22 us | 21 us | 38.29 ms | 40.24 ms |
| pypto-lib::models::deepseek::v4::decode_fwd::pypto_exp_w2_act::Vec | PyPTO-Lib/deepseek-v4/Vec | 15 | 128 us | 12 us | 12 us | 22.71 ms | 20.96 ms |
| pypto-lib::models::deepseek::v4::decode_fwd::pypto_exp_w2_mm::Mat | PyPTO-Lib/deepseek-v4/Mat | 4 | 14 us | 2 us | 2 us | 3.52 ms | 3.17 ms |
| pypto-lib::models::deepseek::v4::decode_fwd::pypto_ffn_norm::Vec | PyPTO-Lib/deepseek-v4/Vec | 32 | 125 us | 30 us | 30 us | 48.81 ms | 54.67 ms |
| pypto-lib::models::deepseek::v4::decode_fwd::pypto_gate_0_aiv::Vec | PyPTO-Lib/deepseek-v4/Vec | 34 | 63 us | 30 us | 37 us | 51.77 ms | 54.25 ms |
| pypto-lib::models::deepseek::v4::decode_fwd::pypto_gate_1_aiv::Vec | PyPTO-Lib/deepseek-v4/Vec | 42 | 175 us | 35 us | 42 us | 75.57 ms | 73.14 ms |
| pypto-lib::models::deepseek::v4::decode_fwd::pypto_hc_head_pre_fused::Vec | PyPTO-Lib/deepseek-v4/Vec | 40 | 323 us | 33 us | 39 us | 71.16 ms | 64.99 ms |
| pypto-lib::models::deepseek::v4::decode_fwd::pypto_hc_head_reduce::Vec | PyPTO-Lib/deepseek-v4/Vec | 28 | 633 us | 27 us | 28 us | 71.70 ms | 53.26 ms |
| pypto-lib::models::deepseek::v4::decode_fwd::pypto_route_hash::Vec | PyPTO-Lib/deepseek-v4/Vec | 6 | 23 us | 3 us | 3 us | 6.19 ms | 5.98 ms |
| pypto-lib::models::deepseek::v4::decode_fwd::pypto_route_sort::Vec | PyPTO-Lib/deepseek-v4/Vec | 19 | 109 us | 17 us | 17 us | 27.19 ms | 28.01 ms |
| pypto-lib::models::deepseek::v4::decode_fwd::pypto_sh_gate_up_act_q::Vec | PyPTO-Lib/deepseek-v4/Vec | 34 | 375 us | 30 us | 33 us | 65.65 ms | 61.08 ms |
| pypto-lib::models::deepseek::v4::decode_fwd::pypto_sh_w2_act::Vec | PyPTO-Lib/deepseek-v4/Vec | 15 | 125 us | 12 us | 11 us | 22.78 ms | 20.69 ms |
| pypto-lib::models::deepseek::v4::decode_fwd::pypto_shared_routed::Vec | PyPTO-Lib/deepseek-v4/Vec | 6 | 17 us | 4 us | 4 us | 324 us | 6.18 ms |
| pypto-lib::models::deepseek::v4::decode_fwd::pypto_x_norm_quant::Vec | PyPTO-Lib/deepseek-v4/Vec | 25 | 93 us | 24 us | 22 us | 37.92 ms | 38.91 ms |
| pypto-lib::models::deepseek::v4::decode_mtp::pypto_mtp_projection_linear_aic::Mat | PyPTO-Lib/deepseek-v4/Mat | 16 | 93 us | 13 us | 14 us | 22.32 ms | 23.24 ms |
| pypto-lib::models::deepseek::v4::decode_mtp::pypto_mtp_projection_linear_aiv::Vec | PyPTO-Lib/deepseek-v4/Vec | 22 | 62 us | 18 us | 19 us | 32.00 ms | 33.27 ms |
| pypto-lib::models::deepseek::v4::decode_mtp::pypto_mtp_projection_norm::Vec | PyPTO-Lib/deepseek-v4/Vec | 23 | 255 us | 18 us | 21 us | 37.99 ms | 35.21 ms |
| pypto-lib::models::deepseek::v4::decode_mtp::pypto_mtp_projection_quant::Vec | PyPTO-Lib/deepseek-v4/Vec | 20 | 46 us | 15 us | 16 us | 27.50 ms | 29.43 ms |
| pypto-lib::models::deepseek::v4::decode_mtp::pypto_mtp_projection_rms::Vec | PyPTO-Lib/deepseek-v4/Vec | 30 | 132 us | 28 us | 26 us | 43.96 ms | 47.20 ms |
| pypto-lib::models::deepseek::v4::decode_sparse_attn_swa::pypto_swa_valid_bias::Vec | PyPTO-Lib/deepseek-v4/Vec | 12 | 15 us | 9 us | 9 us | 481 us | 14.80 ms |
| pypto-lib::models::deepseek::v4::lm_head::pypto_lm_head_output::Vec | PyPTO-Lib/deepseek-v4/Vec | 3 | 9 us | 1 us | 1 us | 189 us | 2.42 ms |
| pypto-lib::models::deepseek::v4::prefill_attention_csa::pypto_gather_kv::Vec | PyPTO-Lib/deepseek-v4/Vec | 3 | 11 us | 1 us | 2 us | 205 us | 2.43 ms |
| pypto-lib::models::deepseek::v4::prefill_attention_csa::pypto_hc_post_prefill::Vec | PyPTO-Lib/deepseek-v4/Vec | 42 | 156 us | 43 us | 41 us | 72.68 ms | 76.06 ms |
| pypto-lib::models::deepseek::v4::prefill_attention_csa::pypto_merge_norm::Vec | PyPTO-Lib/deepseek-v4/Vec | 26 | 367 us | 23 us | 23 us | 44.26 ms | 43.61 ms |
| pypto-lib::models::deepseek::v4::prefill_attention_csa::pypto_prefill_c4_rmsnorm_rope::Vec | PyPTO-Lib/deepseek-v4/Vec | 54 | 512 us | 59 us | 60 us | 115.86 ms | 112.01 ms |
| pypto-lib::models::deepseek::v4::prefill_attention_csa::pypto_prefill_c4_softmax_pool::Vec | PyPTO-Lib/deepseek-v4/Vec | 31 | 447 us | 28 us | 28 us | 54.52 ms | 55.36 ms |
| pypto-lib::models::deepseek::v4::prefill_attention_csa::pypto_prefill_c4_state_update::Vec | PyPTO-Lib/deepseek-v4/Vec | 8 | 31 us | 6 us | 7 us | 9.01 ms | 9.22 ms |
| pypto-lib::models::deepseek::v4::prefill_attention_csa::pypto_prefill_idx_c4_cache_write::Vec | PyPTO-Lib/deepseek-v4/Vec | 16 | 78 us | 14 us | 12 us | 21.40 ms | 23.30 ms |
| pypto-lib::models::deepseek::v4::prefill_attention_csa::pypto_prefill_idx_c4_kv_score_proj::Right | PyPTO-Lib/deepseek-v4/Right | 16 | 17 us | 12 us | 12 us | 708 us | 21.05 ms |
| pypto-lib::models::deepseek::v4::prefill_attention_csa::pypto_prefill_idx_c4_rmsnorm_rope::Vec | PyPTO-Lib/deepseek-v4/Vec | 56 | 527 us | 64 us | 66 us | 122.31 ms | 117.67 ms |
| pypto-lib::models::deepseek::v4::prefill_attention_csa::pypto_prefill_idx_c4_softmax_pool::Vec | PyPTO-Lib/deepseek-v4/Vec | 35 | 457 us | 32 us | 33 us | 62.61 ms | 62.60 ms |
| pypto-lib::models::deepseek::v4::prefill_attention_csa::pypto_prefill_idx_c4_state_update::Vec | PyPTO-Lib/deepseek-v4/Vec | 8 | 32 us | 5 us | 6 us | 9.24 ms | 9.30 ms |
| pypto-lib::models::deepseek::v4::prefill_attention_csa::pypto_prefill_idx_qr_hadamard_quant_aic::Mat | PyPTO-Lib/deepseek-v4/Mat | 3 | 16 us | 3 us | 2 us | 2.67 ms | 2.47 ms |
| pypto-lib::models::deepseek::v4::prefill_attention_csa::pypto_prefill_idx_qr_hadamard_quant_aiv::Vec | PyPTO-Lib/deepseek-v4/Vec | 28 | 52 us | 26 us | 27 us | 40.96 ms | 44.42 ms |
| pypto-lib::models::deepseek::v4::prefill_attention_csa::pypto_prefill_idx_qr_proj_aiv::Vec | PyPTO-Lib/deepseek-v4/Vec | 10 | 28 us | 9 us | 8 us | 12.91 ms | 13.22 ms |
| pypto-lib::models::deepseek::v4::prefill_attention_csa::pypto_prefill_idx_qr_rope::Vec | PyPTO-Lib/deepseek-v4/Vec | 37 | 582 us | 40 us | 42 us | 71.23 ms | 74.12 ms |
| pypto-lib::models::deepseek::v4::prefill_attention_csa::pypto_prefill_idx_score_aiv::Vec | PyPTO-Lib/deepseek-v4/Vec | 28 | 122 us | 25 us | 25 us | 45.29 ms | 47.14 ms |
| pypto-lib::models::deepseek::v4::prefill_attention_csa::pypto_prefill_idx_topk::Vec | PyPTO-Lib/deepseek-v4/Vec | 8 | 13 us | 6 us | 6 us | 423 us | 9.84 ms |
| pypto-lib::models::deepseek::v4::prefill_attention_csa::pypto_proj_a_mm::Mat | PyPTO-Lib/deepseek-v4/Mat | 8 | 37 us | 6 us | 5 us | 10.48 ms | 10.35 ms |
| pypto-lib::models::deepseek::v4::prefill_attention_csa::pypto_proj_b_act::Vec | PyPTO-Lib/deepseek-v4/Vec | 8 | 59 us | 6 us | 7 us | 10.89 ms | 10.57 ms |
| pypto-lib::models::deepseek::v4::prefill_attention_csa::pypto_proj_b_mm::Mat | PyPTO-Lib/deepseek-v4/Mat | 8 | 35 us | 6 us | 5 us | 10.46 ms | 10.15 ms |
| pypto-lib::models::deepseek::v4::prefill_attention_csa::pypto_qk_pv_aic::Mat | PyPTO-Lib/deepseek-v4/Mat | 4 | 26 us | 3 us | 3 us | 3.90 ms | 3.58 ms |
| pypto-lib::models::deepseek::v4::prefill_attention_csa::pypto_quant::Vec | PyPTO-Lib/deepseek-v4/Vec | 14 | 44 us | 12 us | 10 us | 20.17 ms | 20.90 ms |
| pypto-lib::models::deepseek::v4::prefill_attention_csa::pypto_rope::Vec | PyPTO-Lib/deepseek-v4/Vec | 23 | 148 us | 21 us | 24 us | 35.72 ms | 36.73 ms |
| pypto-lib::models::deepseek::v4::prefill_attention_csa::pypto_rope_cs::Vec | PyPTO-Lib/deepseek-v4/Vec | 24 | 298 us | 22 us | 25 us | 39.92 ms | 40.71 ms |
| pypto-lib::models::deepseek::v4::prefill_attention_hca::pypto_prefill_hca_c128_rmsnorm_rope::Vec | PyPTO-Lib/deepseek-v4/Vec | 69 | 2.13 ms | 87 us | 93 us | 192.43 ms | 186.53 ms |
| pypto-lib::models::deepseek::v4::prefill_attention_hca::pypto_prefill_hca_c128_softmax_pool::Vec | PyPTO-Lib/deepseek-v4/Vec | 21 | 149 us | 22 us | 21 us | 37.90 ms | 39.70 ms |
| pypto-lib::models::deepseek::v4::prefill_attention_hca::pypto_prefill_hca_c128_state_scatter_pre::Vec | PyPTO-Lib/deepseek-v4/Vec | 4 | 12 us | 3 us | 2 us | 280 us | 4.24 ms |
| pypto-lib::models::qwen3::14b::decode_fwd::pypto_dcr_xgamma::Vec | PyPTO-Lib/qwen3-14b/Vec | 6 | 13 us | 5 us | 4 us | 430 us | 7.87 ms |
| pypto-lib::models::qwen3::14b::decode_fwd::pypto_down_proj::Mat | PyPTO-Lib/qwen3-14b/Mat | 8 | 34 us | 7 us | 6 us | 12.25 ms | 12.08 ms |
| pypto-lib::models::qwen3::14b::decode_fwd::pypto_k_proj::Mat | PyPTO-Lib/qwen3-14b/Mat | 8 | 41 us | 7 us | 7 us | 12.51 ms | 12.12 ms |
| pypto-lib::models::qwen3::14b::decode_fwd::pypto_out_proj::Mat | PyPTO-Lib/qwen3-14b/Mat | 8 | 51 us | 6 us | 7 us | 12.66 ms | 12.74 ms |
| pypto-lib::models::qwen3::14b::decode_fwd::pypto_post_rms_reduce::Vec | PyPTO-Lib/qwen3-14b/Vec | 18 | 194 us | 22 us | 20 us | 39.91 ms | 38.99 ms |
| pypto-lib::models::qwen3::14b::decode_fwd::pypto_residual_rms_cast::Vec | PyPTO-Lib/qwen3-14b/Vec | 12 | 123 us | 17 us | 14 us | 23.58 ms | 22.73 ms |
| pypto-lib::models::qwen3::14b::decode_fwd::pypto_rms_recip::Vec | PyPTO-Lib/qwen3-14b/Vec | 24 | 327 us | 30 us | 29 us | 56.15 ms | 54.17 ms |
| pypto-lib::models::qwen3::14b::decode_fwd::pypto_rope_qkv::Vec | PyPTO-Lib/qwen3-14b/Vec | 106 | 10.86 ms | 263 us | 262 us | 686.28 ms | 586.94 ms |
| pypto-lib::models::qwen3::14b::decode_fwd::pypto_silu::Vec | PyPTO-Lib/qwen3-14b/Vec | 23 | 612 us | 35 us | 37 us | 75.31 ms | 63.37 ms |
| pypto-lib::models::qwen3::14b::decode_fwd::pypto_x_gamma0::Vec | PyPTO-Lib/qwen3-14b/Vec | 8 | 69 us | 9 us | 8 us | 16.56 ms | 16.03 ms |
| pypto-lib::models::qwen3::14b::decode_layer_a8w8::pypto_act_quant::Vec | PyPTO-Lib/qwen3-14b/Vec | 17 | 255 us | 23 us | 22 us | 44.13 ms | 42.59 ms |
| pypto-lib::models::qwen3::14b::decode_layer_a8w8::pypto_down_cast_residual::Vec | PyPTO-Lib/qwen3-14b/Vec | 7 | 36 us | 8 us | 9 us | 633 us | 13.20 ms |
| pypto-lib::models::qwen3::14b::decode_layer_a8w8::pypto_down_dual_proj::Mat | PyPTO-Lib/qwen3-14b/Mat | 12 | 89 us | 17 us | 15 us | 27.06 ms | 26.05 ms |
| pypto-lib::models::qwen3::14b::decode_layer_a8w8::pypto_fa_fused_aiv::Vec | PyPTO-Lib/qwen3-14b/Vec | 44 | 248 us | 76 us | 65 us | 125.98 ms | 124.00 ms |
| pypto-lib::models::qwen3::14b::decode_layer_a8w8::pypto_gate_up_dual_proj::Mat | PyPTO-Lib/qwen3-14b/Mat | 12 | 91 us | 13 us | 13 us | 25.20 ms | 24.34 ms |
| pypto-lib::models::qwen3::14b::decode_layer_a8w8::pypto_gate_up_dual_proj::Right | PyPTO-Lib/qwen3-14b/Right | 8 | 25 us | 8 us | 6 us | 590 us | 14.10 ms |
| pypto-lib::models::qwen3::14b::decode_layer_a8w8::pypto_k_proj_fused_dequant_aic::Left | PyPTO-Lib/qwen3-14b/Left | 6 | 21 us | 6 us | 6 us | 566 us | 9.33 ms |
| pypto-lib::models::qwen3::14b::decode_layer_a8w8::pypto_k_proj_fused_dequant_aic::Right | PyPTO-Lib/qwen3-14b/Right | 6 | 18 us | 5 us | 5 us | 562 us | 9.73 ms |
| pypto-lib::models::qwen3::14b::decode_layer_a8w8::pypto_k_proj_fused_dequant_aiv::Vec | PyPTO-Lib/qwen3-14b/Vec | 16 | 68 us | 21 us | 21 us | 33.69 ms | 34.01 ms |
| pypto-lib::models::qwen3::14b::decode_layer_a8w8::pypto_online_softmax::Vec | PyPTO-Lib/qwen3-14b/Vec | 20 | 351 us | 30 us | 32 us | 57.53 ms | 54.90 ms |
| pypto-lib::models::qwen3::14b::decode_layer_a8w8::pypto_out_act_quant::Vec | PyPTO-Lib/qwen3-14b/Vec | 17 | 228 us | 19 us | 20 us | 38.09 ms | 36.36 ms |
| pypto-lib::models::qwen3::14b::decode_layer_a8w8::pypto_out_proj_aiv::Vec | PyPTO-Lib/qwen3-14b/Vec | 16 | 74 us | 17 us | 18 us | 32.62 ms | 33.01 ms |
| pypto-lib::models::qwen3::14b::decode_layer_a8w8::pypto_post_rms_reduce::Vec | PyPTO-Lib/qwen3-14b/Vec | 20 | 251 us | 26 us | 26 us | 49.05 ms | 48.31 ms |
| pypto-lib::models::qwen3::14b::decode_layer_a8w8::pypto_qk_norm::Vec | PyPTO-Lib/qwen3-14b/Vec | 25 | 331 us | 34 us | 31 us | 62.86 ms | 57.68 ms |
| pypto-lib::models::qwen3::14b::decode_layer_a8w8::pypto_residual_rms_cast::Vec | PyPTO-Lib/qwen3-14b/Vec | 16 | 228 us | 22 us | 21 us | 40.33 ms | 37.52 ms |
| pypto-lib::models::qwen3::14b::decode_layer_a8w8::pypto_rms_recip::Vec | PyPTO-Lib/qwen3-14b/Vec | 28 | 506 us | 39 us | 37 us | 75.79 ms | 73.71 ms |
| pypto-lib::models::qwen3::14b::decode_layer_a8w8::pypto_rope_qkv::Vec | PyPTO-Lib/qwen3-14b/Vec | 126 | 14.46 ms | 321 us | 334 us | 911.20 ms | 785.11 ms |
| pypto-lib::models::qwen3::14b::decode_layer_a8w8::pypto_silu::Vec | PyPTO-Lib/qwen3-14b/Vec | 12 | 106 us | 10 us | 10 us | 20.71 ms | 22.19 ms |
| pypto-lib::models::qwen3::14b::decode_layer_a8w8::pypto_v_norm::Vec | PyPTO-Lib/qwen3-14b/Vec | 3 | 14 us | 2 us | 2 us | 292 us | 3.80 ms |
| pypto-lib::models::qwen3::14b::decode_layer_a8w8::pypto_x_gamma::Vec | PyPTO-Lib/qwen3-14b/Vec | 15 | 88 us | 14 us | 14 us | 30.79 ms | 31.60 ms |
| pypto-lib::models::qwen3::14b::greedy_sample::pypto_greedy_sample::Vec | PyPTO-Lib/qwen3-14b/Vec | 18 | 185 us | 19 us | 20 us | 41.06 ms | 40.71 ms |
| pypto-lib::models::qwen3::14b::prefill_fwd::pypto_attention_finalize_phase::Vec | PyPTO-Lib/qwen3-14b/Vec | 4 | 16 us | 4 us | 3 us | 331 us | 4.86 ms |
| pypto-lib::models::qwen3::14b::prefill_fwd::pypto_down_proj::Left | PyPTO-Lib/qwen3-14b/Left | 8 | 22 us | 8 us | 9 us | 649 us | 14.72 ms |
| pypto-lib::models::qwen3::14b::prefill_fwd::pypto_down_proj::Mat | PyPTO-Lib/qwen3-14b/Mat | 8 | 45 us | 8 us | 8 us | 14.54 ms | 14.36 ms |
| pypto-lib::models::qwen3::14b::prefill_fwd::pypto_final_rmsnorm::Vec | PyPTO-Lib/qwen3-14b/Vec | 15 | 72 us | 18 us | 17 us | 32.26 ms | 33.74 ms |
| pypto-lib::models::qwen3::14b::prefill_fwd::pypto_gate_up_proj::Mat | PyPTO-Lib/qwen3-14b/Mat | 16 | 67 us | 21 us | 24 us | 38.58 ms | 40.09 ms |
| pypto-lib::models::qwen3::14b::prefill_fwd::pypto_kv_proj::Mat | PyPTO-Lib/qwen3-14b/Mat | 16 | 241 us | 20 us | 19 us | 36.48 ms | 36.56 ms |
| pypto-lib::models::qwen3::14b::prefill_fwd::pypto_lm_head::Mat | PyPTO-Lib/qwen3-14b/Mat | 6 | 44 us | 15 us | 7 us | 10.62 ms | 10.44 ms |
| pypto-lib::models::qwen3::14b::prefill_fwd::pypto_lm_head::Right | PyPTO-Lib/qwen3-14b/Right | 8 | 26 us | 9 us | 9 us | 692 us | 15.19 ms |
| pypto-lib::models::qwen3::14b::prefill_fwd::pypto_out_proj_aic::Left | PyPTO-Lib/qwen3-14b/Left | 8 | 20 us | 8 us | 8 us | 684 us | 14.86 ms |
| pypto-lib::models::qwen3::14b::prefill_fwd::pypto_out_proj_aic::Mat | PyPTO-Lib/qwen3-14b/Mat | 8 | 49 us | 8 us | 9 us | 15.56 ms | 15.47 ms |
| pypto-lib::models::qwen3::14b::prefill_fwd::pypto_out_proj_aiv::Vec | PyPTO-Lib/qwen3-14b/Vec | 4 | 16 us | 3 us | 4 us | 362 us | 5.14 ms |
| pypto-lib::models::qwen3::14b::prefill_fwd::pypto_post_rmsnorm::Vec | PyPTO-Lib/qwen3-14b/Vec | 14 | 65 us | 16 us | 17 us | 29.18 ms | 31.28 ms |
| pypto-lib::models::qwen3::14b::prefill_fwd::pypto_qk_pv_online_phase_0_aic::Mat | PyPTO-Lib/qwen3-14b/Mat | 7 | 77 us | 8 us | 7 us | 13.00 ms | 12.59 ms |
| pypto-lib::models::qwen3::14b::prefill_fwd::pypto_qk_pv_online_phase_0_aiv::Vec | PyPTO-Lib/qwen3-14b/Vec | 250 | 721 us | 728 us | 835 us | 1.47 s | 1.38 s |
| pypto-lib::models::qwen3::14b::prefill_fwd::pypto_qk_pv_skew_probe_aiv::Vec | PyPTO-Lib/qwen3-14b/Vec | 65 | 212 us | 90 us | 89 us | 153.78 ms | 171.54 ms |
| pypto-lib::models::qwen3::14b::prefill_fwd::pypto_rmsnorm::Vec | PyPTO-Lib/qwen3-14b/Vec | 16 | 75 us | 16 us | 14 us | 29.97 ms | 30.91 ms |
| pypto-lib::models::qwen3::14b::prefill_fwd::pypto_rope_kv_cache::Vec | PyPTO-Lib/qwen3-14b/Vec | 52 | 1.64 ms | 73 us | 73 us | 156.76 ms | 148.53 ms |
| pypto-lib::models::qwen3::14b::qwen3_14b_decode_tq_draft::pypto_codebook_expand::Vec | PyPTO-Lib/qwen3-14b/Vec | 3 | 17 us | 3 us | 3 us | 279 us | 3.50 ms |
| pypto-lib::models::qwen3::14b::qwen3_14b_decode_tq_draft::pypto_gate_up_silu_aic::Mat | PyPTO-Lib/qwen3-14b/Mat | 6 | 36 us | 5 us | 5 us | 9.57 ms | 9.35 ms |
| pypto-lib::models::qwen3::14b::qwen3_14b_decode_tq_draft::pypto_k_rotate_quant_aiv::Vec | PyPTO-Lib/qwen3-14b/Vec | 194 | 237 us | 435 us | 474 us | 975.02 ms | 986.74 ms |
| pypto-lib::models::qwen3::14b::qwen3_14b_decode_tq_draft::pypto_kv_pad::Vec | PyPTO-Lib/qwen3-14b/Vec | 11 | 135 us | 9 us | 8 us | 19.71 ms | 18.76 ms |
| pypto-lib::models::qwen3::14b::qwen3_14b_decode_tq_draft::pypto_online_softmax::Vec | PyPTO-Lib/qwen3-14b/Vec | 17 | 281 us | 20 us | 19 us | 43.88 ms | 44.88 ms |
| pypto-lib::models::qwen3::14b::qwen3_14b_decode_tq_draft::pypto_post_rmsnorm::Vec | PyPTO-Lib/qwen3-14b/Vec | 14 | 57 us | 15 us | 13 us | 27.48 ms | 28.58 ms |
| pypto-lib::models::qwen3::14b::qwen3_14b_decode_tq_draft::pypto_qk_dequant_aiv::Vec | PyPTO-Lib/qwen3-14b/Vec | 71 | 941 us | 138 us | 144 us | 256.51 ms | 255.08 ms |
| pypto-lib::models::qwen3::14b::qwen3_14b_decode_tq_draft::pypto_qk_norm::Vec | PyPTO-Lib/qwen3-14b/Vec | 20 | 85 us | 24 us | 23 us | 42.22 ms | 44.85 ms |
| pypto-lib::models::qwen3::14b::qwen3_14b_decode_tq_draft::pypto_rope_k_norm::Vec | PyPTO-Lib/qwen3-14b/Vec | 26 | 655 us | 37 us | 36 us | 67.53 ms | 66.35 ms |
| pypto-lib::models::qwen3::14b::qwen3_14b_decode_tq_draft::pypto_scatter_q_rope::Vec | PyPTO-Lib/qwen3-14b/Vec | 15 | 237 us | 16 us | 16 us | 32.18 ms | 32.47 ms |
| pypto-lib::models::qwen3::14b::qwen3_14b_decode_tq_draft::pypto_softmax::Vec | PyPTO-Lib/qwen3-14b/Vec | 11 | 75 us | 12 us | 11 us | 18.13 ms | 18.24 ms |
| pypto-lib::models::qwen3::14b::qwen3_14b_decode_tq_draft::pypto_sv_dequant_aiv::Vec | PyPTO-Lib/qwen3-14b/Vec | 70 | 730 us | 126 us | 128 us | 213.11 ms | 212.43 ms |
| pypto-lib::models::qwen3::14b::qwen3_14b_decode_tq_draft::pypto_v_norm::Vec | PyPTO-Lib/qwen3-14b/Vec | 9 | 63 us | 8 us | 8 us | 13.79 ms | 13.52 ms |
| pypto-lib::models::qwen3::14b::qwen3_14b_prefill_tq_draft::pypto_k_norm::Vec | PyPTO-Lib/qwen3-14b/Vec | 11 | 62 us | 10 us | 10 us | 18.27 ms | 17.86 ms |
| pypto-lib::models::qwen3::14b::qwen3_14b_prefill_tq_draft::pypto_kv_proj::Mat | PyPTO-Lib/qwen3-14b/Mat | 8 | 20 us | 7 us | 7 us | 535 us | 11.59 ms |
| pypto-lib::models::qwen3::14b::qwen3_14b_prefill_tq_draft::pypto_online_softmax::Vec | PyPTO-Lib/qwen3-14b/Vec | 16 | 318 us | 17 us | 17 us | 37.70 ms | 33.01 ms |
| pypto-lib::models::qwen3::14b::qwen3_14b_prefill_tq_draft::pypto_out_proj_residual::Vec | PyPTO-Lib/qwen3-14b/Vec | 4 | 13 us | 3 us | 2 us | 280 us | 4.14 ms |
| pypto-lib::models::qwen3::14b::qwen3_14b_prefill_tq_draft::pypto_q_rope::Vec | PyPTO-Lib/qwen3-14b/Vec | 14 | 144 us | 17 us | 17 us | 27.92 ms | 27.53 ms |
| pypto-lib::models::qwen3::14b::qwen3_14b_prefill_tq_draft::pypto_rmsnorm::Vec | PyPTO-Lib/qwen3-14b/Vec | 16 | 59 us | 16 us | 15 us | 27.92 ms | 29.94 ms |
| pypto-lib::models::qwen3::14b::qwen3_14b_prefill_tq_draft::pypto_silu::Vec | PyPTO-Lib/qwen3-14b/Vec | 9 | 61 us | 8 us | 8 us | 14.15 ms | 15.25 ms |
| pypto-lib::models::qwen3::14b::topk_select::pypto_greedy_select::Vec | PyPTO-Lib/qwen3-14b/Vec | 18 | 188 us | 23 us | 22 us | 38.31 ms | 38.77 ms |
| pypto-lib::models::qwen3::14b::topk_select::pypto_topk_select::Vec | PyPTO-Lib/qwen3-14b/Vec | 20 | 338 us | 25 us | 25 us | 43.12 ms | 45.89 ms |
| pypto-lib::models::qwen3::32b::qwen3_32b_decode::pypto_gate_proj::Mat | PyPTO-Lib/qwen3-32b/Mat | 8 | 53 us | 8 us | 8 us | 13.26 ms | 12.89 ms |
| pypto-lib::models::qwen3::32b::qwen3_32b_decode::pypto_kv_proj::Left | PyPTO-Lib/qwen3-32b/Left | 16 | 26 us | 17 us | 16 us | 978 us | 30.76 ms |
| pypto-lib::models::qwen3::32b::qwen3_32b_decode::pypto_online_softmax::Vec | PyPTO-Lib/qwen3-32b/Vec | 36 | 1.22 ms | 57 us | 60 us | 120.70 ms | 108.86 ms |
| pypto-lib::models::qwen3::32b::qwen3_32b_decode::pypto_post_rmsnorm::Vec | PyPTO-Lib/qwen3-32b/Vec | 24 | 102 us | 26 us | 27 us | 45.93 ms | 48.41 ms |
| pypto-lib::models::qwen3::32b::qwen3_32b_decode::pypto_qk_matmul::Mat | PyPTO-Lib/qwen3-32b/Mat | 4 | 22 us | 2 us | 3 us | 4.62 ms | 4.18 ms |
| pypto-lib::models::qwen3::32b::qwen3_32b_decode::pypto_rmsnorm::Vec | PyPTO-Lib/qwen3-32b/Vec | 52 | 524 us | 66 us | 70 us | 130.05 ms | 141.61 ms |
| pypto-lib::models::qwen3::32b::qwen3_32b_decode::pypto_rope_kv_cache::Vec | PyPTO-Lib/qwen3-32b/Vec | 31 | 661 us | 40 us | 43 us | 88.88 ms | 81.44 ms |
| pypto-lib::models::qwen3::32b::qwen3_32b_decode::pypto_softmax::Vec | PyPTO-Lib/qwen3-32b/Vec | 22 | 98 us | 25 us | 28 us | 44.09 ms | 45.76 ms |
| pypto-lib::models::qwen3::32b::qwen3_32b_decode_4d::pypto_down_proj::Mat | PyPTO-Lib/qwen3-32b/Mat | 8 | 19 us | 7 us | 7 us | 580 us | 12.71 ms |
| pypto-lib::models::qwen3::32b::qwen3_32b_decode_4d::pypto_out_proj::Mat | PyPTO-Lib/qwen3-32b/Mat | 12 | 30 us | 12 us | 14 us | 21.27 ms | 22.08 ms |
| pypto-lib::models::qwen3::32b::qwen3_32b_decode_4d::pypto_out_proj_residual::Vec | PyPTO-Lib/qwen3-32b/Vec | 16 | 31 us | 15 us | 16 us | 923 us | 28.86 ms |
| pypto-lib::models::qwen3::32b::qwen3_32b_decode_4d::pypto_qk_matmul::Mat | PyPTO-Lib/qwen3-32b/Mat | 4 | 12 us | 4 us | 4 us | 323 us | 4.36 ms |
| pypto-lib::models::qwen3::32b::qwen3_32b_decode_4d::pypto_rmsnorm::Vec | PyPTO-Lib/qwen3-32b/Vec | 52 | 493 us | 80 us | 76 us | 142.74 ms | 150.85 ms |
| pypto-lib::models::qwen3::32b::qwen3_32b_decode_4d::pypto_rope_kv_cache::Vec | PyPTO-Lib/qwen3-32b/Vec | 28 | 579 us | 34 us | 35 us | 66.64 ms | 72.49 ms |
| pypto::tests::st::examples::01_beginner::basic::test_basic_ops::pypto_fused_add_scale_incore_0::Vec | PyPTO/pypto-examples-01_beginner-basic/Vec | 4 | 18 us | 3 us | 5 us | 309 us | 4.69 ms |
| pypto::tests::st::examples::02_intermediate::test_activation::pypto_silu_incore_0::Vec | PyPTO/pypto-examples-02_intermediate/Vec | 6 | 42 us | 6 us | 5 us | 561 us | 8.88 ms |
| pypto::tests::st::examples::02_intermediate::test_ffn_activations::pypto_gelu_kernel::Vec | PyPTO/pypto-examples-02_intermediate/Vec | 7 | 44 us | 7 us | 7 us | 607 us | 11.02 ms |
| pypto::tests::st::examples::02_intermediate::test_layer_norm::pypto_layer_norm_incore_0::Vec | PyPTO/pypto-examples-02_intermediate/Vec | 17 | 192 us | 24 us | 23 us | 39.92 ms | 38.93 ms |
| pypto::tests::st::examples::02_intermediate::test_rms_norm::pypto_rms_norm_incore_0::Vec | PyPTO/pypto-examples-02_intermediate/Vec | 10 | 67 us | 11 us | 11 us | 17.69 ms | 17.52 ms |
| pypto::tests::st::examples::02_intermediate::test_softmax::pypto_tile_softmax_incore_0::Vec | PyPTO/pypto-examples-02_intermediate/Vec | 8 | 24 us | 6 us | 7 us | 598 us | 13.12 ms |
| pypto::tests::st::examples::03_llm_models::test_llama_7b_mini_1h::pypto_kernel_matmul_trans_b::Mat | PyPTO/pypto-examples-03_llm_models/Mat | 8 | 23 us | 7 us | 6 us | 592 us | 12.98 ms |
| pypto::tests::st::examples::03_llm_models::test_llama_7b_mini_1h::pypto_kernel_rms_norm::Vec | PyPTO/pypto-examples-03_llm_models/Vec | 8 | 47 us | 7 us | 11 us | 13.33 ms | 13.56 ms |
| pypto::tests::st::examples::03_llm_models::test_llama_7b_mini_1h::pypto_kernel_rope::Vec | PyPTO/pypto-examples-03_llm_models/Vec | 10 | 81 us | 10 us | 11 us | 20.00 ms | 19.68 ms |
| pypto::tests::st::examples::03_llm_models::test_llama_7b_mini_1h::pypto_kernel_softmax::Vec | PyPTO/pypto-examples-03_llm_models/Vec | 8 | 32 us | 7 us | 6 us | 588 us | 12.89 ms |
| pypto::tests::st::examples::03_llm_models::test_llama_7b_mini_1h::pypto_kernel_swiglu::Vec | PyPTO/pypto-examples-03_llm_models/Vec | 8 | 63 us | 8 us | 8 us | 13.33 ms | 13.53 ms |
| pypto::tests::st::runtime::control_flow::test_ctrl_flow::pypto_kernel_for_if_else::Vec | PyPTO/pypto-runtime-control_flow/Vec | 5 | 33 us | 5 us | 5 us | 7.40 ms | 6.82 ms |
| pypto::tests::st::runtime::control_flow::test_ctrl_flow::pypto_kernel_if_yield::Vec | PyPTO/pypto-runtime-control_flow/Vec | 3 | 17 us | 2 us | 3 us | 291 us | 3.47 ms |
| pypto::tests::st::runtime::control_flow::test_dyn_orch_shape::pypto_kernel_online_update::Vec | PyPTO/pypto-runtime-control_flow/Vec | 21 | 312 us | 25 us | 26 us | 52.94 ms | 48.87 ms |
| pypto::tests::st::runtime::control_flow::test_dyn_orch_shape::pypto_kernel_softmax_prepare::Vec | PyPTO/pypto-runtime-control_flow/Vec | 9 | 68 us | 9 us | 10 us | 14.87 ms | 14.60 ms |
| pypto::tests::st::runtime::cross_core::test_cross_core::pypto_bidirect_aiv::Vec | PyPTO/pypto-runtime-cross_core/Vec | 10 | 17 us | 10 us | 9 us | 537 us | 16.78 ms |
| pypto::tests::st::runtime::cross_core::test_cross_core::pypto_main_incore_0_aiv-0ff59980e9b21d94::Vec | PyPTO/pypto-runtime-cross_core/Vec | 8 | 33 us | 7 us | 8 us | 12.08 ms | 11.92 ms |
| pypto::tests::st::runtime::cross_core::test_cross_core::pypto_main_incore_0_aiv-82edcd9ef10f9f6b::Vec | PyPTO/pypto-runtime-cross_core/Vec | 4 | 21 us | 2 us | 3 us | 4.65 ms | 4.31 ms |
| pypto::tests::st::runtime::cross_core::test_cross_core::pypto_main_incore_0_aiv-f24b408956592e02::Vec | PyPTO/pypto-runtime-cross_core/Vec | 8 | 33 us | 9 us | 7 us | 12.66 ms | 12.94 ms |
| pypto::tests::st::runtime::cross_core::test_cross_core::pypto_main_incore_0_aiv::Vec | PyPTO/pypto-runtime-cross_core/Vec | 4 | 25 us | 3 us | 3 us | 4.68 ms | 4.34 ms |
| pypto::tests::st::runtime::cross_core::test_cross_core::pypto_vector_slotnum::Vec | PyPTO/pypto-runtime-cross_core/Vec | 8 | 26 us | 8 us | 7 us | 12.03 ms | 12.15 ms |
| pypto::tests::st::runtime::cross_core::test_spmd_multi_output_subview::pypto_fused::Vec | PyPTO/pypto-runtime-cross_core/Vec | 19 | 138 us | 19 us | 19 us | 37.21 ms | 36.74 ms |
| pypto::tests::st::runtime::cross_core::test_syncall::pypto_aic_syncall::Mat | PyPTO/pypto-runtime-cross_core/Mat | 3 | 15 us | 1 us | 1 us | 248 us | 3.15 ms |
| pypto::tests::st::runtime::cross_core::test_syncall::pypto_mix_syncall_aiv::Vec | PyPTO/pypto-runtime-cross_core/Vec | 8 | 24 us | 6 us | 6 us | 506 us | 11.69 ms |
| pypto::tests::st::runtime::cross_core::test_syncall::pypto_spmd_syncall_soft_add::Vec | PyPTO/pypto-runtime-cross_core/Vec | 4 | 19 us | 2 us | 3 us | 4.42 ms | 4.02 ms |
| pypto::tests::st::runtime::framework_and_models::test_batch_paged_attention::pypto_KernelSoftmaxPrepare::Vec | PyPTO/pypto-runtime-framework_and_models/Vec | 10 | 61 us | 11 us | 10 us | 16.87 ms | 16.79 ms |
| pypto::tests::st::runtime::framework_and_models::test_ci::pypto_repro_kernel_incore_0::Vec | PyPTO/pypto-runtime-framework_and_models/Vec | 3 | 10 us | 2 us | 2 us | 239 us | 3.18 ms |
| pypto::tests::st::runtime::framework_and_models::test_dynamic_paged_attention::pypto_dyn_kernel_online_update::Vec | PyPTO/pypto-runtime-framework_and_models/Vec | 21 | 260 us | 26 us | 24 us | 47.33 ms | 44.81 ms |
| pypto::tests::st::runtime::framework_and_models::test_dynamic_paged_attention::pypto_dyn_kernel_softmax_prepare::Vec | PyPTO/pypto-runtime-framework_and_models/Vec | 10 | 73 us | 10 us | 9 us | 17.35 ms | 17.39 ms |
| pypto::tests::st::runtime::framework_and_models::test_paged_attention_multi_config::pypto_kernel_online_update-16584c844d7b63d4::Vec | PyPTO/pypto-runtime-framework_and_models/Vec | 21 | 313 us | 26 us | 29 us | 51.74 ms | 47.13 ms |
| pypto::tests::st::runtime::framework_and_models::test_paged_attention_multi_config::pypto_kernel_online_update::Vec | PyPTO/pypto-runtime-framework_and_models/Vec | 21 | 291 us | 24 us | 27 us | 50.51 ms | 47.99 ms |
| pypto::tests::st::runtime::framework_and_models::test_paged_attention_multi_config::pypto_kernel_pv_matmul-1a88d3f94e04dc21::Mat | PyPTO/pypto-runtime-framework_and_models/Mat | 4 | 15 us | 3 us | 3 us | 296 us | 4.01 ms |
| pypto::tests::st::runtime::framework_and_models::test_paged_attention_multi_config::pypto_kernel_pv_matmul-62044ce5003e2a01::Mat | PyPTO/pypto-runtime-framework_and_models/Mat | 4 | 12 us | 2 us | 3 us | 295 us | 3.95 ms |
| pypto::tests::st::runtime::framework_and_models::test_paged_attention_multi_config::pypto_kernel_pv_matmul::Mat | PyPTO/pypto-runtime-framework_and_models/Mat | 4 | 11 us | 2 us | 2 us | 291 us | 3.97 ms |
| pypto::tests::st::runtime::framework_and_models::test_paged_attention_multi_config::pypto_kernel_softmax_prepare-38ccd3bce855159c::Vec | PyPTO/pypto-runtime-framework_and_models/Vec | 19 | 30 us | 19 us | 18 us | 961 us | 33.71 ms |
| pypto::tests::st::runtime::framework_and_models::test_paged_attention_multi_config::pypto_kernel_softmax_prepare-689a89f5726e2b2e::Vec | PyPTO/pypto-runtime-framework_and_models/Vec | 19 | 29 us | 18 us | 17 us | 952 us | 32.39 ms |
| pypto::tests::st::runtime::framework_and_models::test_paged_attention_multi_config::pypto_kernel_softmax_prepare-f7feddcaf73fe379::Vec | PyPTO/pypto-runtime-framework_and_models/Vec | 19 | 36 us | 20 us | 18 us | 967 us | 34.57 ms |
| pypto::tests::st::runtime::framework_and_models::test_paged_attention_multi_config::pypto_kernel_softmax_prepare::Vec | PyPTO/pypto-runtime-framework_and_models/Vec | 19 | 30 us | 20 us | 20 us | 984 us | 32.57 ms |
| pypto::tests::st::runtime::framework_and_models::test_paged_attention_spmd::pypto_SpmdNormalize::Vec | PyPTO/pypto-runtime-framework_and_models/Vec | 3 | 7 us | 3 us | 2 us | 245 us | 3.13 ms |
| pypto::tests::st::runtime::framework_and_models::test_paged_attention_spmd::pypto_SpmdOnlineUpdate::Vec | PyPTO/pypto-runtime-framework_and_models/Vec | 20 | 240 us | 21 us | 20 us | 41.76 ms | 43.07 ms |
| pypto::tests::st::runtime::framework_and_models::test_paged_attention_spmd::pypto_SpmdSoftmaxPrepare::Vec | PyPTO/pypto-runtime-framework_and_models/Vec | 17 | 69 us | 19 us | 21 us | 31.09 ms | 31.65 ms |
| pypto::tests::st::runtime::framework_and_models::test_qwen3_decode_scope3_mixed::pypto_mlp_block_aic::Mat | PyPTO/pypto-runtime-framework_and_models/Mat | 4 | 22 us | 3 us | 3 us | 4.57 ms | 4.26 ms |
| pypto::tests::st::runtime::framework_and_models::test_qwen3_decode_scope3_mixed::pypto_mlp_block_aiv::Vec | PyPTO/pypto-runtime-framework_and_models/Vec | 22 | 68 us | 22 us | 23 us | 40.11 ms | 42.88 ms |
| pypto::tests::st::runtime::framework_and_models::test_qwen3_decode_scope3_mixed::pypto_oproj_block_aiv::Vec | PyPTO/pypto-runtime-framework_and_models/Vec | 8 | 20 us | 7 us | 6 us | 508 us | 11.53 ms |
| pypto::tests::st::runtime::framework_and_models::test_qwen3_decode_scope3_mixed::pypto_postnorm_block::Vec | PyPTO/pypto-runtime-framework_and_models/Vec | 6 | 23 us | 5 us | 4 us | 7.98 ms | 8.26 ms |
| pypto::tests::st::runtime::framework_and_models::test_qwen3_decode_scope3_mixed::pypto_rmsnorm::Vec | PyPTO/pypto-runtime-framework_and_models/Vec | 8 | 39 us | 7 us | 6 us | 11.67 ms | 11.61 ms |
| pypto::tests::st::runtime::ops::test_argmax_reduction::pypto_kernel-12d3a8ac8fdb9bef::Vec | PyPTO/pypto-runtime-ops/Vec | 3 | 8 us | 2 us | 2 us | 243 us | 3.09 ms |
| pypto::tests::st::runtime::ops::test_argmax_reduction::pypto_kernel-1f2102186a086376::Vec | PyPTO/pypto-runtime-ops/Vec | 3 | 5 us | 3 us | 3 us | 242 us | 3.06 ms |
| pypto::tests::st::runtime::ops::test_argmax_reduction::pypto_kernel-2e6b8480b173f393::Vec | PyPTO/pypto-runtime-ops/Vec | 3 | 7 us | 1 us | 2 us | 240 us | 3.08 ms |
| pypto::tests::st::runtime::ops::test_argmax_reduction::pypto_kernel-571bf7c158183d2d::Vec | PyPTO/pypto-runtime-ops/Vec | 3 | 6 us | 1 us | 1 us | 243 us | 3.13 ms |
| pypto::tests::st::runtime::ops::test_argmax_reduction::pypto_kernel-6f5288c34616e1aa::Vec | PyPTO/pypto-runtime-ops/Vec | 3 | 5 us | 2 us | 3 us | 243 us | 3.08 ms |
| pypto::tests::st::runtime::ops::test_argmax_reduction::pypto_kernel-7c07d54356c739b3::Vec | PyPTO/pypto-runtime-ops/Vec | 3 | 7 us | 1 us | 2 us | 240 us | 3.13 ms |
| pypto::tests::st::runtime::ops::test_argmax_reduction::pypto_kernel-b3bf3649a4296c61::Vec | PyPTO/pypto-runtime-ops/Vec | 3 | 7 us | 1 us | 1 us | 240 us | 3.10 ms |
| pypto::tests::st::runtime::ops::test_argmax_reduction::pypto_kernel-c4c3b4056d9b5264::Vec | PyPTO/pypto-runtime-ops/Vec | 3 | 8 us | 2 us | 2 us | 241 us | 3.15 ms |
| pypto::tests::st::runtime::ops::test_argmax_reduction::pypto_kernel-f5f95a5f00667ded::Vec | PyPTO/pypto-runtime-ops/Vec | 3 | 9 us | 1 us | 1 us | 240 us | 3.19 ms |
| pypto::tests::st::runtime::ops::test_argmax_reduction::pypto_kernel::Vec | PyPTO/pypto-runtime-ops/Vec | 3 | 6 us | 2 us | 3 us | 253 us | 3.29 ms |
| pypto::tests::st::runtime::ops::test_auto_tile_matmul::pypto_ddr_split_k_ddr_split_k::Left | PyPTO/pypto-runtime-ops/Left | 4 | 15 us | 2 us | 3 us | 300 us | 4.13 ms |
| pypto::tests::st::runtime::ops::test_auto_tile_matmul::pypto_ddr_split_k_ddr_split_k::Right | PyPTO/pypto-runtime-ops/Right | 4 | 12 us | 2 us | 2 us | 301 us | 4.12 ms |
| pypto::tests::st::runtime::ops::test_broadcast::pypto_col_expand_kernel-9864bc0d6384c627::Vec | PyPTO/pypto-runtime-ops/Vec | 3 | 6 us | 2 us | 2 us | 250 us | 3.17 ms |
| pypto::tests::st::runtime::ops::test_broadcast::pypto_col_expand_kernel-bc2c27b8495cd9a2::Vec | PyPTO/pypto-runtime-ops/Vec | 3 | 4 us | 3 us | 2 us | 252 us | 3.20 ms |
| pypto::tests::st::runtime::ops::test_broadcast::pypto_col_expand_kernel::Vec | PyPTO/pypto-runtime-ops/Vec | 3 | 5 us | 1 us | 2 us | 248 us | 3.20 ms |
| pypto::tests::st::runtime::ops::test_broadcast::pypto_col_expand_mul_column_slice_kernel-376055b7125096bb::Vec | PyPTO/pypto-runtime-ops/Vec | 4 | 17 us | 4 us | 2 us | 4.49 ms | 4.10 ms |
| pypto::tests::st::runtime::ops::test_broadcast::pypto_col_expand_mul_column_slice_kernel-63de932d0dec9e63::Vec | PyPTO/pypto-runtime-ops/Vec | 6 | 28 us | 4 us | 5 us | 8.24 ms | 7.83 ms |
| pypto::tests::st::runtime::ops::test_broadcast::pypto_col_expand_mul_column_slice_kernel::Vec | PyPTO/pypto-runtime-ops/Vec | 6 | 27 us | 6 us | 5 us | 8.40 ms | 8.13 ms |
| pypto::tests::st::runtime::ops::test_broadcast::pypto_col_expand_mul_slice_kernel::Vec | PyPTO/pypto-runtime-ops/Vec | 4 | 29 us | 3 us | 2 us | 4.64 ms | 4.22 ms |
| pypto::tests::st::runtime::ops::test_broadcast::pypto_expand_clone_kernel-78c57111f850e5bc::Vec | PyPTO/pypto-runtime-ops/Vec | 3 | 7 us | 1 us | 2 us | 245 us | 3.11 ms |
| pypto::tests::st::runtime::ops::test_broadcast::pypto_expand_clone_kernel-c687f471d8dddc6a::Vec | PyPTO/pypto-runtime-ops/Vec | 3 | 5 us | 2 us | 2 us | 241 us | 3.14 ms |
| pypto::tests::st::runtime::ops::test_concat::pypto_tile_concat_32x32_incore_0::Vec | PyPTO/pypto-runtime-ops/Vec | 3 | 10 us | 1 us | 1 us | 235 us | 3.09 ms |
| pypto::tests::st::runtime::ops::test_expand_ops::pypto_kernel-0fc36c2fbfe21fb5::Vec | PyPTO/pypto-runtime-ops/Vec | 3 | 7 us | 2 us | 1 us | 253 us | 3.21 ms |
| pypto::tests::st::runtime::ops::test_expand_ops::pypto_kernel::Vec | PyPTO/pypto-runtime-ops/Vec | 3 | 6 us | 1 us | 3 us | 252 us | 3.13 ms |
| pypto::tests::st::runtime::ops::test_gather::pypto_main_incore_0-1dca644ab783d55c::Vec | PyPTO/pypto-runtime-ops/Vec | 5 | 30 us | 4 us | 4 us | 7.08 ms | 6.51 ms |
| pypto::tests::st::runtime::ops::test_gather::pypto_main_incore_0-5c52315cd917b17d::Vec | PyPTO/pypto-runtime-ops/Vec | 5 | 20 us | 4 us | 4 us | 7.19 ms | 7.17 ms |
| pypto::tests::st::runtime::ops::test_gather::pypto_main_incore_0-5e94c22947b68ae8::Vec | PyPTO/pypto-runtime-ops/Vec | 6 | 32 us | 5 us | 4 us | 8.74 ms | 8.03 ms |
| pypto::tests::st::runtime::ops::test_gather::pypto_main_incore_0-81e880e62d4c56b9::Vec | PyPTO/pypto-runtime-ops/Vec | 8 | 51 us | 7 us | 7 us | 12.69 ms | 12.56 ms |
| pypto::tests::st::runtime::ops::test_gather::pypto_main_incore_0-885a88b6ad40ec8f::Vec | PyPTO/pypto-runtime-ops/Vec | 6 | 43 us | 5 us | 5 us | 461 us | 8.22 ms |
| pypto::tests::st::runtime::ops::test_mat_slice_to_left::pypto_mat_slice_to_left::Mat | PyPTO/pypto-runtime-ops/Mat | 3 | 14 us | 3 us | 3 us | 250 us | 3.21 ms |
| pypto::tests::st::runtime::ops::test_matmul::pypto_matmul_acc_64_incore_0::Mat | PyPTO/pypto-runtime-ops/Mat | 4 | 16 us | 2 us | 3 us | 312 us | 4.22 ms |
| pypto::tests::st::runtime::ops::test_matmul::pypto_mixed_add_btrans_aiv::Vec | PyPTO/pypto-runtime-ops/Vec | 6 | 17 us | 4 us | 5 us | 468 us | 8.03 ms |
| pypto::tests::st::runtime::ops::test_memory_planner_ptoas::pypto_kernel::Vec | PyPTO/pypto-runtime-ops/Vec | 14 | 302 us | 15 us | 15 us | 28.54 ms | 29.48 ms |
| pypto::tests::st::runtime::ops::test_memory_reuse_acc_coalesce::pypto_mm_512x512x192_acc_coalesce::Acc | PyPTO/pypto-runtime-ops/Acc | 16 | 32 us | 16 us | 16 us | 956 us | 29.59 ms |
| pypto::tests::st::runtime::ops::test_memory_reuse_acc_coalesce::pypto_mm_512x512x192_acc_coalesce::Left | PyPTO/pypto-runtime-ops/Left | 24 | 36 us | 26 us | 29 us | 1.40 ms | 46.86 ms |
| pypto::tests::st::runtime::ops::test_memory_reuse_acc_coalesce::pypto_mm_512x512x192_acc_coalesce::Right | PyPTO/pypto-runtime-ops/Right | 24 | 36 us | 25 us | 27 us | 1.36 ms | 49.30 ms |
| pypto::tests::st::runtime::ops::test_prod_reduction::pypto_kernel-588a14b69424b5ef::Vec | PyPTO/pypto-runtime-ops/Vec | 3 | 11 us | 3 us | 3 us | 271 us | 3.46 ms |
| pypto::tests::st::runtime::ops::test_prod_reduction::pypto_kernel::Vec | PyPTO/pypto-runtime-ops/Vec | 3 | 11 us | 2 us | 2 us | 263 us | 3.45 ms |
| pypto::tests::st::runtime::ops::test_rsqrt::pypto_main_incore_0-e0989979105661a9::Vec | PyPTO/pypto-runtime-ops/Vec | 3 | 7 us | 3 us | 2 us | 272 us | 3.41 ms |
| pypto::tests::st::runtime::ops::test_scatter::pypto_main_incore_0-a649dfc91e66db6c::Vec | PyPTO/pypto-runtime-ops/Vec | 12 | 84 us | 16 us | 14 us | 22.95 ms | 23.17 ms |
| pypto::tests::st::runtime::ops::test_scatter::pypto_main_incore_0-af62fc156f029d10::Vec | PyPTO/pypto-runtime-ops/Vec | 15 | 73 us | 14 us | 16 us | 26.72 ms | 28.85 ms |
| pypto::tests::st::runtime::ops::test_scatter::pypto_main_incore_0-d12679089a454e18::Vec | PyPTO/pypto-runtime-ops/Vec | 8 | 42 us | 7 us | 7 us | 12.56 ms | 12.67 ms |
| pypto::tests::st::runtime::ops::test_scatter::pypto_main_incore_0-d5d3f1dd092aa73f::Vec | PyPTO/pypto-runtime-ops/Vec | 12 | 74 us | 13 us | 11 us | 21.99 ms | 22.00 ms |
| pypto::tests::st::runtime::ops::test_scatter::pypto_main_incore_0::Vec | PyPTO/pypto-runtime-ops/Vec | 9 | 47 us | 8 us | 9 us | 14.71 ms | 14.52 ms |
| pypto::tests::st::runtime::ops::test_scatter_update::pypto_kernel-a359420001b9111f::Vec | PyPTO/pypto-runtime-ops/Vec | 14 | 108 us | 14 us | 13 us | 25.07 ms | 26.63 ms |
| pypto::tests::st::runtime::ops::test_scatter_update::pypto_kernel-cb9c4a9226d73b99::Vec | PyPTO/pypto-runtime-ops/Vec | 15 | 159 us | 15 us | 15 us | 28.72 ms | 30.38 ms |
| pypto::tests::st::runtime::ops::test_scatter_update::pypto_main_incore_0::Vec | PyPTO/pypto-runtime-ops/Vec | 14 | 110 us | 14 us | 14 us | 25.08 ms | 25.98 ms |
| pypto::tests::st::runtime::ops::test_tensor_batch_matmul::pypto_batch_matmul_3d_btrans-a796aec773083fe1::Left | PyPTO/pypto-runtime-ops/Left | 8 | 17 us | 7 us | 6 us | 533 us | 11.60 ms |
| pypto::tests::st::runtime::ops::test_tensor_batch_matmul::pypto_batch_matmul_3d_btrans-a796aec773083fe1::Mat | PyPTO/pypto-runtime-ops/Mat | 8 | 16 us | 6 us | 6 us | 531 us | 11.37 ms |
| pypto::tests::st::runtime::ops::test_tensor_batch_matmul::pypto_batch_matmul_3d_btrans-a796aec773083fe1::Right | PyPTO/pypto-runtime-ops/Right | 8 | 21 us | 7 us | 7 us | 547 us | 12.12 ms |
| pypto::tests::st::runtime::ops::test_tensor_batch_matmul::pypto_mixed_add_btrans_nd_aiv::Vec | PyPTO/pypto-runtime-ops/Vec | 6 | 16 us | 5 us | 5 us | 452 us | 8.22 ms |
| pypto::tests::st::runtime::ops::test_trans::pypto_kernel-17c839e7de54a6cd::Vec | PyPTO/pypto-runtime-ops/Vec | 3 | 10 us | 1 us | 2 us | 254 us | 3.27 ms |
| pypto::tests::st::runtime::ops::test_trans::pypto_kernel-2b8511e5bd60e571::Vec | PyPTO/pypto-runtime-ops/Vec | 3 | 8 us | 3 us | 1 us | 250 us | 3.17 ms |
| pypto::tests::st::runtime::ops::test_trans::pypto_kernel-bdc41f704d68a63e::Vec | PyPTO/pypto-runtime-ops/Vec | 7 | 48 us | 7 us | 6 us | 10.23 ms | 9.91 ms |
| pypto::tests::st::runtime::ops::test_trans::pypto_kernel-ff09810cf359506a::Vec | PyPTO/pypto-runtime-ops/Vec | 3 | 7 us | 3 us | 1 us | 238 us | 3.13 ms |
| pypto::tests::st::runtime::ops::test_trans::pypto_kernel::Vec | PyPTO/pypto-runtime-ops/Vec | 6 | 16 us | 5 us | 5 us | 418 us | 7.67 ms |
| target_hazard::Vec | PyPTO/Vec | 3 | 7 us | 2 us | 2 us | 245 us | 3.10 ms |
