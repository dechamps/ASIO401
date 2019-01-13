#include "winusb.h"

#include "log.h"

#include "../ASIO401Util/windows_error.h"
#include "../ASIO401Util/windows_handle.h"

#include <dechamps_cpputil/string.h>

namespace asio401 {

	std::string GetUsbdPipeTypeString(USBD_PIPE_TYPE usbdPipeType) {
		return ::dechamps_cpputil::EnumToString(usbdPipeType, {
			{ UsbdPipeTypeControl, "Control" },
			{ UsbdPipeTypeIsochronous, "Isochronous" },
			{ UsbdPipeTypeBulk, "Bulk" },
			{ UsbdPipeTypeInterrupt, "Interrupt" },
			});
	}

	std::string DescribeWinUsbPipeInformation(const WINUSB_PIPE_INFORMATION& winUsbPipeInformation) {
		std::stringstream result;
		result.fill('0');
		result << "WINUSB_PIPE_INFORMATION with PipeType "
			<< GetUsbdPipeTypeString(winUsbPipeInformation.PipeType) << ", PipeId 0x"
			<< std::hex << std::setw(2) << int(winUsbPipeInformation.PipeId) << std::dec << " ("
			<< (winUsbPipeInformation.PipeId & 0x80 ? "IN" : "OUT") << "), MaximumPacketSize "
			<< winUsbPipeInformation.MaximumPacketSize << ", Interval "
			<< int(winUsbPipeInformation.Interval);
		return result.str();
	}

	void WinUsbInterfaceHandleDeleter::operator()(WINUSB_INTERFACE_HANDLE winUsbInterfaceHandle) {
		if (WinUsb_Free(winUsbInterfaceHandle) != TRUE) {
			Log() << "Unable to free WinUSB handle: " << GetWindowsErrorString(::GetLastError());
		}
	}

	WinUsbHandle WinUsbOpen(std::string_view path) {
		Log() << "Opening file handle for USB device at path: " << path;
		WindowsHandleUniquePtr windowsFile(CreateFileA(std::string(path).c_str(), GENERIC_WRITE | GENERIC_READ, /*dwShareMode=*/0, /*lpSecurityAttributes=*/NULL, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, /*hTemplateFile=*/NULL));
		if (windowsFile.get() == INVALID_HANDLE_VALUE) {
			const auto error = GetLastError();
			if (error == ERROR_ACCESS_DENIED) throw std::runtime_error("USB device access denied. Is it being used by another application? " + GetWindowsErrorString(error));
			throw std::runtime_error("Unable to open USB device file: " + GetWindowsErrorString(error));
		}

		Log() << "Initializing WinUSB";
		WINUSB_INTERFACE_HANDLE winUsbInterfaceHandle = NULL;
		if (WinUsb_Initialize(windowsFile.get(), &winUsbInterfaceHandle) != TRUE || winUsbInterfaceHandle == NULL) {
			throw std::runtime_error("Unable to initialize WinUSB: " + GetWindowsErrorString(GetLastError()));
		}

		Log() << "WinUSB initialized";
		return { std::move(windowsFile), WinUsbInterfaceHandleUniquePtr(winUsbInterfaceHandle) };
	}

}