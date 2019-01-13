#pragma once

#include "../ASIO401Util/windows_handle.h"

#include <winusb.h>

#include <memory>
#include <string_view>

namespace asio401 {

	struct WinUsbInterfaceHandleDeleter {
		void operator()(WINUSB_INTERFACE_HANDLE winUsbInterfaceHandle);
	};
	using WinUsbInterfaceHandleUniquePtr = std::unique_ptr<void, WinUsbInterfaceHandleDeleter>;

	class WinUsbHandle {
	public:
		WinUsbHandle(WindowsHandleUniquePtr windowsFile, WinUsbInterfaceHandleUniquePtr winUSBInterface) :
			windowsFile(std::move(windowsFile)), winUsbInterface(std::move(winUSBInterface)) {}

		WINUSB_INTERFACE_HANDLE InterfaceHandle() { return winUsbInterface.get(); }

	private:
		WindowsHandleUniquePtr windowsFile;
		WinUsbInterfaceHandleUniquePtr winUsbInterface;
	};

	WinUsbHandle WinUsbOpen(std::string_view path);

}
