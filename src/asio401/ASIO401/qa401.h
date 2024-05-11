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

		static constexpr auto sampleSizeInBytes = 4;  // 32-bit big endian signed integer. According to QuantAsylum the actual precision is 24 bits.
		static constexpr auto sampleEndianness = ::dechamps_cpputil::Endianness::BIG;
		static constexpr auto hardwareQueueSizeInFrames = 1024;  // Measured empirically
		static constexpr auto inputChannelCount = 2;
		static constexpr auto outputChannelCount = 2;
		
		QA401(std::string_view devicePath);
		~QA401();

		// Note that there is no Start() call. Technically we could implement one by writing 5 into register 4 but that has rather nasty side effects. See https://github.com/dechamps/ASIO401/issues/9
		// Instead we do that register write in Reset(), and exploit the fact that the QA401 won't actually start streaming until the first write is sent. See https://github.com/dechamps/ASIO401/issues/10

		void Reset(InputHighPassFilterState inputHighPassFilterState, AttenuatorState attenuatorState, SampleRate sampleRate);

		using FinishResult = QA40x::FinishResult;
		void StartWrite(std::span<const std::byte> buffer) { return qa40x.StartWrite(buffer); }
		_Check_return_ bool WritePending() const { return qa40x.WritePending(); }
		void AbortWrite() { return qa40x.AbortWrite(); }
		_Check_return_ FinishResult FinishWrite() { return qa40x.FinishWrite(); }
		void StartRead(std::span<std::byte> buffer) { return qa40x.StartRead(buffer); }
		_Check_return_ bool ReadPending() const { return qa40x.ReadPending(); }
		void AbortRead() { return qa40x.AbortRead(); }
		_Check_return_ FinishResult FinishRead() { return qa40x.FinishRead(); }
		void Ping();

	private:
		void AbortPing();

		QA40x qa40x;
		bool pinging = false;
	};

}
