#pragma once

#include "../ASIO401Util/windows_handle.h"

#include <winusb.h>

#include <cassert>
#include <memory>
#include <optional>
#include <string_view>
#include <span>

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
		struct Read final {};
		struct Write final {};

		WinUsbOverlappedIO(Write, WINUSB_INTERFACE_HANDLE, UCHAR pipeId, std::span<const std::byte> buffer, WindowsReusableEvent&);
		WinUsbOverlappedIO(Read, WINUSB_INTERFACE_HANDLE, UCHAR pipeId, std::span<std::byte> buffer, WindowsReusableEvent&);

		~WinUsbOverlappedIO() { assert(awaited); }

		WinUsbOverlappedIO(const WinUsbOverlappedIO&) = delete;
		WinUsbOverlappedIO& operator=(const WinUsbOverlappedIO&) = delete;

		void Wait(bool tolerateAborted = false);

	private:
		const WINUSB_INTERFACE_HANDLE winusbInterfaceHandle;
		const size_t size;
		WindowsOverlappedEvent windowsOverlappedEvent;
#ifndef NDEBUG
		bool awaited = false;
#endif
	};

	class ReusableWinUsbOverlappedIO final {
	public:
		ReusableWinUsbOverlappedIO() = default;
		ReusableWinUsbOverlappedIO(ReusableWinUsbOverlappedIO&) = delete;
		ReusableWinUsbOverlappedIO& operator=(ReusableWinUsbOverlappedIO&) = delete;

		void Write(WINUSB_INTERFACE_HANDLE winusbInterfaceHandle, UCHAR pipeId, std::span<const std::byte> buffer) {
			assert(!IsPending());
			overlappedIO.emplace(WinUsbOverlappedIO::Write(), winusbInterfaceHandle, pipeId, buffer, windowsReusableEvent);
		}
		void Read(WINUSB_INTERFACE_HANDLE winusbInterfaceHandle, UCHAR pipeId, std::span<std::byte> buffer) {
			assert(!IsPending());
			overlappedIO.emplace(WinUsbOverlappedIO::Read(), winusbInterfaceHandle, pipeId, buffer, windowsReusableEvent);
		}

		bool IsPending() const { return overlappedIO.has_value(); }

		void Wait(bool tolerateAborted = false) {
			assert(IsPending());;
			overlappedIO->Wait(tolerateAborted);
			overlappedIO.reset();
		}

	private:
		WindowsReusableEvent windowsReusableEvent;
		std::optional<WinUsbOverlappedIO> overlappedIO;
	};

	void WinUsbAbort(WINUSB_INTERFACE_HANDLE winusbInterfaceHandle, UCHAR pipeId);

}
