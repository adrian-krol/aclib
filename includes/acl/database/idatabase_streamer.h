#pragma once

////////////////////////////////////////////////////////////////////////////////
// The MIT License (MIT)
//
// Copyright (c) 2020 Nicholas Frechette & Animation Compression Library contributors
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

#include "acl/core/impl/compiler_utils.h"

#include <cstdint>
#include <functional>

ACL_IMPL_FILE_PRAGMA_PUSH

namespace acl
{
	////////////////////////////////////////////////////////////////////////////////
	// The interface for database streamers.
	//
	// Streamers are responsible for allocating/freeing the bulk data as well as
	// streaming the data in/out. Streaming in is safe from any thread but streaming out
	// cannot happen while decompression is in progress otherwise the behavior is undefined.
	////////////////////////////////////////////////////////////////////////////////
	class idatabase_streamer
	{
	public:
		//////////////////////////////////////////////////////////////////////////
		// Streamer destructor
		virtual ~idatabase_streamer() {}

		//////////////////////////////////////////////////////////////////////////
		// Returns true if the streamer is initialized
		virtual bool is_initialized() const = 0;

		//////////////////////////////////////////////////////////////////////////
		// Returns a valid pointer to the bulk data used to decompress from
		virtual const uint8_t* get_bulk_data() const = 0;

		//////////////////////////////////////////////////////////////////////////
		// Called when we request some data to be streamed in.
		// The offset into the bulk data and the size in bytes to stream in are provided as arguments.
		// Once the streaming request has been fulfilled (sync or async), call the continuation function with
		// the status result. The continuation can be called from any thread at any moment safely.
		virtual void stream_in(uint32_t offset, uint32_t size, const std::function<void(bool success)>& continuation) = 0;

		//////////////////////////////////////////////////////////////////////////
		// Called when we request some data to be streamed out.
		// The offset into the bulk data and the size in bytes to stream out are provided as arguments.
		// Once the streaming request has been fulfilled (sync or async), call the continuation function with
		// the status result. The continuation cannot be called while decompression is in progress with the associated
		// database/bulk data. Doing so will result in undefined behavior as the data could be in use while we stream it out.
		virtual void stream_out(uint32_t offset, uint32_t size, const std::function<void(bool success)>& continuation) = 0;
	};
}

ACL_IMPL_FILE_PRAGMA_POP
