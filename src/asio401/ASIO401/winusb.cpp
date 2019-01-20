#include "winusb.h"

#include "log.h"

#include "../ASIO401Util/windows_error.h"
#include "../ASIO401Util/windows_handle.h"

#include <dechamps_cpputil/string.h>

namespace asio401 {

	namespace {

		void ValidateOverlapped(const OVERLAPPED& overlapped) {
			if (overlapped.hEvent == NULL) {
				Log() << "WinUSB overlapped I/O " << &overlapped << " doesn't have a valid event";
				throw std::runtime_error("WinUSB overlapped I/O doesn't have a valid event");
			}
		}
		void PrepareOverlapped(OVERLAPPED& overlapped) {
			ValidateOverlapped(overlapped);

			const auto result = ::WaitForSingleObject(overlapped.hEvent, 0);
			if (result != WAIT_TIMEOUT) {
				if (result == WAIT_OBJECT_0) {
					Log() << "WinUSB overlapped I/O " << &overlapped << " event is initially signaled";
					throw std::runtime_error("WinUSB overlapped I/O event is initially signaled");
				}
				const auto error = GetWindowsErrorString(GetLastError());
				Log() << "Error during initial validation of WinUSB overlapped I/O " << &overlapped << ": wait failed with " << result << ", " << error;
				throw std::runtime_error("Error during initial validation of WinUSB overlapped I/O: wait failed with " + std::to_string(result) + ", " + error);
			}

			{
				const auto eventHandle = overlapped.hEvent;
				overlapped = { 0 };
				overlapped.hEvent = eventHandle;
			}
		}

	}

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

	WinUsbOverlappedIO WinUsbWrite(WINUSB_INTERFACE_HANDLE winusbInterfaceHandle, UCHAR pipeId, const void* data, size_t size, OVERLAPPED& overlapped) {
		Log() << "Writing " << size << " bytes to WinUSB pipe " << GetUsbPipeIdString(pipeId) << " using overlapped I/O " << &overlapped;
		PrepareOverlapped(overlapped);
		if (WinUsb_WritePipe(winusbInterfaceHandle, pipeId, reinterpret_cast<PUCHAR>(const_cast<void*>(data)), ULONG(size), /*LengthTransferred=*/NULL, &overlapped) != FALSE || GetLastError() != ERROR_IO_PENDING) {
			throw std::runtime_error("Unable to write " + std::to_string(size) + " bytes to WinUSB pipe " + GetUsbPipeIdString(pipeId) + ": " + GetWindowsErrorString(GetLastError()));
		}
		return WinUsbOverlappedIO(winusbInterfaceHandle, overlapped, size);
	}

	WinUsbOverlappedIO WinUsbRead(WINUSB_INTERFACE_HANDLE winusbInterfaceHandle, UCHAR pipeId, void* data, size_t size, OVERLAPPED& overlapped) {
		Log() << "Reading " << size << " bytes from WinUSB pipe " << GetUsbPipeIdString(pipeId) << " using overlapped I/O " << &overlapped;
		PrepareOverlapped(overlapped);
		if (WinUsb_ReadPipe(winusbInterfaceHandle, pipeId, reinterpret_cast<PUCHAR>(data), ULONG(size), /*LengthTransferred=*/NULL, &overlapped) != FALSE || GetLastError() != ERROR_IO_PENDING) {
			throw std::runtime_error("Unable to read " + std::to_string(size) + " bytes from WinUSB pipe " + GetUsbPipeIdString(pipeId) + ": " + GetWindowsErrorString(GetLastError()));
		}
		return WinUsbOverlappedIO(winusbInterfaceHandle, overlapped, size);
	}

	void WinUsbAbort(WINUSB_INTERFACE_HANDLE winusbInterfaceHandle, UCHAR pipeId) {
		Log() << "Aborting WinUSB pipe " << GetUsbPipeIdString(pipeId);
		if (WinUsb_AbortPipe(winusbInterfaceHandle, pipeId) != TRUE) {
			throw std::runtime_error("Unable to abort transfers on WinUSB pipe " + GetUsbPipeIdString(pipeId) + ": " + GetWindowsErrorString(GetLastError()));
		}
	}

	void WinUsbOverlappedIO::Forget() {
		Log() << "Forgetting about WinUSB overlapped I/O " << &state->overlapped;
		if (!state.has_value()) throw std::runtime_error("Attempted to forget already empty WinUSB overlapped I/O");
		if (::ResetEvent(state->overlapped.hEvent) == 0) {
			const auto error = GetWindowsErrorString(GetLastError());
			Log() << "Unable to reset event for WinUSB overlapped I/O " << &state->overlapped << ": " << error;
			throw std::runtime_error("Unable to reset event in WinUSB overlapped I/O");
		}
		state.reset();
	}

	WinUsbOverlappedIO::~WinUsbOverlappedIO() noexcept(false) {
		if (!state.has_value()) return;

		Log() << "Waiting for WinUSB overlapped I/O " << &state->overlapped << " to complete";
		ValidateOverlapped(state->overlapped);

		ULONG lengthTransferred = 0;
		std::optional<DWORD> getOverlappedResultError;
		if (::WinUsb_GetOverlappedResult(state->winusbInterfaceHandle, &state->overlapped, &lengthTransferred, /*bWait=*/TRUE) == 0) getOverlappedResultError = GetLastError();

		std::optional<DWORD> resetEventError;
		if (::ResetEvent(state->overlapped.hEvent) == 0) resetEventError = GetLastError();
		
		if (getOverlappedResultError.has_value()) {
			const auto error = GetWindowsErrorString(*getOverlappedResultError);
			Log() << "WinUSB overlapped I/O " << &state->overlapped << " failed: " << error;
			throw std::runtime_error("WinUSB overlapped  I/O failed: " + error);
		}
		if (lengthTransferred != state->size) {
			Log() << "Invalid length for WinUSB overlapped I/O " << &state->overlapped << ": expected " << state->size << " bytes, got " << lengthTransferred << " bytes";
			throw std::runtime_error("Unable to transfer " + std::to_string(lengthTransferred) + " bytes in WinUSB overlapped I/O");
		}
		if (resetEventError.has_value()) {
			const auto error = GetWindowsErrorString(*resetEventError);
			Log() << "Unable to reset event for WinUSB overlapped I/O " << &state->overlapped << ": " << error;
			throw std::runtime_error("Unable to reset event in WinUSB overlapped I/O");
		}

		Log() << "WinUSB overlapped I/O " << &state->overlapped << " successful";
	}

}