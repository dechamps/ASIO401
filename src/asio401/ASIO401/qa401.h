#pragma once

#include "winusb.h"

#include <string_view>

namespace asio401 {

	class QA401 {
	public:
		QA401(std::string_view devicePath);

		void Reset();
		void Start();
		void StartWrite(const void* buffer, size_t size);
		void FinishWrite();
		void StartRead(void* buffer, size_t size);
		void FinishRead();
		void Ping();

	private:
		void Validate();

		void WriteRegister(uint8_t registerNumber, uint32_t value);
		WinUsbOverlappedIO WriteRegister(uint8_t registerNumber, uint32_t value, OVERLAPPED& overlapped);

		WinUsbHandle winUsb;

		WindowsOverlappedEvent readOverlapped;
		WindowsOverlappedEvent writeOverlapped;
		WindowsOverlappedEvent pingOverlapped;

		std::optional<WinUsbOverlappedIO> readIO;
		std::optional<WinUsbOverlappedIO> writeIO;
		std::optional<WinUsbOverlappedIO> pingIO;
	};

}
