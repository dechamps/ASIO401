#pragma once

#include "winusb.h"

#include <string_view>

namespace asio401 {

	class QA401 {
	public:
		QA401(std::string_view devicePath);

	private:
		void Validate();
		void PrepareDeviceForStreaming();
		void WriteRegister(uint8_t registerNumber, uint32_t value);

		WinUsbHandle winUsb;
	};

}
