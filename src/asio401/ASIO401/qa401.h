#pragma once

#include "winusb.h"

#include <string_view>

namespace asio401 {

	class QA401 {
	public:
		QA401(std::string_view devicePath);

		void Reset();
		void Start();
		void Write(const void* buffer, size_t size);
		void Read(void* buffer, size_t size);
		void Ping();

	private:
		void Validate();
		void WriteRegister(uint8_t registerNumber, uint32_t value);
		void WritePipe(UCHAR pipeId, const void* data, size_t size);
		void ReadPipe(UCHAR pipeId, void* data, size_t size);

		WinUsbHandle winUsb;
	};

}
