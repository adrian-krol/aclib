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

#include "acl/core/memory.h"
#include "acl/core/error.h"
#include "acl/core/enum_utils.h"
#include "acl/core/track_types.h"
#include "acl/math/quat_32.h"
#include "acl/math/vector4_32.h"
#include "acl/compression/stream/clip_context.h"

#include <stdint.h>

namespace acl
{
	inline void extract_clip_bone_ranges(Allocator& allocator, ClipContext& clip_context)
	{
		clip_context.ranges = allocate_type_array<BoneRanges>(allocator, clip_context.num_bones);

		for (SegmentContext& segment : clip_context.segment_iterator())
		{
			for (uint16_t bone_index = 0; bone_index < clip_context.num_bones; ++bone_index)
			{
				const BoneStreams& bone_stream = segment.bone_streams[bone_index];

				Vector4_32 rotation_min = vector_set(1e10f);
				Vector4_32 rotation_max = vector_set(-1e10f);
				Vector4_32 translation_min = vector_set(1e10f);
				Vector4_32 translation_max = vector_set(-1e10f);

				for (uint32_t sample_index = 0; sample_index < bone_stream.rotations.get_num_samples(); ++sample_index)
				{
					Quat_32 rotation = bone_stream.rotations.get_raw_sample<Quat_32>(sample_index);

					rotation_min = vector_min(rotation_min, quat_to_vector(rotation));
					rotation_max = vector_max(rotation_max, quat_to_vector(rotation));
				}

				for (uint32_t sample_index = 0; sample_index < bone_stream.translations.get_num_samples(); ++sample_index)
				{
					Vector4_32 translation = bone_stream.translations.get_raw_sample<Vector4_32>(sample_index);

					translation_min = vector_min(translation_min, translation);
					translation_max = vector_max(translation_max, translation);
				}

				BoneRanges& bone_ranges = clip_context.ranges[bone_index];
				bone_ranges.rotation = TrackStreamRange(rotation_min, rotation_max);
				bone_ranges.translation = TrackStreamRange(translation_min, translation_max);
			}
		}
	}

	inline void normalize_rotation_streams(BoneStreams* bone_streams, const BoneRanges* bone_ranges, uint16_t num_bones)
	{
		for (uint16_t bone_index = 0; bone_index < num_bones; ++bone_index)
		{
			BoneStreams& bone_stream = bone_streams[bone_index];
			const BoneRanges& bone_range = bone_ranges[bone_index];

			// We expect all our samples to have the same width of sizeof(Vector4_32)
			ACL_ENSURE(bone_stream.rotations.get_sample_size() == sizeof(Vector4_32), "Unexpected rotation sample size. %u != %u", bone_stream.rotations.get_sample_size(), sizeof(Vector4_32));

			// Constant or default tracks are not normalized
			if (!bone_stream.is_rotation_animated())
				continue;

			uint32_t num_samples = bone_stream.rotations.get_num_samples();
			RotationFormat8 rotation_format = bone_stream.rotations.get_rotation_format();

			Vector4_32 range_min = bone_range.rotation.get_min();
			Vector4_32 range_extent = bone_range.rotation.get_extent();

			for (uint32_t sample_index = 0; sample_index < num_samples; ++sample_index)
			{
				// normalized value is between [0.0 .. 1.0]
				// value = (normalized value * range extent) + range min
				// normalized value = (value - range min) / range extent
				Vector4_32 rotation = bone_stream.rotations.get_raw_sample<Vector4_32>(sample_index);
				Vector4_32 normalized_rotation = vector_div(vector_sub(rotation, range_min), range_extent);
				Vector4_32 is_range_zero_mask = vector_less_than(range_extent, vector_set(0.000000001f));
				normalized_rotation = vector_blend(is_range_zero_mask, vector_zero_32(), normalized_rotation);

#if defined(ACL_USE_ERROR_CHECKS)
				switch (rotation_format)
				{
				case RotationFormat8::Quat_128:
					ACL_ENSURE(vector_all_greater_equal(normalized_rotation, vector_zero_32()) && vector_all_less_equal(normalized_rotation, vector_set(1.0f)), "Invalid normalized rotation. 0.0 <= [%f, %f, %f, %f] <= 1.0", vector_get_x(normalized_rotation), vector_get_y(normalized_rotation), vector_get_z(normalized_rotation), vector_get_w(normalized_rotation));
					break;
				case RotationFormat8::QuatDropW_96:
				case RotationFormat8::QuatDropW_48:
				case RotationFormat8::QuatDropW_32:
				case RotationFormat8::QuatDropW_Variable:
					ACL_ENSURE(vector_all_greater_equal3(normalized_rotation, vector_zero_32()) && vector_all_less_equal3(normalized_rotation, vector_set(1.0f)), "Invalid normalized rotation. 0.0 <= [%f, %f, %f] <= 1.0", vector_get_x(normalized_rotation), vector_get_y(normalized_rotation), vector_get_z(normalized_rotation));
					break;
				}
#endif

				bone_stream.rotations.set_raw_sample(sample_index, normalized_rotation);
			}
		}
	}

	inline void normalize_translation_streams(BoneStreams* bone_streams, const BoneRanges* bone_ranges, uint16_t num_bones)
	{
		for (uint16_t bone_index = 0; bone_index < num_bones; ++bone_index)
		{
			BoneStreams& bone_stream = bone_streams[bone_index];
			const BoneRanges& bone_range = bone_ranges[bone_index];

			// We expect all our samples to have the same width of sizeof(Vector4_32)
			ACL_ENSURE(bone_stream.translations.get_sample_size() == sizeof(Vector4_32), "Unexpected translation sample size. %u != %u", bone_stream.translations.get_sample_size(), sizeof(Vector4_32));

			// Constant or default tracks are not normalized
			if (!bone_stream.is_translation_animated())
				continue;

			uint32_t num_samples = bone_stream.translations.get_num_samples();

			Vector4_32 range_min = bone_range.translation.get_min();
			Vector4_32 range_extent = bone_range.translation.get_extent();

			for (uint32_t sample_index = 0; sample_index < num_samples; ++sample_index)
			{
				// normalized value is between [0.0 .. 1.0]
				// value = (normalized value * range extent) + range min
				// normalized value = (value - range min) / range extent
				Vector4_32 translation = bone_stream.translations.get_raw_sample<Vector4_32>(sample_index);
				Vector4_32 normalized_translation = vector_div(vector_sub(translation, range_min), range_extent);
				Vector4_32 is_range_zero_mask = vector_less_than(range_extent, vector_set(0.000000001f));
				normalized_translation = vector_blend(is_range_zero_mask, vector_zero_32(), normalized_translation);

				ACL_ENSURE(vector_all_greater_equal3(normalized_translation, vector_zero_32()) && vector_all_less_equal3(normalized_translation, vector_set(1.0f)), "Invalid normalized translation. 0.0 <= [%f, %f, %f] <= 1.0", vector_get_x(normalized_translation), vector_get_y(normalized_translation), vector_get_z(normalized_translation));

				bone_stream.translations.set_raw_sample(sample_index, normalized_translation);
			}
		}
	}

	inline void normalize_clip_streams(ClipContext& clip_context, RangeReductionFlags8 range_reduction)
	{
		for (SegmentContext& segment : clip_context.segment_iterator())
		{
			if (is_enum_flag_set(range_reduction, RangeReductionFlags8::Rotations))
			{
				normalize_rotation_streams(segment.bone_streams, clip_context.ranges, segment.num_bones);
				clip_context.are_rotations_normalized = true;
			}

			if (is_enum_flag_set(range_reduction, RangeReductionFlags8::Translations))
			{
				normalize_translation_streams(segment.bone_streams, clip_context.ranges, segment.num_bones);
				clip_context.are_translations_normalized = true;
			}
		}
	}
}
