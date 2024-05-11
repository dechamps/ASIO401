#pragma once

#include "../ASIO401Util/windows_handle.h"

#include <winusb.h>

#include <cassert>
#include <memory>
#include <optional>
#include <string_view>
#include <span>
#include <variant>

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

	class WinUsbOverlappedIO final {
	public:
		struct Write final {
			explicit Write(std::span<const std::byte> buffer) : buffer(buffer) {}
			std::span<const std::byte> buffer;
		};
		struct Read final {
			explicit Read(std::span<std::byte> buffer) : buffer(buffer) {}
			std::span<std::byte> buffer;
		};
		using Operation = std::variant<Read, Write>;

		WinUsbOverlappedIO(WINUSB_INTERFACE_HANDLE, UCHAR pipeId, Operation, WindowsReusableEvent&);
		~WinUsbOverlappedIO() { assert(awaited); }
		WinUsbOverlappedIO(const WinUsbOverlappedIO&) = delete;
		WinUsbOverlappedIO& operator=(const WinUsbOverlappedIO&) = delete;

		enum class AwaitResult { SUCCESSFUL, ABORTED };
		_Check_return_ AwaitResult Await();

	private:
		const WINUSB_INTERFACE_HANDLE winusbInterfaceHandle;
		const size_t size;
		WindowsOverlappedEvent windowsOverlappedEvent;
#ifndef NDEBUG
		bool awaited = false;
#endif
	};

	void WinUsbAbort(WINUSB_INTERFACE_HANDLE winusbInterfaceHandle, UCHAR pipeId);

}
