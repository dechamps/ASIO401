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
	}

}