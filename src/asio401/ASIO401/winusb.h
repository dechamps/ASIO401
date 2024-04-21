#pragma once

#include "../ASIO401Util/windows_handle.h"

#include <winusb.h>

#include <memory>
#include <optional>
#include <string_view>

namespace asio401 {

	std::string GetUsbPipeIdString(UCHAR pipeId);
	std::string GetUsbdPipeTypeString(USBD_PIPE_TYPE usbdPipeType);
	std::string DescribeWinUsbPipeInformation(const WINUSB_PIPE_INFORMATION& winUsbPipeInformation);

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

	class WinUsbOverlappedIO {
	public:
		WinUsbOverlappedIO(WINUSB_INTERFACE_HANDLE winusbInterfaceHandle, OVERLAPPED& overlapped, size_t size) : state(std::in_place, winusbInterfaceHandle, overlapped, size) {}
		~WinUsbOverlappedIO();

		WinUsbOverlappedIO(const WinUsbOverlappedIO&) = delete;
		WinUsbOverlappedIO(WinUsbOverlappedIO&& other) { *this = std::move(other); }
		WinUsbOverlappedIO& operator=(const WinUsbOverlappedIO&) = delete;
		WinUsbOverlappedIO& operator=(WinUsbOverlappedIO&& other) {
			if (other.state.has_value()) state.emplace(*other.state); other.state.reset();
			return *this;
		}

		void Wait(bool tolerateAborted = false);

	private:
		struct State {
			State(WINUSB_INTERFACE_HANDLE winusbInterfaceHandle, OVERLAPPED& overlapped, size_t size) : winusbInterfaceHandle(winusbInterfaceHandle), overlapped(overlapped), size(size) {}

			const WINUSB_INTERFACE_HANDLE winusbInterfaceHandle;
			OVERLAPPED& overlapped;
			const size_t size;
		};
		std::optional<State> state;
	};

	WinUsbOverlappedIO WinUsbWrite(WINUSB_INTERFACE_HANDLE winusbInterfaceHandle, UCHAR pipeId, const void* data, size_t size, OVERLAPPED& overlapped);
	WinUsbOverlappedIO WinUsbRead(WINUSB_INTERFACE_HANDLE winusbInterfaceHandle, UCHAR pipeId, void* data, size_t size, OVERLAPPED& overlapped);
	void WinUsbAbort(WINUSB_INTERFACE_HANDLE winusbInterfaceHandle, UCHAR pipeId);

}
