#include "QA40x.h"

#include "log.h"

#include "../ASIO401Util/windows_error.h"

#include <dechamps_cpputil/string.h>

#include <winusb.h>

#include <set>

namespace asio401 {

	QA40x::QA40x(std::string_view devicePath, UCHAR registerPipeId, UCHAR writePipeId, UCHAR readPipeId) :
		registerPipeId(registerPipeId), writePipeId(writePipeId), readPipeId(readPipeId),
		winUsb(WinUsbOpen(devicePath)) {
		Validate();
	}

	void QA40x::Validate() {
		Log() << "Querying QA40x USB interface descriptor";
		USB_INTERFACE_DESCRIPTOR usbInterfaceDescriptor = { 0 };
		if (WinUsb_QueryInterfaceSettings(winUsb.InterfaceHandle(), 0, &usbInterfaceDescriptor) != TRUE) {
			throw std::runtime_error("Unable to query USB interface descriptor: " + GetWindowsErrorString(GetLastError()));
		}

		Log() << "Number of endpoints: " << int(usbInterfaceDescriptor.bNumEndpoints);
		if (usbInterfaceDescriptor.bNumEndpoints == 0) {
			throw std::runtime_error("No USB endpoints - did you run the QuantAsylum Analyzer app first to configure the hardware?");
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
			WinUsbAbort(winUsb.InterfaceHandle(), pipeId);
		}
		for (const auto overlappedIO : { &readIO, &writeIO, &registerIO }) {
			if (!overlappedIO->has_value()) continue;
			// It is not clear if we really need to get the overlapped result after an abort. WinUsb_AbortPipe() states
			// "this is a synchronous operation", which would seem to suggest the overlapped operation is done and we
			// could just forget about it. It's not clear if this is really the case though, and general Windows I/O
			// rules would normally require us to wait for cancelled operations to complete before we can clean up the
			// overlapped state. Given the ambiguity, let's err on the safe side and await the overlapped operation -
			// there are no real downsides to doing that anyway.
			(*overlappedIO)->Wait(/*tolerateAborted=*/true);
			overlappedIO->reset();
		}
	}

	void QA40x::StartWrite(const void* buffer, size_t size) {
		if (IsLoggingEnabled()) Log() << "Need to write " << size << " bytes to QA40x";
		if (writeIO.has_value()) throw std::runtime_error("Attempted to start a QA40x write while one is already in flight");
		writeIO = WinUsbWrite(winUsb.InterfaceHandle(), writePipeId, buffer, size, writeOverlapped.getOverlapped());
	}

	void QA40x::FinishWrite() {
		if (!writeIO.has_value()) return;
		if (IsLoggingEnabled()) Log() << "Finishing QA40x write";
		writeIO->Wait();
		writeIO.reset();
	}

	void QA40x::StartRead(void* buffer, size_t size) {
		if (IsLoggingEnabled()) Log() << "Need to read " << size << " bytes from QA40x";
		if (readIO.has_value()) throw std::runtime_error("Attempted to start a QA40x read while one is already in flight");
		readIO = WinUsbRead(winUsb.InterfaceHandle(), readPipeId, buffer, size, readOverlapped.getOverlapped());
	}
	
	void QA40x::FinishRead() {
		if (!readIO.has_value()) return;
		if (IsLoggingEnabled()) Log() << "Finishing QA40x read";
		readIO->Wait();
		readIO.reset();
	}

	void QA40x::StartWriteRegister(uint8_t registerNumber, uint32_t value) {
		if (IsLoggingEnabled()) Log() << "Writing " << value << " to QA40x register #" << int(registerNumber);
		if (registerIO.has_value()) throw std::runtime_error("Attempted to start a QA40x register write while one is already in flight");
		registerWriteBuffer = { registerNumber, uint8_t(value >> 24), uint8_t(value >> 16), uint8_t(value >> 8), uint8_t(value >> 0) };
		registerIO = WinUsbWrite(winUsb.InterfaceHandle(), registerPipeId, registerWriteBuffer.data(), registerWriteBuffer.size(), registerOverlapped.getOverlapped());
	}

	void QA40x::FinishWriteRegister() {
		if (!registerIO.has_value()) return;
		if (IsLoggingEnabled()) Log() << "Finishing QA40x register write";
		registerIO->Wait();
		registerIO.reset();
	}

}