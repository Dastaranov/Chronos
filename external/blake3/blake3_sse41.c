#if defined(IS_X86) && !defined(BLAKE3_NO_SSE41)

#include "blake3_impl.h"
#include <immintrin.h>

#define DEGREE 4

INLINE __m128i loadu(const uint8_t src[16]) {
  return _mm_loadu_si128((const __m128i *)src);
}

INLINE void storeu(__m128i src, uint8_t dest[16]) {
  _mm_storeu_si128((__m128i *)dest, src);
}

INLINE __m128i addv(__m128i a, __m128i b) { return _mm_add_epi32(a, b); }

// Note that clang-format doesn't like the name "xor" for some reason.
INLINE __m128i xorv(__m128i a, __m128i b) { return _mm_xor_si128(a, b); }

INLINE __m128i set1(uint32_t x) { return _mm_set1_epi32((int32_t)x); }

INLINE __m128i set4(uint32_t a, uint32_t b, uint32_t c, uint32_t d) {
  return _mm_setr_epi32((int32_t)a, (int32_t)b, (int32_t)c, (int32_t)d);
}

INLINE __m128i rot16(__m128i x) {
  return _mm_shuffle_epi8(
      x, _mm_set_epi8(13, 12, 15, 14, 9, 8, 11, 10, 5, 4, 7, 6, 1, 0, 3, 2));
}

INLINE __m128i rot12(__m128i x) {
  return xorv(_mm_srli_epi32(x, 12), _mm_slli_epi32(x, 32 - 12));
}

INLINE __m128i rot8(__m128i x) {
  return xorv(_mm_srli_epi32(x, 8), _mm_slli_epi32(x, 32 - 8));
}

INLINE __m128i rot7(__m128i x) {
  return xorv(_mm_srli_epi32(x, 7), _mm_slli_epi32(x, 32 - 7));
}

INLINE void g1(__m128i *row0, __m128i *row1, __m128i *row2, __m128i *row3,
               __m128i m) {
  *row0 = addv(addv(*row0, m), *row1);
  *row3 = xorv(*row3, *row0);
  *row3 = rot16(*row3);
  *row2 = addv(*row2, *row3);
  *row1 = xorv(*row1, *row2);
  *row1 = rot12(*row1);
}

INLINE void g2(__m128i *row0, __m128i *row1, __m128i *row2, __m128i *row3,
               __m128i m) {
  *row0 = addv(addv(*row0, m), *row1);
  *row3 = xorv(*row3, *row0);
  *row3 = rot8(*row3);
  *row2 = addv(*row2, *row3);
  *row1 = xorv(*row1, *row2);
  *row1 = rot7(*row1);
}

// Note the optimization here of leaving row1 as the unrotated row, rather than
// row0. All the message loads below are adjusted to compensate for this. See
// discussion at https://github.com/sneves/blake2-avx2/pull/4
INLINE void diagonalize(__m128i *row0, __m128i *row2, __m128i *row3) {
  *row0 = _mm_shuffle_epi32(*row0, _MM_SHUFFLE(2, 1, 0, 3));
  *row3 = _mm_shuffle_epi32(*row3, _MM_SHUFFLE(1, 0, 3, 2));
  *row2 = _mm_shuffle_epi32(*row2, _MM_SHUFFLE(0, 3, 2, 1));
}

INLINE void undiagonalize(__m128i *row0, __m128i *row2, __m128i *row3) {
  *row0 = _mm_shuffle_epi32(*row0, _MM_SHUFFLE(0, 3, 2, 1));
  *row3 = _mm_shuffle_epi32(*row3, _MM_SHUFFLE(1, 0, 3, 2));
  *row2 = _mm_shuffle_epi32(*row2, _MM_SHUFFLE(2, 1, 0, 3));
}

INLINE void compress_pre(__m128i rows[4], const uint32_t cv[8],
                         const uint8_t block[BLAKE3_BLOCK_LEN],
                         uint8_t block_len, uint64_t counter, uint8_t flags) {
  rows[0] = loadu((uint8_t *)&cv[0]);
  rows[1] = loadu((uint8_t *)&cv[4]);
  rows[2] = set4(IV[0], IV[1], IV[2], IV[3]);
  rows[3] = set4(counter_low(counter), counter_high(counter),
                 (uint32_t)block_len, (uint32_t)flags);

  __m128i m0 = loadu(&block[sizeof(__m128i) * 0]);
  __m128i m1 = loadu(&block[sizeof(__m128i) * 1]);
  __m128i m2 = loadu(&block[sizeof(__m128i) * 2]);
  __m128i m3 = loadu(&block[sizeof(__m128i) * 3]);

  // Round 1
  g1(&rows[0], &rows[1], &rows[2], &rows[3], m0);
  g2(&rows[0], &rows[1], &rows[2], &rows[3], m1);
  diagonalize(&rows[0], &rows[2], &rows[3]);
  g1(&rows[0], &rows[1], &rows[2], &rows[3], m2);
  g2(&rows[0], &rows[1], &rows[2], &rows[3], m3);
  undiagonalize(&rows[0], &rows[2], &rows[3]);

  // Round 2
  m0 = _mm_shuffle_epi8(
      m0, _mm_set_epi8(1, 0, 3, 2, 5, 4, 7, 6, 9, 8, 11, 10, 13, 12, 15, 14));
  m1 = _mm_shuffle_epi8(
      m1, _mm_set_epi8(1, 0, 3, 2, 5, 4, 7, 6, 9, 8, 11, 10, 13, 12, 15, 14));
  m2 = _mm_shuffle_epi8(
      m2, _mm_set_epi8(1, 0, 3, 2, 5, 4, 7, 6, 9, 8, 11, 10, 13, 12, 15, 14));
  m3 = _mm_shuffle_epi8(
      m3, _mm_set_epi8(1, 0, 3, 2, 5, 4, 7, 6, 9, 8, 11, 10, 13, 12, 15, 14));
  g1(&rows[0], &rows[1], &rows[2], &rows[3], m0);
  g2(&rows[0], &rows[1], &rows[2], &rows[3], m1);
  diagonalize(&rows[0], &rows[2], &rows[3]);
  g1(&rows[0], &rows[1], &rows[2], &rows[3], m2);
  g2(&rows[0], &rows[1], &rows[2], &rows[3], m3);
  undiagonalize(&rows[0], &rows[2], &rows[3]);

  // Round 3
  m0 = _mm_shuffle_epi8(
      m0, _mm_set_epi8(1, 0, 3, 2, 5, 4, 7, 6, 9, 8, 11, 10, 13, 12, 15, 14));
  m1 = _mm_shuffle_epi8(
      m1, _mm_set_epi8(1, 0, 3, 2, 5, 4, 7, 6, 9, 8, 11, 10, 13, 12, 15, 14));
  m2 = _mm_shuffle_epi8(
      m2, _mm_set_epi8(1, 0, 3, 2, 5, 4, 7, 6, 9, 8, 11, 10, 13, 12, 15, 14));
  m3 = _mm_shuffle_epi8(
      m3, _mm_set_epi8(1, 0, 3, 2, 5, 4, 7, 6, 9, 8, 11, 10, 13, 12, 15, 14));
  g1(&rows[0], &rows[1], &rows[2], &rows[3], m0);
  g2(&rows[0], &rows[1], &rows[2], &rows[3], m1);
  diagonalize(&rows[0], &rows[2], &rows[3]);
  g1(&rows[0], &rows[1], &rows[2], &rows[3], m2);
  g2(&rows[0], &rows[1], &rows[2], &rows[3], m3);
  undiagonalize(&rows[0], &rows[2], &rows[3]);

  // Round 4
  m0 = _mm_shuffle_epi8(
      m0, _mm_set_epi8(1, 0, 3, 2, 5, 4, 7, 6, 9, 8, 11, 10, 13, 12, 15, 14));
  m1 = _mm_shuffle_epi8(
      m1, _mm_set_epi8(1, 0, 3, 2, 5, 4, 7, 6, 9, 8, 11, 10, 13, 12, 15, 14));
  m2 = _mm_shuffle_epi8(
      m2, _mm_set_epi8(1, 0, 3, 2, 5, 4, 7, 6, 9, 8, 11, 10, 13, 12, 15, 14));
  m3 = _mm_shuffle_epi8(
      m3, _mm_set_epi8(1, 0, 3, 2, 5, 4, 7, 6, 9, 8, 11, 10, 13, 12, 15, 14));
  g1(&rows[0], &rows[1], &rows[2], &rows[3], m0);
  g2(&rows[0], &rows[1], &rows[2], &rows[3], m1);
  diagonalize(&rows[0], &rows[2], &rows[3]);
  g1(&rows[0], &rows[1], &rows[2], &rows[3], m2);
  g2(&rows[0], &rows[1], &rows[2], &rows[3], m3);
  undiagonalize(&rows[0], &rows[2], &rows[3]);

  // Round 5
  m0 = _mm_shuffle_epi8(
      m0, _mm_set_epi8(1, 0, 3, 2, 5, 4, 7, 6, 9, 8, 11, 10, 13, 12, 15, 14));
  m1 = _mm_shuffle_epi8(
      m1, _mm_set_epi8(1, 0, 3, 2, 5, 4, 7, 6, 9, 8, 11, 10, 13, 12, 15, 14));
  m2 = _mm_shuffle_epi8(
      m2, _mm_set_epi8(1, 0, 3, 2, 5, 4, 7, 6, 9, 8, 11, 10, 13, 12, 15, 14));
  m3 = _mm_shuffle_epi8(
      m3, _mm_set_epi8(1, 0, 3, 2, 5, 4, 7, 6, 9, 8, 11, 10, 13, 12, 15, 14));
  g1(&rows[0], &rows[1], &rows[2], &rows[3], m0);
  g2(&rows[0], &rows[1], &rows[2], &rows[3], m1);
  diagonalize(&rows[0], &rows[2], &rows[3]);
  g1(&rows[0], &rows[1], &rows[2], &rows[3], m2);
  g2(&rows[0], &rows[1], &rows[2], &rows[3], m3);
  undiagonalize(&rows[0], &rows[2], &rows[3]);

  // Round 6
  m0 = _mm_shuffle_epi8(
      m0, _mm_set_epi8(1, 0, 3, 2, 5, 4, 7, 6, 9, 8, 11, 10, 13, 12, 15, 14));
  m1 = _mm_shuffle_epi8(
      m1, _mm_set_epi8(1, 0, 3, 2, 5, 4, 7, 6, 9, 8, 11, 10, 13, 12, 15, 14));
  m2 = _mm_shuffle_epi8(
      m2, _mm_set_epi8(1, 0, 3, 2, 5, 4, 7, 6, 9, 8, 11, 10, 13, 12, 15, 14));
  m3 = _mm_shuffle_epi8(
      m3, _mm_set_epi8(1, 0, 3, 2, 5, 4, 7, 6, 9, 8, 11, 10, 13, 12, 15, 14));
  g1(&rows[0], &rows[1], &rows[2], &rows[3], m0);
  g2(&rows[0], &rows[1], &rows[2], &rows[3], m1);
  diagonalize(&rows[0], &rows[2], &rows[3]);
  g1(&rows[0], &rows[1], &rows[2], &rows[3], m2);
  g2(&rows[0], &rows[1], &rows[2], &rows[3], m3);
  undiagonalize(&rows[0], &rows[2], &rows[3]);

  // Round 7
  m0 = _mm_shuffle_epi8(
      m0, _mm_set_epi8(1, 0, 3, 2, 5, 4, 7, 6, 9, 8, 11, 10, 13, 12, 15, 14));
  m1 = _mm_shuffle_epi8(
      m1, _mm_set_epi8(1, 0, 3, 2, 5, 4, 7, 6, 9, 8, 11, 10, 13, 12, 15, 14));
  m2 = _mm_shuffle_epi8(
      m2, _mm_set_epi8(1, 0, 3, 2, 5, 4, 7, 6, 9, 8, 11, 10, 13, 12, 15, 14));
  m3 = _mm_shuffle_epi8(
      m3, _mm_set_epi8(1, 0, 3, 2, 5, 4, 7, 6, 9, 8, 11, 10, 13, 12, 15, 14));
  g1(&rows[0], &rows[1], &rows[2], &rows[3], m0);
  g2(&rows[0], &rows[1], &rows[2], &rows[3], m1);
  diagonalize(&rows[0], &rows[2], &rows[3]);
  g1(&rows[0], &rows[1], &rows[2], &rows[3], m2);
  g2(&rows[0], &rows[1], &rows[2], &rows[3], m3);
  undiagonalize(&rows[0], &rows[2], &rows[3]);
}

void blake3_compress_in_place_sse41(uint32_t cv[8],
                                     const uint8_t block[BLAKE3_BLOCK_LEN],
                                     uint8_t block_len, uint64_t counter,
                                     uint8_t flags) {
  __m128i rows[4];
  compress_pre(rows, cv, block, block_len, counter, flags);
  storeu(xorv(rows[0], rows[2]), (uint8_t *)&cv[0]);
  storeu(xorv(rows[1], rows[3]), (uint8_t *)&cv[4]);
}

void blake3_compress_xof_sse41(const uint32_t cv[8],
                               const uint8_t block[BLAKE3_BLOCK_LEN],
                               uint8_t block_len, uint64_t counter,
                               uint8_t flags, uint8_t out[64]) {
  __m128i rows[4];
  compress_pre(rows, cv, block, block_len, counter, flags);
  storeu(xorv(rows[0], rows[2]), &out[0]);
  storeu(xorv(rows[1], rows[3]), &out[16]);
  storeu(xorv(rows[2], loadu((uint8_t *)&cv[0])), &out[32]);
  storeu(xorv(rows[3], loadu((uint8_t *)&cv[4])), &out[48]);
}

INLINE void round_fn_vec(__m128i v[16], __m128i m[16], size_t r) {
  const __m128i s =
      _mm_set_epi8(1, 0, 3, 2, 5, 4, 7, 6, 9, 8, 11, 10, 13, 12, 15, 14);
  v[0] = addv(v[0], m[(size_t)MSG_SCHEDULE[r][0]]);
  v[1] = addv(v[1], m[(size_t)MSG_SCHEDULE[r][2]]);
  v[2] = addv(v[2], m[(size_t)MSG_SCHEDULE[r][4]]);
  v[3] = addv(v[3], m[(size_t)MSG_SCHEDULE[r][6]]);
  v[0] = addv(v[0], v[4]);
  v[1] = addv(v[1], v[5]);
  v[2] = addv(v[2], v[6]);
  v[3] = addv(v[3], v[7]);
  v[12] = xorv(v[12], v[0]);
  v[13] = xorv(v[13], v[1]);
  v[14] = xorv(v[14], v[2]);
  v[15] = xorv(v[15], v[3]);
  v[12] = rot16(v[12]);
  v[13] = rot16(v[13]);
  v[14] = rot16(v[14]);
  v[15] = rot16(v[15]);
  v[8] = addv(v[8], v[12]);
  v[9] = addv(v[9], v[13]);
  v[10] = addv(v[10], v[14]);
  v[11] = addv(v[11], v[15]);
  v[4] = xorv(v[4], v[8]);
  v[5] = xorv(v[5], v[9]);
  v[6] = xorv(v[6], v[10]);
  v[7] = xorv(v[7], v[11]);
  v[4] = rot12(v[4]);
  v[5] = rot12(v[5]);
  v[6] = rot12(v[6]);
  v[7] = rot12(v[7]);
  v[0] = addv(v[0], m[(size_t)MSG_SCHEDULE[r][1]]);
  v[1] = addv(v[1], m[(size_t)MSG_SCHEDULE[r][3]]);
  v[2] = addv(v[2], m[(size_t)MSG_SCHEDULE[r][5]]);
  v[3] = addv(v[3], m[(size_t)MSG_SCHEDULE[r][7]]);
  v[0] = addv(v[0], v[4]);
  v[1] = addv(v[1], v[5]);
  v[2] = addv(v[2], v[6]);
  v[3] = addv(v[3], v[7]);
  v[12] = xorv(v[12], v[0]);
  v[13] = xorv(v[13], v[1]);
  v[14] = xorv(v[14], v[2]);
  v[15] = xorv(v[15], v[3]);
  v[12] = rot8(v[12]);
  v[13] = rot8(v[13]);
  v[14] = rot8(v[14]);
  v[15] = rot8(v[15]);
  v[8] = addv(v[8], v[12]);
  v[9] = addv(v[9], v[13]);
  v[10] = addv(v[10], v[14]);
  v[11] = addv(v[11], v[15]);
  v[4] = xorv(v[4], v[8]);
  v[5] = xorv(v[5], v[9]);
  v[6] = xorv(v[6], v[10]);
  v[7] = xorv(v[7], v[11]);
  v[4] = rot7(v[4]);
  v[5] = rot7(v[5]);
  v[6] = rot7(v[6]);
  v[7] = rot7(v[7]);

  m[0] = _mm_shuffle_epi8(m[0], s);
  m[2] = _mm_shuffle_epi8(m[2], s);
  m[4] = _mm_shuffle_epi8(m[4], s);
  m[6] = _mm_shuffle_epi8(m[6], s);
  m[8] = _mm_shuffle_epi8(m[8], s);
  m[10] = _mm_shuffle_epi8(m[10], s);
  m[12] = _mm_shuffle_epi8(m[12], s);
  m[14] = _mm_shuffle_epi8(m[14], s);
  v[0] = addv(v[0], m[(size_t)MSG_SCHEDULE[r][8]]);
  v[1] = addv(v[1], m[(size_t)MSG_SCHEDULE[r][10]]);
  v[2] = addv(v[2], m[(size_t)MSG_SCHEDULE[r][12]]);
  v[3] = addv(v[3], m[(size_t)MSG_SCHEDULE[r][14]]);
  v[0] = addv(v[0], v[5]);
  v[1] = addv(v[1], v[6]);
  v[2] = addv(v[2], v[7]);
  v[3] = addv(v[3], v[4]);
  v[15] = xorv(v[15], v[0]);
  v[12] = xorv(v[12], v[1]);
  v[13] = xorv(v[13], v[2]);
  v[14] = xorv(v[14], v[3]);
  v[15] = rot16(v[15]);
  v[12] = rot16(v[12]);
  v[13] = rot16(v[13]);
  v[14] = rot16(v[14]);
  v[10] = addv(v[10], v[15]);
  v[11] = addv(v[11], v[12]);
  v[8] = addv(v[8], v[13]);
  v[9] = addv(v[9], v[14]);
  v[5] = xorv(v[5], v[10]);
  v[6] = xorv(v[6], v[11]);
  v[7] = xorv(v[7], v[8]);
  v[4] = xorv(v[4], v[9]);
  v[5] = rot12(v[5]);
  v[6] = rot12(v[6]);
  v[7] = rot12(v[7]);
  v[4] = rot12(v[4]);
  v[0] = addv(v[0], m[(size_t)MSG_SCHEDULE[r][9]]);
  v[1] = addv(v[1], m[(size_t)MSG_SCHEDULE[r][11]]);
  v[2] = addv(v[2], m[(size_t)MSG_SCHEDULE[r][13]]);
  v[3] = addv(v[3], m[(size_t)MSG_SCHEDULE[r][15]]);
  v[0] = addv(v[0], v[5]);
  v[1] = addv(v[1], v[6]);
  v[2] = addv(v[2], v[7]);
  v[3] = addv(v[3], v[4]);
  v[15] = xorv(v[15], v[0]);
  v[12] = xorv(v[12], v[1]);
  v[13] = xorv(v[13], v[2]);
  v[14] = xorv(v[14], v[3]);
  v[15] = rot8(v[15]);
  v[12] = rot8(v[12]);
  v[13] = rot8(v[13]);
  v[14] = rot8(v[14]);
  v[10] = addv(v[10], v[15]);
  v[11] = addv(v[11], v[12]);
  v[8] = addv(v[8], v[13]);
  v[9] = addv(v[9], v[14]);
  v[5] = xorv(v[5], v[10]);
  v[6] = xorv(v[6], v[11]);
  v[7] = xorv(v[7], v[8]);
  v[4] = xorv(v[4], v[9]);
  v[5] = rot7(v[5]);
  v[6] = rot7(v[6]);
  v[7] = rot7(v[7]);
  v[4] = rot7(v[4]);
}

INLINE void transpose_vecs(__m128i vecs[DEGREE]) {
  // Interleave 32-bit lanes.
  __m128i ab_01 = _mm_unpacklo_epi32(vecs[0], vecs[1]);
  __m128i ab_23 = _mm_unpackhi_epi32(vecs[0], vecs[1]);
  __m128i cd_01 = _mm_unpacklo_epi32(vecs[2], vecs[3]);
  __m128i cd_23 = _mm_unpackhi_epi32(vecs[2], vecs[3]);

  // Interleave 64-bit lanes.
  __m128i abcd_0 = _mm_unpacklo_epi64(ab_01, cd_01);
  __m128i abcd_1 = _mm_unpackhi_epi64(ab_01, cd_01);
  __m128i abcd_2 = _mm_unpacklo_epi64(ab_23, cd_23);
  __m128i abcd_3 = _mm_unpackhi_epi64(ab_23, cd_23);

  vecs[0] = abcd_0;
  vecs[1] = abcd_1;
  vecs[2] = abcd_2;
  vecs[3] = abcd_3;
}

INLINE void untranspose_vecs(__m128i vecs[DEGREE]) {
  // This is the inverse of transpose_vecs.
  __m128i abcd_0 = vecs[0];
  __m128i abcd_1 = vecs[1];
  __m128i abcd_2 = vecs[2];
  __m128i abcd_3 = vecs[3];

  __m128i ab_01 = _mm_unpacklo_epi64(abcd_0, abcd_1);
  __m128i cd_01 = _mm_unpackhi_epi64(abcd_0, abcd_1);
  __m128i ab_23 = _mm_unpacklo_epi64(abcd_2, abcd_3);
  __m128i cd_23 = _mm_unpackhi_epi64(abcd_2, abcd_3);

  vecs[0] = _mm_unpacklo_epi32(ab_01, cd_01);
  vecs[1] = _mm_unpackhi_epi32(ab_01, cd_01);
  vecs[2] = _mm_unpacklo_epi32(ab_23, cd_23);
  vecs[3] = _mm_unpackhi_epi32(ab_23, cd_23);
}

INLINE void transpose_msg_vecs(const uint8_t *const *inputs,
                               size_t block_offset, __m128i out[16]) {
  out[0] = loadu(&inputs[0][block_offset + 0 * sizeof(__m128i)]);
  out[1] = loadu(&inputs[1][block_offset + 0 * sizeof(__m128i)]);
  out[2] = loadu(&inputs[2][block_offset + 0 * sizeof(__m128i)]);
  out[3] = loadu(&inputs[3][block_offset + 0 * sizeof(__m128i)]);
  out[4] = loadu(&inputs[0][block_offset + 1 * sizeof(__m128i)]);
  out[5] = loadu(&inputs[1][block_offset + 1 * sizeof(__m128i)]);
  out[6] = loadu(&inputs[2][block_offset + 1 * sizeof(__m128i)]);
  out[7] = loadu(&inputs[3][block_offset + 1 * sizeof(__m128i)]);
  out[8] = loadu(&inputs[0][block_offset + 2 * sizeof(__m128i)]);
  out[9] = loadu(&inputs[1][block_offset + 2 * sizeof(__m128i)]);
  out[10] = loadu(&inputs[2][block_offset + 2 * sizeof(__m128i)]);
  out[11] = loadu(&inputs[3][block_offset + 2 * sizeof(__m128i)]);
  out[12] = loadu(&inputs[0][block_offset + 3 * sizeof(__m128i)]);
  out[13] = loadu(&inputs[1][block_offset + 3 * sizeof(__m128i)]);
  out[14] = loadu(&inputs[2][block_offset + 3 * sizeof(__m128i)]);
  out[15] = loadu(&inputs[3][block_offset + 3 * sizeof(__m128i)]);
  for (size_t i = 0; i < 4; ++i) {
    _mm_prefetch((const void *)&inputs[i][block_offset + 256], _MM_HINT_T0);
  }
  transpose_vecs(&out[0]);
  transpose_vecs(&out[4]);
  transpose_vecs(&out[8]);
  transpose_vecs(&out[12]);
}

INLINE void load_counters(uint64_t counter, bool increment_counter,
                          __m128i *out_lo, __m128i *out_hi) {
  const __m128i mask = _mm_set1_epi32(-(int32_t)increment_counter);
  const __m128i add0 = _mm_set_epi32(3, 2, 1, 0);
  const __m128i add1 = _mm_and_si128(mask, add0);
  __m128i l = _mm_add_epi32(_mm_set1_epi32((int32_t)counter), add1);
  __m128i carry = _mm_cmpgt_epi32(_mm_xor_si128(add1, _mm_set1_epi32(0x80000000)), 
                                  _mm_xor_si128(   l, _mm_set1_epi32(0x80000000)));
  __m128i h = _mm_sub_epi32(_mm_set1_epi32((int32_t)(counter >> 32)), carry);
  *out_lo = l;
  *out_hi = h;
}

static void blake3_hash4_sse41(const uint8_t *const *inputs, size_t blocks,
                               const uint32_t key[8], uint64_t counter,
                               bool increment_counter, uint8_t flags,
                               uint8_t flags_start, uint8_t flags_end,
                               uint8_t *out) {
  __m128i h_vecs[8] = {
      set1(key[0]), set1(key[1]), set1(key[2]), set1(key[3]),
      set1(key[4]), set1(key[5]), set1(key[6]), set1(key[7]),
  };
  __m128i counter_low_vec, counter_high_vec;
  load_counters(counter, increment_counter, &counter_low_vec,
                &counter_high_vec);
  uint8_t block_flags = flags | flags_start;

  for (size_t block = 0; block < blocks; block++) {
    if (block + 1 == blocks) {
      block_flags |= flags_end;
    }
    __m128i block_len_vec = set1(BLAKE3_BLOCK_LEN);
    __m128i block_flags_vec = set1(block_flags);
    __m128i msg_vecs[16];
    transpose_msg_vecs(inputs, block * BLAKE3_BLOCK_LEN, msg_vecs);

    __m128i v[16] = {
        h_vecs[0],       h_vecs[1],        h_vecs[2],     h_vecs[3],
        h_vecs[4],       h_vecs[5],        h_vecs[6],     h_vecs[7],
        set1(IV[0]),     set1(IV[1]),      set1(IV[2]),   set1(IV[3]),
        counter_low_vec, counter_high_vec, block_len_vec, block_flags_vec,
    };
    round_fn_vec(v, msg_vecs, 0);
    round_fn_vec(v, msg_vecs, 1);
    round_fn_vec(v, msg_vecs, 2);
    round_fn_vec(v, msg_vecs, 3);
    round_fn_vec(v, msg_vecs, 4);
    round_fn_vec(v, msg_vecs, 5);
    round_fn_vec(v, msg_vecs, 6);
    h_vecs[0] = xorv(v[0], v[8]);
    h_vecs[1] = xorv(v[1], v[9]);
    h_vecs[2] = xorv(v[2], v[10]);
    h_vecs[3] = xorv(v[3], v[11]);
    h_vecs[4] = xorv(v[4], v[12]);
    h_vecs[5] = xorv(v[5], v[13]);
    h_vecs[6] = xorv(v[6], v[14]);
    h_vecs[7] = xorv(v[7], v[15]);

    block_flags = flags;
  }

  untranspose_vecs(&h_vecs[0]);
  untranspose_vecs(&h_vecs[4]);
  // The first four vecs now contain the first half of each output, and the
  // second four vecs contain the second half of each output.
  storeu(h_vecs[0], &out[0 * sizeof(__m128i)]);
  storeu(h_vecs[4], &out[1 * sizeof(__m128i)]);
  storeu(h_vecs[1], &out[2 * sizeof(__m128i)]);
  storeu(h_vecs[5], &out[3 * sizeof(__m128i)]);
  storeu(h_vecs[2], &out[4 * sizeof(__m128i)]);
  storeu(h_vecs[6], &out[5 * sizeof(__m128i)]);
  storeu(h_vecs[3], &out[6 * sizeof(__m128i)]);
  storeu(h_vecs[7], &out[7 * sizeof(__m128i)]);
}

INLINE void hash_one_sse41(const uint8_t *input, size_t blocks,
                           const uint32_t key[8], uint64_t counter,
                           uint8_t flags, uint8_t flags_start,
                           uint8_t flags_end, uint8_t out[BLAKE3_OUT_LEN]) {
  uint32_t cv[8];
  memcpy(cv, key, BLAKE3_KEY_LEN);
  uint8_t block_flags = flags | flags_start;
  while (blocks > 0) {
    if (blocks == 1) {
      block_flags |= flags_end;
    }
    blake3_compress_in_place_sse41(cv, input, BLAKE3_BLOCK_LEN, counter,
                                   block_flags);
    input = &input[BLAKE3_BLOCK_LEN];
    blocks -= 1;
    counter += 1;
    block_flags = flags;
  }
  memcpy(out, cv, BLAKE3_OUT_LEN);
}

void blake3_hash_many_sse41(const uint8_t *const *inputs, size_t num_inputs,
                            size_t blocks, const uint32_t key[8],
                            uint64_t counter, bool increment_counter,
                            uint8_t flags, uint8_t flags_start,
                            uint8_t flags_end, uint8_t *out) {
  while (num_inputs >= DEGREE) {
    blake3_hash4_sse41(inputs, blocks, key, counter, increment_counter, flags,
                       flags_start, flags_end, out);
    if (increment_counter) {
      counter += DEGREE;
    }
    inputs += DEGREE;
    num_inputs -= DEGREE;
    out = &out[DEGREE * BLAKE3_OUT_LEN];
  }
  while (num_inputs > 0) {
    hash_one_sse41(inputs[0], blocks, key, counter, flags, flags_start,
                   flags_end, out);
    if (increment_counter) {
      counter += 1;
    }
    inputs += 1;
    num_inputs -= 1;
    out = &out[BLAKE3_OUT_LEN];
  }
}

#endif
