#include "QA40x.h"

#include "log.h"

#include "../ASIO401Util/windows_error.h"

#include <dechamps_cpputil/string.h>

#include <winusb.h>

#include <set>

namespace asio401 {

	QA40x::QA40x(std::string_view devicePath, UCHAR registerPipeId, UCHAR writePipeId, UCHAR readPipeId, const bool requiresApp) :
		registerPipeId(registerPipeId), writePipeId(writePipeId), readPipeId(readPipeId),
		winUsb(WinUsbOpen(devicePath)) {
		Validate(requiresApp);
	}

	void QA40x::Validate(const bool requiresApp) {
		Log() << "Querying QA40x USB interface descriptor";
		USB_INTERFACE_DESCRIPTOR usbInterfaceDescriptor = { 0 };
		if (WinUsb_QueryInterfaceSettings(winUsb.InterfaceHandle(), 0, &usbInterfaceDescriptor) != TRUE) {
			throw std::runtime_error("Unable to query USB interface descriptor: " + GetWindowsErrorString(GetLastError()));
		}

		Log() << "Number of endpoints: " << int(usbInterfaceDescriptor.bNumEndpoints);
		if (usbInterfaceDescriptor.bNumEndpoints == 0) {
			throw std::runtime_error(
				requiresApp ?
				"No USB endpoints - did you run the QuantAsylum Analyzer app first to configure the hardware?" :
				"No USB endpoints");
		}

		std::set<UCHAR> missingPipeIds = { registerPipeId, writePipeId, readPipeId };
		for (UCHAR endpointIndex = 0; endpointIndex < usbInterfaceDescriptor.bNumEndpoints; ++endpointIndex) {
			Log() << "Querying pipe #" << int(endpointIndex);
			WINUSB_PIPE_INFORMATION pipeInformation = { 0 };
			if (WinUsb_QueryPipe(winUsb.InterfaceHandle(), 0, endpointIndex, &pipeInformation) != TRUE) {
				throw std::runtime_error("Unable to query WinUSB pipe #" + std::to_string(int(endpointIndex)) + ": " + GetWindowsErrorString(GetLastError()));
			}
			Log() << "Pipe (" << GetUsbPipeIdString(pipeInformation.PipeId) << ") information: " << DescribeWinUsbPipeInformation(pipeInformation);
			missingPipeIds.erase(pipeInformation.PipeId);
		}
		if (!missingPipeIds.empty()) {
			throw std::runtime_error("Could not find WinUSB pipes: " + ::dechamps_cpputil::Join(missingPipeIds, ", ", GetUsbPipeIdString));
		}
		
		Log() << "QA40x descriptors appear valid";
	}

	void QA40x::AbortIO() {
		Log() << "Aborting all QA40x I/O";

		for (const auto& pipeId : { registerPipeId, writePipeId, readPipeId }) {
			// According to some sources, it would be a good idea to also call WinUsb_ResetPipe() here, as otherwise WinUsb_AbortPipe() may hang, e.g.:
			//   https://android.googlesource.com/platform/development/+/487b1deae9082ff68833adf9eb47d57557f8bf16/host/windows/usb/winusb/adb_winusb_endpoint_object.cpp#66
			// However in practice, if we implement this suggestion, and the process is abruptly terminated, then the next instance will hang on the first read from the read pipe! No idea why...
			WinUsbAbort(winUsb.InterfaceHandle(), pipeId);
		}
		for (const auto overlappedIO : { &readIO, &writeIO, &registerIO }) {
			if (!overlappedIO->IsPending()) continue;
			// It is not clear if we really need to get the overlapped result after an abort. WinUsb_AbortPipe() states
			// "this is a synchronous operation", which would seem to suggest the overlapped operation is done and we
			// could just forget about it. It's not clear if this is really the case though, and general Windows I/O
			// rules would normally require us to wait for cancelled operations to complete before we can clean up the
			// overlapped state. Given the ambiguity, let's err on the safe side and await the overlapped operation -
			// there are no real downsides to doing that anyway.
			overlappedIO->Wait(/*tolerateAborted=*/true);
		}
	}

	void QA40x::StartWrite(const std::byte* buffer, size_t size) {
		if (IsLoggingEnabled()) Log() << "Need to write " << size << " bytes to QA40x";
		writeIO.Write(winUsb.InterfaceHandle(), writePipeId, buffer, size);
	}

	void QA40x::FinishWrite() {
		if (!writeIO.IsPending()) return;
		if (IsLoggingEnabled()) Log() << "Finishing QA40x write";
		writeIO.Wait();
	}

	void QA40x::StartRead(std::byte* buffer, size_t size) {
		if (IsLoggingEnabled()) Log() << "Need to read " << size << " bytes from QA40x";
		readIO.Read(winUsb.InterfaceHandle(), readPipeId, buffer, size);
	}
	
	void QA40x::FinishRead() {
		if (!readIO.IsPending()) return;
		if (IsLoggingEnabled()) Log() << "Finishing QA40x read";
		readIO.Wait();
	}

	void QA40x::StartWriteRegister(uint8_t registerNumber, uint32_t value) {
		if (IsLoggingEnabled()) Log() << "Writing " << value << " to QA40x register #" << int(registerNumber);
		registerWriteBuffer = { std::byte(registerNumber), std::byte(value >> 24), std::byte(value >> 16), std::byte(value >> 8), std::byte(value >> 0) };
		registerIO.Write(winUsb.InterfaceHandle(), registerPipeId, registerWriteBuffer.data(), registerWriteBuffer.size());
	}

	void QA40x::FinishWriteRegister() {
		if (!registerIO.IsPending()) return;
		if (IsLoggingEnabled()) Log() << "Finishing QA40x register write";
		registerIO.Wait();
	}

}