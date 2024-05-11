#pragma once

#include "winusb.h"

#include <array>
#include <span>
#include <vector>

namespace asio401 {

	class QA40x final {
	public:
		QA40x(std::string_view devicePath, UCHAR registerPipeId, UCHAR writePipeId, UCHAR readPipeId, bool requiresApp);

		enum class ChannelType { REGISTER, WRITE, READ };
		template <ChannelType channelType>
		struct Channel {
		public:
			explicit Channel(QA40x& qa40x);
			void Abort();

			class Pending {
			public:
				Pending(Channel, uint8_t registerNumber, uint32_t value, WindowsReusableEvent&) requires (channelType == ChannelType::REGISTER);
				Pending(Channel, std::span<const std::byte> buffer, WindowsReusableEvent&) requires (channelType == ChannelType::WRITE);
				Pending(Channel, std::span<std::byte> buffer, WindowsReusableEvent&) requires (channelType == ChannelType::READ);

				Pending(const Pending&) = delete;
				Pending& operator=(const Pending&) = delete;

				using AwaitResult = WinUsbOverlappedIO::AwaitResult;
				_Check_return_ AwaitResult Await();

			private:
				Pending(QA40x&, UCHAR pipeId, WinUsbOverlappedIO::Operation, WindowsReusableEvent&);

				template <ChannelType = channelType> struct TypeSpecific final { TypeSpecific() = delete; };
				template <> struct TypeSpecific<ChannelType::REGISTER> {
					std::array<std::byte, 5> buffer;
				};
				template <> struct TypeSpecific<ChannelType::WRITE> { };
				template <> struct TypeSpecific<ChannelType::READ> { };
				[[no_unique_address, msvc::no_unique_address]] TypeSpecific<> typeSpecific;

				WinUsbOverlappedIO winUsbOverlappedIO;
			};

		private:
			WINUSB_INTERFACE_HANDLE winUsbInterfaceHandle;
			UCHAR pipeId;
		};
		using RegisterChannel = Channel<ChannelType::REGISTER>;
		using WriteChannel = Channel<ChannelType::WRITE>;
		using ReadChannel = Channel<ChannelType::READ>;

	private:
		void Validate(bool requiresApp);

		const UCHAR registerPipeId;
		const UCHAR writePipeId;
		const UCHAR readPipeId;

		WinUsbHandle winUsb;
	};
	extern template QA40x::RegisterChannel;
	extern template QA40x::WriteChannel;
	extern template QA40x::ReadChannel;

	template <QA40x::ChannelType channelType>
	class QA40xIOSlot final {
	public:
		QA40xIOSlot() = default;
		QA40xIOSlot(const QA40xIOSlot&) = delete;
		QA40xIOSlot& operator=(const QA40xIOSlot&) = delete;
		
		void Start(QA40x::Channel<channelType> channel, uint8_t registerNumber, uint32_t value) requires (channelType == QA40x::ChannelType::REGISTER) { GenericStart(channel, registerNumber, value); }
		void Start(QA40x::Channel<channelType> channel, std::span<const std::byte> buffer) requires (channelType == QA40x::ChannelType::WRITE) { GenericStart(channel, buffer); }
		void Start(QA40x::Channel<channelType> channel, std::span<std::byte> buffer) requires (channelType == QA40x::ChannelType::READ) { GenericStart(channel, buffer); }

		template <typename... Args>
		void Execute(QA40x::Channel<channelType> channel, Args&&... args) {
			Start(channel, std::forward<Args>(args)...);
			AwaitRejectingAborted();
		}
		
		_Check_return_ bool HasPending() const { return pending.has_value(); }
		_Check_return_ QA40x::Channel<channelType>::Pending::AwaitResult Await();
		
	private:
		template <typename... Args> void GenericStart(QA40x::Channel<channelType>, Args&&...);
		void AwaitRejectingAborted();

		WindowsReusableEvent windowsReusableEvent;
		std::optional<typename QA40x::Channel<channelType>::Pending> pending;
	};
	using RegisterQA40xIOSlot = QA40xIOSlot<QA40x::ChannelType::REGISTER>;
	using WriteQA40xIOSlot = QA40xIOSlot<QA40x::ChannelType::WRITE>;
	using ReadQA40xIOSlot = QA40xIOSlot<QA40x::ChannelType::READ>;
	extern template RegisterQA40xIOSlot;
	extern template WriteQA40xIOSlot;
	extern template ReadQA40xIOSlot;

}
