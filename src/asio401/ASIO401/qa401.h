#pragma once

#include "qa40x.h"

#include <dechamps_cpputil/endian.h>

#include <string_view>

namespace asio401 {

	class QA401 {
	public:
		enum class InputHighPassFilterState { ENGAGED, DISENGAGED };  // See https://github.com/dechamps/ASIO401/issues/7
		enum class AttenuatorState { ENGAGED, DISENGAGED };
		enum class SampleRate { KHZ48, KHZ192 };  // According to QuantAsylum, the QA401 only supports these two

		static constexpr auto sampleSizeInBytes = 4u;  // 32-bit big endian signed integer. According to QuantAsylum the actual precision is 24 bits.
		static constexpr auto sampleEndianness = ::dechamps_cpputil::Endianness::BIG;
		static constexpr auto hardwareQueueSizeInFrames = 1024u;  // Measured empirically
		static constexpr auto inputChannelCount = 2u;
		static constexpr auto outputChannelCount = 2u;
		static constexpr auto writeGranularityInFrames = 32u;  // Measured empirically
		
		QA401(std::string_view devicePath);
		~QA401();

		// Note that there is no Start() call. Technically we could implement one by writing 5 into register 4 but that has rather nasty side effects. See https://github.com/dechamps/ASIO401/issues/9
		// Instead we do that register write in Reset(), and exploit the fact that the QA401 won't actually start streaming until the first write is sent. See https://github.com/dechamps/ASIO401/issues/10

		void Reset(InputHighPassFilterState inputHighPassFilterState, AttenuatorState attenuatorState, SampleRate sampleRate);
		void Ping();

		QA40x::WriteChannel GetWriteChannel() { return QA40x::WriteChannel(qa40x); }
		QA40x::ReadChannel GetReadChannel() { return QA40x::ReadChannel(qa40x); };

	private:
		void AbortPing();

		void WriteRegister(uint8_t registerNumber, uint32_t value) { registerIOSlot.Execute(QA40x::RegisterChannel(qa40x), registerNumber, value); }

		QA40x qa40x;
		RegisterQA40xIOSlot registerIOSlot;
		bool pinging = false;
	};

}
