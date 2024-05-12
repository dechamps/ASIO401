#include "QA40x.h"

#include "log.h"

#include "../ASIO401Util/variant.h"
#include "../ASIO401Util/windows_error.h"

#include <dechamps_cpputil/string.h>

#include <winusb.h>

#include <cassert>
#include <set>
#include <string_view>

namespace asio401 {

	namespace {

		template <QA40x::ChannelType channelType>
		constexpr std::string_view channelName = [] {
			switch (channelType) {
			case QA40x::ChannelType::REGISTER: return "register";
			case QA40x::ChannelType::WRITE: return "write";
			case QA40x::ChannelType::READ: return "read";
			}
		}();

	}

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

	template <QA40x::ChannelType channelType>
	QA40x::Channel<channelType>::Channel(QA40x& qa40x) :
		winUsbInterfaceHandle(qa40x.winUsb.InterfaceHandle()),
		pipeId([&] {
			if constexpr (channelType == ChannelType::REGISTER) {
				return qa40x.registerPipeId;
			}
			else if constexpr (channelType == ChannelType::WRITE) {
				return qa40x.writePipeId;
			}
			else if constexpr (channelType == ChannelType::READ) {
				return qa40x.readPipeId;
			}
		}()) {}

	template <QA40x::ChannelType channelType>
	void QA40x::Channel<channelType>::Abort() {
		if (IsLoggingEnabled()) Log() << "Aborting all QA40x " << channelName<channelType> << " pending operations";
		// According to some sources, it would be a good idea to also call WinUsb_ResetPipe() here, as otherwise WinUsb_AbortPipe() may hang, e.g.:
		//   https://android.googlesource.com/platform/development/+/487b1deae9082ff68833adf9eb47d57557f8bf16/host/windows/usb/winusb/adb_winusb_endpoint_object.cpp#66
		// However in practice, if we implement this suggestion, and the process is abruptly terminated, then the next instance will hang on the first read from the read pipe! No idea why...
		WinUsbAbort(winUsbInterfaceHandle, pipeId);
	}

	template <QA40x::ChannelType channelType>
	QA40x::Channel<channelType>::Pending::Pending(Channel channel, uint8_t registerNumber, uint32_t value, WindowsReusableEvent& windowsReusableEvent) requires (channelType == ChannelType::REGISTER) :
		typeSpecific([&] {
			if (IsLoggingEnabled()) Log() << "Writing " << value << " to QA40x register #" << int(registerNumber) << " as pending operation " << this;
			return TypeSpecific<>{ .buffer = { std::byte(registerNumber), std::byte(value >> 24), std::byte(value >> 16), std::byte(value >> 8), std::byte(value >> 0) } };
		}()),
		winUsbOverlappedIO(channel.winUsbInterfaceHandle, channel.pipeId, WinUsbOverlappedIO::Write(typeSpecific.buffer), windowsReusableEvent) {}

	template <QA40x::ChannelType channelType>
	QA40x::Channel<channelType>::Pending::Pending(Channel channel, std::span<const std::byte> buffer, WindowsReusableEvent& windowsReusableEvent) requires (channelType == ChannelType::WRITE) :
		typeSpecific([&] {
			if (IsLoggingEnabled()) Log() << "Writing " << buffer.size() << " bytes to QA40x" << " as pending operation " << this;
			assert(!buffer.empty());
			return TypeSpecific<>{};
		}()),
		winUsbOverlappedIO(channel.winUsbInterfaceHandle, channel.pipeId, WinUsbOverlappedIO::Write(buffer), windowsReusableEvent) {}

	template <QA40x::ChannelType channelType>
	QA40x::Channel<channelType>::Pending::Pending(Channel channel, std::span<std::byte> buffer, WindowsReusableEvent& windowsReusableEvent) requires (channelType == ChannelType::READ) :
		typeSpecific([&] {
			if (IsLoggingEnabled()) Log() << "Reading " << buffer.size() << " bytes from QA40x" << " as pending operation " << this;
			assert(!buffer.empty());
			return TypeSpecific<>{};
		}()),
		winUsbOverlappedIO(channel.winUsbInterfaceHandle, channel.pipeId, WinUsbOverlappedIO::Read(buffer), windowsReusableEvent) {}

	template <QA40x::ChannelType channelType>
	_Check_return_ QA40x::AwaitResult QA40x::Channel<channelType>::Pending::Await() {
		if (IsLoggingEnabled()) Log() << "Awaiting result of QA40x pending " << channelName<channelType> << " operation " << this;
		return winUsbOverlappedIO.Await();
	}

	template QA40x::RegisterChannel;
	template QA40x::WriteChannel;
	template QA40x::ReadChannel;

	template <QA40x::ChannelType channelType>
	QA40x::AwaitResult QA40xIOSlot<channelType>::Await() {
		assert(pending.has_value());
		auto result = pending->Await();
		pending.reset();
		return result;
	}

	template <QA40x::ChannelType channelType>
	void QA40xIOSlot<channelType>::AwaitRejectingAborted() {
		if (Await() == QA40x::AwaitResult::ABORTED)
			throw std::runtime_error("QA40x" + std::string(channelName<channelType>) + " operation was unexpectedly aborted");
	}

	template <QA40x::ChannelType channelType> template <typename... Args>
	void QA40xIOSlot<channelType>::GenericStart(QA40x::Channel<channelType> channelType, Args&&... args) {
		assert(!pending.has_value());
		pending.emplace(channelType, std::forward<Args>(args)..., windowsReusableEvent);
	}

	template RegisterQA40xIOSlot;
	template WriteQA40xIOSlot;
	template ReadQA40xIOSlot;

}