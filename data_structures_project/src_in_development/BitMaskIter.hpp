//Copyright Nathan Ward 2019..
//Distributed under the Boost Software License, Version 1.0.
//(See http://www.boost.org/LICENSE_1_0.txt)

/* This iterator class is inspired from google_flash_hash_map
 * see: github.com/abseil/abseil-cpp/absl/container
 */

#ifndef BIT_MASK_ITER_HPP
#define BIT_MASK_ITER_HPP

#include <cstdint>
#include <utility>

#include <bitset>
#include <iostream>

/* Due to intel's ISA with sse2/3 and avx2/512 
 * little endian-ness, the bits in this class
 * read right to left.
 * E.g. index 0 is the least significant bit 
 * and index 63 is the most significant bit.
 */
class BitMaskIter64 
{
public:
  explicit constexpr BitMaskIter64(uint32_t a, uint32_t b, uint32_t c, uint32_t d) noexcept
    :bits_{
      (static_cast<uint64_t>(((d << 16) | c)) << 32) | (static_cast<uint64_t>((b << 16) | a))
    }
  {
  }

  explicit constexpr BitMaskIter64(uint32_t a, uint32_t b) noexcept
    :bits_{(static_cast<uint64_t>(b) << 32) | a}
  {
  }
  
  explicit constexpr BitMaskIter64(uint64_t bits) noexcept
    :bits_{bits}
  {
  }

  explicit constexpr BitMaskIter64() noexcept
    :bits_{0}
  {
  }

  constexpr BitMaskIter64& operator++() noexcept 
  {
    bits_ &= (bits_ - 1);
    return *this;
  }

  [[nodiscard]] constexpr explicit operator bool() const noexcept 
  {
    return bits_;
  }

  [[nodiscard]] constexpr auto countTrailingZeros() const noexcept 
  {
    auto bits = bits_;
    auto count = 64;
    bits &= ~bits + 1;  //bit binary search
    if (bits)
      --count;
    if (bits & 0x00000000FFFFFFFF)
      count -= 32;
    if (bits & 0x0000FFFF0000FFFF)
      count -= 16;
    if (bits & 0x00FF00FF00FF00FF)
      count -= 8;
    if (bits & 0x0F0F0F0F0F0F0F0F)
      count -= 4;
    if (bits & 0x3333333333333333)
      count -= 2;
    if (bits & 0x5555555555555555)
      count -= 1;
    return count;
  }


  [[nodiscard]] constexpr auto operator*() const noexcept 
  {
    if (!bits_)
      return -1;
    return countTrailingZeros();
  }

  constexpr friend bool operator==(const BitMaskIter64& lhs, const BitMaskIter64& rhs) noexcept 
  {
    return lhs.bits_ == rhs.bits_;
  }

  constexpr friend bool operator!=(const BitMaskIter64& lhs, const BitMaskIter64& rhs) noexcept 
  {
    return lhs.bits_ != rhs.bits_;
  }

  [[nodiscard]] constexpr BitMaskIter64 begin() const noexcept 
  {
    return *this; 
  }

  [[nodiscard]] constexpr BitMaskIter64 end() const noexcept 
  {
    return BitMaskIter64{}; 
  }

  [[nodiscard]]constexpr bool getFirstBit() const noexcept 
  {
    return ((bits_ >> 63) & 1);  
  }

  [[nodiscard]] constexpr bool getLastBit() const noexcept 
  {
    return (bits_ & 1);
  }
  
  [[nodiscard]] constexpr auto getFirstUnsetBit() const noexcept 
  {
    auto bits = bits_;
    if (bits == 0xFFFFFFFFFFFFFFFF)
      return -1;

    auto count = 63;
    while (bits & 1) {
      bits >>= 1;
      --count;
    }
    return count;
  }

public:
  uint64_t bits_{0};
};

#endif
