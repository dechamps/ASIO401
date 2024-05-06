#pragma once

#include "winusb.h"

#include <array>
#include <string_view>

namespace asio401 {

	class QA40x {
	public:
		QA40x(std::string_view devicePath, UCHAR registerPipeId, UCHAR writePipeId, UCHAR readPipeId, bool requiresApp);
		~QA40x() { AbortIO(); }

		void StartWriteRegister(uint8_t registerNumber, uint32_t value);
		void FinishWriteRegister();
		void WriteRegister(uint8_t registerNumber, uint32_t value) {
			StartWriteRegister(registerNumber, value);
			FinishWriteRegister();
		}
		void StartWrite(const void* buffer, size_t size);
		void FinishWrite();
		void StartRead(void* buffer, size_t size);
		void FinishRead();
		void AbortIO();

	private:
		void Validate(bool requiresApp);

		const UCHAR registerPipeId;
		const UCHAR writePipeId;
		const UCHAR readPipeId;

		WinUsbHandle winUsb;

		std::array<uint8_t, 5> registerWriteBuffer;

		WindowsOverlappedEvent readOverlapped;
		WindowsOverlappedEvent writeOverlapped;
		WindowsOverlappedEvent registerOverlapped;

		std::optional<WinUsbOverlappedIO> readIO;
		std::optional<WinUsbOverlappedIO> writeIO;
		std::optional<WinUsbOverlappedIO> registerIO;
	};

}
