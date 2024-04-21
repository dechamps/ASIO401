#pragma once

#include "qa40x.h"

#include <dechamps_cpputil/endian.h>

#include <string_view>

namespace asio401 {
	
	// This class implements the USB protocol described at https://github.com/QuantAsylum/QA40x_BareMetal
	class QA403 {
	public:
		// TODO: these have been copy-pasted from QA401 and have not yet been verified empirically on the QA403.
		static constexpr auto sampleSizeInBytes = 4;  // 32-bit big endian signed integer
		static constexpr auto sampleEndianness = ::dechamps_cpputil::Endianness::LITTLE;
		static constexpr auto hardwareQueueSizeInFrames = 1024;
		static constexpr auto inputChannelCount = 2;
		static constexpr auto outputChannelCount = 2;
		static constexpr auto readPaddingInFrames = 64;  // Number of frames in the first read that can be a remnant of the previous stream, and should be ignored. See https://github.com/dechamps/ASIO401/issues/5
		static constexpr auto sampleRate = 48000;  // TODO: support other sample rates
		
		QA403(std::string_view devicePath);

		void Reset();  // TODO: set parameters
		void StartWrite(const void* buffer, size_t size);
		void FinishWrite();
		void StartRead(void* buffer, size_t size);
		void FinishRead();

	private:
		QA40x qa40x;
	};

}
