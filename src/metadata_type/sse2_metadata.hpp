#ifndef SSE2_METADATA_H
#define SSE2_METADATA_H

#include <immintrin.h>
#include <cstdint>
#include "../BitMaskIter.hpp"
#include "../metadata.hpp"

struct sse2_metadata
{
  using bit_mask = BitMaskIter64;

  explicit sse2_metadata(metadata* md) noexcept
  {
    group1_ = _mm_loadu_si128(reinterpret_cast<const __m128i*>(md));
    group2_ = _mm_loadu_si128(reinterpret_cast<const __m128i*>(md + 16));
    group3_ = _mm_loadu_si128(reinterpret_cast<const __m128i*>(md + 32));
    group4_ = _mm_loadu_si128(reinterpret_cast<const __m128i*>(md + 48));
  }

  [[nodiscard]] bit_mask Match(const metadata md) const noexcept
  {
    auto mask = _mm_set1_epi8(md);
    return bit_mask {
            static_cast<uint32_t>(_mm_movemask_epi8(_mm_cmpeq_epi8(mask, group1_))),
            static_cast<uint32_t>(_mm_movemask_epi8(_mm_cmpeq_epi8(mask, group2_))),
            static_cast<uint32_t>(_mm_movemask_epi8(_mm_cmpeq_epi8(mask, group3_))),
            static_cast<uint32_t>(_mm_movemask_epi8(_mm_cmpeq_epi8(mask, group4_)))
          };
  }
  
  [[nodiscard]] constexpr int getFirstOpenBucket() const noexcept 
  {
    return getFirstBits().getFirstUnsetBit();
  }

  [[nodiscard]] bit_mask getFirstBits() const noexcept 
  {
    return bit_mask {
            static_cast<uint32_t>(_mm_movemask_epi8(group1_)),
            static_cast<uint32_t>(_mm_movemask_epi8(group2_)),
            static_cast<uint32_t>(_mm_movemask_epi8(group3_)),
            static_cast<uint32_t>(_mm_movemask_epi8(group4_))
          };
  }
  
  __m128i group1_;
  __m128i group2_;
  __m128i group3_;
  __m128i group4_;
};

#endif
