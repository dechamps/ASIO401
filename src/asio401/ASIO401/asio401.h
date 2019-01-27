#pragma once

#include "config.h"
#include "qa401.h"

#include <dechamps_ASIOUtil/asiosdk/asiosys.h>
#include <dechamps_ASIOUtil/asiosdk/asio.h>

#include <windows.h>

#include <atomic>
#include <optional>
#include <stdexcept>
#include <thread>
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

		void ControlPanel();

	private:
		class PreparedState {
		public:
			PreparedState(ASIO401& asio401, ASIOBufferInfo* asioBufferInfos, long numChannels, long bufferSizeInFrames, ASIOCallbacks* callbacks);
			PreparedState(const PreparedState&) = delete;
			PreparedState(PreparedState&&) = delete;

			bool IsRunning() const { return runningState != nullptr; }
			bool IsChannelActive(bool isInput, long channel) const;

			void GetLatencies(long* inputLatency, long* outputLatency);
			void Start();
			void Stop();

			void GetSamplePosition(ASIOSamples* sPos, ASIOTimeStamp* tStamp);

			void RequestReset();

		private:
			struct Buffers
			{
				Buffers(size_t bufferSetCount, size_t inputChannelCount, size_t outputChannelCount, size_t bufferSizeInFrames, size_t inputSampleSizeInBytes, size_t outputSampleSizeInBytes);
				~Buffers();
				uint8_t* GetInputBuffer(size_t bufferSetIndex, size_t channelIndex) { return buffers.data() + bufferSetIndex * GetBufferSetSizeInBytes() + channelIndex * GetInputBufferSizeInBytes(); }
				uint8_t* GetOutputBuffer(size_t bufferSetIndex, size_t channelIndex) { return GetInputBuffer(bufferSetIndex, inputChannelCount) + channelIndex * GetOutputBufferSizeInBytes(); }
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
				std::vector<uint8_t> buffers;
			};

			class RunningState {
			public:
				RunningState(PreparedState& preparedState);
				~RunningState();

				void GetSamplePosition(ASIOSamples* sPos, ASIOTimeStamp* tStamp) const;

			private:
				struct SamplePosition {
					ASIOSamples samples = { 0 };
					ASIOTimeStamp timestamp = { 0 };
				};

				class Registration {
				public:
					Registration(RunningState*& holder, RunningState& runningState) : holder(holder) {
						holder = &runningState;
					}
					~Registration() { holder = nullptr; }

				private:
					RunningState*& holder;
				};

				void RunThread() noexcept;

				PreparedState& preparedState;
				const ASIOSampleRate sampleRate;
				const bool host_supports_timeinfo;
				std::atomic<bool> stopRequested = false;
				std::atomic<SamplePosition> samplePosition;

				Registration registration{ preparedState.runningState, *this };
				std::thread thread;
			};

			ASIO401& asio401;
			
			const ASIOCallbacks callbacks;
			Buffers buffers;
			const std::vector<ASIOBufferInfo> bufferInfos;

			// RunningState will set runningState before ownedRunningState has finished constructing.
			// This allows PreparedState to properly forward stream callbacks that might fire before RunningState construction is fully complete.
			// During steady-state operation, runningState just points to *ownedRunningState.
			RunningState* runningState = nullptr;
			std::optional<RunningState> ownedRunningState;
		};

		const HWND windowHandle = nullptr;
		const Config config;
		QA401 qa401;

		ASIOSampleRate sampleRate = 48000;
		bool sampleRateWasAccessed = false;

		std::optional<PreparedState> preparedState;
	};

}