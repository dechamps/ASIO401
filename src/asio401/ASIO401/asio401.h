#pragma once

#include "config.h"
#include "qa401.h"
#include "qa403.h"

#include "../ASIO401Util/variant.h"

#include <dechamps_ASIOUtil/asiosdk/asiosys.h>
#include <dechamps_ASIOUtil/asiosdk/asio.h>

#include <windows.h>

#include <atomic>
#include <optional>
#include <stdexcept>
#include <mutex>
#include <thread>
#include <variant>
#include <vector>

namespace asio401 {

	class ASIOException : public std::runtime_error {
	public:
		template <typename... Args> ASIOException(ASIOError asioError, Args&&... args) : asioError(asioError), std::runtime_error(std::forward<Args>(args)...) {}
		ASIOError GetASIOError() const { return asioError; }

	private:
		ASIOError asioError;
	};

	class ASIO401 final {
	public:
		ASIO401(void* sysHandle);

		void GetBufferSize(long* minSize, long* maxSize, long* preferredSize, long* granularity);
		void GetChannels(long* numInputChannels, long* numOutputChannels);
		void GetChannelInfo(ASIOChannelInfo* info);
		bool CanSampleRate(ASIOSampleRate sampleRate);
		void SetSampleRate(ASIOSampleRate requestedSampleRate);
		void GetSampleRate(ASIOSampleRate* sampleRateResult);

		void CreateBuffers(ASIOBufferInfo* bufferInfos, long numChannels, long bufferSize, ASIOCallbacks* callbacks);
		void DisposeBuffers();

		void GetLatencies(long* inputLatency, long* outputLatency);
		void Start();
		void Stop();
		void GetSamplePosition(ASIOSamples* sPos, ASIOTimeStamp* tStamp);
		void OutputReady();

		void ControlPanel();

	private:
		using Device = std::variant<QA401, QA403>;

		class PreparedState {
		public:
			PreparedState(ASIO401& asio401, ASIOBufferInfo* asioBufferInfos, long numChannels, long bufferSizeInFrames, ASIOCallbacks* callbacks);
			PreparedState(const PreparedState&) = delete;
			PreparedState(PreparedState&&) = delete;

			bool IsRunning() const { return runningState.has_value(); }
			bool IsChannelActive(bool isInput, long channel) const;

			void GetLatencies(long* inputLatency, long* outputLatency);
			void Start();
			void Stop();

			void GetSamplePosition(ASIOSamples* sPos, ASIOTimeStamp* tStamp);
			void OutputReady();

			void RequestReset();

		private:
			struct Buffers
			{
				Buffers(size_t bufferSetCount, size_t inputChannelCount, size_t outputChannelCount, size_t bufferSizeInFrames, size_t inputSampleSizeInBytes, size_t outputSampleSizeInBytes);
				~Buffers();
				std::byte* GetInputBuffer(size_t bufferSetIndex, size_t channelIndex) { return buffers.data() + bufferSetIndex * GetBufferSetSizeInBytes() + channelIndex * GetInputBufferSizeInBytes(); }
				std::byte* GetOutputBuffer(size_t bufferSetIndex, size_t channelIndex) { return GetInputBuffer(bufferSetIndex, inputChannelCount) + channelIndex * GetOutputBufferSizeInBytes(); }
				size_t GetBufferSetSizeInBytes() const { return buffers.size() / bufferSetCount; }
				size_t GetInputBufferSizeInBytes() const { if (buffers.empty()) return 0; return bufferSizeInFrames * inputSampleSizeInBytes; }
				size_t GetOutputBufferSizeInBytes() const { if (buffers.empty()) return 0; return bufferSizeInFrames * outputSampleSizeInBytes; }

				const size_t bufferSetCount;
				const size_t inputChannelCount;
				const size_t outputChannelCount;
				const size_t bufferSizeInFrames;
				const size_t inputSampleSizeInBytes;
				const size_t outputSampleSizeInBytes;

				// This is a giant buffer containing all ASIO buffers. It is organized as follows:
				// [ input channel 0 buffer 0 ] [ input channel 1 buffer 0 ] ... [ input channel N buffer 0 ] [ output channel 0 buffer 0 ] [ output channel 1 buffer 0 ] .. [ output channel N buffer 0 ]
				// [ input channel 0 buffer 1 ] [ input channel 1 buffer 1 ] ... [ input channel N buffer 1 ] [ output channel 0 buffer 1 ] [ output channel 1 buffer 1 ] .. [ output channel N buffer 1 ]
				// The reason why this is a giant blob is to slightly improve performance by (theroretically) improving memory locality.
				std::vector<std::byte> buffers;
			};

			class RunningState {
			public:
				RunningState(PreparedState& preparedState);
				~RunningState();

				// Note: the reason why this is not done in the constructor is to allow `PreparedState::Start()`
				// to properly set `PreparedState::runningState` before callbacks start flying. This is because
				// the ASIO host application may decide to call GetSamplePosition() or OutputReady() as soon
				// as bufferSwitch() is called without waiting for Start() to return - we don't want these calls
				// to race with `PreparedState::Start()` constructing `PreparedState::runningState`.
				void Start() { thread = std::thread([&] { RunThread(); }); }

				void GetSamplePosition(ASIOSamples* sPos, ASIOTimeStamp* tStamp) const;
				void OutputReady();

			private:
				struct SamplePosition {
					ASIOSamples samples = { 0 };
					ASIOTimeStamp timestamp = { 0 };
				};

				template <QA40x::ChannelType channelType>
				class QA40xBuffer final {
				public:
					explicit QA40xBuffer(size_t size);

					std::span<std::byte> data();
					std::span<const std::byte> data() const;

					QA40xIOSlot<channelType>& GetIoSlot() { return ioSlot; }
					const QA40xIOSlot<channelType>& GetIoSlot() const { return ioSlot; }

				private:
					std::vector<std::byte> buffer;
					QA40xIOSlot<channelType> ioSlot;
				};

				void RunThread() noexcept;
				void SetupDevice();
				void TearDownDevice();
				void BufferSwitch(long driverBufferIndex, SamplePosition currentSamplePosition);
				void Abort();

				PreparedState& preparedState;
				const ASIOSampleRate sampleRate;
				const bool hostSupportsOutputReady;
				const bool host_supports_timeinfo;
				std::atomic<bool> stopRequested = false;
				std::atomic<SamplePosition> samplePosition;

				std::mutex outputReadyMutex;
				std::condition_variable outputReadyCondition;
				bool outputReady = true;

				std::thread thread;
			};

			ASIO401& asio401;
			
			const ASIOCallbacks callbacks;
			Buffers buffers;
			const std::vector<ASIOBufferInfo> bufferInfos;
			std::optional<RunningState> runningState;
		};

		template <class... Functors>
		auto WithDevice(Functors&&... functors) const { return OnVariant(device, std::forward<Functors>(functors)...); }
		template <class... Functors>
		auto WithDevice(Functors&&... functors) { return OnVariant(device, std::forward<Functors>(functors)...); }

		long GetDeviceInputChannelCount() const { return WithDevice([](const auto& device) { return device.inputChannelCount; }); }
		long GetDeviceOutputChannelCount() const { return WithDevice([](const auto& device) { return device.outputChannelCount; }); }
		::dechamps_cpputil::Endianness GetDeviceSampleEndianness() const { return WithDevice([](const auto& device) { return device.sampleEndianness; }); }
		size_t GetDeviceSampleSizeInBytes() const { return WithDevice([](const auto& device) { return device.sampleSizeInBytes; }); }
		size_t GetHardwareQueueSizeInFrames() const { return WithDevice([](const auto& device) { return device.hardwareQueueSizeInFrames; }); }
		size_t GetDeviceWriteGranularityInFrames() const { return WithDevice([](const auto& device) { return device.writeGranularityInFrames; }); }

		void ValidateConfig() const;

		struct BufferSizes {
			long minimum;
			long maximum;
			long preferred;
			long granularity;
		};
		BufferSizes ComputeBufferSizes() const;

		void ComputeLatencies(long* inputLatency, long* outputLatency, long bufferSizeInFrames, bool outputOnly) const;

		static Device GetDevice();

		const HWND windowHandle = nullptr;
		const Config config;
		Device device;

		ASIOSampleRate sampleRate = 48000;
		bool sampleRateWasAccessed = false;
		bool hostSupportsOutputReady = false;

		std::optional<PreparedState> preparedState;
	};

}