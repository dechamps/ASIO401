#include "winusb.h"

#include "log.h"

#include "../ASIO401Util/windows_error.h"
#include "../ASIO401Util/windows_handle.h"

#include <dechamps_cpputil/string.h>

namespace asio401 {

	std::string GetUsbPipeIdString(UCHAR pipeId) {
		std::stringstream result;
		result.fill('0');
		result << "Pipe ID 0x" << std::hex << std::setw(2) << int(pipeId) << std::dec << " [" << (pipeId & 0x80 ? "IN" : "OUT") << "]";
		return result.str();
	}

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

	WinUsbOverlappedIO::WinUsbOverlappedIO(Write, WINUSB_INTERFACE_HANDLE winusbInterfaceHandle, UCHAR pipeId, const std::byte* data, size_t size, WindowsReusableEvent& windowsReusableEvent) :
		winusbInterfaceHandle(winusbInterfaceHandle), size(size), windowsOverlappedEvent(windowsReusableEvent) {
		if (IsLoggingEnabled()) Log() << "Writing " << size << " bytes to WinUSB pipe " << GetUsbPipeIdString(pipeId) << " using overlapped I/O " << this;
		if (WinUsb_WritePipe(winusbInterfaceHandle, pipeId, reinterpret_cast<PUCHAR>(const_cast<std::byte*>(data)), ULONG(size), /*LengthTransferred=*/NULL, &windowsOverlappedEvent.getOverlapped()) != FALSE || GetLastError() != ERROR_IO_PENDING) {
			throw std::runtime_error("Unable to write " + std::to_string(size) + " bytes to WinUSB pipe " + GetUsbPipeIdString(pipeId) + ": " + GetWindowsErrorString(GetLastError()));
		}
	}

	WinUsbOverlappedIO::WinUsbOverlappedIO(Read, WINUSB_INTERFACE_HANDLE winusbInterfaceHandle, UCHAR pipeId, std::byte* data, size_t size, WindowsReusableEvent& windowsReusableEvent) :
		winusbInterfaceHandle(winusbInterfaceHandle), size(size), windowsOverlappedEvent(windowsReusableEvent) {
		if (IsLoggingEnabled()) Log() << "Reading " << size << " bytes from WinUSB pipe " << GetUsbPipeIdString(pipeId) << " using overlapped I/O " << this;
		if (WinUsb_ReadPipe(winusbInterfaceHandle, pipeId, reinterpret_cast<PUCHAR>(data), ULONG(size), /*LengthTransferred=*/NULL, &windowsOverlappedEvent.getOverlapped()) != FALSE || GetLastError() != ERROR_IO_PENDING) {
			throw std::runtime_error("Unable to read " + std::to_string(size) + " bytes from WinUSB pipe " + GetUsbPipeIdString(pipeId) + ": " + GetWindowsErrorString(GetLastError()));
		}
	}

	void WinUsbAbort(WINUSB_INTERFACE_HANDLE winusbInterfaceHandle, UCHAR pipeId) {
		Log() << "Aborting WinUSB pipe " << GetUsbPipeIdString(pipeId);
		if (WinUsb_AbortPipe(winusbInterfaceHandle, pipeId) != TRUE) {
			throw std::runtime_error("Unable to abort transfers on WinUSB pipe " + GetUsbPipeIdString(pipeId) + ": " + GetWindowsErrorString(GetLastError()));
		}
	}

	void WinUsbOverlappedIO::Wait(bool tolerateAborted) {
		if (IsLoggingEnabled()) Log() << "Waiting for WinUSB overlapped I/O " << this << " to complete";

#ifndef NDEBUG
		awaited = true;
#endif

		ULONG lengthTransferred = 0;
		std::optional<DWORD> getOverlappedResultError;
		if (::WinUsb_GetOverlappedResult(winusbInterfaceHandle, &windowsOverlappedEvent.getOverlapped(), &lengthTransferred, /*bWait=*/TRUE) == 0) getOverlappedResultError = GetLastError();
		
		if (getOverlappedResultError.has_value()) {
			if (tolerateAborted && *getOverlappedResultError == ERROR_OPERATION_ABORTED) {
				if (IsLoggingEnabled()) Log() << "WinUSB overlapped I/O " << this << " aborted as expected";
				return;
			}

			const auto error = GetWindowsErrorString(*getOverlappedResultError);
			Log() << "WinUSB overlapped I/O " << this << " failed: " << error;
			throw std::runtime_error("WinUSB overlapped I/O failed: " + error);
		}
		if (lengthTransferred != size) {
			Log() << "Invalid length for WinUSB overlapped I/O " << this << ": expected " << size << " bytes, got " << lengthTransferred << " bytes";
			throw std::runtime_error("Unable to transfer " + std::to_string(lengthTransferred) + " bytes in WinUSB overlapped I/O");
		}

		if (IsLoggingEnabled()) Log() << "WinUSB overlapped I/O " << this << " successful";
	}

}