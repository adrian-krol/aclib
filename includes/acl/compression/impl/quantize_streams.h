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

#include "acl/core/iallocator.h"
#include "acl/core/impl/compiler_utils.h"
#include "acl/core/error.h"
#include "acl/core/utils.h"
#include "acl/math/quat_packing.h"
#include "acl/math/vector4_packing.h"
#include "acl/compression/impl/track_bit_rate_database.h"
#include "acl/compression/impl/transform_bit_rate_permutations.h"
#include "acl/compression/impl/clip_context.h"
#include "acl/compression/impl/sample_streams.h"
#include "acl/compression/impl/normalize_streams.h"
#include "acl/compression/impl/convert_rotation_streams.h"
#include "acl/compression/skeleton_error_metric.h"
#include "acl/compression/compression_settings.h"

#include <rtm/quatf.h>
#include <rtm/vector4f.h>

#include <cstddef>
#include <cstdint>
#include <functional>

// 0 = no debug info, 1 = basic info, 2 = verbose
#define ACL_IMPL_DEBUG_VARIABLE_QUANTIZATION		0

// 0 = no profiling, 1 = we perform quantization 10 times in a row for every segment
#define ACL_IMPL_PROFILE_MATH						0

#if ACL_IMPL_PROFILE_MATH && defined(__ANDROID__)
#include <android/log.h>
#endif

ACL_IMPL_FILE_PRAGMA_PUSH

namespace acl
{
	namespace acl_impl
	{
		struct QuantizationContext
		{
			IAllocator& allocator;
			ClipContext& clip;
			const ClipContext& raw_clip;
			const ClipContext& additive_base_clip;
			SegmentContext* segment;
			BoneStreams* bone_streams;
			const transform_metadata* metadata;
			uint16_t num_bones;
			const itransform_error_metric* error_metric;

			track_bit_rate_database bit_rate_database;
			single_track_query local_query;
			hierarchical_track_query object_query;

			uint32_t num_samples;
			uint32_t segment_sample_start_index;
			float sample_rate;
			float clip_duration;
			float error_threshold;					// Error threshold of the current bone being optimized
			bool has_scale;
			bool has_additive_base;
			bool needs_conversion;

			rotation_format8 rotation_format;
			vector_format8 translation_format;
			vector_format8 scale_format;
			compression_level8 compression_level;

			const BoneStreams* raw_bone_streams;

			rtm::qvvf* additive_local_pose;			// 1 per transform
			rtm::qvvf* raw_local_pose;				// 1 per transform
			rtm::qvvf* lossy_local_pose;			// 1 per transform

			uint8_t* raw_local_transforms;			// 1 per transform per sample in segment
			uint8_t* base_local_transforms;			// 1 per transform per sample in segment
			uint8_t* raw_object_transforms;			// 1 per transform per sample in segment
			uint8_t* base_object_transforms;		// 1 per transform per sample in segment

			uint8_t* local_transforms_converted;	// 1 per transform
			uint8_t* lossy_object_pose;				// 1 per transform
			size_t metric_transform_size;

			BoneBitRate* bit_rate_per_bone;			// 1 per transform
			uint16_t* parent_transform_indices;		// 1 per transform
			uint16_t* self_transform_indices;		// 1 per transform

			uint16_t* chain_bone_indices;			// 1 per transform
			uint16_t num_bones_in_chain;
			uint16_t padding0;	// unused
			uint32_t padding1;	// unused

			QuantizationContext(IAllocator& allocator_, ClipContext& clip_, const ClipContext& raw_clip_, const ClipContext& additive_base_clip_, const CompressionSettings& settings_)
				: allocator(allocator_)
				, clip(clip_)
				, raw_clip(raw_clip_)
				, additive_base_clip(additive_base_clip_)
				, segment(nullptr)
				, bone_streams(nullptr)
				, metadata(clip_.metadata)
				, num_bones(clip_.num_bones)
				, error_metric(settings_.error_metric)
				, bit_rate_database(allocator_, settings_.rotation_format, settings_.translation_format, settings_.scale_format, clip_.segments->bone_streams, raw_clip_.segments->bone_streams, clip_.num_bones, clip_.segments->num_samples)
				, local_query()
				, object_query(allocator_)
				, num_samples(~0U)
				, segment_sample_start_index(~0U)
				, sample_rate(clip_.sample_rate)
				, clip_duration(clip_.duration)
				, error_threshold(settings_.error_threshold)
				, has_scale(clip_.has_scale)
				, has_additive_base(clip_.has_additive_base)
				, rotation_format(settings_.rotation_format)
				, translation_format(settings_.translation_format)
				, scale_format(settings_.scale_format)
				, compression_level(settings_.level)
				, raw_bone_streams(raw_clip_.segments[0].bone_streams)
				, num_bones_in_chain(0)
			{
				local_query.bind(bit_rate_database);
				object_query.bind(bit_rate_database);

				needs_conversion = settings_.error_metric->needs_conversion(clip_.has_scale);
				const size_t metric_transform_size_ = settings_.error_metric->get_transform_size(clip_.has_scale);
				metric_transform_size = metric_transform_size_;

				additive_local_pose = clip_.has_additive_base ? allocate_type_array<rtm::qvvf>(allocator, num_bones) : nullptr;
				raw_local_pose = allocate_type_array<rtm::qvvf>(allocator, num_bones);
				lossy_local_pose = allocate_type_array<rtm::qvvf>(allocator, num_bones);
				raw_local_transforms = allocate_type_array_aligned<uint8_t>(allocator, metric_transform_size_ * num_bones * clip_.segments->num_samples, 64);
				base_local_transforms = clip_.has_additive_base ? allocate_type_array_aligned<uint8_t>(allocator, metric_transform_size_ * num_bones * clip_.segments->num_samples, 64) : nullptr;
				raw_object_transforms = allocate_type_array_aligned<uint8_t>(allocator, metric_transform_size_ * num_bones * clip_.segments->num_samples, 64);
				base_object_transforms = clip_.has_additive_base ? allocate_type_array_aligned<uint8_t>(allocator, metric_transform_size_ * num_bones * clip_.segments->num_samples, 64) : nullptr;
				local_transforms_converted = needs_conversion ? allocate_type_array_aligned<uint8_t>(allocator, metric_transform_size_ * num_bones, 64) : nullptr;
				lossy_object_pose = allocate_type_array_aligned<uint8_t>(allocator, metric_transform_size_ * num_bones, 64);
				bit_rate_per_bone = allocate_type_array<BoneBitRate>(allocator, num_bones);
				parent_transform_indices = allocate_type_array<uint16_t>(allocator, num_bones);
				self_transform_indices = allocate_type_array<uint16_t>(allocator, num_bones);
				chain_bone_indices = allocate_type_array<uint16_t>(allocator, num_bones);

				for (uint16_t transform_index = 0; transform_index < num_bones; ++transform_index)
				{
					const transform_metadata& metadata_ = clip_.metadata[transform_index];
					parent_transform_indices[transform_index] = metadata_.parent_index;
					self_transform_indices[transform_index] = transform_index;
				}
			}

			QuantizationContext(IAllocator& allocator_, ClipContext& clip_, const ClipContext& raw_clip_, const ClipContext& additive_base_clip_, const compression_settings& settings_)
				: allocator(allocator_)
				, clip(clip_)
				, raw_clip(raw_clip_)
				, additive_base_clip(additive_base_clip_)
				, segment(nullptr)
				, bone_streams(nullptr)
				, metadata(clip_.metadata)
				, num_bones(clip_.num_bones)
				, error_metric(settings_.error_metric)
				, bit_rate_database(allocator_, settings_.rotation_format, settings_.translation_format, settings_.scale_format, clip_.segments->bone_streams, raw_clip_.segments->bone_streams, clip_.num_bones, clip_.segments->num_samples)
				, local_query()
				, object_query(allocator_)
				, num_samples(~0U)
				, segment_sample_start_index(~0U)
				, sample_rate(clip_.sample_rate)
				, clip_duration(clip_.duration)
				, error_threshold(0.0F)
				, has_scale(clip_.has_scale)
				, has_additive_base(clip_.has_additive_base)
				, rotation_format(settings_.rotation_format)
				, translation_format(settings_.translation_format)
				, scale_format(settings_.scale_format)
				, compression_level(settings_.level)
				, raw_bone_streams(raw_clip_.segments[0].bone_streams)
				, num_bones_in_chain(0)
			{
				local_query.bind(bit_rate_database);
				object_query.bind(bit_rate_database);

				needs_conversion = settings_.error_metric->needs_conversion(clip_.has_scale);
				const size_t metric_transform_size_ = settings_.error_metric->get_transform_size(clip_.has_scale);
				metric_transform_size = metric_transform_size_;

				additive_local_pose = clip_.has_additive_base ? allocate_type_array<rtm::qvvf>(allocator, num_bones) : nullptr;
				raw_local_pose = allocate_type_array<rtm::qvvf>(allocator, num_bones);
				lossy_local_pose = allocate_type_array<rtm::qvvf>(allocator, num_bones);
				raw_local_transforms = allocate_type_array_aligned<uint8_t>(allocator, metric_transform_size_ * num_bones * clip_.segments->num_samples, 64);
				base_local_transforms = clip_.has_additive_base ? allocate_type_array_aligned<uint8_t>(allocator, metric_transform_size_ * num_bones * clip_.segments->num_samples, 64) : nullptr;
				raw_object_transforms = allocate_type_array_aligned<uint8_t>(allocator, metric_transform_size_ * num_bones * clip_.segments->num_samples, 64);
				base_object_transforms = clip_.has_additive_base ? allocate_type_array_aligned<uint8_t>(allocator, metric_transform_size_ * num_bones * clip_.segments->num_samples, 64) : nullptr;
				local_transforms_converted = needs_conversion ? allocate_type_array_aligned<uint8_t>(allocator, metric_transform_size_ * num_bones, 64) : nullptr;
				lossy_object_pose = allocate_type_array_aligned<uint8_t>(allocator, metric_transform_size_ * num_bones, 64);
				bit_rate_per_bone = allocate_type_array<BoneBitRate>(allocator, num_bones);
				parent_transform_indices = allocate_type_array<uint16_t>(allocator, num_bones);
				self_transform_indices = allocate_type_array<uint16_t>(allocator, num_bones);
				chain_bone_indices = allocate_type_array<uint16_t>(allocator, num_bones);

				for (uint16_t transform_index = 0; transform_index < num_bones; ++transform_index)
				{
					const transform_metadata& metadata_ = clip_.metadata[transform_index];
					parent_transform_indices[transform_index] = metadata_.parent_index;
					self_transform_indices[transform_index] = transform_index;
				}
			}

			~QuantizationContext()
			{
				deallocate_type_array(allocator, additive_local_pose, num_bones);
				deallocate_type_array(allocator, raw_local_pose, num_bones);
				deallocate_type_array(allocator, lossy_local_pose, num_bones);
				deallocate_type_array(allocator, raw_local_transforms, metric_transform_size * num_bones * clip.segments->num_samples);
				deallocate_type_array(allocator, base_local_transforms, metric_transform_size * num_bones * clip.segments->num_samples);
				deallocate_type_array(allocator, raw_object_transforms, metric_transform_size * num_bones * clip.segments->num_samples);
				deallocate_type_array(allocator, base_object_transforms, metric_transform_size * num_bones * clip.segments->num_samples);
				deallocate_type_array(allocator, local_transforms_converted, metric_transform_size * num_bones);
				deallocate_type_array(allocator, lossy_object_pose, metric_transform_size * num_bones);
				deallocate_type_array(allocator, bit_rate_per_bone, num_bones);
				deallocate_type_array(allocator, parent_transform_indices, num_bones);
				deallocate_type_array(allocator, self_transform_indices, num_bones);
				deallocate_type_array(allocator, chain_bone_indices, num_bones);
			}

			void set_segment(SegmentContext& segment_)
			{
				segment = &segment_;
				bone_streams = segment_.bone_streams;
				num_samples = segment_.num_samples;
				segment_sample_start_index = segment_.clip_sample_offset;
				bit_rate_database.set_segment(segment_.bone_streams, segment_.num_bones, segment_.num_samples);

				// Cache every raw local/object transforms and the base local transforms since they never change
				const itransform_error_metric* error_metric_ = error_metric;
				const size_t sample_transform_size = metric_transform_size * num_bones;

				const auto convert_transforms_impl = std::mem_fn(has_scale ? &itransform_error_metric::convert_transforms : &itransform_error_metric::convert_transforms_no_scale);
				const auto apply_additive_to_base_impl = std::mem_fn(has_scale ? &itransform_error_metric::apply_additive_to_base : &itransform_error_metric::apply_additive_to_base_no_scale);
				const auto local_to_object_space_impl = std::mem_fn(has_scale ? &itransform_error_metric::local_to_object_space : &itransform_error_metric::local_to_object_space_no_scale);

				itransform_error_metric::convert_transforms_args convert_transforms_args_raw;
				convert_transforms_args_raw.dirty_transform_indices = self_transform_indices;
				convert_transforms_args_raw.num_dirty_transforms = num_bones;
				convert_transforms_args_raw.transforms = raw_local_pose;
				convert_transforms_args_raw.num_transforms = num_bones;

				itransform_error_metric::convert_transforms_args convert_transforms_args_base = convert_transforms_args_raw;
				convert_transforms_args_base.transforms = additive_local_pose;

				itransform_error_metric::apply_additive_to_base_args apply_additive_to_base_args_raw;
				apply_additive_to_base_args_raw.dirty_transform_indices = self_transform_indices;
				apply_additive_to_base_args_raw.num_dirty_transforms = num_bones;
				apply_additive_to_base_args_raw.local_transforms = nullptr;
				apply_additive_to_base_args_raw.base_transforms = nullptr;
				apply_additive_to_base_args_raw.num_transforms = num_bones;

				itransform_error_metric::local_to_object_space_args local_to_object_space_args_raw;
				local_to_object_space_args_raw.dirty_transform_indices = self_transform_indices;
				local_to_object_space_args_raw.num_dirty_transforms = num_bones;
				local_to_object_space_args_raw.parent_transform_indices = parent_transform_indices;
				local_to_object_space_args_raw.local_transforms = nullptr;
				local_to_object_space_args_raw.num_transforms = num_bones;

				for (uint32_t sample_index = 0; sample_index < segment_.num_samples; ++sample_index)
				{
					// Sample our streams and calculate the error
					// The sample time is calculated from the full clip duration to be consistent with decompression
					const float sample_time = rtm::scalar_min(float(segment_.clip_sample_offset + sample_index) / sample_rate, clip_duration);

					sample_streams(raw_bone_streams, num_bones, sample_time, raw_local_pose);

					uint8_t* sample_raw_local_transforms = raw_local_transforms + (sample_index * sample_transform_size);

					if (needs_conversion)
						convert_transforms_impl(error_metric_, convert_transforms_args_raw, sample_raw_local_transforms);
					else
						std::memcpy(sample_raw_local_transforms, raw_local_pose, sample_transform_size);

					if (has_additive_base)
					{
						const float normalized_sample_time = additive_base_clip.num_samples > 1 ? (sample_time / clip_duration) : 0.0F;
						const float additive_sample_time = additive_base_clip.num_samples > 1 ? (normalized_sample_time * additive_base_clip.duration) : 0.0F;
						sample_streams(additive_base_clip.segments[0].bone_streams, num_bones, additive_sample_time, additive_local_pose);

						uint8_t* sample_base_local_transforms = base_local_transforms + (sample_index * sample_transform_size);

						if (needs_conversion)
							convert_transforms_impl(error_metric_, convert_transforms_args_base, sample_base_local_transforms);
						else
							std::memcpy(sample_base_local_transforms, additive_local_pose, sample_transform_size);

						apply_additive_to_base_args_raw.local_transforms = sample_raw_local_transforms;
						apply_additive_to_base_args_raw.base_transforms = sample_base_local_transforms;
						apply_additive_to_base_impl(error_metric_, apply_additive_to_base_args_raw, sample_raw_local_transforms);
					}

					local_to_object_space_args_raw.local_transforms = sample_raw_local_transforms;

					uint8_t* sample_raw_object_transforms = raw_object_transforms + (sample_index * sample_transform_size);
					local_to_object_space_impl(error_metric_, local_to_object_space_args_raw, sample_raw_object_transforms);
				}
			}

			bool is_valid() const { return segment != nullptr; }

			QuantizationContext(const QuantizationContext&) = delete;
			QuantizationContext(QuantizationContext&&) = delete;
			QuantizationContext& operator=(const QuantizationContext&) = delete;
			QuantizationContext& operator=(QuantizationContext&&) = delete;
		};

		inline void quantize_fixed_rotation_stream(IAllocator& allocator, const RotationTrackStream& raw_stream, rotation_format8 rotation_format, RotationTrackStream& out_quantized_stream)
		{
			// We expect all our samples to have the same width of sizeof(rtm::vector4f)
			ACL_ASSERT(raw_stream.get_sample_size() == sizeof(rtm::vector4f), "Unexpected rotation sample size. %u != %zu", raw_stream.get_sample_size(), sizeof(rtm::vector4f));

			const uint32_t num_samples = raw_stream.get_num_samples();
			const uint32_t rotation_sample_size = get_packed_rotation_size(rotation_format);
			const float sample_rate = raw_stream.get_sample_rate();
			RotationTrackStream quantized_stream(allocator, num_samples, rotation_sample_size, sample_rate, rotation_format);

			for (uint32_t sample_index = 0; sample_index < num_samples; ++sample_index)
			{
				const rtm::quatf rotation = raw_stream.get_raw_sample<rtm::quatf>(sample_index);
				uint8_t* quantized_ptr = quantized_stream.get_raw_sample_ptr(sample_index);

				switch (rotation_format)
				{
				case rotation_format8::quatf_full:
					pack_vector4_128(rtm::quat_to_vector(rotation), quantized_ptr);
					break;
				case rotation_format8::quatf_drop_w_full:
					pack_vector3_96(rtm::quat_to_vector(rotation), quantized_ptr);
					break;
				case rotation_format8::quatf_drop_w_variable:
				default:
					ACL_ASSERT(false, "Invalid or unsupported rotation format: %s", get_rotation_format_name(rotation_format));
					break;
				}
			}

			out_quantized_stream = std::move(quantized_stream);
		}

		inline void quantize_fixed_rotation_stream(QuantizationContext& context, uint16_t bone_index, rotation_format8 rotation_format)
		{
			ACL_ASSERT(bone_index < context.num_bones, "Invalid bone index: %u", bone_index);

			BoneStreams& bone_stream = context.bone_streams[bone_index];

			// Default tracks aren't quantized
			if (bone_stream.is_rotation_default)
				return;

			quantize_fixed_rotation_stream(context.allocator, bone_stream.rotations, rotation_format, bone_stream.rotations);
		}

		inline void quantize_variable_rotation_stream(QuantizationContext& context, const RotationTrackStream& raw_clip_stream, const RotationTrackStream& raw_segment_stream, const TrackStreamRange& clip_range, uint8_t bit_rate, RotationTrackStream& out_quantized_stream)
		{
			// We expect all our samples to have the same width of sizeof(rtm::vector4f)
			ACL_ASSERT(raw_segment_stream.get_sample_size() == sizeof(rtm::vector4f), "Unexpected rotation sample size. %u != %zu", raw_segment_stream.get_sample_size(), sizeof(rtm::vector4f));

			const uint32_t num_samples = is_constant_bit_rate(bit_rate) ? 1 : raw_segment_stream.get_num_samples();
			const uint32_t sample_size = sizeof(uint64_t) * 2;
			const float sample_rate = raw_segment_stream.get_sample_rate();
			RotationTrackStream quantized_stream(context.allocator, num_samples, sample_size, sample_rate, rotation_format8::quatf_drop_w_variable, bit_rate);

			if (is_constant_bit_rate(bit_rate))
			{
				rtm::vector4f rotation = raw_clip_stream.get_raw_sample<rtm::vector4f>(context.segment_sample_start_index);
				rotation = convert_rotation(rotation, rotation_format8::quatf_full, rotation_format8::quatf_drop_w_variable);

				const rtm::vector4f normalized_rotation = normalize_sample(rotation, clip_range);

				uint8_t* quantized_ptr = quantized_stream.get_raw_sample_ptr(0);
				pack_vector3_u48_unsafe(normalized_rotation, quantized_ptr);
			}
			else
			{
				const uint32_t num_bits_at_bit_rate = get_num_bits_at_bit_rate(bit_rate);

				for (uint32_t sample_index = 0; sample_index < num_samples; ++sample_index)
				{
					uint8_t* quantized_ptr = quantized_stream.get_raw_sample_ptr(sample_index);

					if (is_raw_bit_rate(bit_rate))
					{
						rtm::vector4f rotation = raw_clip_stream.get_raw_sample<rtm::vector4f>(context.segment_sample_start_index + sample_index);
						rotation = convert_rotation(rotation, rotation_format8::quatf_full, rotation_format8::quatf_drop_w_variable);
						pack_vector3_96(rotation, quantized_ptr);
					}
					else
					{
						const rtm::quatf rotation = raw_segment_stream.get_raw_sample<rtm::quatf>(sample_index);
						pack_vector3_uXX_unsafe(rtm::quat_to_vector(rotation), num_bits_at_bit_rate, quantized_ptr);
					}
				}
			}

			out_quantized_stream = std::move(quantized_stream);
		}

		inline void quantize_variable_rotation_stream(QuantizationContext& context, uint16_t bone_index, uint8_t bit_rate)
		{
			ACL_ASSERT(bone_index < context.num_bones, "Invalid bone index: %u", bone_index);

			BoneStreams& bone_stream = context.bone_streams[bone_index];

			// Default tracks aren't quantized
			if (bone_stream.is_rotation_default)
				return;

			const BoneStreams& raw_bone_stream = context.raw_bone_streams[bone_index];
			const rotation_format8 highest_bit_rate = get_highest_variant_precision(rotation_variant8::quat_drop_w);
			const TrackStreamRange& bone_range = context.clip.ranges[bone_index].rotation;

			// If our format is variable, we keep them fixed at the highest bit rate in the variant
			if (bone_stream.is_rotation_constant)
				quantize_fixed_rotation_stream(context.allocator, bone_stream.rotations, highest_bit_rate, bone_stream.rotations);
			else
				quantize_variable_rotation_stream(context, raw_bone_stream.rotations, bone_stream.rotations, bone_range, bit_rate, bone_stream.rotations);
		}

		inline void quantize_fixed_translation_stream(IAllocator& allocator, const TranslationTrackStream& raw_stream, vector_format8 translation_format, TranslationTrackStream& out_quantized_stream)
		{
			// We expect all our samples to have the same width of sizeof(rtm::vector4f)
			ACL_ASSERT(raw_stream.get_sample_size() == sizeof(rtm::vector4f), "Unexpected translation sample size. %u != %zu", raw_stream.get_sample_size(), sizeof(rtm::vector4f));
			ACL_ASSERT(raw_stream.get_vector_format() == vector_format8::vector3f_full, "Expected a vector3f_full vector format, found: %s", get_vector_format_name(raw_stream.get_vector_format()));

			const uint32_t num_samples = raw_stream.get_num_samples();
			const uint32_t sample_size = get_packed_vector_size(translation_format);
			const float sample_rate = raw_stream.get_sample_rate();
			TranslationTrackStream quantized_stream(allocator, num_samples, sample_size, sample_rate, translation_format);

			for (uint32_t sample_index = 0; sample_index < num_samples; ++sample_index)
			{
				const rtm::vector4f translation = raw_stream.get_raw_sample<rtm::vector4f>(sample_index);
				uint8_t* quantized_ptr = quantized_stream.get_raw_sample_ptr(sample_index);

				switch (translation_format)
				{
				case vector_format8::vector3f_full:
					pack_vector3_96(translation, quantized_ptr);
					break;
				case vector_format8::vector3f_variable:
				default:
					ACL_ASSERT(false, "Invalid or unsupported vector format: %s", get_vector_format_name(translation_format));
					break;
				}
			}

			out_quantized_stream = std::move(quantized_stream);
		}

		inline void quantize_fixed_translation_stream(QuantizationContext& context, uint16_t bone_index, vector_format8 translation_format)
		{
			ACL_ASSERT(bone_index < context.num_bones, "Invalid bone index: %u", bone_index);

			BoneStreams& bone_stream = context.bone_streams[bone_index];

			// Default tracks aren't quantized
			if (bone_stream.is_translation_default)
				return;

			// Constant translation tracks store the remaining sample with full precision
			const vector_format8 format = bone_stream.is_translation_constant ? vector_format8::vector3f_full : translation_format;

			quantize_fixed_translation_stream(context.allocator, bone_stream.translations, format, bone_stream.translations);
		}

		inline void quantize_variable_translation_stream(QuantizationContext& context, const TranslationTrackStream& raw_clip_stream, const TranslationTrackStream& raw_segment_stream, const TrackStreamRange& clip_range, uint8_t bit_rate, TranslationTrackStream& out_quantized_stream)
		{
			// We expect all our samples to have the same width of sizeof(rtm::vector4f)
			ACL_ASSERT(raw_segment_stream.get_sample_size() == sizeof(rtm::vector4f), "Unexpected translation sample size. %u != %zu", raw_segment_stream.get_sample_size(), sizeof(rtm::vector4f));
			ACL_ASSERT(raw_segment_stream.get_vector_format() == vector_format8::vector3f_full, "Expected a vector3f_full vector format, found: %s", get_vector_format_name(raw_segment_stream.get_vector_format()));

			const uint32_t num_samples = is_constant_bit_rate(bit_rate) ? 1 : raw_segment_stream.get_num_samples();
			const uint32_t sample_size = sizeof(uint64_t) * 2;
			const float sample_rate = raw_segment_stream.get_sample_rate();
			TranslationTrackStream quantized_stream(context.allocator, num_samples, sample_size, sample_rate, vector_format8::vector3f_variable, bit_rate);

			if (is_constant_bit_rate(bit_rate))
			{
				const rtm::vector4f translation = raw_clip_stream.get_raw_sample<rtm::vector4f>(context.segment_sample_start_index);
				const rtm::vector4f normalized_translation = normalize_sample(translation, clip_range);

				uint8_t* quantized_ptr = quantized_stream.get_raw_sample_ptr(0);
				pack_vector3_u48_unsafe(normalized_translation, quantized_ptr);
			}
			else
			{
				const uint32_t num_bits_at_bit_rate = get_num_bits_at_bit_rate(bit_rate);

				for (uint32_t sample_index = 0; sample_index < num_samples; ++sample_index)
				{
					uint8_t* quantized_ptr = quantized_stream.get_raw_sample_ptr(sample_index);

					if (is_raw_bit_rate(bit_rate))
					{
						const rtm::vector4f translation = raw_clip_stream.get_raw_sample<rtm::vector4f>(context.segment_sample_start_index + sample_index);
						pack_vector3_96(translation, quantized_ptr);
					}
					else
					{
						const rtm::vector4f translation = raw_segment_stream.get_raw_sample<rtm::vector4f>(sample_index);
						pack_vector3_uXX_unsafe(translation, num_bits_at_bit_rate, quantized_ptr);
					}
				}
			}

			out_quantized_stream = std::move(quantized_stream);
		}

		inline void quantize_variable_translation_stream(QuantizationContext& context, uint16_t bone_index, uint8_t bit_rate)
		{
			ACL_ASSERT(bone_index < context.num_bones, "Invalid bone index: %u", bone_index);

			BoneStreams& bone_stream = context.bone_streams[bone_index];

			// Default tracks aren't quantized
			if (bone_stream.is_translation_default)
				return;

			const TrackStreamRange& bone_range = context.clip.ranges[bone_index].translation;
			const BoneStreams& raw_bone_stream = context.raw_bone_streams[bone_index];

			// Constant translation tracks store the remaining sample with full precision
			if (bone_stream.is_translation_constant)
				quantize_fixed_translation_stream(context.allocator, bone_stream.translations, vector_format8::vector3f_full, bone_stream.translations);
			else
				quantize_variable_translation_stream(context, raw_bone_stream.translations, bone_stream.translations, bone_range, bit_rate, bone_stream.translations);
		}

		inline void quantize_fixed_scale_stream(IAllocator& allocator, const ScaleTrackStream& raw_stream, vector_format8 scale_format, ScaleTrackStream& out_quantized_stream)
		{
			// We expect all our samples to have the same width of sizeof(rtm::vector4f)
			ACL_ASSERT(raw_stream.get_sample_size() == sizeof(rtm::vector4f), "Unexpected scale sample size. %u != %zu", raw_stream.get_sample_size(), sizeof(rtm::vector4f));
			ACL_ASSERT(raw_stream.get_vector_format() == vector_format8::vector3f_full, "Expected a vector3f_full vector format, found: %s", get_vector_format_name(raw_stream.get_vector_format()));

			const uint32_t num_samples = raw_stream.get_num_samples();
			const uint32_t sample_size = get_packed_vector_size(scale_format);
			const float sample_rate = raw_stream.get_sample_rate();
			ScaleTrackStream quantized_stream(allocator, num_samples, sample_size, sample_rate, scale_format);

			for (uint32_t sample_index = 0; sample_index < num_samples; ++sample_index)
			{
				const rtm::vector4f scale = raw_stream.get_raw_sample<rtm::vector4f>(sample_index);
				uint8_t* quantized_ptr = quantized_stream.get_raw_sample_ptr(sample_index);

				switch (scale_format)
				{
				case vector_format8::vector3f_full:
					pack_vector3_96(scale, quantized_ptr);
					break;
				case vector_format8::vector3f_variable:
				default:
					ACL_ASSERT(false, "Invalid or unsupported vector format: %s", get_vector_format_name(scale_format));
					break;
				}
			}

			out_quantized_stream = std::move(quantized_stream);
		}

		inline void quantize_fixed_scale_stream(QuantizationContext& context, uint16_t bone_index, vector_format8 scale_format)
		{
			ACL_ASSERT(bone_index < context.num_bones, "Invalid bone index: %u", bone_index);

			BoneStreams& bone_stream = context.bone_streams[bone_index];

			// Default tracks aren't quantized
			if (bone_stream.is_scale_default)
				return;

			// Constant scale tracks store the remaining sample with full precision
			const vector_format8 format = bone_stream.is_scale_constant ? vector_format8::vector3f_full : scale_format;

			quantize_fixed_scale_stream(context.allocator, bone_stream.scales, format, bone_stream.scales);
		}

		inline void quantize_variable_scale_stream(QuantizationContext& context, const ScaleTrackStream& raw_clip_stream, const ScaleTrackStream& raw_segment_stream, const TrackStreamRange& clip_range, uint8_t bit_rate, ScaleTrackStream& out_quantized_stream)
		{
			// We expect all our samples to have the same width of sizeof(rtm::vector4f)
			ACL_ASSERT(raw_segment_stream.get_sample_size() == sizeof(rtm::vector4f), "Unexpected scale sample size. %u != %zu", raw_segment_stream.get_sample_size(), sizeof(rtm::vector4f));
			ACL_ASSERT(raw_segment_stream.get_vector_format() == vector_format8::vector3f_full, "Expected a vector3f_full vector format, found: %s", get_vector_format_name(raw_segment_stream.get_vector_format()));

			const uint32_t num_samples = is_constant_bit_rate(bit_rate) ? 1 : raw_segment_stream.get_num_samples();
			const uint32_t sample_size = sizeof(uint64_t) * 2;
			const float sample_rate = raw_segment_stream.get_sample_rate();
			ScaleTrackStream quantized_stream(context.allocator, num_samples, sample_size, sample_rate, vector_format8::vector3f_variable, bit_rate);

			if (is_constant_bit_rate(bit_rate))
			{
				const rtm::vector4f scale = raw_clip_stream.get_raw_sample<rtm::vector4f>(context.segment_sample_start_index);
				const rtm::vector4f normalized_scale = normalize_sample(scale, clip_range);

				uint8_t* quantized_ptr = quantized_stream.get_raw_sample_ptr(0);
				pack_vector3_u48_unsafe(normalized_scale, quantized_ptr);
			}
			else
			{
				const uint32_t num_bits_at_bit_rate = get_num_bits_at_bit_rate(bit_rate);

				for (uint32_t sample_index = 0; sample_index < num_samples; ++sample_index)
				{
					uint8_t* quantized_ptr = quantized_stream.get_raw_sample_ptr(sample_index);

					if (is_raw_bit_rate(bit_rate))
					{
						const rtm::vector4f scale = raw_clip_stream.get_raw_sample<rtm::vector4f>(context.segment_sample_start_index + sample_index);
						pack_vector3_96(scale, quantized_ptr);
					}
					else
					{
						const rtm::vector4f scale = raw_segment_stream.get_raw_sample<rtm::vector4f>(sample_index);
						pack_vector3_uXX_unsafe(scale, num_bits_at_bit_rate, quantized_ptr);
					}
				}
			}

			out_quantized_stream = std::move(quantized_stream);
		}

		inline void quantize_variable_scale_stream(QuantizationContext& context, uint16_t bone_index, uint8_t bit_rate)
		{
			ACL_ASSERT(bone_index < context.num_bones, "Invalid bone index: %u", bone_index);

			BoneStreams& bone_stream = context.bone_streams[bone_index];

			// Default tracks aren't quantized
			if (bone_stream.is_scale_default)
				return;

			const TrackStreamRange& bone_range = context.clip.ranges[bone_index].scale;
			const BoneStreams& raw_bone_stream = context.raw_bone_streams[bone_index];

			// Constant scale tracks store the remaining sample with full precision
			if (bone_stream.is_scale_constant)
				quantize_fixed_scale_stream(context.allocator, bone_stream.scales, vector_format8::vector3f_full, bone_stream.scales);
			else
				quantize_variable_scale_stream(context, raw_bone_stream.scales, bone_stream.scales, bone_range, bit_rate, bone_stream.scales);
		}

		enum class error_scan_stop_condition { until_error_too_high, until_end_of_segment };

		inline float calculate_max_error_at_bit_rate_local(QuantizationContext& context, uint32_t target_bone_index, error_scan_stop_condition stop_condition)
		{
			const itransform_error_metric* error_metric = context.error_metric;
			const bool needs_conversion = context.needs_conversion;
			const bool has_additive_base = context.has_additive_base;
			const transform_metadata& target_bone = context.metadata[target_bone_index];
			const uint32_t num_transforms = context.num_bones;
			const size_t sample_transform_size = context.metric_transform_size * context.num_bones;
			const float sample_rate = context.sample_rate;
			const float clip_duration = context.clip_duration;
			const rtm::scalarf error_threshold = rtm::scalar_set(context.error_threshold);
			const uint16_t target_bone_index_ = (uint16_t)target_bone_index;

			const auto convert_transforms_impl = std::mem_fn(context.has_scale ? &itransform_error_metric::convert_transforms : &itransform_error_metric::convert_transforms_no_scale);
			const auto apply_additive_to_base_impl = std::mem_fn(context.has_scale ? &itransform_error_metric::apply_additive_to_base : &itransform_error_metric::apply_additive_to_base_no_scale);
			const auto calculate_error_impl = std::mem_fn(context.has_scale ? &itransform_error_metric::calculate_error : &itransform_error_metric::calculate_error_no_scale);

			itransform_error_metric::convert_transforms_args convert_transforms_args_lossy;
			convert_transforms_args_lossy.dirty_transform_indices = &target_bone_index_;
			convert_transforms_args_lossy.num_dirty_transforms = 1;
			convert_transforms_args_lossy.transforms = context.lossy_local_pose;
			convert_transforms_args_lossy.num_transforms = num_transforms;

			itransform_error_metric::apply_additive_to_base_args apply_additive_to_base_args_lossy;
			apply_additive_to_base_args_lossy.dirty_transform_indices = &target_bone_index_;
			apply_additive_to_base_args_lossy.num_dirty_transforms = 1;
			apply_additive_to_base_args_lossy.local_transforms = needs_conversion ? (const void*)context.local_transforms_converted : (const void*)context.lossy_local_pose;
			apply_additive_to_base_args_lossy.base_transforms = nullptr;
			apply_additive_to_base_args_lossy.num_transforms = num_transforms;

			itransform_error_metric::calculate_error_args calculate_error_args;
			calculate_error_args.transform0 = nullptr;
			calculate_error_args.transform1 = needs_conversion ? (const void*)(context.local_transforms_converted + (context.metric_transform_size * target_bone_index)) : (const void*)(context.lossy_local_pose + target_bone_index);
			calculate_error_args.construct_sphere_shell(target_bone.shell_distance);

			const uint8_t* raw_transform = context.raw_local_transforms + (target_bone_index * context.metric_transform_size);
			const uint8_t* base_transforms = context.base_local_transforms;

			context.local_query.build(target_bone_index, context.bit_rate_per_bone[target_bone_index]);

			float sample_indexf = float(context.segment_sample_start_index);
			rtm::scalarf max_error = rtm::scalar_set(0.0F);

			for (uint32_t sample_index = 0; sample_index < context.num_samples; ++sample_index, sample_indexf += 1.0F)
			{
				// Sample our streams and calculate the error
				// The sample time is calculated from the full clip duration to be consistent with decompression
				const float sample_time = rtm::scalar_min(sample_indexf / sample_rate, clip_duration);

				context.bit_rate_database.sample(context.local_query, sample_time, context.lossy_local_pose, num_transforms);

				if (needs_conversion)
					convert_transforms_impl(error_metric, convert_transforms_args_lossy, context.local_transforms_converted);

				if (has_additive_base)
				{
					apply_additive_to_base_args_lossy.base_transforms = base_transforms;
					base_transforms += sample_transform_size;

					apply_additive_to_base_impl(error_metric, apply_additive_to_base_args_lossy, context.lossy_local_pose);
				}

				calculate_error_args.transform0 = raw_transform;
				raw_transform += sample_transform_size;

				const rtm::scalarf error = calculate_error_impl(error_metric, calculate_error_args);

				max_error = rtm::scalar_max(max_error, error);
				if (stop_condition == error_scan_stop_condition::until_error_too_high && rtm::scalar_greater_equal(error, error_threshold))
					break;
			}

			return rtm::scalar_cast(max_error);
		}

		inline float calculate_max_error_at_bit_rate_object(QuantizationContext& context, uint32_t target_bone_index, error_scan_stop_condition stop_condition)
		{
			const itransform_error_metric* error_metric = context.error_metric;
			const bool needs_conversion = context.needs_conversion;
			const bool has_additive_base = context.has_additive_base;
			const transform_metadata& target_bone = context.metadata[target_bone_index];
			const size_t sample_transform_size = context.metric_transform_size * context.num_bones;
			const float sample_rate = context.sample_rate;
			const float clip_duration = context.clip_duration;
			const rtm::scalarf error_threshold = rtm::scalar_set(context.error_threshold);

			const auto convert_transforms_impl = std::mem_fn(context.has_scale ? &itransform_error_metric::convert_transforms : &itransform_error_metric::convert_transforms_no_scale);
			const auto apply_additive_to_base_impl = std::mem_fn(context.has_scale ? &itransform_error_metric::apply_additive_to_base : &itransform_error_metric::apply_additive_to_base_no_scale);
			const auto local_to_object_space_impl = std::mem_fn(context.has_scale ? &itransform_error_metric::local_to_object_space : &itransform_error_metric::local_to_object_space_no_scale);
			const auto calculate_error_impl = std::mem_fn(context.has_scale ? &itransform_error_metric::calculate_error : &itransform_error_metric::calculate_error_no_scale);

			itransform_error_metric::convert_transforms_args convert_transforms_args_lossy;
			convert_transforms_args_lossy.dirty_transform_indices = context.chain_bone_indices;
			convert_transforms_args_lossy.num_dirty_transforms = context.num_bones_in_chain;
			convert_transforms_args_lossy.transforms = context.lossy_local_pose;
			convert_transforms_args_lossy.num_transforms = context.num_bones;

			itransform_error_metric::apply_additive_to_base_args apply_additive_to_base_args_lossy;
			apply_additive_to_base_args_lossy.dirty_transform_indices = context.chain_bone_indices;
			apply_additive_to_base_args_lossy.num_dirty_transforms = context.num_bones_in_chain;
			apply_additive_to_base_args_lossy.local_transforms = needs_conversion ? (const void*)(context.local_transforms_converted) : (const void*)context.lossy_local_pose;
			apply_additive_to_base_args_lossy.base_transforms = nullptr;
			apply_additive_to_base_args_lossy.num_transforms = context.num_bones;

			itransform_error_metric::local_to_object_space_args local_to_object_space_args_lossy;
			local_to_object_space_args_lossy.dirty_transform_indices = context.chain_bone_indices;
			local_to_object_space_args_lossy.num_dirty_transforms = context.num_bones_in_chain;
			local_to_object_space_args_lossy.parent_transform_indices = context.parent_transform_indices;
			local_to_object_space_args_lossy.local_transforms = needs_conversion ? (const void*)(context.local_transforms_converted) : (const void*)context.lossy_local_pose;
			local_to_object_space_args_lossy.num_transforms = context.num_bones;

			itransform_error_metric::calculate_error_args calculate_error_args;
			calculate_error_args.transform0 = nullptr;
			calculate_error_args.transform1 = context.lossy_object_pose + (target_bone_index * context.metric_transform_size);
			calculate_error_args.construct_sphere_shell(target_bone.shell_distance);

			const uint8_t* raw_transform = context.raw_object_transforms + (target_bone_index * context.metric_transform_size);
			const uint8_t* base_transforms = context.base_local_transforms;

			context.object_query.build(target_bone_index, context.bit_rate_per_bone, context.bone_streams);

			float sample_indexf = float(context.segment_sample_start_index);
			rtm::scalarf max_error = rtm::scalar_set(0.0F);

			for (uint32_t sample_index = 0; sample_index < context.num_samples; ++sample_index, sample_indexf += 1.0F)
			{
				// Sample our streams and calculate the error
				// The sample time is calculated from the full clip duration to be consistent with decompression
				const float sample_time = rtm::scalar_min(sample_indexf / sample_rate, clip_duration);

				context.bit_rate_database.sample(context.object_query, sample_time, context.lossy_local_pose, context.num_bones);

				if (needs_conversion)
					convert_transforms_impl(error_metric, convert_transforms_args_lossy, context.local_transforms_converted);

				if (has_additive_base)
				{
					apply_additive_to_base_args_lossy.base_transforms = base_transforms;
					base_transforms += sample_transform_size;

					apply_additive_to_base_impl(error_metric, apply_additive_to_base_args_lossy, context.lossy_local_pose);
				}

				local_to_object_space_impl(error_metric, local_to_object_space_args_lossy, context.lossy_object_pose);

				calculate_error_args.transform0 = raw_transform;
				raw_transform += sample_transform_size;

				const rtm::scalarf error = calculate_error_impl(error_metric, calculate_error_args);

				max_error = rtm::scalar_max(max_error, error);
				if (stop_condition == error_scan_stop_condition::until_error_too_high && rtm::scalar_greater_equal(error, error_threshold))
					break;
			}

			return rtm::scalar_cast(max_error);
		}

		inline void calculate_local_space_bit_rates(QuantizationContext& context)
		{
			// To minimize the bit rate, we first start by trying every permutation in local space
			// until our error is acceptable.
			// We try permutations from the lowest memory footprint to the highest.

			const uint32_t num_bones = context.num_bones;
			for (uint32_t bone_index = 0; bone_index < num_bones; ++bone_index)
			{
				// Update our error threshold
				const float error_threshold = context.metadata[bone_index].precision;
				context.error_threshold = error_threshold;

				// Bit rates at this point are one of three value:
				// 0: if the segment track is normalized, it can be constant within the segment
				// 1: if the segment track isn't normalized, it starts at the lowest bit rate
				// 255: if the track is constant/default for the whole clip
				const BoneBitRate bone_bit_rates = context.bit_rate_per_bone[bone_index];

				if (bone_bit_rates.rotation == k_invalid_bit_rate && bone_bit_rates.translation == k_invalid_bit_rate && bone_bit_rates.scale == k_invalid_bit_rate)
				{
#if ACL_IMPL_DEBUG_VARIABLE_QUANTIZATION
					printf("%u: Best bit rates: %u | %u | %u\n", bone_index, bone_bit_rates.rotation, bone_bit_rates.translation, bone_bit_rates.scale);
#endif
					continue;	// Every track bit rate is constant/default, nothing else to do
				}

				BoneBitRate best_bit_rates = bone_bit_rates;
				float best_error = 1.0E10F;
				uint32_t prev_transform_size = ~0U;
				bool is_error_good_enough = false;

				if (context.has_scale)
				{
					const size_t num_permutations = get_array_size(acl_impl::k_local_bit_rate_permutations);
					for (size_t permutation_index = 0; permutation_index < num_permutations; ++permutation_index)
					{
						const uint8_t rotation_bit_rate = acl_impl::k_local_bit_rate_permutations[permutation_index][0];
						if (bone_bit_rates.rotation == 1)
						{
							if (rotation_bit_rate == 0)
								continue;	// Skip permutations we aren't interested in
						}
						else if (bone_bit_rates.rotation == k_invalid_bit_rate)
						{
							if (rotation_bit_rate != 0)
								continue;	// Skip permutations we aren't interested in
						}

						const uint8_t translation_bit_rate = acl_impl::k_local_bit_rate_permutations[permutation_index][1];
						if (bone_bit_rates.translation == 1)
						{
							if (translation_bit_rate == 0)
								continue;	// Skip permutations we aren't interested in
						}
						else if (bone_bit_rates.translation == k_invalid_bit_rate)
						{
							if (translation_bit_rate != 0)
								continue;	// Skip permutations we aren't interested in
						}

						const uint8_t scale_bit_rate = acl_impl::k_local_bit_rate_permutations[permutation_index][2];
						if (bone_bit_rates.scale == 1)
						{
							if (scale_bit_rate == 0)
								continue;	// Skip permutations we aren't interested in
						}
						else if (bone_bit_rates.scale == k_invalid_bit_rate)
						{
							if (scale_bit_rate != 0)
								continue;	// Skip permutations we aren't interested in
						}

						const uint32_t rotation_size = get_num_bits_at_bit_rate(rotation_bit_rate);
						const uint32_t translation_size = get_num_bits_at_bit_rate(translation_bit_rate);
						const uint32_t scale_size = get_num_bits_at_bit_rate(scale_bit_rate);
						const uint32_t transform_size = rotation_size + translation_size + scale_size;

						if (transform_size != prev_transform_size && is_error_good_enough)
						{
							// We already found the lowest transform size and we tried every permutation with that same size
							break;
						}

						prev_transform_size = transform_size;

						context.bit_rate_per_bone[bone_index].rotation = bone_bit_rates.rotation != k_invalid_bit_rate ? rotation_bit_rate : k_invalid_bit_rate;
						context.bit_rate_per_bone[bone_index].translation = bone_bit_rates.translation != k_invalid_bit_rate ? translation_bit_rate : k_invalid_bit_rate;
						context.bit_rate_per_bone[bone_index].scale = bone_bit_rates.scale != k_invalid_bit_rate ? scale_bit_rate : k_invalid_bit_rate;

						const float error = calculate_max_error_at_bit_rate_local(context, bone_index, error_scan_stop_condition::until_error_too_high);

#if ACL_IMPL_DEBUG_VARIABLE_QUANTIZATION > 1
						printf("%u: %u | %u | %u (%u) = %f\n", bone_index, rotation_bit_rate, translation_bit_rate, scale_bit_rate, transform_size, error);
#endif

						if (error < best_error)
						{
							best_error = error;
							best_bit_rates = context.bit_rate_per_bone[bone_index];
							is_error_good_enough = error < error_threshold;
						}
					}
				}
				else
				{
					const size_t num_permutations = get_array_size(acl_impl::k_local_bit_rate_permutations_no_scale);
					for (size_t permutation_index = 0; permutation_index < num_permutations; ++permutation_index)
					{
						const uint8_t rotation_bit_rate = acl_impl::k_local_bit_rate_permutations_no_scale[permutation_index][0];
						if (bone_bit_rates.rotation == 1)
						{
							if (rotation_bit_rate == 0)
								continue;	// Skip permutations we aren't interested in
						}
						else if (bone_bit_rates.rotation == k_invalid_bit_rate)
						{
							if (rotation_bit_rate != 0)
								continue;	// Skip permutations we aren't interested in
						}

						const uint8_t translation_bit_rate = acl_impl::k_local_bit_rate_permutations_no_scale[permutation_index][1];
						if (bone_bit_rates.translation == 1)
						{
							if (translation_bit_rate == 0)
								continue;	// Skip permutations we aren't interested in
						}
						else if (bone_bit_rates.translation == k_invalid_bit_rate)
						{
							if (translation_bit_rate != 0)
								continue;	// Skip permutations we aren't interested in
						}

						const uint32_t rotation_size = get_num_bits_at_bit_rate(rotation_bit_rate);
						const uint32_t translation_size = get_num_bits_at_bit_rate(translation_bit_rate);
						const uint32_t transform_size = rotation_size + translation_size;

						if (transform_size != prev_transform_size && is_error_good_enough)
						{
							// We already found the lowest transform size and we tried every permutation with that same size
							break;
						}

						prev_transform_size = transform_size;

						context.bit_rate_per_bone[bone_index].rotation = bone_bit_rates.rotation != k_invalid_bit_rate ? rotation_bit_rate : k_invalid_bit_rate;
						context.bit_rate_per_bone[bone_index].translation = bone_bit_rates.translation != k_invalid_bit_rate ? translation_bit_rate : k_invalid_bit_rate;

						const float error = calculate_max_error_at_bit_rate_local(context, bone_index, error_scan_stop_condition::until_error_too_high);

#if ACL_IMPL_DEBUG_VARIABLE_QUANTIZATION > 1
						printf("%u: %u | %u | %u (%u) = %f\n", bone_index, rotation_bit_rate, translation_bit_rate, k_invalid_bit_rate, transform_size, error);
#endif

						if (error < best_error)
						{
							best_error = error;
							best_bit_rates = context.bit_rate_per_bone[bone_index];
							is_error_good_enough = error < error_threshold;
						}
					}
				}

#if ACL_IMPL_DEBUG_VARIABLE_QUANTIZATION
				printf("%u: Best bit rates: %u | %u | %u\n", bone_index, best_bit_rates.rotation, best_bit_rates.translation, best_bit_rates.scale);
#endif

				context.bit_rate_per_bone[bone_index] = best_bit_rates;
			}
		}

		constexpr uint32_t increment_and_clamp_bit_rate(uint32_t bit_rate, uint32_t increment)
		{
			return bit_rate >= k_highest_bit_rate ? bit_rate : std::min<uint32_t>(bit_rate + increment, k_highest_bit_rate);
		}

		inline float increase_bone_bit_rate(QuantizationContext& context, uint32_t bone_index, uint32_t num_increments, float old_error, BoneBitRate& out_best_bit_rates)
		{
			const BoneBitRate bone_bit_rates = context.bit_rate_per_bone[bone_index];
			const uint32_t num_scale_increments = context.has_scale ? num_increments : 0;
			const uint16_t bone_index_ = static_cast<uint16_t>(bone_index);

			BoneBitRate best_bit_rates = bone_bit_rates;
			float best_error = old_error;

			for (uint32_t rotation_increment = 0; rotation_increment <= num_increments; ++rotation_increment)
			{
				const uint32_t rotation_bit_rate = increment_and_clamp_bit_rate(bone_bit_rates.rotation, rotation_increment);

				for (uint32_t translation_increment = 0; translation_increment <= num_increments; ++translation_increment)
				{
					const uint32_t translation_bit_rate = increment_and_clamp_bit_rate(bone_bit_rates.translation, translation_increment);

					for (uint32_t scale_increment = 0; scale_increment <= num_scale_increments; ++scale_increment)
					{
						const uint32_t scale_bit_rate = increment_and_clamp_bit_rate(bone_bit_rates.scale, scale_increment);

						if (rotation_increment + translation_increment + scale_increment != num_increments)
						{
							if (scale_bit_rate >= k_highest_bit_rate)
								break;
							else
								continue;
						}

						context.bit_rate_per_bone[bone_index] = BoneBitRate{ (uint8_t)rotation_bit_rate, (uint8_t)translation_bit_rate, (uint8_t)scale_bit_rate };
						const float error = calculate_max_error_at_bit_rate_object(context, bone_index_, error_scan_stop_condition::until_error_too_high);

						if (error < best_error)
						{
							best_error = error;
							best_bit_rates = context.bit_rate_per_bone[bone_index];
						}

						context.bit_rate_per_bone[bone_index] = bone_bit_rates;

						if (scale_bit_rate >= k_highest_bit_rate)
							break;
					}

					if (translation_bit_rate >= k_highest_bit_rate)
						break;
				}

				if (rotation_bit_rate >= k_highest_bit_rate)
					break;
			}

			out_best_bit_rates = best_bit_rates;
			return best_error;
		}

		inline float calculate_bone_permutation_error(QuantizationContext& context, BoneBitRate* permutation_bit_rates, uint8_t* bone_chain_permutation, uint32_t bone_index, BoneBitRate* best_bit_rates, float old_error)
		{
			const float error_threshold = context.error_threshold;
			float best_error = old_error;

			do
			{
				// Copy our current bit rates to the permutation rates
				std::memcpy(permutation_bit_rates, context.bit_rate_per_bone, sizeof(BoneBitRate) * context.num_bones);

				bool is_permutation_valid = false;
				const uint32_t num_bones_in_chain = context.num_bones_in_chain;
				for (uint32_t chain_link_index = 0; chain_link_index < num_bones_in_chain; ++chain_link_index)
				{
					if (bone_chain_permutation[chain_link_index] != 0)
					{
						// Increase bit rate
						const uint32_t chain_bone_index = context.chain_bone_indices[chain_link_index];
						BoneBitRate chain_bone_best_bit_rates;
						increase_bone_bit_rate(context, chain_bone_index, bone_chain_permutation[chain_link_index], old_error, chain_bone_best_bit_rates);
						is_permutation_valid |= chain_bone_best_bit_rates.rotation != permutation_bit_rates[chain_bone_index].rotation;
						is_permutation_valid |= chain_bone_best_bit_rates.translation != permutation_bit_rates[chain_bone_index].translation;
						is_permutation_valid |= chain_bone_best_bit_rates.scale != permutation_bit_rates[chain_bone_index].scale;
						permutation_bit_rates[chain_bone_index] = chain_bone_best_bit_rates;
					}
				}

				if (!is_permutation_valid)
					continue;	// Couldn't increase any bit rate, skip this permutation

				// Measure error
				std::swap(context.bit_rate_per_bone, permutation_bit_rates);
				const float permutation_error = calculate_max_error_at_bit_rate_object(context, bone_index, error_scan_stop_condition::until_error_too_high);
				std::swap(context.bit_rate_per_bone, permutation_bit_rates);

				if (permutation_error < best_error)
				{
					best_error = permutation_error;
					std::memcpy(best_bit_rates, permutation_bit_rates, sizeof(BoneBitRate) * context.num_bones);

					if (permutation_error < error_threshold)
						break;
				}
			} while (std::next_permutation(bone_chain_permutation, bone_chain_permutation + context.num_bones_in_chain));

			return best_error;
		}

		inline uint32_t calculate_bone_chain_indices(const ClipContext& clip, uint32_t bone_index, uint16_t* out_chain_bone_indices)
		{
			const BoneChain bone_chain = clip.get_bone_chain(bone_index);

			uint32_t num_bones_in_chain = 0;
			for (uint16_t chain_bone_index : bone_chain)
				out_chain_bone_indices[num_bones_in_chain++] = chain_bone_index;

			return num_bones_in_chain;
		}

		inline void initialize_bone_bit_rates(const SegmentContext& segment, rotation_format8 rotation_format, vector_format8 translation_format, vector_format8 scale_format, BoneBitRate* out_bit_rate_per_bone)
		{
			const bool is_rotation_variable = is_rotation_format_variable(rotation_format);
			const bool is_translation_variable = is_vector_format_variable(translation_format);
			const bool is_scale_variable = segment_context_has_scale(segment) && is_vector_format_variable(scale_format);

			const uint32_t num_bones = segment.num_bones;
			for (uint32_t bone_index = 0; bone_index < num_bones; ++bone_index)
			{
				BoneBitRate& bone_bit_rate = out_bit_rate_per_bone[bone_index];

				const bool rotation_supports_constant_tracks = segment.are_rotations_normalized;
				if (is_rotation_variable && !segment.bone_streams[bone_index].is_rotation_constant)
					bone_bit_rate.rotation = rotation_supports_constant_tracks ? 0 : k_lowest_bit_rate;
				else
					bone_bit_rate.rotation = k_invalid_bit_rate;

				const bool translation_supports_constant_tracks = segment.are_translations_normalized;
				if (is_translation_variable && !segment.bone_streams[bone_index].is_translation_constant)
					bone_bit_rate.translation = translation_supports_constant_tracks ? 0 : k_lowest_bit_rate;
				else
					bone_bit_rate.translation = k_invalid_bit_rate;

				const bool scale_supports_constant_tracks = segment.are_scales_normalized;
				if (is_scale_variable && !segment.bone_streams[bone_index].is_scale_constant)
					bone_bit_rate.scale = scale_supports_constant_tracks ? 0 : k_lowest_bit_rate;
				else
					bone_bit_rate.scale = k_invalid_bit_rate;
			}
		}

		inline void quantize_all_streams(QuantizationContext& context)
		{
			ACL_ASSERT(context.is_valid(), "QuantizationContext isn't valid");

			const bool is_rotation_variable = is_rotation_format_variable(context.rotation_format);
			const bool is_translation_variable = is_vector_format_variable(context.translation_format);
			const bool is_scale_variable = is_vector_format_variable(context.scale_format);

			for (uint16_t bone_index = 0; bone_index < context.num_bones; ++bone_index)
			{
				const BoneBitRate& bone_bit_rate = context.bit_rate_per_bone[bone_index];

				if (is_rotation_variable)
					quantize_variable_rotation_stream(context, bone_index, bone_bit_rate.rotation);
				else
					quantize_fixed_rotation_stream(context, bone_index, context.rotation_format);

				if (is_translation_variable)
					quantize_variable_translation_stream(context, bone_index, bone_bit_rate.translation);
				else
					quantize_fixed_translation_stream(context, bone_index, context.translation_format);

				if (context.has_scale)
				{
					if (is_scale_variable)
						quantize_variable_scale_stream(context, bone_index, bone_bit_rate.scale);
					else
						quantize_fixed_scale_stream(context, bone_index, context.scale_format);
				}
			}
		}

		inline void find_optimal_bit_rates(QuantizationContext& context)
		{
			ACL_ASSERT(context.is_valid(), "QuantizationContext isn't valid");

			initialize_bone_bit_rates(*context.segment, context.rotation_format, context.translation_format, context.scale_format, context.bit_rate_per_bone);

			// First iterate over all bones and find the optimal bit rate for each track using the local space error.
			// We use the local space error to prime the algorithm. If each parent bone has infinite precision,
			// the local space error is equivalent. Since parents are lossy, it is a good approximation. It means
			// that whatever bit rate we find for a bone, it cannot be lower to reach our error threshold since
			// a lossy parent means we need to be equally or more accurate to maintain the threshold.
			//
			// In practice, the error from a child can compensate the error introduced by the parent but
			// this is unlikely to hold true for a whole track at every key. We thus make the assumption
			// that increasing the precision is always good regardless of the hierarchy level.

			calculate_local_space_bit_rates(context);

			// Now that we found an approximate lower bound for the bit rates, we start at the root and perform a brute force search.
			// For each bone, we do the following:
			//    - If object space error meets our error threshold, do nothing
			//    - Iterate over each bone in the chain and increment the bit rate by 1 (rotation or translation, pick lowest error)
			//    - Pick the bone that improved the error the most and increment the bit rate by 1
			//    - Repeat until we meet our error threshold
			//
			// The root is already optimal from the previous step since the local space error is equal to the object space error.
			// Next we'll add one bone to the chain under the root. Performing the above steps, we perform an exhaustive search
			// to find the smallest memory footprint that will meet our error threshold. No combination with a lower memory footprint
			// could yield a smaller error.
			// Next we'll add another bone to the chain. By performing these steps recursively, we can ensure that the accuracy always
			// increases and the memory footprint is always as low as possible.

			// 3 bone chain expansion:
			// 3:	[bone 0] + 1 [bone 1] + 0 [bone 2] + 0 (3)
			//		[bone 0] + 0 [bone 1] + 1 [bone 2] + 0 (3)
			//		[bone 0] + 0 [bone 1] + 0 [bone 2] + 1 (3)
			// 6:	[bone 0] + 2 [bone 1] + 0 [bone 2] + 0 (6)
			//		[bone 0] + 1 [bone 1] + 1 [bone 2] + 0 (6)
			//		[bone 0] + 1 [bone 1] + 0 [bone 2] + 1 (6)
			//		[bone 0] + 0 [bone 1] + 1 [bone 2] + 1 (6)
			//		[bone 0] + 0 [bone 1] + 2 [bone 2] + 0 (6)
			//		[bone 0] + 0 [bone 1] + 0 [bone 2] + 2 (6)
			//10:	[bone 0] + 3 [bone 1] + 0 [bone 2] + 0 (9)
			//		[bone 0] + 2 [bone 1] + 1 [bone 2] + 0 (9)
			//		[bone 0] + 2 [bone 1] + 0 [bone 2] + 1 (9)
			//		[bone 0] + 1 [bone 1] + 2 [bone 2] + 0 (9)
			//		[bone 0] + 1 [bone 1] + 1 [bone 2] + 1 (9)
			//		[bone 0] + 1 [bone 1] + 0 [bone 2] + 2 (9)
			//		[bone 0] + 0 [bone 1] + 3 [bone 2] + 0 (9)
			//		[bone 0] + 0 [bone 1] + 2 [bone 2] + 1 (9)
			//		[bone 0] + 0 [bone 1] + 1 [bone 2] + 2 (9)
			//		[bone 0] + 0 [bone 1] + 0 [bone 2] + 3 (9)

			uint8_t* bone_chain_permutation = allocate_type_array<uint8_t>(context.allocator, context.num_bones);
			BoneBitRate* permutation_bit_rates = allocate_type_array<BoneBitRate>(context.allocator, context.num_bones);
			BoneBitRate* best_permutation_bit_rates = allocate_type_array<BoneBitRate>(context.allocator, context.num_bones);
			BoneBitRate* best_bit_rates = allocate_type_array<BoneBitRate>(context.allocator, context.num_bones);
			std::memcpy(best_bit_rates, context.bit_rate_per_bone, sizeof(BoneBitRate) * context.num_bones);

			const uint32_t num_bones = context.num_bones;
			for (uint32_t bone_index = 0; bone_index < num_bones; ++bone_index)
			{
				// Update our error threshold
				const float error_threshold = context.metadata[bone_index].precision;
				context.error_threshold = error_threshold;

				const uint32_t num_bones_in_chain = calculate_bone_chain_indices(context.clip, bone_index, context.chain_bone_indices);
				context.num_bones_in_chain = (uint16_t)num_bones_in_chain;

				float error = calculate_max_error_at_bit_rate_object(context, bone_index, error_scan_stop_condition::until_error_too_high);
				if (error < error_threshold)
					continue;

				const float initial_error = error;

				while (error >= error_threshold)
				{
					// Generate permutations for up to 3 bit rate increments
					// Perform an exhaustive search of the permutations and pick the best result
					// If our best error is under the threshold, we are done, otherwise we will try again from there
					const float original_error = error;
					float best_error = error;

					// The first permutation increases the bit rate of a single track/bone
					std::fill(bone_chain_permutation, bone_chain_permutation + num_bones, uint8_t(0));
					bone_chain_permutation[num_bones_in_chain - 1] = 1;
					error = calculate_bone_permutation_error(context, permutation_bit_rates, bone_chain_permutation, bone_index, best_permutation_bit_rates, original_error);
					if (error < best_error)
					{
						best_error = error;
						std::memcpy(best_bit_rates, best_permutation_bit_rates, sizeof(BoneBitRate) * num_bones);

						if (error < error_threshold)
							break;
					}

					if (context.compression_level >= compression_level8::high)
					{
						// The second permutation increases the bit rate of 2 track/bones
						std::fill(bone_chain_permutation, bone_chain_permutation + num_bones, uint8_t(0));
						bone_chain_permutation[num_bones_in_chain - 1] = 2;
						error = calculate_bone_permutation_error(context, permutation_bit_rates, bone_chain_permutation, bone_index, best_permutation_bit_rates, original_error);
						if (error < best_error)
						{
							best_error = error;
							std::memcpy(best_bit_rates, best_permutation_bit_rates, sizeof(BoneBitRate) * num_bones);

							if (error < error_threshold)
								break;
						}

						if (num_bones_in_chain > 1)
						{
							std::fill(bone_chain_permutation, bone_chain_permutation + num_bones, uint8_t(0));
							bone_chain_permutation[num_bones_in_chain - 2] = 1;
							bone_chain_permutation[num_bones_in_chain - 1] = 1;
							error = calculate_bone_permutation_error(context, permutation_bit_rates, bone_chain_permutation, bone_index, best_permutation_bit_rates, original_error);
							if (error < best_error)
							{
								best_error = error;
								std::memcpy(best_bit_rates, best_permutation_bit_rates, sizeof(BoneBitRate) * num_bones);

								if (error < error_threshold)
									break;
							}
						}
					}

					if (context.compression_level >= compression_level8::highest)
					{
						// The third permutation increases the bit rate of 3 track/bones
						std::fill(bone_chain_permutation, bone_chain_permutation + num_bones, uint8_t(0));
						bone_chain_permutation[num_bones_in_chain - 1] = 3;
						error = calculate_bone_permutation_error(context, permutation_bit_rates, bone_chain_permutation, bone_index, best_permutation_bit_rates, original_error);
						if (error < best_error)
						{
							best_error = error;
							std::memcpy(best_bit_rates, best_permutation_bit_rates, sizeof(BoneBitRate) * num_bones);

							if (error < error_threshold)
								break;
						}

						if (num_bones_in_chain > 1)
						{
							std::fill(bone_chain_permutation, bone_chain_permutation + num_bones, uint8_t(0));
							bone_chain_permutation[num_bones_in_chain - 2] = 2;
							bone_chain_permutation[num_bones_in_chain - 1] = 1;
							error = calculate_bone_permutation_error(context, permutation_bit_rates, bone_chain_permutation, bone_index, best_permutation_bit_rates, original_error);
							if (error < best_error)
							{
								best_error = error;
								std::memcpy(best_bit_rates, best_permutation_bit_rates, sizeof(BoneBitRate) * num_bones);

								if (error < error_threshold)
									break;
							}

							if (num_bones_in_chain > 2)
							{
								std::fill(bone_chain_permutation, bone_chain_permutation + num_bones, uint8_t(0));
								bone_chain_permutation[num_bones_in_chain - 3] = 1;
								bone_chain_permutation[num_bones_in_chain - 2] = 1;
								bone_chain_permutation[num_bones_in_chain - 1] = 1;
								error = calculate_bone_permutation_error(context, permutation_bit_rates, bone_chain_permutation, bone_index, best_permutation_bit_rates, original_error);
								if (error < best_error)
								{
									best_error = error;
									std::memcpy(best_bit_rates, best_permutation_bit_rates, sizeof(BoneBitRate) * num_bones);

									if (error < error_threshold)
										break;
								}
							}
						}
					}

					if (best_error >= original_error)
						break;	// No progress made

					error = best_error;
					if (error < original_error)
					{
#if ACL_IMPL_DEBUG_VARIABLE_QUANTIZATION
						std::swap(context.bit_rate_per_bone, best_bit_rates);
						float new_error = calculate_max_error_at_bit_rate_object(context, bone_index, error_scan_stop_condition::until_end_of_segment);
						std::swap(context.bit_rate_per_bone, best_bit_rates);

						for (uint16_t i = 0; i < context.num_bones; ++i)
						{
							const BoneBitRate& bone_bit_rate = context.bit_rate_per_bone[i];
							const BoneBitRate& best_bone_bit_rate = best_bit_rates[i];
							bool rotation_differs = bone_bit_rate.rotation != best_bone_bit_rate.rotation;
							bool translation_differs = bone_bit_rate.translation != best_bone_bit_rate.translation;
							bool scale_differs = bone_bit_rate.scale != best_bone_bit_rate.scale;
							if (rotation_differs || translation_differs || scale_differs)
								printf("%u: %u | %u | %u => %u  %u %u (%f)\n", i, bone_bit_rate.rotation, bone_bit_rate.translation, bone_bit_rate.scale, best_bone_bit_rate.rotation, best_bone_bit_rate.translation, best_bone_bit_rate.scale, new_error);
						}
#endif

						std::memcpy(context.bit_rate_per_bone, best_bit_rates, sizeof(BoneBitRate) * num_bones);
					}
				}

				if (error < initial_error)
				{
#if ACL_IMPL_DEBUG_VARIABLE_QUANTIZATION
					std::swap(context.bit_rate_per_bone, best_bit_rates);
					float new_error = calculate_max_error_at_bit_rate_object(context, bone_index, error_scan_stop_condition::until_end_of_segment);
					std::swap(context.bit_rate_per_bone, best_bit_rates);

					for (uint16_t i = 0; i < context.num_bones; ++i)
					{
						const BoneBitRate& bone_bit_rate = context.bit_rate_per_bone[i];
						const BoneBitRate& best_bone_bit_rate = best_bit_rates[i];
						bool rotation_differs = bone_bit_rate.rotation != best_bone_bit_rate.rotation;
						bool translation_differs = bone_bit_rate.translation != best_bone_bit_rate.translation;
						bool scale_differs = bone_bit_rate.scale != best_bone_bit_rate.scale;
						if (rotation_differs || translation_differs || scale_differs)
							printf("%u: %u | %u | %u => %u  %u %u (%f)\n", i, bone_bit_rate.rotation, bone_bit_rate.translation, bone_bit_rate.scale, best_bone_bit_rate.rotation, best_bone_bit_rate.translation, best_bone_bit_rate.scale, new_error);
					}
#endif

					std::memcpy(context.bit_rate_per_bone, best_bit_rates, sizeof(BoneBitRate) * num_bones);
				}

				// Our error remains too high, this should be rare.
				// Attempt to increase the bit rate as much as we can while still back tracking if it doesn't help.
				error = calculate_max_error_at_bit_rate_object(context, bone_index, error_scan_stop_condition::until_end_of_segment);
				while (error >= error_threshold)
				{
					// From child to parent, increase the bit rate indiscriminately
					uint32_t num_maxed_out = 0;
					for (int32_t chain_link_index = num_bones_in_chain - 1; chain_link_index >= 0; --chain_link_index)
					{
						const uint32_t chain_bone_index = context.chain_bone_indices[chain_link_index];

						// Work with a copy. We'll increase the bit rate as much as we can and retain the values
						// that yield the smallest error BUT increasing the bit rate does NOT always means
						// that the error will reduce and improve. It could get worse in which case we'll do nothing.

						BoneBitRate& bone_bit_rate = context.bit_rate_per_bone[chain_bone_index];

						// Copy original values
						BoneBitRate best_bone_bit_rate = bone_bit_rate;
						float best_bit_rate_error = error;

						while (error >= error_threshold)
						{
							static_assert(offsetof(BoneBitRate, rotation) == 0 && offsetof(BoneBitRate, scale) == sizeof(BoneBitRate) - 1, "Invalid BoneBitRate offsets");
							uint8_t& smallest_bit_rate = *std::min_element<uint8_t*>(&bone_bit_rate.rotation, &bone_bit_rate.scale + 1);

							if (smallest_bit_rate >= k_highest_bit_rate)
							{
								num_maxed_out++;
								break;
							}

							// If rotation == translation and translation has room, bias translation
							// This seems to yield an overall tiny win but it isn't always the case.
							// TODO: Brute force this?
							if (bone_bit_rate.rotation == bone_bit_rate.translation && bone_bit_rate.translation < k_highest_bit_rate && bone_bit_rate.scale >= k_highest_bit_rate)
								bone_bit_rate.translation++;
							else
								smallest_bit_rate++;

							ACL_ASSERT((bone_bit_rate.rotation <= k_highest_bit_rate || bone_bit_rate.rotation == k_invalid_bit_rate) && (bone_bit_rate.translation <= k_highest_bit_rate || bone_bit_rate.translation == k_invalid_bit_rate) && (bone_bit_rate.scale <= k_highest_bit_rate || bone_bit_rate.scale == k_invalid_bit_rate), "Invalid bit rate! [%u, %u, %u]", bone_bit_rate.rotation, bone_bit_rate.translation, bone_bit_rate.scale);

							error = calculate_max_error_at_bit_rate_object(context, bone_index, error_scan_stop_condition::until_end_of_segment);

							if (error < best_bit_rate_error)
							{
								best_bone_bit_rate = bone_bit_rate;
								best_bit_rate_error = error;

#if ACL_IMPL_DEBUG_VARIABLE_QUANTIZATION
								printf("%u: => %u %u %u (%f)\n", chain_bone_index, bone_bit_rate.rotation, bone_bit_rate.translation, bone_bit_rate.scale, error);
								for (uint32_t i = chain_link_index + 1; i < num_bones_in_chain; ++i)
								{
									const uint16_t chain_bone_index2 = context.chain_bone_indices[chain_link_index];
									float error2 = calculate_max_error_at_bit_rate_object(context, chain_bone_index2, error_scan_stop_condition::until_end_of_segment);
									printf("  %u: => (%f)\n", i, error2);
								}
#endif
							}
						}

						// Only retain the lowest error bit rates
						bone_bit_rate = best_bone_bit_rate;
						error = best_bit_rate_error;

						if (error < error_threshold)
							break;
					}

					if (num_maxed_out == num_bones_in_chain)
						break;

					// TODO: Try to lower the bit rate again in the reverse direction?
				}

				// Despite our best efforts, we failed to meet the threshold with our heuristics.
				// No longer attempt to find what is best for size, max out the bit rates until we meet the threshold.
				// Only do this if the rotation format is full precision quaternions. This last step is not guaranteed
				// to reach the error threshold but it will very likely increase the memory footprint. Even if we do
				// reach the error threshold for the given bone, another sibling bone already processed might now
				// have an error higher than it used to if quantization caused its error to compensate. More often than
				// not, sibling bones will remain fairly close in their error. Some packed rotation formats, namely
				// drop W component can have a high error even with raw values, it is assumed that if such a format
				// is used then a best effort approach to reach the error threshold is entirely fine.
				if (error >= error_threshold && context.rotation_format == rotation_format8::quatf_full)
				{
					// From child to parent, max out the bit rate
					for (int32_t chain_link_index = num_bones_in_chain - 1; chain_link_index >= 0; --chain_link_index)
					{
						const uint32_t chain_bone_index = context.chain_bone_indices[chain_link_index];
						BoneBitRate& bone_bit_rate = context.bit_rate_per_bone[chain_bone_index];
						bone_bit_rate.rotation = std::max<uint8_t>(bone_bit_rate.rotation, k_highest_bit_rate);
						bone_bit_rate.translation = std::max<uint8_t>(bone_bit_rate.translation, k_highest_bit_rate);
						bone_bit_rate.scale = std::max<uint8_t>(bone_bit_rate.scale, k_highest_bit_rate);

						error = calculate_max_error_at_bit_rate_object(context, bone_index, error_scan_stop_condition::until_end_of_segment);
						if (error < error_threshold)
							break;
					}
				}
			}

#if ACL_IMPL_DEBUG_VARIABLE_QUANTIZATION
			printf("Variable quantization optimization results:\n");
			for (uint16_t i = 0; i < context.num_bones; ++i)
			{
				float error = calculate_max_error_at_bit_rate_object(context, i, error_scan_stop_condition::until_end_of_segment);
				const BoneBitRate& bone_bit_rate = context.bit_rate_per_bone[i];
				printf("%u: %u | %u | %u => %f %s\n", i, bone_bit_rate.rotation, bone_bit_rate.translation, bone_bit_rate.scale, error, error >= error_threshold ? "!" : "");
			}
#endif

			deallocate_type_array(context.allocator, bone_chain_permutation, num_bones);
			deallocate_type_array(context.allocator, permutation_bit_rates, num_bones);
			deallocate_type_array(context.allocator, best_permutation_bit_rates, num_bones);
			deallocate_type_array(context.allocator, best_bit_rates, num_bones);
		}

		inline void quantize_streams(IAllocator& allocator, ClipContext& clip_context, const CompressionSettings& settings, const ClipContext& raw_clip_context, const ClipContext& additive_base_clip_context, OutputStats& out_stats)
		{
			(void)out_stats;

			const bool is_rotation_variable = is_rotation_format_variable(settings.rotation_format);
			const bool is_translation_variable = is_vector_format_variable(settings.translation_format);
			const bool is_scale_variable = is_vector_format_variable(settings.scale_format);
			const bool is_any_variable = is_rotation_variable || is_translation_variable || is_scale_variable;

			QuantizationContext context(allocator, clip_context, raw_clip_context, additive_base_clip_context, settings);

			for (SegmentContext& segment : clip_context.segment_iterator())
			{
#if ACL_IMPL_DEBUG_VARIABLE_QUANTIZATION
				printf("Quantizing segment %u...\n", segment.segment_index);
#endif

#if ACL_IMPL_PROFILE_MATH
				{
					ScopeProfiler timer;

					for (int32_t i = 0; i < 10; ++i)
					{
						context.set_segment(segment);

						if (is_any_variable)
							find_optimal_bit_rates(context);
					}

					timer.stop();

#if defined(__ANDROID__)
					__android_log_print(ANDROID_LOG_INFO, "acl", "Quantization optimization for segment %u took: %.4f ms", segment.segment_index, timer.get_elapsed_milliseconds());
#else
					printf("Quantization optimization for segment %u took: %.4f ms\n", segment.segment_index, timer.get_elapsed_milliseconds());
#endif
				}
#endif

				context.set_segment(segment);

				if (is_any_variable)
					find_optimal_bit_rates(context);

				// Quantize our streams now that we found the optimal bit rates
				quantize_all_streams(context);
			}

#if defined(SJSON_CPP_WRITER)
			if (are_all_enum_flags_set(out_stats.logging, StatLogging::Detailed))
			{
				sjson::ObjectWriter& writer = *out_stats.writer;
				writer["track_bit_rate_database_size"] = static_cast<uint32_t>(context.bit_rate_database.get_allocated_size());

				size_t transform_cache_size = 0;
				transform_cache_size += sizeof(rtm::qvvf) * context.num_bones;	// raw_local_pose
				transform_cache_size += sizeof(rtm::qvvf) * context.num_bones;	// lossy_local_pose
				transform_cache_size += context.metric_transform_size * context.num_bones;	// lossy_object_pose
				transform_cache_size += context.metric_transform_size * context.num_bones * context.clip.segments->num_samples;	// raw_local_transforms
				transform_cache_size += context.metric_transform_size * context.num_bones * context.clip.segments->num_samples;	// raw_object_transforms

				if (context.needs_conversion)
					transform_cache_size += context.metric_transform_size * context.num_bones;	// local_transforms_converted

				if (context.has_additive_base)
				{
					transform_cache_size += sizeof(rtm::qvvf) * context.num_bones;	// additive_local_pose
					transform_cache_size += context.metric_transform_size * context.num_bones * context.clip.segments->num_samples;	// base_local_transforms
					transform_cache_size += context.metric_transform_size * context.num_bones * context.clip.segments->num_samples;	// base_object_transforms
				}

				writer["transform_cache_size"] = static_cast<uint32_t>(transform_cache_size);
			}
#endif
		}

		inline void quantize_streams(IAllocator& allocator, ClipContext& clip_context, const compression_settings& settings, const ClipContext& raw_clip_context, const ClipContext& additive_base_clip_context, OutputStats& out_stats)
		{
			(void)out_stats;

			const bool is_rotation_variable = is_rotation_format_variable(settings.rotation_format);
			const bool is_translation_variable = is_vector_format_variable(settings.translation_format);
			const bool is_scale_variable = is_vector_format_variable(settings.scale_format);
			const bool is_any_variable = is_rotation_variable || is_translation_variable || is_scale_variable;

			QuantizationContext context(allocator, clip_context, raw_clip_context, additive_base_clip_context, settings);

			for (SegmentContext& segment : clip_context.segment_iterator())
			{
#if ACL_IMPL_DEBUG_VARIABLE_QUANTIZATION
				printf("Quantizing segment %u...\n", segment.segment_index);
#endif

#if ACL_IMPL_PROFILE_MATH
				{
					ScopeProfiler timer;

					for (int32_t i = 0; i < 10; ++i)
					{
						context.set_segment(segment);

						if (is_any_variable)
							find_optimal_bit_rates(context);
					}

					timer.stop();

#if defined(__ANDROID__)
					__android_log_print(ANDROID_LOG_INFO, "acl", "Quantization optimization for segment %u took: %.4f ms", segment.segment_index, timer.get_elapsed_milliseconds());
#else
					printf("Quantization optimization for segment %u took: %.4f ms\n", segment.segment_index, timer.get_elapsed_milliseconds());
#endif
				}
#endif

				context.set_segment(segment);

				if (is_any_variable)
					find_optimal_bit_rates(context);

				// Quantize our streams now that we found the optimal bit rates
				quantize_all_streams(context);
			}

#if defined(SJSON_CPP_WRITER)
			if (are_all_enum_flags_set(out_stats.logging, StatLogging::Detailed))
			{
				sjson::ObjectWriter& writer = *out_stats.writer;
				writer["track_bit_rate_database_size"] = static_cast<uint32_t>(context.bit_rate_database.get_allocated_size());

				size_t transform_cache_size = 0;
				transform_cache_size += sizeof(rtm::qvvf) * context.num_bones;	// raw_local_pose
				transform_cache_size += sizeof(rtm::qvvf) * context.num_bones;	// lossy_local_pose
				transform_cache_size += context.metric_transform_size * context.num_bones;	// lossy_object_pose
				transform_cache_size += context.metric_transform_size * context.num_bones * context.clip.segments->num_samples;	// raw_local_transforms
				transform_cache_size += context.metric_transform_size * context.num_bones * context.clip.segments->num_samples;	// raw_object_transforms

				if (context.needs_conversion)
					transform_cache_size += context.metric_transform_size * context.num_bones;	// local_transforms_converted

				if (context.has_additive_base)
				{
					transform_cache_size += sizeof(rtm::qvvf) * context.num_bones;	// additive_local_pose
					transform_cache_size += context.metric_transform_size * context.num_bones * context.clip.segments->num_samples;	// base_local_transforms
					transform_cache_size += context.metric_transform_size * context.num_bones * context.clip.segments->num_samples;	// base_object_transforms
				}

				writer["transform_cache_size"] = static_cast<uint32_t>(transform_cache_size);
			}
#endif
		}
	}
}

ACL_IMPL_FILE_PRAGMA_POP
