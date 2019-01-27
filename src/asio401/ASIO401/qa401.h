#pragma once

#include "winusb.h"

#include <array>
#include <string_view>

namespace asio401 {

	class QA401 {
	public:
		enum class InputHighPassFilterState { ENGAGED, DISENGAGED };  // See https://github.com/dechamps/ASIO401/issues/7
		enum class AttenuatorState { ENGAGED, DISENGAGED };
		enum class SampleRate { KHZ48, KHZ192 };  // According to QuantAsylum, the QA401 only supports these two

		static constexpr auto sampleSizeInBytes = 4;  // 32-bit big endian signed integer. According to QuantAsylum the actual precision is 24 bits.
		static constexpr auto hardwareQueueSizeInFrames = 1024;  // Measured empirically
		static constexpr auto inputChannelCount = 2;
		static constexpr auto outputChannelCount = 2;
		static constexpr auto readPaddingInFrames = 64;  // Number of frames in the first read that can be a remnant of the previous stream, and should be ignored. See https://github.com/dechamps/ASIO401/issues/5
		
		QA401(std::string_view devicePath);
		~QA401() { AbortIO(); }

		// Note that there is no Start() call. Technically we could implement one by writing 5 into register 4 but that has rather nasty side effects. See https://github.com/dechamps/ASIO401/issues/9
		// Instead we do that register write in Reset(), and exploit the fact that the QA401 won't actually start streaming until the first write is sent. See https://github.com/dechamps/ASIO401/issues/10

		void Reset(InputHighPassFilterState inputHighPassFilterState, AttenuatorState attenuatorState, SampleRate sampleRate);
		void StartWrite(const void* buffer, size_t size);
		void FinishWrite();
		void StartRead(void* buffer, size_t size);
		void FinishRead();
		void Ping();

	private:
		class RegisterWriteRequest {
		public:
			constexpr RegisterWriteRequest(uint8_t registerNumber, uint32_t value);

			const void* data() const { return request.data(); }
			size_t size() const { return request.size(); }

			uint8_t getRegisterNumber() const;
			uint32_t getValue() const;

		private:
			std::array<uint8_t, 5> request;
		};

		void Validate();

		void WriteRegister(uint8_t registerNumber, uint32_t value);
		WinUsbOverlappedIO WriteRegister(const RegisterWriteRequest& request, OVERLAPPED& overlapped);

		void AbortIO();

		WinUsbHandle winUsb;

		WindowsOverlappedEvent readOverlapped;
		WindowsOverlappedEvent writeOverlapped;
		WindowsOverlappedEvent pingOverlapped;

		std::optional<WinUsbOverlappedIO> readIO;
		std::optional<WinUsbOverlappedIO> writeIO;
		std::optional<WinUsbOverlappedIO> pingIO;
	};

}
