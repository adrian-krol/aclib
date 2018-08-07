#pragma once

////////////////////////////////////////////////////////////////////////////////
// The MIT License (MIT)
//
// Copyright (c) 2017 Nicholas Frechette & Animation Compression Library contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.
////////////////////////////////////////////////////////////////////////////////

#include "acl/core/error.h"
#include "acl/core/memory_utils.h"
#include "acl/core/track_types.h"
#include "acl/math/vector4_32.h"
#include "acl/math/scalar_packing.h"

#include <cstdint>

namespace acl
{
	inline void pack_vector4_128(const Vector4_32& vector, uint8_t* out_vector_data)
	{
		vector_unaligned_write(vector, out_vector_data);
	}

	inline Vector4_32 unpack_vector4_128(const uint8_t* vector_data)
	{
		return vector_unaligned_load_32(vector_data);
	}

	inline void pack_vector4_64(const Vector4_32& vector, bool is_unsigned, uint8_t* out_vector_data)
	{
		uint32_t vector_x = is_unsigned ? pack_scalar_unsigned(vector_get_x(vector), 16) : pack_scalar_signed(vector_get_x(vector), 16);
		uint32_t vector_y = is_unsigned ? pack_scalar_unsigned(vector_get_y(vector), 16) : pack_scalar_signed(vector_get_y(vector), 16);
		uint32_t vector_z = is_unsigned ? pack_scalar_unsigned(vector_get_z(vector), 16) : pack_scalar_signed(vector_get_z(vector), 16);
		uint32_t vector_w = is_unsigned ? pack_scalar_unsigned(vector_get_w(vector), 16) : pack_scalar_signed(vector_get_w(vector), 16);

		uint16_t* data = safe_ptr_cast<uint16_t>(out_vector_data);
		data[0] = safe_static_cast<uint16_t>(vector_x);
		data[1] = safe_static_cast<uint16_t>(vector_y);
		data[2] = safe_static_cast<uint16_t>(vector_z);
		data[3] = safe_static_cast<uint16_t>(vector_w);
	}

	inline Vector4_32 unpack_vector4_64(const uint8_t* vector_data, bool is_unsigned)
	{
		const uint16_t* data_ptr_u16 = safe_ptr_cast<const uint16_t>(vector_data);
		uint16_t x16 = data_ptr_u16[0];
		uint16_t y16 = data_ptr_u16[1];
		uint16_t z16 = data_ptr_u16[2];
		uint16_t w16 = data_ptr_u16[3];
		float x = is_unsigned ? unpack_scalar_unsigned(x16, 16) : unpack_scalar_signed(x16, 16);
		float y = is_unsigned ? unpack_scalar_unsigned(y16, 16) : unpack_scalar_signed(y16, 16);
		float z = is_unsigned ? unpack_scalar_unsigned(z16, 16) : unpack_scalar_signed(z16, 16);
		float w = is_unsigned ? unpack_scalar_unsigned(w16, 16) : unpack_scalar_signed(w16, 16);
		return vector_set(x, y, z, w);
	}

	inline void pack_vector4_32(const Vector4_32& vector, bool is_unsigned, uint8_t* out_vector_data)
	{
		uint32_t vector_x = is_unsigned ? pack_scalar_unsigned(vector_get_x(vector), 8) : pack_scalar_signed(vector_get_x(vector), 8);
		uint32_t vector_y = is_unsigned ? pack_scalar_unsigned(vector_get_y(vector), 8) : pack_scalar_signed(vector_get_y(vector), 8);
		uint32_t vector_z = is_unsigned ? pack_scalar_unsigned(vector_get_z(vector), 8) : pack_scalar_signed(vector_get_z(vector), 8);
		uint32_t vector_w = is_unsigned ? pack_scalar_unsigned(vector_get_w(vector), 8) : pack_scalar_signed(vector_get_w(vector), 8);

		out_vector_data[0] = safe_static_cast<uint8_t>(vector_x);
		out_vector_data[1] = safe_static_cast<uint8_t>(vector_y);
		out_vector_data[2] = safe_static_cast<uint8_t>(vector_z);
		out_vector_data[3] = safe_static_cast<uint8_t>(vector_w);
	}

	inline Vector4_32 unpack_vector4_32(const uint8_t* vector_data, bool is_unsigned)
	{
		uint8_t x8 = vector_data[0];
		uint8_t y8 = vector_data[1];
		uint8_t z8 = vector_data[2];
		uint8_t w8 = vector_data[3];
		float x = is_unsigned ? unpack_scalar_unsigned(x8, 8) : unpack_scalar_signed(x8, 8);
		float y = is_unsigned ? unpack_scalar_unsigned(y8, 8) : unpack_scalar_signed(y8, 8);
		float z = is_unsigned ? unpack_scalar_unsigned(z8, 8) : unpack_scalar_signed(z8, 8);
		float w = is_unsigned ? unpack_scalar_unsigned(w8, 8) : unpack_scalar_signed(w8, 8);
		return vector_set(x, y, z, w);
	}

	inline void pack_vector3_96(const Vector4_32& vector, uint8_t* out_vector_data)
	{
		vector_unaligned_write3(vector, out_vector_data);
	}

	// Assumes the 'vector_data' is padded in order to load up to 16 bytes from it
	inline Vector4_32 unpack_vector3_96_unsafe(const uint8_t* vector_data)
	{
		return vector_unaligned_load_32(vector_data);
	}

	// Assumes the 'vector_data' is in big-endian order
	inline Vector4_32 unpack_vector3_96(const uint8_t* vector_data, uint64_t bit_offset)
	{
		uint64_t byte_offset = bit_offset / 8;
		uint64_t vector_u64 = unaligned_load<uint64_t>(vector_data + byte_offset);
		vector_u64 = byte_swap(vector_u64);
		vector_u64 <<= bit_offset % 8;
		vector_u64 >>= 64 - 32;

		const uint64_t x64 = vector_u64;

		bit_offset += 32;
		byte_offset = bit_offset / 8;
		vector_u64 = unaligned_load<uint64_t>(vector_data + byte_offset);
		vector_u64 = byte_swap(vector_u64);
		vector_u64 <<= bit_offset % 8;
		vector_u64 >>= 64 - 32;

		const uint64_t y64 = vector_u64;

		bit_offset += 32;
		byte_offset = bit_offset / 8;
		vector_u64 = unaligned_load<uint64_t>(vector_data + byte_offset);
		vector_u64 = byte_swap(vector_u64);
		vector_u64 <<= bit_offset % 8;
		vector_u64 >>= 64 - 32;

		const uint64_t z64 = vector_u64;

		const float x = aligned_load<float>(&x64);
		const float y = aligned_load<float>(&y64);
		const float z = aligned_load<float>(&z64);

		return vector_set(x, y, z);
	}

	inline void pack_vector3_u48(const Vector4_32& vector, uint8_t* out_vector_data)
	{
		uint32_t vector_x = pack_scalar_unsigned(vector_get_x(vector), 16);
		uint32_t vector_y = pack_scalar_unsigned(vector_get_y(vector), 16);
		uint32_t vector_z = pack_scalar_unsigned(vector_get_z(vector), 16);

		uint16_t* data = safe_ptr_cast<uint16_t>(out_vector_data);
		data[0] = safe_static_cast<uint16_t>(vector_x);
		data[1] = safe_static_cast<uint16_t>(vector_y);
		data[2] = safe_static_cast<uint16_t>(vector_z);
	}

	inline void pack_vector3_s48(const Vector4_32& vector, uint8_t* out_vector_data)
	{
		uint32_t vector_x = pack_scalar_signed(vector_get_x(vector), 16);
		uint32_t vector_y = pack_scalar_signed(vector_get_y(vector), 16);
		uint32_t vector_z = pack_scalar_signed(vector_get_z(vector), 16);

		uint16_t* data = safe_ptr_cast<uint16_t>(out_vector_data);
		data[0] = safe_static_cast<uint16_t>(vector_x);
		data[1] = safe_static_cast<uint16_t>(vector_y);
		data[2] = safe_static_cast<uint16_t>(vector_z);
	}

	// Assumes the 'vector_data' is padded in order to load up to 16 bytes from it
	inline Vector4_32 unpack_vector3_u48_unsafe(const uint8_t* vector_data)
	{
#if defined(ACL_SSE2_INTRINSICS)
		__m128i zero = _mm_setzero_si128();
		__m128i x16y16z16 = _mm_loadu_si128((const __m128i*)vector_data);
		__m128i x32y32z32 = _mm_unpacklo_epi16(x16y16z16, zero);
		__m128 value = _mm_cvtepi32_ps(x32y32z32);
		return _mm_mul_ps(value, _mm_set_ps1(1.0f / 65535.0f));
#elif defined(ACL_NEON_INTRINSICS)
		uint8x8_t x8y8z8 = vld1_u8(vector_data);
		uint16x4_t x16y16z16 = vreinterpret_u16_u8(x8y8z8);
		uint32x4_t x32y32z32 = vmovl_u16(x16y16z16);

		float32x4_t value = vcvtq_f32_u32(x32y32z32);
		return vmulq_n_f32(value, 1.0f / 65535.0f);
#else
		const uint16_t* data_ptr_u16 = safe_ptr_cast<const uint16_t>(vector_data);
		uint16_t x16 = data_ptr_u16[0];
		uint16_t y16 = data_ptr_u16[1];
		uint16_t z16 = data_ptr_u16[2];
		float x = unpack_scalar_unsigned(x16, 16);
		float y = unpack_scalar_unsigned(y16, 16);
		float z = unpack_scalar_unsigned(z16, 16);
		return vector_set(x, y, z);
#endif
	}

	inline Vector4_32 unpack_vector3_s48(const uint8_t* vector_data)
	{
		const uint16_t* data_ptr_u16 = safe_ptr_cast<const uint16_t>(vector_data);
		uint16_t x16 = data_ptr_u16[0];
		uint16_t y16 = data_ptr_u16[1];
		uint16_t z16 = data_ptr_u16[2];
		float x = unpack_scalar_signed(x16, 16);
		float y = unpack_scalar_signed(y16, 16);
		float z = unpack_scalar_signed(z16, 16);
		return vector_set(x, y, z);
	}

	inline void pack_vector3_32(const Vector4_32& vector, uint8_t XBits, uint8_t YBits, uint8_t ZBits, bool is_unsigned, uint8_t* out_vector_data)
	{
		ACL_ASSERT(XBits + YBits + ZBits == 32, "Sum of XYZ bits does not equal 32!");

		uint32_t vector_x = is_unsigned ? pack_scalar_unsigned(vector_get_x(vector), XBits) : pack_scalar_signed(vector_get_x(vector), XBits);
		uint32_t vector_y = is_unsigned ? pack_scalar_unsigned(vector_get_y(vector), YBits) : pack_scalar_signed(vector_get_y(vector), YBits);
		uint32_t vector_z = is_unsigned ? pack_scalar_unsigned(vector_get_z(vector), ZBits) : pack_scalar_signed(vector_get_z(vector), ZBits);

		uint32_t vector_u32 = (vector_x << (YBits + ZBits)) | (vector_y << ZBits) | vector_z;

		// Written 2 bytes at a time to ensure safe alignment
		uint16_t* data = safe_ptr_cast<uint16_t>(out_vector_data);
		data[0] = safe_static_cast<uint16_t>(vector_u32 >> 16);
		data[1] = safe_static_cast<uint16_t>(vector_u32 & 0xFFFF);
	}

	inline Vector4_32 unpack_vector3_32(uint8_t XBits, uint8_t YBits, uint8_t ZBits, bool is_unsigned, const uint8_t* vector_data)
	{
		ACL_ASSERT(XBits + YBits + ZBits == 32, "Sum of XYZ bits does not equal 32!");

		// Read 2 bytes at a time to ensure safe alignment
		const uint16_t* data_ptr_u16 = safe_ptr_cast<const uint16_t>(vector_data);
		uint32_t vector_u32 = (safe_static_cast<uint32_t>(data_ptr_u16[0]) << 16) | safe_static_cast<uint32_t>(data_ptr_u16[1]);
		uint32_t x32 = vector_u32 >> (YBits + ZBits);
		uint32_t y32 = (vector_u32 >> ZBits) & ((1 << YBits) - 1);
		uint32_t z32 = vector_u32 & ((1 << ZBits) - 1);
		float x = is_unsigned ? unpack_scalar_unsigned(x32, XBits) : unpack_scalar_signed(x32, XBits);
		float y = is_unsigned ? unpack_scalar_unsigned(y32, YBits) : unpack_scalar_signed(y32, YBits);
		float z = is_unsigned ? unpack_scalar_unsigned(z32, ZBits) : unpack_scalar_signed(z32, ZBits);
		return vector_set(x, y, z);
	}

	inline void pack_vector3_u24(const Vector4_32& vector, uint8_t* out_vector_data)
	{
		uint32_t vector_x = pack_scalar_unsigned(vector_get_x(vector), 8);
		uint32_t vector_y = pack_scalar_unsigned(vector_get_y(vector), 8);
		uint32_t vector_z = pack_scalar_unsigned(vector_get_z(vector), 8);

		out_vector_data[0] = safe_static_cast<uint8_t>(vector_x);
		out_vector_data[1] = safe_static_cast<uint8_t>(vector_y);
		out_vector_data[2] = safe_static_cast<uint8_t>(vector_z);
	}

	inline void pack_vector3_s24(const Vector4_32& vector, uint8_t* out_vector_data)
	{
		uint32_t vector_x = pack_scalar_signed(vector_get_x(vector), 8);
		uint32_t vector_y = pack_scalar_signed(vector_get_y(vector), 8);
		uint32_t vector_z = pack_scalar_signed(vector_get_z(vector), 8);

		out_vector_data[0] = safe_static_cast<uint8_t>(vector_x);
		out_vector_data[1] = safe_static_cast<uint8_t>(vector_y);
		out_vector_data[2] = safe_static_cast<uint8_t>(vector_z);
	}

	// Assumes the 'vector_data' is padded in order to load up to 16 bytes from it
	inline Vector4_32 unpack_vector3_u24_unsafe(const uint8_t* vector_data)
	{
#if defined(ACL_SSE2_INTRINSICS) && 0
		// This implementation leverages fast fixed point coercion, it relies on the
		// input being positive and normalized as well as fixed point (division by 256, not 255)
		// TODO: Enable this, it's a bit faster but requires compensating with the clip range to avoid losing precision
		__m128i zero = _mm_setzero_si128();
		__m128i exponent = _mm_set1_epi32(0x3f800000);

		__m128i x8y8z8 = _mm_loadu_si128((const __m128i*)vector_data);
		__m128i x16y16z16 = _mm_unpacklo_epi8(x8y8z8, zero);
		__m128i x32y32z32 = _mm_unpacklo_epi16(x16y16z16, zero);
		__m128i segment_extent_i32 = _mm_or_si128(_mm_slli_epi32(x32y32z32, 23 - 8), exponent);
		return _mm_sub_ps(_mm_castsi128_ps(segment_extent_i32), _mm_castsi128_ps(exponent));
#elif defined(ACL_SSE2_INTRINSICS)
		__m128i zero = _mm_setzero_si128();
		__m128i x8y8z8 = _mm_loadu_si128((const __m128i*)vector_data);
		__m128i x16y16z16 = _mm_unpacklo_epi8(x8y8z8, zero);
		__m128i x32y32z32 = _mm_unpacklo_epi16(x16y16z16, zero);
		__m128 value = _mm_cvtepi32_ps(x32y32z32);
		return _mm_mul_ps(value, _mm_set_ps1(1.0f / 255.0f));
#elif defined(ACL_NEON_INTRINSICS)
		uint8x8_t x8y8z8 = vld1_u8(vector_data);
		uint16x8_t x16y16z16 = vmovl_u8(x8y8z8);
		uint32x4_t x32y32z32 = vmovl_u16(vget_low_u16(x16y16z16));

		float32x4_t value = vcvtq_f32_u32(x32y32z32);
		return vmulq_n_f32(value, 1.0f / 255.0f);
#else
		uint8_t x8 = vector_data[0];
		uint8_t y8 = vector_data[1];
		uint8_t z8 = vector_data[2];
		float x = unpack_scalar_unsigned(x8, 8);
		float y = unpack_scalar_unsigned(y8, 8);
		float z = unpack_scalar_unsigned(z8, 8);
		return vector_set(x, y, z);
#endif
	}

	inline Vector4_32 unpack_vector3_s24(const uint8_t* vector_data)
	{
		uint8_t x8 = vector_data[0];
		uint8_t y8 = vector_data[1];
		uint8_t z8 = vector_data[2];
		float x = unpack_scalar_signed(x8, 8);
		float y = unpack_scalar_signed(y8, 8);
		float z = unpack_scalar_signed(z8, 8);
		return vector_set(x, y, z);
	}

	// Packs data in big-endian order
	inline void pack_vector3_uXX(const Vector4_32& vector, uint8_t num_bits, uint8_t* out_vector_data)
	{
		uint32_t vector_x = pack_scalar_unsigned(vector_get_x(vector), num_bits);
		uint32_t vector_y = pack_scalar_unsigned(vector_get_y(vector), num_bits);
		uint32_t vector_z = pack_scalar_unsigned(vector_get_z(vector), num_bits);

		uint64_t vector_u64 = static_cast<uint64_t>(vector_x) << (64 - num_bits * 1);
		vector_u64 |= static_cast<uint64_t>(vector_y) << (64 - num_bits * 2);
		vector_u64 |= static_cast<uint64_t>(vector_z) << (64 - num_bits * 3);
		vector_u64 = byte_swap(vector_u64);

		unaligned_write(vector_u64, out_vector_data);
	}

	// Packs data in big-endian order
	inline void pack_vector3_sXX(const Vector4_32& vector, uint8_t num_bits, uint8_t* out_vector_data)
	{
		uint32_t vector_x = pack_scalar_signed(vector_get_x(vector), num_bits);
		uint32_t vector_y = pack_scalar_signed(vector_get_y(vector), num_bits);
		uint32_t vector_z = pack_scalar_signed(vector_get_z(vector), num_bits);

		uint64_t vector_u64 = static_cast<uint64_t>(vector_x) << (64 - num_bits * 1);
		vector_u64 |= static_cast<uint64_t>(vector_y) << (64 - num_bits * 2);
		vector_u64 |= static_cast<uint64_t>(vector_z) << (64 - num_bits * 3);
		vector_u64 = byte_swap(vector_u64);

		unaligned_write(vector_u64, out_vector_data);
	}

	// Assumes the 'vector_data' is in big-endian order
	inline Vector4_32 unpack_vector3_uXX_unsafe(uint8_t num_bits, const uint8_t* vector_data, uint32_t bit_offset)
	{
		ACL_ASSERT(num_bits * 3 <= 64, "Attempting to read too many bits");
		ACL_ASSERT(num_bits <= 19, "This function does not support reading more than 19 bits per component");

		struct PackedTableEntry
		{
			constexpr PackedTableEntry(uint8_t num_bits)
				: max_value(num_bits == 0 ? 1.0f : (1.0f / float((1 << num_bits) - 1)))
				, mask((1 << num_bits) - 1)
			{}

			float max_value;
			uint32_t mask;
		};

		// TODO: We technically don't need the first 3 entries, which could save a few bytes
		alignas(64) static constexpr PackedTableEntry k_packed_constants[20] =
		{
			PackedTableEntry(0), PackedTableEntry(1), PackedTableEntry(2), PackedTableEntry(3),
			PackedTableEntry(4), PackedTableEntry(5), PackedTableEntry(6), PackedTableEntry(7),
			PackedTableEntry(8), PackedTableEntry(9), PackedTableEntry(10), PackedTableEntry(11),
			PackedTableEntry(12), PackedTableEntry(13), PackedTableEntry(14), PackedTableEntry(15),
			PackedTableEntry(16), PackedTableEntry(17), PackedTableEntry(18), PackedTableEntry(19),
		};

#if defined(ACL_SSE2_INTRINSICS)
		const uint32_t bit_shift = 32 - num_bits;
		const __m128i mask = _mm_castps_si128(_mm_load_ps1((const float*)&k_packed_constants[num_bits].mask));
		const __m128 inv_max_value = _mm_load_ps1(&k_packed_constants[num_bits].max_value);

		uint32_t byte_offset = bit_offset / 8;
		uint32_t vector_u32 = unaligned_load<uint32_t>(vector_data + byte_offset);
		vector_u32 = byte_swap(vector_u32);
		const uint32_t x32 = (vector_u32 >> (bit_shift - (bit_offset % 8)));

		bit_offset += num_bits;

		byte_offset = bit_offset / 8;
		vector_u32 = unaligned_load<uint32_t>(vector_data + byte_offset);
		vector_u32 = byte_swap(vector_u32);
		const uint32_t y32 = (vector_u32 >> (bit_shift - (bit_offset % 8)));

		bit_offset += num_bits;

		byte_offset = bit_offset / 8;
		vector_u32 = unaligned_load<uint32_t>(vector_data + byte_offset);
		vector_u32 = byte_swap(vector_u32);
		const uint32_t z32 = (vector_u32 >> (bit_shift - (bit_offset % 8)));

		__m128i int_value = _mm_set_epi32(x32, z32, y32, x32);
		int_value = _mm_and_si128(int_value, mask);
		const __m128 value = _mm_cvtepi32_ps(int_value);
		return _mm_mul_ps(value, inv_max_value);
#elif defined(ACL_NEON_INTRINSICS)
		const uint32_t bit_shift = 32 - num_bits;
		uint32x4_t mask = vdupq_n_u32(k_packed_constants[num_bits].mask);
		float inv_max_value = k_packed_constants[num_bits].max_value;

		uint32_t byte_offset = bit_offset / 8;
		uint32_t vector_u32 = unaligned_load<uint32_t>(vector_data + byte_offset);
		vector_u32 = byte_swap(vector_u32);
		const uint32_t x32 = (vector_u32 >> (bit_shift - (bit_offset % 8)));

		bit_offset += num_bits;

		byte_offset = bit_offset / 8;
		vector_u32 = unaligned_load<uint32_t>(vector_data + byte_offset);
		vector_u32 = byte_swap(vector_u32);
		const uint32_t y32 = (vector_u32 >> (bit_shift - (bit_offset % 8)));

		bit_offset += num_bits;

		byte_offset = bit_offset / 8;
		vector_u32 = unaligned_load<uint32_t>(vector_data + byte_offset);
		vector_u32 = byte_swap(vector_u32);
		const uint32_t z32 = (vector_u32 >> (bit_shift - (bit_offset % 8)));

		uint32x2_t xy = vcreate_u32(uint64_t(x32) | (uint64_t(y32) << 32));
		uint32x2_t z = vcreate_u32(uint64_t(z32));
		uint32x4_t value_u32 = vcombine_u32(xy, z);
		value_u32 = vandq_u32(value_u32, mask);
		float32x4_t value_f32 = vcvtq_f32_u32(value_u32);
		return vmulq_n_f32(value_f32, inv_max_value);
#else
		const uint8_t num_bits_to_read = num_bits * 3;

		uint32_t byte_offset = bit_offset / 8;
		uint64_t vector_u64 = unaligned_load<uint64_t>(vector_data + byte_offset);
		vector_u64 = byte_swap(vector_u64);
		vector_u64 <<= bit_offset % 8;
		vector_u64 >>= 64 - num_bits_to_read;

		const uint32_t x32 = safe_static_cast<uint32_t>(vector_u64 >> (num_bits * 2));
		const uint32_t y32 = safe_static_cast<uint32_t>((vector_u64 >> num_bits) & ((1 << num_bits) - 1));
		uint32_t z32;

		if (num_bits_to_read + (bit_offset % 8) > 64)
		{
			// Larger values can be split over 2x u64 entries
			bit_offset += num_bits * 2;
			byte_offset = bit_offset / 8;
			vector_u64 = unaligned_load<uint64_t>(vector_data + byte_offset);
			vector_u64 = byte_swap(vector_u64);
			vector_u64 <<= bit_offset % 8;
			vector_u64 >>= 64 - num_bits;
			z32 = safe_static_cast<uint32_t>(vector_u64);
		}
		else
			z32 = safe_static_cast<uint32_t>(vector_u64 & ((1 << num_bits) - 1));

		const float x = unpack_scalar_unsigned(x32, num_bits);
		const float y = unpack_scalar_unsigned(y32, num_bits);
		const float z = unpack_scalar_unsigned(z32, num_bits);
		return vector_set(x, y, z);
#endif
	}

	// Assumes the 'vector_data' is in big-endian order
	inline Vector4_32 unpack_vector3_sXX_unsafe(uint8_t num_bits, const uint8_t* vector_data, uint32_t bit_offset)
	{
		ACL_ASSERT(num_bits * 3 <= 64, "Attempting to read too many bits");

		Vector4_32 unsigned_value = unpack_vector3_uXX_unsafe(num_bits, vector_data, bit_offset);
		return vector_sub(vector_mul(unsigned_value, 2.0f), vector_set(1.0f));
	}

	//////////////////////////////////////////////////////////////////////////

	// TODO: constexpr
	inline uint32_t get_packed_vector_size(VectorFormat8 format)
	{
		switch (format)
		{
		case VectorFormat8::Vector3_96:		return sizeof(float) * 3;
		case VectorFormat8::Vector3_48:		return sizeof(uint16_t) * 3;
		case VectorFormat8::Vector3_32:		return sizeof(uint32_t);
		case VectorFormat8::Vector3_Variable:
		default:
			ACL_ASSERT(false, "Invalid or unsupported vector format: %s", get_vector_format_name(format));
			return 0;
		}
	}
}
