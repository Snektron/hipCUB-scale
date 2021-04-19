/******************************************************************************
 * Copyright (c) 2011-2020, NVIDIA CORPORATION.  All rights reserved.
 * Modifications Copyright (c) 2021, Advanced Micro Devices, Inc.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of the NVIDIA CORPORATION nor the
 *       names of its contributors may be used to endorse or promote products
 *       derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL NVIDIA CORPORATION BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 ******************************************************************************/

/**
 * \file
 * radix_rank_sort_operations.cuh contains common abstractions, definitions and
 * operations used for radix sorting and ranking.
 */

 #ifndef HIPCUB_ROCPRIM_BLOCK_RADIX_RANK_SORT_OPERATIONS_HPP_
 #define HIPCUB_ROCPRIM_BLOCK_RADIX_RANK_SORT_OPERATIONS_HPP_

#include "../../../config.hpp"

 #include <rocprim/config.hpp>
 #include <rocprim/type_traits.hpp>
 #include <rocprim/detail/various.hpp>

BEGIN_HIPCUB_NAMESPACE

/** \brief Twiddling keys for radix sort. */
template <bool IS_DESCENDING, typename KeyT>
struct RadixSortTwiddle
{
    typedef Traits<KeyT> TraitsT;
    typedef typename TraitsT::UnsignedBits UnsignedBits;
    static HIPCUB_HOST_DEVICE __forceinline__ UnsignedBits In(UnsignedBits key)
    {
        key = TraitsT::TwiddleIn(key);
        if (IS_DESCENDING) key = ~key;
        return key;
    }
    static HIPCUB_HOST_DEVICE __forceinline__ UnsignedBits Out(UnsignedBits key)
    {
        if (IS_DESCENDING) key = ~key;
        key = TraitsT::TwiddleOut(key);
        return key;
    }
    static HIPCUB_HOST_DEVICE __forceinline__ UnsignedBits DefaultKey()
    {
        return Out(~UnsignedBits(0));
    }
};

// /** \brief Twiddling keys for radix sort. */
// template <bool IS_DESCENDING, typename KeyT>
// struct RadixSortTwiddle
// {
//     typedef Traits<KeyT> TraitsT;
//     typedef typename TraitsT::UnsignedBits UnsignedBits;
//     static __host__ HIPCUB_DEVICE inline
//     UnsignedBits In(UnsignedBits key)
//     {
//         key = TraitsT::TwiddleIn(key);
//         if (IS_DESCENDING) key = ~key;
//         return key;
//     }
//     static __host__ HIPCUB_DEVICE inline
//     UnsignedBits Out(UnsignedBits key)
//     {
//         if (IS_DESCENDING) key = ~key;
//         key = TraitsT::TwiddleOut(key);
//         return key;
//     }
//     static __host__ HIPCUB_DEVICE inline
//     UnsignedBits DefaultKey()
//     {
//         return Out(~UnsignedBits(0));
//     }
// };

/** \brief Base struct for digit extractor. Contains common code to provide
    special handling for floating-point -0.0.

    \note This handles correctly both the case when the keys are
    bitwise-complemented after twiddling for descending sort (in onesweep) as
    well as when the keys are not bit-negated, but the implementation handles
    descending sort separately (in other implementations in CUB). Twiddling
    alone maps -0.0f to 0x7fffffff and +0.0f to 0x80000000 for float, which are
    subsequent bit patterns and bitwise complements of each other. For onesweep,
    both -0.0f and +0.0f are mapped to the bit pattern of +0.0f (0x80000000) for
    ascending sort, and to the pattern of -0.0f (0x7fffffff) for descending
    sort. For all other sorting implementations in CUB, both are always mapped
    to +0.0f. Since bit patterns for both -0.0f and +0.0f are next to each other
    and only one of them is used, the sorting works correctly. For double, the
    same applies, but with 64-bit patterns.
*/
template <typename KeyT>
struct BaseDigitExtractor
{
    typedef typename get_unsigned_bits_type<KeyT>::unsigned_type UnsignedBits;

    static constexpr bool FLOAT_KEY = ::rocprim::is_floating_point<KeyT>::value;


    static HIPCUB_DEVICE inline
    UnsignedBits ProcessFloatMinusZero(UnsignedBits key)
    {
        if (!FLOAT_KEY) return key;

        UnsignedBits TWIDDLED_MINUS_ZERO_BITS =
            TwiddleIn<KeyT>(UnsignedBits(1) << UnsignedBits(8 * sizeof(UnsignedBits) - 1));
        UnsignedBits TWIDDLED_ZERO_BITS = TwiddleIn<KeyT>(0);
        return key == TWIDDLED_MINUS_ZERO_BITS ? TWIDDLED_ZERO_BITS : key;
    }
};

// /** \brief A wrapper type to extract digits. Uses the BFE intrinsic to extract a
//  * key from a digit. */
// template <typename KeyT>
// struct BFEDigitExtractor : BaseDigitExtractor<KeyT>
// {
//     using typename BaseDigitExtractor<KeyT>::UnsignedBits;
//
//     uint32_t bit_start, num_bits;
//     explicit HIPCUB_DEVICE inline
//     BFEDigitExtractor(
//         uint32_t bit_start = 0, uint32_t num_bits = 0)
//         : bit_start(bit_start), num_bits(num_bits)
//     { }
//
//     HIPCUB_DEVICE inline
//     uint32_t Digit(UnsignedBits key)
//     {
//         return BFE(ProcessFloatMinusZero(key), bit_start, num_bits);
//     }
// };

/** \brief A wrapper type to extract digits. Uses a combination of shift and
 * bitwise and to extract digits. */
template <typename KeyT>
struct ShiftDigitExtractor : BaseDigitExtractor<KeyT>
{
    using typename BaseDigitExtractor<KeyT>::UnsignedBits;

    uint32_t bit_start, mask;
    explicit HIPCUB_DEVICE inline
    ShiftDigitExtractor(
        uint32_t bit_start = 0, uint32_t num_bits = 0)
        : bit_start(bit_start), mask((1 << num_bits) - 1)
    { }

    HIPCUB_DEVICE inline
    uint32_t digit(UnsignedBits key)
    {
        return uint32_t(this->ProcessFloatMinusZero(key) >> UnsignedBits(bit_start)) & mask;
    }
};

END_HIPCUB_NAMESPACE

#endif //HIPCUB_ROCPRIM_BLOCK_RADIX_RANK_SORT_OPERATIONS_HPP_
