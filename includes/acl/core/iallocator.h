#pragma once

////////////////////////////////////////////////////////////////////////////////
// The MIT License (MIT)
//
// Copyright (c) 2018 Nicholas Frechette & Animation Compression Library contributors
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

#include <cstdint>

#if defined(__ANDROID__)
namespace std
{
	// Missing function because android uses an older compiler that doesn't support all of C++11
	template<typename Type>
	using is_trivially_default_constructible = has_trivial_default_constructor<Type>;
}
#endif

namespace acl
{
	class IAllocator
	{
	public:
		static constexpr size_t k_default_alignment = 16;

		IAllocator() {}
		virtual ~IAllocator() {}

		IAllocator(const IAllocator&) = delete;
		IAllocator& operator=(const IAllocator&) = delete;

		virtual void* allocate(size_t size, size_t alignment = k_default_alignment) = 0;
		virtual void deallocate(void* ptr, size_t size) = 0;
	};

	//////////////////////////////////////////////////////////////////////////

	template<typename AllocatedType, typename... Args>
	AllocatedType* allocate_type(IAllocator& allocator, Args&&... args)
	{
		AllocatedType* ptr = reinterpret_cast<AllocatedType*>(allocator.allocate(sizeof(AllocatedType), alignof(AllocatedType)));
		if (std::is_trivially_default_constructible<AllocatedType>::value)
			return ptr;
		return new(ptr) AllocatedType(std::forward<Args>(args)...);
	}

	template<typename AllocatedType, typename... Args>
	AllocatedType* allocate_type_aligned(IAllocator& allocator, size_t alignment, Args&&... args)
	{
		ACL_ENSURE(is_alignment_valid<AllocatedType>(alignment), "Invalid alignment: %u. Expected a power of two at least equal to %u", alignment, alignof(AllocatedType));
		AllocatedType* ptr = reinterpret_cast<AllocatedType*>(allocator.allocate(sizeof(AllocatedType), alignment));
		if (std::is_trivially_default_constructible<AllocatedType>::value)
			return ptr;
		return new(ptr) AllocatedType(std::forward<Args>(args)...);
	}

	template<typename AllocatedType>
	void deallocate_type(IAllocator& allocator, AllocatedType* ptr)
	{
		if (ptr == nullptr)
			return;

		if (!std::is_trivially_destructible<AllocatedType>::value)
			ptr->~AllocatedType();

		allocator.deallocate(ptr, sizeof(AllocatedType));
	}

	template<typename AllocatedType, typename... Args>
	AllocatedType* allocate_type_array(IAllocator& allocator, size_t num_elements, Args&&... args)
	{
		AllocatedType* ptr = reinterpret_cast<AllocatedType*>(allocator.allocate(sizeof(AllocatedType) * num_elements, alignof(AllocatedType)));
		if (std::is_trivially_default_constructible<AllocatedType>::value)
			return ptr;
		for (size_t element_index = 0; element_index < num_elements; ++element_index)
			new(&ptr[element_index]) AllocatedType(std::forward<Args>(args)...);
		return ptr;
	}

	template<typename AllocatedType, typename... Args>
	AllocatedType* allocate_type_array_aligned(IAllocator& allocator, size_t num_elements, size_t alignment, Args&&... args)
	{
		ACL_ENSURE(is_alignment_valid<AllocatedType>(alignment), "Invalid alignment: %u. Expected a power of two at least equal to %u", alignment, alignof(AllocatedType));
		AllocatedType* ptr = reinterpret_cast<AllocatedType*>(allocator.allocate(sizeof(AllocatedType) * num_elements, alignment));
		if (std::is_trivially_default_constructible<AllocatedType>::value)
			return ptr;
		for (size_t element_index = 0; element_index < num_elements; ++element_index)
			new(&ptr[element_index]) AllocatedType(std::forward<Args>(args)...);
		return ptr;
	}

	template<typename AllocatedType>
	void deallocate_type_array(IAllocator& allocator, AllocatedType* elements, size_t num_elements)
	{
		if (elements == nullptr)
			return;

		if (!std::is_trivially_destructible<AllocatedType>::value)
		{
			for (size_t element_index = 0; element_index < num_elements; ++element_index)
				elements[element_index].~AllocatedType();
		}

		allocator.deallocate(elements, sizeof(AllocatedType) * num_elements);
	}
}
