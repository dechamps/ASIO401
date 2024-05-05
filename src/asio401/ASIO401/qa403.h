#pragma once

#include "qa40x.h"

#include <dechamps_cpputil/endian.h>

#include <string_view>

namespace asio401 {
	
	// This class implements the USB protocol described at https://github.com/QuantAsylum/QA40x_BareMetal
	class QA403 {
	public:
		static constexpr auto sampleSizeInBytes = 4;  // 32-bit big endian signed integer
		static constexpr auto sampleEndianness = ::dechamps_cpputil::Endianness::LITTLE;
		static constexpr auto hardwareQueueSizeInFrames = 1024;  // Measured empirically
		static constexpr auto inputChannelCount = 2;
		static constexpr auto outputChannelCount = 2;
		static constexpr auto sampleRate = 48000;  // TODO: support other sample rates
		
		QA403(std::string_view devicePath);

		void Reset();  // TODO: set parameters
		void Start();
		void StartWrite(const void* buffer, size_t size);
		void FinishWrite();
		void StartRead(void* buffer, size_t size);
		void FinishRead();

	private:
		QA40x qa40x;
	};

}
