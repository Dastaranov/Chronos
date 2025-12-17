#if BLAKE3_USE_NEON == 1

#include "blake3_impl.h"
#include <arm_neon.h>

#define DEGREE 4

INLINE uint32x4_t rot16(uint32x4_t x) {
  return vrev32q_u16(x);
}

INLINE uint32x4_t rot12(uint32x4_t x) {
  return vsriq_n_u32(vshlq_n_u32(x, 32 - 12), x, 12);
}

INLINE uint32x4_t rot8(uint32x4_t x) {
  return vsriq_n_u32(vshlq_n_u32(x, 32 - 8), x, 8);
}

INLINE uint32x4_t rot7(uint32x4_t x) {
  return vsriq_n_u32(vshlq_n_u32(x, 32 - 7), x, 7);
}

INLINE void g(uint32x4_t *a, uint32x4_t *b, uint32x4_t *c, uint32x4_t *d,
               uint32x4_t m) {
  *a = vaddq_u32(*a, *b);
  *a = vaddq_u32(*a, m);
  *d = veorq_u32(*d, *a);
  *d = rot16(*d);
  *c = vaddq_u32(*c, *d);
  *b = veorq_u32(*b, *c);
  *b = rot12(*b);
  *a = vaddq_u32(*a, *b);
  *a = vaddq_u32(*a, m);
  *d = veorq_u32(*d, *a);
  *d = rot8(*d);
  *c = vaddq_u32(*c, *d);
  *b = veorq_u32(*b, *c);
  *b = rot7(*b);
}

INLINE void round_fn(uint32x4_t v[16], uint32x4_t m[16], size_t r) {
  const uint8_t *schedule = MSG_SCHEDULE[r];
  g(&v[0], &v[4], &v[8], &v[12], m[schedule[0]]);
  g(&v[1], &v[5], &v[9], &v[13], m[schedule[1]]);
  g(&v[2], &v[6], &v[10], &v[14], m[schedule[2]]);
  g(&v[3], &v[7], &v[11], &v[15], m[schedule[3]]);
  g(&v[0], &v[5], &v[10], &v[15], m[schedule[4]]);
  g(&v[1], &v[6], &v[11], &v[12], m[schedule[5]]);
  g(&v[2], &v[7], &v[8], &v[13], m[schedule[6]]);
  g(&v[3], &v[4], &v[9], &v[14], m[schedule[7]]);
}

INLINE void transpose_vecs(uint32x4_t vecs[DEGREE]) {
  // Interleave 32-bit lanes.
  uint32x4x2_t ab = vzipq_u32(vecs[0], vecs[1]);
  uint32x4x2_t cd = vzipq_u32(vecs[2], vecs[3]);

  // Interleave 64-bit lanes.
  uint64x2x2_t ab_0 = vzipq_u64(vreinterpretq_u64_u32(ab.val[0]),
                               vreinterpretq_u64_u32(cd.val[0]));
  uint64x2x2_t ab_1 = vzipq_u64(vreinterpretq_u64_u32(ab.val[1]),
                               vreinterpretq_u64_u32(cd.val[1]));

  vecs[0] = vreinterpretq_u32_u64(ab_0.val[0]);
  vecs[1] = vreinterpretq_u32_u64(ab_1.val[0]);
  vecs[2] = vreinterpretq_u32_u64(ab_0.val[1]);
  vecs[3] = vreinterpretq_u32_u64(ab_1.val[1]);
}

INLINE void untranspose_vecs(uint32x4_t vecs[DEGREE]) {
  // This is the inverse of transpose_vecs.
  uint64x2x2_t ab_0 = vzipq_u64(vreinterpretq_u64_u32(vecs[0]),
                               vreinterpretq_u64_u32(vecs[2]));
  uint64x2x2_t ab_1 = vzipq_u64(vreinterpretq_u64_u32(vecs[1]),
                               vreinterpretq_u64_u32(vecs[3]));

  uint32x4x2_t ab = vzipq_u32(vreinterpretq_u32_u64(ab_0.val[0]),
                               vreinterpretq_u32_u64(ab_1.val[0]));
  uint32x4x2_t cd = vzipq_u32(vreinterpretq_u32_u64(ab_0.val[1]),
                               vreinterpretq_u32_u64(ab_1.val[1]));

  vecs[0] = ab.val[0];
  vecs[1] = ab.val[1];
  vecs[2] = cd.val[0];
  vecs[3] = cd.val[1];
}

INLINE void transpose_msg_vecs(const uint8_t *const *inputs,
                               size_t block_offset, uint32x4_t out[16]) {
  out[0] = vld1q_u32((const uint32_t *)(&inputs[0][block_offset]));
  out[1] = vld1q_u32((const uint32_t *)(&inputs[1][block_offset]));
  out[2] = vld1q_u32((const uint32_t *)(&inputs[2][block_offset]));
  out[3] = vld1q_u32((const uint32_t *)(&inputs[3][block_offset]));
  out[4] = vld1q_u32((const uint32_t *)(&inputs[0][block_offset + 16]));
  out[5] = vld1q_u32((const uint32_t *)(&inputs[1][block_offset + 16]));
  out[6] = vld1q_u32((const uint32_t *)(&inputs[2][block_offset + 16]));
  out[7] = vld1q_u32((const uint32_t *)(&inputs[3][block_offset + 16]));
  out[8] = vld1q_u32((const uint32_t *)(&inputs[0][block_offset + 32]));
  out[9] = vld1q_u32((const uint32_t *)(&inputs[1][block_offset + 32]));
  out[10] = vld1q_u32((const uint32_t *)(&inputs[2][block_offset + 32]));
  out[11] = vld1q_u32((const uint32_t *)(&inputs[3][block_offset + 32]));
  out[12] = vld1q_u32((const uint32_t *)(&inputs[0][block_offset + 48]));
  out[13] = vld1q_u32((const uint32_t *)(&inputs[1][block_offset + 48]));
  out[14] = vld1q_u32((const uint32_t *)(&inputs[2][block_offset + 48]));
  out[15] = vld1q_u32((const uint32_t *)(&inputs[3][block_offset + 48]));
  transpose_vecs(&out[0]);
  transpose_vecs(&out[4]);
  transpose_vecs(&out[8]);
  transpose_vecs(&out[12]);
}

INLINE void load_counters(uint64_t counter, bool increment_counter,
                          uint32x4_t *out_lo, uint32x4_t *out_hi) {
  uint32x4_t add = {0, 1, 2, 3};
  if (!increment_counter) {
    add = vdupq_n_u32(0);
  }
  uint32x4_t l = vaddq_u32(vdupq_n_u32((uint32_t)counter), add);
  uint32x4_t carry = vcgtq_u32(add, l);
  uint32x4_t h = vdupq_n_u32((uint32_t)(counter >> 32));
  h = vsubq_u32(h, carry);
  *out_lo = l;
  *out_hi = h;
}

static void blake3_hash4_neon(const uint8_t *const *inputs, size_t blocks,
                              const uint32_t key[8], uint64_t counter,
                              bool increment_counter, uint8_t flags,
                              uint8_t flags_start, uint8_t flags_end,
                              uint8_t *out) {
  uint32x4_t h_vecs[8] = {
      vld1q_u32(&key[0]), vld1q_u32(&key[2]), vld1q_u32(&key[4]),
      vld1q_u32(&key[6]),
  };
  transpose_vecs(h_vecs);
  uint32x4_t iv_vecs[4] = {
      vld1q_u32(&IV[0]),
      vld1q_u32(&IV[4]),
  };
  transpose_vecs(iv_vecs);

  uint32x4_t counter_low_vec, counter_high_vec;
  load_counters(counter, increment_counter, &counter_low_vec,
                &counter_high_vec);
  uint8_t block_flags = flags | flags_start;

  for (size_t block = 0; block < blocks; block++) {
    if (block + 1 == blocks) {
      block_flags |= flags_end;
    }
    uint32x4_t block_len_vec = vdupq_n_u32(BLAKE3_BLOCK_LEN);
    uint32x4_t block_flags_vec = vdupq_n_u32(block_flags);
    uint32x4_t msg_vecs[16];
    transpose_msg_vecs(inputs, block * BLAKE3_BLOCK_LEN, msg_vecs);

    uint32x4_t v[16] = {
        h_vecs[0],   h_vecs[1],   h_vecs[2],   h_vecs[3],
        h_vecs[4],   h_vecs[5],   h_vecs[6],   h_vecs[7],
        iv_vecs[0],  iv_vecs[1],  iv_vecs[2],  iv_vecs[3],
        counter_low_vec, counter_high_vec, block_len_vec, block_flags_vec,
    };
    round_fn(v, msg_vecs, 0);
    round_fn(v, msg_vecs, 1);
    round_fn(v, msg_vecs, 2);
    round_fn(v, msg_vecs, 3);
    round_fn(v, msg_vecs, 4);
    round_fn(v, msg_vecs, 5);
    round_fn(v, msg_vecs, 6);
    h_vecs[0] = veorq_u32(v[0], v[8]);
    h_vecs[1] = veorq_u32(v[1], v[9]);
    h_vecs[2] = veorq_u32(v[2], v[10]);
    h_vecs[3] = veorq_u32(v[3], v[11]);
    h_vecs[4] = veorq_u32(v[4], v[12]);
    h_vecs[5] = veorq_u32(v[5], v[13]);
    h_vecs[6] = veorq_u32(v[6], v[14]);
    h_vecs[7] = veorq_u32(v[7], v[15]);

    block_flags = flags;
  }

  untranspose_vecs(&h_vecs[0]);
  untranspose_vecs(&h_vecs[4]);
  vst1q_u32((uint32_t *)(&out[0 * 4 * sizeof(uint32_t)]), h_vecs[0]);
  vst1q_u32((uint32_t *)(&out[1 * 4 * sizeof(uint32_t)]), h_vecs[4]);
  vst1q_u32((uint32_t *)(&out[2 * 4 * sizeof(uint32_t)]), h_vecs[1]);
  vst1q_u32((uint32_t *)(&out[3 * 4 * sizeof(uint32_t)]), h_vecs[5]);
  vst1q_u32((uint32_t *)(&out[4 * 4 * sizeof(uint32_t)]), h_vecs[2]);
  vst1q_u32((uint32_t *)(&out[5 * 4 * sizeof(uint32_t)]), h_vecs[6]);
  vst1q_u32((uint32_t *)(&out[6 * 4 * sizeof(uint32_t)]), h_vecs[3]);
  vst1q_u32((uint32_t *)(&out[7 * 4 * sizeof(uint32_t)]), h_vecs[7]);
}

void blake3_hash_many_neon(const uint8_t *const *inputs, size_t num_inputs,
                           size_t blocks, const uint32_t key[8],
                           uint64_t counter, bool increment_counter,
                           uint8_t flags, uint8_t flags_start,
                           uint8_t flags_end, uint8_t *out) {
  while (num_inputs >= DEGREE) {
    blake3_hash4_neon(inputs, blocks, key, counter, increment_counter, flags,
                      flags_start, flags_end, out);
    if (increment_counter) {
      counter += DEGREE;
    }
    inputs += DEGREE;
    num_inputs -= DEGREE;
    out = &out[DEGREE * BLAKE3_OUT_LEN];
  }
  blake3_hash_many_portable(inputs, num_inputs, blocks, key, counter,
                            increment_counter, flags, flags_start, flags_end,
                            out);
}

#endif
