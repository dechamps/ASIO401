#include "winusb.h"

#include "log.h"

#include "../ASIO401Util/windows_error.h"
#include "../ASIO401Util/windows_handle.h"

namespace asio401 {

	void WinUsbInterfaceHandleDeleter::operator()(WINUSB_INTERFACE_HANDLE winUsbInterfaceHandle) {
		if (WinUsb_Free(winUsbInterfaceHandle) != TRUE) {
			Log() << "Unable to free WinUSB handle: " << GetWindowsErrorString(::GetLastError());
		}
	}

	WinUsbHandle WinUsbOpen(std::string_view path) {
		Log() << "Opening file handle for USB device at path: " << path;
		WindowsHandleUniquePtr windowsFile(CreateFileA(std::string(path).c_str(), GENERIC_WRITE | GENERIC_READ, /*dwShareMode=*/0, /*lpSecurityAttributes=*/NULL, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, /*hTemplateFile=*/NULL));
		if (windowsFile.get() == INVALID_HANDLE_VALUE) {
			throw std::runtime_error("Unable to open USB device file: " + GetWindowsErrorString(GetLastError()));
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