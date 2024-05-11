#pragma once

#include "winusb.h"

#include <array>
#include <string_view>

namespace asio401 {

	class QA40x {
	public:
		QA40x(std::string_view devicePath, UCHAR registerPipeId, UCHAR writePipeId, UCHAR readPipeId, bool requiresApp);
		~QA40x();

		using FinishResult = ReusableWinUsbOverlappedIO::AwaitResult;

		void StartWriteRegister(uint8_t registerNumber, uint32_t value);
		void AbortWriteRegister();
		_Check_return_ QA40x::FinishResult FinishWriteRegister();
		void WriteRegister(uint8_t registerNumber, uint32_t value);

		void StartWrite(std::span<const std::byte> buffer);
		_Check_return_ bool WritePending() const { return writeIO.IsPending(); }
		void AbortWrite();
		_Check_return_ FinishResult FinishWrite();

		void StartRead(std::span<std::byte> buffer);
		_Check_return_ bool ReadPending() const { return readIO.IsPending(); }
		void AbortRead();
		_Check_return_ FinishResult FinishRead();

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
