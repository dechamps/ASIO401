#include "qa401.h"

#include "log.h"

#include "../ASIO401Util/windows_error.h"

#include <dechamps_cpputil/string.h>

#include <winusb.h>

#include <set>

namespace asio401 {

	namespace {

		constexpr UCHAR registerPipeId = 0x02;
		constexpr UCHAR writePipeId = 0x04;
		constexpr UCHAR readPipeId = 0x86;

		const auto requiredPipeIds = { registerPipeId, writePipeId, readPipeId };

	}

	QA401::QA401(std::string_view devicePath) :
		winUsb(WinUsbOpen(devicePath)) {
		Validate();
		PrepareDeviceForStreaming();
	}

	void QA401::Validate() {
		Log() << "Querying QA401 USB interface descriptor";
		USB_INTERFACE_DESCRIPTOR usbInterfaceDescriptor = { 0 };
		if (WinUsb_QueryInterfaceSettings(winUsb.InterfaceHandle(), 0, &usbInterfaceDescriptor) != TRUE) {
			throw std::runtime_error("Unable to query USB interface descriptor: " + GetWindowsErrorString(GetLastError()));
		}

		Log() << "Number of endpoints: " << int(usbInterfaceDescriptor.bNumEndpoints);
		if (usbInterfaceDescriptor.bNumEndpoints == 0) {
			throw std::runtime_error("No USB endpoints - did you run the QuantAsylum Analyzer app first to configure the hardware?");
		}

		std::set<UCHAR> missingPipeIds = requiredPipeIds;
		for (UCHAR endpointIndex = 0; endpointIndex < usbInterfaceDescriptor.bNumEndpoints; ++endpointIndex) {
			Log() << "Querying pipe #" << int(endpointIndex);
			WINUSB_PIPE_INFORMATION pipeInformation = { 0 };
			if (WinUsb_QueryPipe(winUsb.InterfaceHandle(), 0, endpointIndex, &pipeInformation) != TRUE) {
				throw std::runtime_error("Unable to query WinUSB pipe #" + std::to_string(int(endpointIndex)) + ": " + GetWindowsErrorString(GetLastError()));
			}
			Log() << "Pipe information: " << DescribeWinUsbPipeInformation(pipeInformation);
			missingPipeIds.erase(pipeInformation.PipeId);
		}
		if (!missingPipeIds.empty()) {
			throw std::runtime_error("Could not find WinUSB pipe IDs: " + ::dechamps_cpputil::Join(missingPipeIds, ", ", ::dechamps_cpputil::CharAsNumber()));
		}
		
		Log() << "QA401 descriptors appear valid";
	}

	void QA401::PrepareDeviceForStreaming() {
		Log() << "Preparing QA401 device for streaming";

		// Black magic incantations provided by QuantAsylum.
		WriteRegister(4, 1);
		WriteRegister(4, 0);
		WriteRegister(4, 3);
		WriteRegister(4, 1);
		WriteRegister(4, 3);
		WriteRegister(4, 0);
		WriteRegister(5, 4);
		WriteRegister(6, 4);
		::Sleep(10);
		WriteRegister(6, 6);
		WriteRegister(6, 0);

		Log() << "QA401 now ready for streaming";
	}

	void QA401::WriteRegister(uint8_t registerNumber, uint32_t value) {
		Log() << "Writing " << value << " to QA401 register #" << int(registerNumber);
		uint8_t request[] = { registerNumber, uint8_t(value >> 24), uint8_t(value >> 16), uint8_t(value >> 8), uint8_t(value >> 0) };
		ULONG lengthTransferred = 0;
		if (WinUsb_WritePipe(winUsb.InterfaceHandle(), registerPipeId, request, sizeof(request), &lengthTransferred, /*Overlapped=*/NULL) != TRUE) {
			throw std::runtime_error("Unable to write " + std::to_string(sizeof(request)) + " bytes to register pipe: " + GetWindowsErrorString(GetLastError()));
		}
		if (lengthTransferred != sizeof(request)) {
			throw std::runtime_error("Unable to write more than " + std::to_string(lengthTransferred) + " out of " + std::to_string(sizeof(request)) + " bytes to register pipe");
		}
	}

}