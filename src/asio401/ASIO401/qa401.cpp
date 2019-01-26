#include "qa401.h"

#include "log.h"

#include "../ASIO401Util/windows_error.h"

#include <dechamps_cpputil/string.h>

#include <winusb.h>

#include <set>

namespace asio401 {

	namespace {

		constexpr UCHAR registerPipeId = 0x02;
		constexpr UCHAR writePipeId = 0x04;
		constexpr UCHAR readPipeId = 0x88;

		const auto requiredPipeIds = { registerPipeId, writePipeId, readPipeId };

	}

	constexpr QA401::RegisterWriteRequest::RegisterWriteRequest(uint8_t registerNumber, uint32_t value) :
		request({ registerNumber, uint8_t(value >> 24), uint8_t(value >> 16), uint8_t(value >> 8), uint8_t(value >> 0) }) {}

	uint8_t QA401::RegisterWriteRequest::getRegisterNumber() const { return request[0]; }

	uint32_t QA401::RegisterWriteRequest::getValue() const {
		return (request[1] << 24) + (request[2] << 16) + (request[1] << 8) + request[0];
	}

	QA401::QA401(std::string_view devicePath) :
		winUsb(WinUsbOpen(devicePath)) {
		Validate();
	}

	void QA401::Validate() {
		Log() << "Querying QA401 USB interface descriptor";
		USB_INTERFACE_DESCRIPTOR usbInterfaceDescriptor = { 0 };
		if (WinUsb_QueryInterfaceSettings(winUsb.InterfaceHandle(), 0, &usbInterfaceDescriptor) != TRUE) {
			throw std::runtime_error("Unable to query USB interface descriptor: " + GetWindowsErrorString(GetLastError()));
		}

		Log() << "Number of endpoints: " << int(usbInterfaceDescriptor.bNumEndpoints);
		if (usbInterfaceDescriptor.bNumEndpoints == 0) {
			throw std::runtime_error("No USB endpoints - did you run the QuantAsylum Analyzer app first to configure the hardware?");
		}

		std::set<UCHAR> missingPipeIds = requiredPipeIds;
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
		
		Log() << "QA401 descriptors appear valid";
	}

	void QA401::Reset(AttenuatorState attenuatorState, SampleRate sampleRate) {
		Log() << "Resetting QA401 with attenuator " << (attenuatorState == AttenuatorState::DISENGAGED ? "disengaged" : "engaged") << " and sample rate " << (sampleRate == SampleRate::KHZ48 ? "48 kHz" : "192 kHz");

		AbortIO();

		// Black magic incantations provided by QuantAsylum.
		WriteRegister(4, 1);
		WriteRegister(4, 0);
		WriteRegister(4, 3);
		WriteRegister(4, 1);
		WriteRegister(4, 3);
		WriteRegister(4, 0);
		// Note: according to QuantAsylum these parameters can be changed at any time, except the sample rate, which can only be changed on reset.
		WriteRegister(5, 
			(attenuatorState == AttenuatorState::DISENGAGED ? 0x02 : 0) |
			(sampleRate == SampleRate::KHZ48 ? 0x04 : 0)
		);
		WriteRegister(6, 4);
		::Sleep(10);
		WriteRegister(6, 6);
		WriteRegister(6, 0);

		// Read some input frames and throw them away, because they could be a remnant from the previous stream. See https://github.com/dechamps/ASIO401/issues/5
		// We have to do an equivalent write at the same time because otherwise this read would block forever. See https://github.com/dechamps/ASIO401/issues/10
		constexpr auto preReadFrameCount = 64;
		{
			std::array<uint8_t, preReadFrameCount * sampleSizeInBytes * outputChannelCount> writeBuffer = { 0 };
			StartWrite(writeBuffer.data(), writeBuffer.size());
			FinishWrite();
		}
		{
			std::array<uint8_t, preReadFrameCount * sampleSizeInBytes * inputChannelCount> readBuffer;
			StartRead(readBuffer.data(), readBuffer.size());
			WriteRegister(4, 5);
			FinishRead();
		}
		WriteRegister(4, 0);

		Log() << "QA401 is reset";
	}

	void QA401::AbortIO() {
		Log() << "Aborting all QA401 I/O";

		for (const auto& pipeId : requiredPipeIds) {
			WinUsbAbort(winUsb.InterfaceHandle(), pipeId);
		}
		for (const auto overlappedIO : { &readIO, &writeIO, &pingIO }) {
			if (!overlappedIO->has_value()) continue;
			(*overlappedIO)->Forget();
			overlappedIO->reset();
		}
	}

	void QA401::Start() {
		Log() << "Starting QA401 streaming";

		// Black magic incantation provided by QuantAsylum.
		WriteRegister(4, 5);
	}

	void QA401::StartWrite(const void* buffer, size_t size) {
		Log() << "Need to write " << size << " bytes to QA401";
		if (writeIO.has_value()) throw std::runtime_error("Attempted to start a QA401 write while one is already in flight");
		writeIO = WinUsbWrite(winUsb.InterfaceHandle(), writePipeId, buffer, size, writeOverlapped.getOverlapped());
	}

	void QA401::FinishWrite() {
		if (!writeIO.has_value()) return;
		Log() << "Finishing QA401 write";
		writeIO->Wait();
		writeIO.reset();
	}

	void QA401::StartRead(void* buffer, size_t size) {
		Log() << "Need to read " << size << " bytes from QA401";
		if (readIO.has_value()) throw std::runtime_error("Attempted to start a QA401 read while one is already in flight");
		readIO = WinUsbRead(winUsb.InterfaceHandle(), readPipeId, buffer, size, readOverlapped.getOverlapped());
	}
	
	void QA401::FinishRead() {
		if (!readIO.has_value()) return;
		Log() << "Finishing QA401 read";
		readIO->Wait();
		readIO.reset();
	}

	void QA401::Ping() {
		if (pingIO.has_value()) {
			pingIO->Wait();
			pingIO.reset();
		}

		// Black magic incantation provided by QuantAsylum. It's not clear what this is for; it only seems to keep the "Link" LED on during streaming.
		static constexpr RegisterWriteRequest pingRequest(7, 3);
		pingIO = WriteRegister(pingRequest, pingOverlapped.getOverlapped());
	}

	void QA401::WriteRegister(uint8_t registerNumber, uint32_t value) {
		RegisterWriteRequest registerWriteRequest(registerNumber, value);
		WindowsOverlappedEvent overlappedEvent;
		WriteRegister(registerWriteRequest, overlappedEvent.getOverlapped()).Wait();
	}

	WinUsbOverlappedIO QA401::WriteRegister(const RegisterWriteRequest& request, OVERLAPPED& overlapped) {
		Log() << "Writing " << request.getValue() << " to QA401 register #" << int(request.getRegisterNumber());
		return WinUsbWrite(winUsb.InterfaceHandle(), registerPipeId, request.data(), request.size(), overlapped);
	}

}