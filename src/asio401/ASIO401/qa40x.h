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
		void StartWrite(const std::byte* buffer, size_t size);
		void FinishWrite();
		void StartRead(std::byte* buffer, size_t size);
		void FinishRead();
		void AbortIO();

	private:
		void Validate(bool requiresApp);

		const UCHAR registerPipeId;
		const UCHAR writePipeId;
		const UCHAR readPipeId;

		WinUsbHandle winUsb;

		std::array<std::byte, 5> registerWriteBuffer;

		ReusableWinUsbOverlappedIO readIO;
		ReusableWinUsbOverlappedIO writeIO;
		ReusableWinUsbOverlappedIO registerIO;
	};

}
