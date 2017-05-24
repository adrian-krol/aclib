#pragma once

#include "acl/memory.h"
#include "acl/assert.h"
#include "acl/math/quat_64.h"
#include "acl/math/vector4_64.h"
#include "acl/compression/animation_track_range.h"

#include <stdint.h>
#include <utility>

namespace acl
{
	class AnimationTrack
	{
	public:
		bool is_initialized() const { return m_allocator != nullptr; }

		uint32_t get_num_samples() const { return m_num_samples; }

		AnimationTrackRange get_range() const
		{
			if (m_is_range_dirty)
			{
				m_range = calculate_range();
				m_is_range_dirty = false;
			}

			return m_range;
		}

	protected:
		enum class AnimationTrackType : uint8_t
		{
			Rotation = 0,
			Translation = 1,
			// TODO: Scale
		};

		AnimationTrack()
			: m_allocator(nullptr)
			, m_sample_data(nullptr)
			, m_time_data(nullptr)
			, m_num_samples(0)
			, m_type(AnimationTrackType::Rotation)
		{}

		AnimationTrack(AnimationTrack&& track)
			: m_allocator(track.m_allocator)
			, m_sample_data(track.m_sample_data)
			, m_time_data(track.m_time_data)
			, m_num_samples(track.m_num_samples)
			, m_is_range_dirty(track.m_is_range_dirty)
			, m_type(track.m_type)
			, m_range(track.m_range)
		{}

		AnimationTrack(Allocator& allocator, uint32_t num_samples, AnimationTrackType type)
			: m_allocator(&allocator)
			, m_sample_data(allocate_type_array<double>(allocator, num_samples * get_animation_track_sample_size(type)))
			, m_time_data(allocate_type_array<double>(allocator, num_samples))
			, m_num_samples(num_samples)
			, m_is_range_dirty(true)
			, m_type(type)
			, m_range(AnimationTrackRange())
		{}

		~AnimationTrack()
		{
			if (is_initialized())
			{
				m_allocator->deallocate(m_sample_data);
				m_allocator->deallocate(m_time_data);
			}
		}

		AnimationTrack& operator=(AnimationTrack&& track)
		{
			std::swap(m_allocator, track.m_allocator);
			std::swap(m_sample_data, track.m_sample_data);
			std::swap(m_time_data, track.m_time_data);
			std::swap(m_num_samples, track.m_num_samples);
			std::swap(m_is_range_dirty, track.m_is_range_dirty);
			std::swap(m_type, track.m_type);
			std::swap(m_range, track.m_range);
			return *this;
		}

		AnimationTrack(const AnimationTrack&) = delete;
		AnimationTrack& operator=(const AnimationTrack&) = delete;

		AnimationTrackRange calculate_range() const
		{
			ensure(is_initialized());

			if (m_num_samples == 0)
				return AnimationTrackRange();

			size_t sample_size = get_animation_track_sample_size(m_type);

			uint32_t sample_index = 0;
			const double* sample = &m_sample_data[sample_index * sample_size];

			double x = sample[0];
			double y = sample[1];
			double z = sample[2];
			// TODO: Add padding and avoid the branch altogether
			double w = sample_size == 4 ? sample[3] : z;	// Constant branch, trivially predicted

			Vector4_64 value = vector_set(x, y, z, w);

			Vector4_64 min = value;
			Vector4_64 max = value;

			for (sample_index = 1; sample_index < m_num_samples; ++sample_index)
			{
				sample = &m_sample_data[sample_index * sample_size];

				x = sample[0];
				y = sample[1];
				z = sample[2];
				// TODO: Add padding and avoid the branch altogether
				w = sample_size == 4 ? sample[3] : z;	// Constant branch, trivially predicted

				value = vector_set(x, y, z, w);

				min = vector_min(min, value);
				max = vector_max(max, value);
			}

			return AnimationTrackRange(min, max);
		}

		// TODO: constexpr
		// Returns the number of values per sample
		static inline size_t get_animation_track_sample_size(AnimationTrackType type)
		{
			switch (type)
			{
			default:
			case AnimationTrackType::Rotation:		return 4;
			case AnimationTrackType::Translation:	return 3;
			}
		}

		Allocator*						m_allocator;
		double*							m_sample_data;
		double*							m_time_data;

		uint32_t						m_num_samples;
		mutable bool					m_is_range_dirty;		// TODO: Do we really need to cache this? nasty with mutable...

		AnimationTrackType				m_type;

		mutable AnimationTrackRange		m_range;

		// TODO: Support different sampling methods: linear, cubic
	};

	class AnimationRotationTrack : public AnimationTrack
	{
	public:
		AnimationRotationTrack()
			: AnimationTrack()
		{}

		AnimationRotationTrack(Allocator& allocator, uint32_t num_samples)
			: AnimationTrack(allocator, num_samples, AnimationTrackType::Rotation)
		{}

		AnimationRotationTrack(AnimationRotationTrack&& track)
			: AnimationTrack(std::forward<AnimationTrack>(track))
		{}

		AnimationRotationTrack& operator=(AnimationRotationTrack&& track)
		{
			AnimationTrack::operator=(std::forward<AnimationTrack>(track));
			return *this;
		}

		void set_sample(uint32_t sample_index, const Quat_64& rotation, double sample_time)
		{
			ensure(is_initialized());

			size_t sample_size = get_animation_track_sample_size(m_type);
			ensure(sample_size == 4);

			double* sample = &m_sample_data[sample_index * sample_size];
			sample[0] = quat_get_x(rotation);
			sample[1] = quat_get_y(rotation);
			sample[2] = quat_get_z(rotation);
			sample[3] = quat_get_w(rotation);

			m_time_data[sample_index] = sample_time;
			m_is_range_dirty = true;
		}

		Quat_64 get_sample(uint32_t sample_index) const
		{
			ensure(is_initialized());
			ensure(m_type == AnimationTrackType::Rotation);

			size_t sample_size = get_animation_track_sample_size(m_type);

			const double* sample = &m_sample_data[sample_index * sample_size];
			return quat_set(sample[0], sample[1], sample[2], sample[3]);
		}

		AnimationRotationTrack(const AnimationRotationTrack&) = delete;
		AnimationRotationTrack& operator=(const AnimationRotationTrack&) = delete;
	};

	class AnimationTranslationTrack : public AnimationTrack
	{
	public:
		AnimationTranslationTrack()
			: AnimationTrack()
		{}

		AnimationTranslationTrack(Allocator& allocator, uint32_t num_samples)
			: AnimationTrack(allocator, num_samples, AnimationTrackType::Translation)
		{}

		AnimationTranslationTrack(AnimationTranslationTrack&& track)
			: AnimationTrack(std::forward<AnimationTrack>(track))
		{}

		AnimationTranslationTrack& operator=(AnimationTranslationTrack&& track)
		{
			AnimationTrack::operator=(std::forward<AnimationTrack>(track));
			return *this;
		}

		void set_sample(uint32_t sample_index, const Vector4_64& translation, double sample_time)
		{
			ensure(is_initialized());

			size_t sample_size = get_animation_track_sample_size(m_type);
			ensure(sample_size == 3);

			double* sample = &m_sample_data[sample_index * sample_size];
			sample[0] = vector_get_x(translation);
			sample[1] = vector_get_y(translation);
			sample[2] = vector_get_z(translation);

			m_time_data[sample_index] = sample_time;
			m_is_range_dirty = true;
		}

		Vector4_64 get_sample(uint32_t sample_index) const
		{
			ensure(is_initialized());
			ensure(m_type == AnimationTrackType::Translation);

			size_t sample_size = get_animation_track_sample_size(m_type);

			const double* sample = &m_sample_data[sample_index * sample_size];
			return vector_set(sample[0], sample[1], sample[2], 0.0);
		}

		AnimationTranslationTrack(const AnimationTranslationTrack&) = delete;
		AnimationTranslationTrack& operator=(const AnimationTranslationTrack&) = delete;
	};
}
