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

#if defined(SJSON_CPP_WRITER)

#include "acl/core/ialgorithm.h"
#include "acl/core/memory_cache.h"
#include "acl/algorithm/uniformly_sampled/decoder.h"
#include "acl/decompression/default_output_writer.h"
#include "acl/compression/stream/clip_context.h"
#include "acl/compression/skeleton_error_metric.h"

namespace acl
{
	inline void write_summary_segment_stats(const SegmentContext& segment, RotationFormat8 rotation_format, VectorFormat8 translation_format, VectorFormat8 scale_format, sjson::ObjectWriter& writer)
	{
		writer["segment_index"] = segment.segment_index;
		writer["num_samples"] = segment.num_samples;

		const uint32_t format_per_track_data_size = get_format_per_track_data_size(*segment.clip, rotation_format, translation_format, scale_format);

		uint32_t segment_size = 0;
		segment_size += format_per_track_data_size;						// Format per track data
		segment_size = align_to(segment_size, 2);						// Align range data
		segment_size += segment.range_data_size;						// Range data
		segment_size = align_to(segment_size, 4);						// Align animated data
		segment_size += segment.animated_data_size;						// Animated track data

		writer["segment_size"] = segment_size;
		writer["animated_frame_size"] = double(segment.animated_data_size) / double(segment.num_samples);
	}

	inline void write_detailed_segment_stats(const SegmentContext& segment, sjson::ObjectWriter& writer)
	{
		uint32_t bit_rate_counts[k_num_bit_rates] = {0};

		for (const BoneStreams& bone_stream : segment.bone_iterator())
		{
			const uint8_t rotation_bit_rate = bone_stream.rotations.get_bit_rate();
			if (rotation_bit_rate != k_invalid_bit_rate)
				bit_rate_counts[rotation_bit_rate]++;

			const uint8_t translation_bit_rate = bone_stream.translations.get_bit_rate();
			if (translation_bit_rate != k_invalid_bit_rate)
				bit_rate_counts[translation_bit_rate]++;

			const uint8_t scale_bit_rate = bone_stream.scales.get_bit_rate();
			if (scale_bit_rate != k_invalid_bit_rate)
				bit_rate_counts[scale_bit_rate]++;
		}

		writer["bit_rate_counts"] = [&](sjson::ArrayWriter& writer)
		{
			for (uint8_t bit_rate = 0; bit_rate < k_num_bit_rates; ++bit_rate)
				writer.push(bit_rate_counts[bit_rate]);
		};

		// We assume that we always interpolate between 2 poses
		const uint32_t animated_pose_byte_size = align_to(segment.animated_pose_bit_size * 2, 8) / 8;
		constexpr uint32_t k_cache_line_byte_size = 64;
		const uint32_t num_clip_header_cache_lines = align_to(segment.clip->total_header_size, k_cache_line_byte_size) / k_cache_line_byte_size;
		const uint32_t num_segment_header_cache_lines = align_to(segment.total_header_size, k_cache_line_byte_size) / k_cache_line_byte_size;
		const uint32_t num_animated_pose_cache_lines = align_to(animated_pose_byte_size, k_cache_line_byte_size) / k_cache_line_byte_size;
		writer["decomp_touched_bytes"] = segment.clip->total_header_size + segment.total_header_size + animated_pose_byte_size;
		writer["decomp_touched_cache_lines"] = num_clip_header_cache_lines + num_segment_header_cache_lines + num_animated_pose_cache_lines;
	}

	inline void write_exhaustive_segment_stats(IAllocator& allocator, const SegmentContext& segment, const ClipContext& raw_clip_context, const ClipContext& additive_base_clip_context, const RigidSkeleton& skeleton, const CompressionSettings& settings, sjson::ObjectWriter& writer)
	{
		const uint16_t num_bones = skeleton.get_num_bones();
		const bool has_scale = segment_context_has_scale(segment);

		Transform_32* raw_local_pose = allocate_type_array<Transform_32>(allocator, num_bones);
		Transform_32* base_local_pose = allocate_type_array<Transform_32>(allocator, num_bones);
		Transform_32* lossy_local_pose = allocate_type_array<Transform_32>(allocator, num_bones);

		const float sample_rate = float(raw_clip_context.segments[0].bone_streams[0].rotations.get_sample_rate());
		const float ref_duration = float(raw_clip_context.num_samples - 1) / sample_rate;

		const float segment_duration = float(segment.num_samples - 1) / sample_rate;

		BoneError worst_bone_error = { k_invalid_bone_index, 0.0f, 0.0f };

		writer["error_per_frame_and_bone"] = [&](sjson::ArrayWriter& writer)
		{
			for (uint32_t sample_index = 0; sample_index < segment.num_samples; ++sample_index)
			{
				const float sample_time = min(float(sample_index) / sample_rate, segment_duration);
				const float ref_sample_time = min(float(segment.clip_sample_offset + sample_index) / sample_rate, ref_duration);

				sample_streams(raw_clip_context.segments[0].bone_streams, num_bones, ref_sample_time, raw_local_pose);
				sample_streams(segment.bone_streams, num_bones, sample_time, lossy_local_pose);

				if (raw_clip_context.has_additive_base)
				{
					const float normalized_sample_time = additive_base_clip_context.num_samples > 1 ? (ref_sample_time / ref_duration) : 0.0f;
					const float additive_sample_time = normalized_sample_time * additive_base_clip_context.duration;
					sample_streams(additive_base_clip_context.segments[0].bone_streams, num_bones, additive_sample_time, base_local_pose);
				}

				writer.push_newline();
				writer.push([&](sjson::ArrayWriter& writer)
				{
					for (uint16_t bone_index = 0; bone_index < num_bones; ++bone_index)
					{
						float error;
						if (has_scale)
							error = settings.error_metric->calculate_object_bone_error(skeleton, raw_local_pose, base_local_pose, lossy_local_pose, bone_index);
						else
							error = settings.error_metric->calculate_object_bone_error_no_scale(skeleton, raw_local_pose, base_local_pose, lossy_local_pose, bone_index);

						writer.push(error);

						if (error > worst_bone_error.error)
						{
							worst_bone_error.error = error;
							worst_bone_error.index = bone_index;
							worst_bone_error.sample_time = sample_time;
						}
					}
				});
			}
		};

		writer["max_error"] = worst_bone_error.error;
		writer["worst_bone"] = worst_bone_error.index;
		writer["worst_time"] = worst_bone_error.sample_time;

		deallocate_type_array(allocator, raw_local_pose, num_bones);
		deallocate_type_array(allocator, base_local_pose, num_bones);
		deallocate_type_array(allocator, lossy_local_pose, num_bones);
	}

	constexpr uint32_t k_num_decompression_timing_passes = 5;

	struct UniformlySampledFastPathDecompressionSettings : public uniformly_sampled::DecompressionSettings
	{
		constexpr bool is_rotation_format_supported(RotationFormat8 format) const { return format == RotationFormat8::QuatDropW_Variable; }
		constexpr bool is_translation_format_supported(VectorFormat8 format) const { return format == VectorFormat8::Vector3_Variable; }
		constexpr bool is_scale_format_supported(VectorFormat8 format) const { return format == VectorFormat8::Vector3_Variable; }
		constexpr RotationFormat8 get_rotation_format(RotationFormat8 format) const { return RotationFormat8::QuatDropW_Variable; }
		constexpr VectorFormat8 get_translation_format(VectorFormat8 format) const { return VectorFormat8::Vector3_Variable; }
		constexpr VectorFormat8 get_scale_format(VectorFormat8 format) const { return VectorFormat8::Vector3_Variable; }

		constexpr bool are_clip_range_reduction_flags_supported(RangeReductionFlags8 flags) const { return true; }
		constexpr RangeReductionFlags8 get_clip_range_reduction(RangeReductionFlags8 flags) const { return RangeReductionFlags8::AllTracks; }

		constexpr bool supports_mixed_packing() const { return false; }
	};

	inline void write_decompression_performance_stats(IAllocator& allocator, IAlgorithm& algorithm, const AnimationClip& clip, const CompressedClip& compressed_clip,
		StatLogging logging, sjson::ObjectWriter& writer, const char* action_type, bool forward_order, bool measure_upper_bound,
		void* contexts[], CPUCacheFlusher& cache_flusher, Transform_32* lossy_pose_transforms)
	{
		const int32_t num_samples = static_cast<int32_t>(clip.get_num_samples());
		const double duration = clip.get_duration();
		const uint16_t num_bones = clip.get_num_bones();

		// If we can, we use a fast-path that simulates what a real game engine would use
		// by disabling the things they normally wouldn't care about like deprecated formats
		// and debugging features
		const CompressionSettings& settings = algorithm.get_compression_settings();
		const bool use_uniform_fast_path = settings.rotation_format == RotationFormat8::QuatDropW_Variable
			&& settings.translation_format == VectorFormat8::Vector3_Variable
			&& settings.scale_format == VectorFormat8::Vector3_Variable
			&& are_all_enum_flags_set(settings.range_reduction, RangeReductionFlags8::AllTracks)
			&& settings.segmenting.enabled;

		writer[action_type] = [&](sjson::ObjectWriter& writer)
		{
			const int32_t initial_sample_index = forward_order ? 0 : num_samples - 1;
			const int32_t sample_index_sentinel = forward_order ? num_samples : -1;
			const int32_t delta_sample_index = forward_order ? 1 : -1;

			double clip_max, clip_min, clip_total = 0;

			writer["data"] = [&](sjson::ArrayWriter& writer)
			{
				for (int32_t sample_index = initial_sample_index; sample_index != sample_index_sentinel; sample_index += delta_sample_index)
				{
					const float sample_time = static_cast<float>(duration * sample_index / (num_samples - 1));

					double decompression_time = 0;

					for (uint32_t pass_index = 0; pass_index < k_num_decompression_timing_passes; ++pass_index)
					{
						void*& context = contexts[pass_index];

						if (measure_upper_bound)
						{
							// Clearing the context ensures the decoder cannot reuse any state cached from the last sample.
							algorithm.deallocate_decompression_context(allocator, context);
							context = algorithm.allocate_decompression_context(allocator, compressed_clip);
						}

						cache_flusher.flush_cache(&compressed_clip, compressed_clip.get_size());

						ScopeProfiler timer;
						if (use_uniform_fast_path)
						{
							UniformlySampledFastPathDecompressionSettings decompression_settings;
							DefaultOutputWriter pose_writer(lossy_pose_transforms, num_bones);
							uniformly_sampled::decompress_pose(decompression_settings, compressed_clip, context, sample_time, pose_writer);
						}
						else
							algorithm.decompress_pose(compressed_clip, context, sample_time, lossy_pose_transforms, num_bones);
						timer.stop();

						const double elapsed_ms = timer.get_elapsed_milliseconds();
						if (pass_index == 0 || elapsed_ms < decompression_time)
							decompression_time = elapsed_ms;
					}

					if (are_any_enum_flags_set(logging, StatLogging::ExhaustiveDecompression))
						writer.push(decompression_time);

					if (sample_index == initial_sample_index || decompression_time > clip_max)
						clip_max = decompression_time;

					if (sample_index == initial_sample_index || decompression_time < clip_min)
						clip_min = decompression_time;

					clip_total += decompression_time;
				}
			};

			writer["max_decompression_time_ms"] = clip_max;
			writer["avg_decompression_time_ms"] = clip_total / static_cast<double>(num_samples);
			writer["min_decompression_time_ms"] = clip_min;
		};
	}

	inline void write_decompression_performance_stats(IAllocator& allocator, IAlgorithm& algorithm, const AnimationClip& raw_clip, const CompressedClip& compressed_clip, StatLogging logging, sjson::ObjectWriter& writer)
	{
		void* contexts[k_num_decompression_timing_passes];

		for (uint32_t pass_index = 0; pass_index < k_num_decompression_timing_passes; ++pass_index)
			contexts[pass_index] = algorithm.allocate_decompression_context(allocator, compressed_clip);

		CPUCacheFlusher* cache_flusher = allocate_type<CPUCacheFlusher>(allocator);

		const uint16_t num_bones = raw_clip.get_num_bones();
		Transform_32* lossy_pose_transforms = allocate_type_array<Transform_32>(allocator, num_bones);

		writer["decompression_time_per_sample"] = [&](sjson::ObjectWriter& writer)
		{
			write_decompression_performance_stats(allocator, algorithm, raw_clip, compressed_clip, logging, writer, "forward_playback", true, false, contexts, *cache_flusher, lossy_pose_transforms);
			write_decompression_performance_stats(allocator, algorithm, raw_clip, compressed_clip, logging, writer, "backward_playback", false, false, contexts, *cache_flusher, lossy_pose_transforms);
			write_decompression_performance_stats(allocator, algorithm, raw_clip, compressed_clip, logging, writer, "initial_seek", true, true, contexts, *cache_flusher, lossy_pose_transforms);
		};

		for (uint32_t pass_index = 0; pass_index < k_num_decompression_timing_passes; ++pass_index)
			algorithm.deallocate_decompression_context(allocator, contexts[pass_index]);

		deallocate_type_array(allocator, lossy_pose_transforms, num_bones);
		deallocate_type(allocator, cache_flusher);
	}

	inline void write_stats(IAllocator& allocator, const AnimationClip& clip, const ClipContext& clip_context, const RigidSkeleton& skeleton,
		const CompressedClip& compressed_clip, const CompressionSettings& settings, const ClipHeader& header, const ClipContext& raw_clip_context,
		const ClipContext& additive_base_clip_context, const ScopeProfiler& compression_time,
		OutputStats& stats)
	{
		ACL_ASSERT(stats.writer != nullptr, "Attempted to log stats without a writer");

		const uint32_t raw_size = clip.get_raw_size();
		const uint32_t compressed_size = compressed_clip.get_size();
		const double compression_ratio = double(raw_size) / double(compressed_size);

		sjson::ObjectWriter& writer = *stats.writer;
		writer["algorithm_name"] = get_algorithm_name(AlgorithmType8::UniformlySampled);
		writer["algorithm_uid"] = settings.get_hash();
		writer["clip_name"] = clip.get_name().c_str();
		writer["raw_size"] = raw_size;
		writer["compressed_size"] = compressed_size;
		writer["compression_ratio"] = compression_ratio;
		writer["compression_time"] = compression_time.get_elapsed_seconds();
		writer["duration"] = clip.get_duration();
		writer["num_samples"] = clip.get_num_samples();
		writer["num_bones"] = clip.get_num_bones();
		writer["rotation_format"] = get_rotation_format_name(settings.rotation_format);
		writer["translation_format"] = get_vector_format_name(settings.translation_format);
		writer["scale_format"] = get_vector_format_name(settings.scale_format);
		writer["range_reduction"] = get_range_reduction_name(settings.range_reduction);
		writer["has_scale"] = clip_context.has_scale;
		writer["error_metric"] = settings.error_metric->get_name();

		if (are_all_enum_flags_set(stats.logging, StatLogging::Detailed) || are_all_enum_flags_set(stats.logging, StatLogging::Exhaustive))
		{
			uint32_t num_default_rotation_tracks = 0;
			uint32_t num_default_translation_tracks = 0;
			uint32_t num_default_scale_tracks = 0;
			uint32_t num_constant_rotation_tracks = 0;
			uint32_t num_constant_translation_tracks = 0;
			uint32_t num_constant_scale_tracks = 0;
			uint32_t num_animated_rotation_tracks = 0;
			uint32_t num_animated_translation_tracks = 0;
			uint32_t num_animated_scale_tracks = 0;

			for (const BoneStreams& bone_stream : clip_context.segments[0].bone_iterator())
			{
				if (bone_stream.is_rotation_default)
					num_default_rotation_tracks++;
				else if (bone_stream.is_rotation_constant)
					num_constant_rotation_tracks++;
				else
					num_animated_rotation_tracks++;

				if (bone_stream.is_translation_default)
					num_default_translation_tracks++;
				else if (bone_stream.is_translation_constant)
					num_constant_translation_tracks++;
				else
					num_animated_translation_tracks++;

				if (bone_stream.is_scale_default)
					num_default_scale_tracks++;
				else if (bone_stream.is_scale_constant)
					num_constant_scale_tracks++;
				else
					num_animated_scale_tracks++;
			}

			const uint32_t num_default_tracks = num_default_rotation_tracks + num_default_translation_tracks + num_default_scale_tracks;
			const uint32_t num_constant_tracks = num_constant_rotation_tracks + num_constant_translation_tracks + num_constant_scale_tracks;
			const uint32_t num_animated_tracks = num_animated_rotation_tracks + num_animated_translation_tracks + num_animated_scale_tracks;

			writer["num_default_rotation_tracks"] = num_default_rotation_tracks;
			writer["num_default_translation_tracks"] = num_default_translation_tracks;
			writer["num_default_scale_tracks"] = num_default_scale_tracks;

			writer["num_constant_rotation_tracks"] = num_constant_rotation_tracks;
			writer["num_constant_translation_tracks"] = num_constant_translation_tracks;
			writer["num_constant_scale_tracks"] = num_constant_scale_tracks;

			writer["num_animated_rotation_tracks"] = num_animated_rotation_tracks;
			writer["num_animated_translation_tracks"] = num_animated_translation_tracks;
			writer["num_animated_scale_tracks"] = num_animated_scale_tracks;

			writer["num_default_tracks"] = num_default_tracks;
			writer["num_constant_tracks"] = num_constant_tracks;
			writer["num_animated_tracks"] = num_animated_tracks;
		}

		if (settings.segmenting.enabled)
		{
			writer["segmenting"] = [&](sjson::ObjectWriter& writer)
			{
				writer["num_segments"] = header.num_segments;
				writer["range_reduction"] = get_range_reduction_name(settings.segmenting.range_reduction);
				writer["ideal_num_samples"] = settings.segmenting.ideal_num_samples;
				writer["max_num_samples"] = settings.segmenting.max_num_samples;
			};
		}

		writer["segments"] = [&](sjson::ArrayWriter& writer)
		{
			for (const SegmentContext& segment : clip_context.segment_iterator())
			{
				writer.push([&](sjson::ObjectWriter& writer)
				{
					write_summary_segment_stats(segment, settings.rotation_format, settings.translation_format, settings.scale_format, writer);

					if (are_all_enum_flags_set(stats.logging, StatLogging::Detailed))
					{
						write_detailed_segment_stats(segment, writer);
					}

					if (are_all_enum_flags_set(stats.logging, StatLogging::Exhaustive))
					{
						write_exhaustive_segment_stats(allocator, segment, raw_clip_context, additive_base_clip_context, skeleton, settings, writer);
					}
				});
			}
		};
	}
}

#endif	// #if defined(SJSON_CPP_WRITER)
