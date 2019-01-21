#pragma once

#include "winusb.h"

#include <array>
#include <string_view>

namespace asio401 {

	class QA401 {
	public:
		QA401(std::string_view devicePath);

		void Reset();
		void SetAttenuator(bool enabled);
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

		WinUsbHandle winUsb;

		WindowsOverlappedEvent readOverlapped;
		WindowsOverlappedEvent writeOverlapped;
		WindowsOverlappedEvent pingOverlapped;

		std::optional<WinUsbOverlappedIO> readIO;
		std::optional<WinUsbOverlappedIO> writeIO;
		std::optional<WinUsbOverlappedIO> pingIO;
	};

}
