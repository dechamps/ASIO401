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

	QA40x::~QA40x() {
		assert(!readIO.IsPending());
		assert(!writeIO.IsPending());
		assert(!registerIO.IsPending());
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

	void QA40x::StartWrite(std::span<const std::byte> buffer) {
		if (IsLoggingEnabled()) Log() << "Need to write " << buffer.size() << " bytes to QA40x";
		writeIO.Write(winUsb.InterfaceHandle(), writePipeId, buffer);
	}

	void QA40x::AbortWrite() {
		Log() << "Aborting QA40x write";
		WinUsbAbort(winUsb.InterfaceHandle(), writePipeId);
	}

	_Check_return_ QA40x::FinishResult QA40x::FinishWrite() {
		if (IsLoggingEnabled()) Log() << "Finishing QA40x write";
		return writeIO.Await();
	}

	void QA40x::StartRead(std::span<std::byte> buffer) {
		if (IsLoggingEnabled()) Log() << "Need to read " << buffer.size() << " bytes from QA40x";
		readIO.Read(winUsb.InterfaceHandle(), readPipeId, buffer);
	}

	void QA40x::AbortRead() {
		Log() << "Aborting QA40x read";
		// According to some sources, it would be a good idea to also call WinUsb_ResetPipe() here, as otherwise WinUsb_AbortPipe() may hang, e.g.:
		//   https://android.googlesource.com/platform/development/+/487b1deae9082ff68833adf9eb47d57557f8bf16/host/windows/usb/winusb/adb_winusb_endpoint_object.cpp#66
		// However in practice, if we implement this suggestion, and the process is abruptly terminated, then the next instance will hang on the first read from the read pipe! No idea why...
		WinUsbAbort(winUsb.InterfaceHandle(), readPipeId);
	}
	
	_Check_return_ QA40x::FinishResult QA40x::FinishRead() {
		if (IsLoggingEnabled()) Log() << "Finishing QA40x read";
		return readIO.Await();
	}

	void QA40x::StartWriteRegister(uint8_t registerNumber, uint32_t value) {
		if (IsLoggingEnabled()) Log() << "Writing " << value << " to QA40x register #" << int(registerNumber);
		registerWriteBuffer = { std::byte(registerNumber), std::byte(value >> 24), std::byte(value >> 16), std::byte(value >> 8), std::byte(value >> 0) };
		registerIO.Write(winUsb.InterfaceHandle(), registerPipeId, registerWriteBuffer);
	}

	void QA40x::AbortWriteRegister() {
		Log() << "Aborting QA40x register write";
		WinUsbAbort(winUsb.InterfaceHandle(), registerPipeId);
	}

	_Check_return_ QA40x::FinishResult QA40x::FinishWriteRegister() {
		if (IsLoggingEnabled()) Log() << "Finishing QA40x register write";
		return registerIO.Await();
	}

	void QA40x::WriteRegister(uint8_t registerNumber, uint32_t value) {
		StartWriteRegister(registerNumber, value);
		switch (FinishWriteRegister()) {
		case FinishResult::ABORTED:
			throw new std::runtime_error("QA40x register write was unexpectedly aborted");
		case FinishResult::SUCCESSFUL:
			break;
		}
	}

}