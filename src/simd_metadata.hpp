#ifndef SIMD_METADATA_H
#define SIMD_METADATA_H

#include "metadata.hpp"

#if defined (__AVX512__)
  #include "metadata_type/avx512_metadata.hpp"
  using simd_type = avx512_metadata; 
#elif defined (__AVX2__) 
  #include "metadata_type/avx2_metadata.hpp"
  using simd_type = avx2_metadata; 
#elif defined (__SSE2__) || defined (__SSE3__)
  #include "metadata_type/sse2_metadata.hpp"
  using simd_type = sse2_metadata;
#else
  static_assert(false, "Must utilize AVX512, AVX2, or SSE2/SSE3 Intel ISA's");
#endif

struct simd_metadata : public simd_type
{
  explicit simd_metadata(metadata* md)
    :simd_type(md)
  {
  }

  static constexpr int size = 64;
};

#endif //SIMD_METADATA_H
