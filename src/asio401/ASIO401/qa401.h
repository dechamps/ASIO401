#pragma once

#include "winusb.h"

#include <array>
#include <string_view>

namespace asio401 {

	class QA401 {
	public:
		enum class AttenuatorState { ENGAGED, DISENGAGED };
		enum class SampleRate { KHZ48, KHZ192 };  // According to QuantAsylum, the QA401 only supports these two

		static constexpr auto sampleSizeInBytes = 4;  // 32-bit big endian signed integer. According to QuantAsylum the actual precision is 24 bits.
		static constexpr auto hardwareQueueSizeInFrames = 1024;  // Measured empirically
		static constexpr auto inputChannelCount = 2;
		static constexpr auto outputChannelCount = 2;
		
		QA401(std::string_view devicePath);
		~QA401() { AbortIO(); }

		void Reset(AttenuatorState attenuatorState, SampleRate sampleRate);
		void Start();
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
