#include "asio401.h"

#include "devices.h"

#include <cassert>
#include <algorithm>
#include <memory>
#include <mutex>
#include <string>
#include <sstream>
#include <string_view>
#include <vector>

#include <avrt.h>

#include <dechamps_cpputil/endian.h>
#include <dechamps_cpputil/string.h>

#include <dechamps_ASIOUtil/asio.h>

#include <dechamps_CMakeUtils/version.h>

#include "../ASIO401Util/windows_error.h"

#include "log.h"

namespace asio401 {

	namespace {

		class Win32HighResolutionTimer {
		public:
			Win32HighResolutionTimer() {
				Log() << "Starting high resolution timer";
				timeBeginPeriod(1);
			}
			Win32HighResolutionTimer(const Win32HighResolutionTimer&) = delete;
			Win32HighResolutionTimer(Win32HighResolutionTimer&&) = delete;
			~Win32HighResolutionTimer() {
				Log() << "Stopping high resolution timer";
				timeEndPeriod(1);
			}
			DWORD GetTimeMilliseconds() const { return timeGetTime(); }
		};

		class AvrtHighPriority {
		public:
			AvrtHighPriority() : avrtHandle([&] {
				Log() << "Setting thread characteristics";
				DWORD taskIndex = 0;
				auto avrtHandle = AvSetMmThreadCharacteristicsA("Pro Audio", &taskIndex);
				if (avrtHandle == 0) Log() << "Failed to set thread characteristics: " << GetWindowsErrorString(GetLastError());
				return avrtHandle;
			}()) {
				if (avrtHandle == 0) return;
				Log() << "Setting thread priority";
				if (AvSetMmThreadPriority(avrtHandle, AVRT_PRIORITY_CRITICAL) == 0) Log() << "Unable to set thread priority: " << GetWindowsErrorString(GetLastError());
			}

			~AvrtHighPriority() {
				Log() << "Reverting thread characteristics";
				if (AvRevertMmThreadCharacteristics(avrtHandle) == 0) Log() << "Failed to revert thread characteristics: " << GetWindowsErrorString(GetLastError());
			}

		private:
			const HANDLE avrtHandle;
		};

		std::optional<ASIOSampleRate> previousSampleRate;

		long Message(decltype(ASIOCallbacks::asioMessage) asioMessage, long selector, long value, void* message, double* opt) {
			Log() << "Sending message: selector = " << ::dechamps_ASIOUtil::GetASIOMessageSelectorString(selector) << ", value = " << value << ", message = " << message << ", opt = " << opt;
			const auto result = asioMessage(selector, value, message, opt);
			Log() << "Result: " << result;
			return result;
		}

		// This is purely for instrumentation - it makes it possible to see host capabilities in the log.
		// Such information could be used to inform future development (there's no point in supporting more ASIO features if host applications don't support them).
		void ProbeHostMessages(decltype(ASIOCallbacks::asioMessage) asioMessage) {
			for (const auto selector : {
				kAsioSelectorSupported, kAsioEngineVersion, kAsioResetRequest, kAsioBufferSizeChange,
				kAsioResyncRequest, kAsioLatenciesChanged, kAsioSupportsTimeInfo, kAsioSupportsTimeCode,
				kAsioMMCCommand, kAsioSupportsInputMonitor, kAsioSupportsInputGain, kAsioSupportsInputMeter,
				kAsioSupportsOutputGain, kAsioSupportsOutputMeter, kAsioOverload }) {
				Log() << "Probing for message selector: " << ::dechamps_ASIOUtil::GetASIOMessageSelectorString(selector);
				if (Message(asioMessage, kAsioSelectorSupported, selector, nullptr, nullptr) != 1) continue;

				switch (selector) {
				case kAsioEngineVersion:
					Message(asioMessage, kAsioEngineVersion, 0, nullptr, nullptr);
					break;
				}
			}
		}

		long GetBufferInfosChannelCount(const ASIOBufferInfo* asioBufferInfos, const long numChannels, const bool input) {
			long result = 0;
			for (long channelIndex = 0; channelIndex < numChannels; ++channelIndex)
				if (!asioBufferInfos[channelIndex].isInput == !input)
					++result;
			return result;
		}

		constexpr GUID qa401DeviceGUID = { 0xFDA49C5C, 0x7006, 0x4EE9, { 0x88, 0xB2, 0xA0, 0xF8, 0x06, 0x50, 0x81, 0x50 } };

		// According to QuantAsylum, QA401 uses 24-bit big-endian integer samples in 32-bit container, left-aligned
		constexpr ASIOSampleType qa401SampleType = ASIOSTInt32MSB;
		constexpr size_t qa401SampleSize = 4;
		constexpr size_t qa401HardwareQueueSizeInFrames = 1024;
		constexpr ASIOSampleRate qa401SampleRate = 48000;

	}

	ASIO401::ASIO401(void* sysHandle) :
		windowHandle(reinterpret_cast<decltype(windowHandle)>(sysHandle)),
		config([&] {
		const auto config = LoadConfig();
		if (!config.has_value()) throw ASIOException(ASE_HWMalfunction, "could not load ASIO401 configuration. See ASIO401 log for details.");
		return *config;
	}()), qa401([&] {
		const auto devicePath = GetDevicePath(qa401DeviceGUID);
		if (!devicePath.has_value()) {
			throw ASIOException(ASE_NotPresent, "QA401 USB device not found. Is it connected?");
		}
		return *devicePath;
	}()) {
		Log() << "sysHandle = " << sysHandle;
		qa401.Reset();
	}

	void ASIO401::GetBufferSize(long* minSize, long* maxSize, long* preferredSize, long* granularity)
	{
		if (config.bufferSizeSamples.has_value()) {
			Log() << "Using buffer size " << *config.bufferSizeSamples << " from configuration";
			*minSize = *maxSize = *preferredSize = long(*config.bufferSizeSamples);
			*granularity = 0;
		}
		else {
			*minSize = 64;  // Mostly arbitrary; based on the size of a single USB bulk transfer packet
			*preferredSize = qa401HardwareQueueSizeInFrames;  // Keeps the QA401 hardware queue filled at all times, good tradeoff between reliability and latency
			*maxSize = qa401HardwareQueueSizeInFrames;  // Larger buffers have been observed to cause glitches in the input
			*granularity = 64;  // Mostly arbitrary; based on the size of a single USB bulk transfer packet
		}
		Log() << "Returning: min buffer size " << *minSize << ", max buffer size " << *maxSize << ", preferred buffer size " << *preferredSize << ", granularity " << *granularity;
	}

	void ASIO401::GetChannels(long* numInputChannels, long* numOutputChannels)
	{
		*numInputChannels = GetInputChannelCount();
		*numOutputChannels = GetOutputChannelCount();
		Log() << "Returning " << *numInputChannels << " input channels and " << *numOutputChannels << " output channels";
	}

	namespace {
		std::string getChannelName(size_t channel)
		{
			std::stringstream channel_name;
			channel_name << channel;
			return channel_name.str();
		}
	}

	void ASIO401::GetChannelInfo(ASIOChannelInfo* info)
	{
		Log() << "CASIO401::getChannelInfo()";

		Log() << "Channel info requested for " << (info->isInput ? "input" : "output") << " channel " << info->channel;
		if (info->isInput)
		{
			if (info->channel < 0 || info->channel >= GetInputChannelCount()) throw ASIOException(ASE_InvalidParameter, "no such input channel");
		}
		else
		{
			if (info->channel < 0 || info->channel >= GetOutputChannelCount()) throw ASIOException(ASE_InvalidParameter, "no such output channel");
		}

		info->isActive = preparedState.has_value() && preparedState->IsChannelActive(info->isInput, info->channel);
		info->channelGroup = 0;
		info->type = qa401SampleType;
		std::stringstream channel_string;
		channel_string << (info->isInput ? "IN" : "OUT") << " " << getChannelName(info->channel);
		strcpy_s(info->name, 32, channel_string.str().c_str());
		Log() << "Returning: " << info->name << ", " << (info->isActive ? "active" : "inactive") << ", group " << info->channelGroup << ", type " << ::dechamps_ASIOUtil::GetASIOSampleTypeString(info->type);
	}

	bool ASIO401::CanSampleRate(ASIOSampleRate sampleRate)
	{
		Log() << "Checking for sample rate: " << sampleRate;
		return sampleRate == qa401SampleRate;
	}

	void ASIO401::GetSampleRate(ASIOSampleRate* sampleRateResult)
	{
		sampleRateWasAccessed = true;
		previousSampleRate = sampleRate;
		*sampleRateResult = sampleRate;
		Log() << "Returning sample rate: " << *sampleRateResult;
	}

	void ASIO401::SetSampleRate(ASIOSampleRate requestedSampleRate)
	{
		Log() << "Request to set sample rate: " << requestedSampleRate;

		if (!CanSampleRate(requestedSampleRate)) throw ASIOException(ASE_NoClock, "cannot do sample rate " + std::to_string(requestedSampleRate) + " Hz");

		sampleRateWasAccessed = true;
		previousSampleRate = requestedSampleRate;

		if (requestedSampleRate == sampleRate) {
			Log() << "Requested sampled rate is equal to current sample rate";
			return;
		}

		sampleRate = requestedSampleRate;
		if (preparedState.has_value())
		{
			Log() << "Sending a reset request to the host as it's not possible to change sample rate while streaming";
			preparedState->RequestReset();
		}
	}

	void ASIO401::CreateBuffers(ASIOBufferInfo* bufferInfos, long numChannels, long bufferSize, ASIOCallbacks* callbacks) {
		Log() << "Request to create buffers for " << numChannels << " channels, size " << bufferSize << " samples";
		if (numChannels < 1 || bufferSize < 1 || callbacks == nullptr || callbacks->bufferSwitch == nullptr)
			throw ASIOException(ASE_InvalidParameter, "invalid createBuffer() parameters");

		if (preparedState.has_value()) {
			throw ASIOException(ASE_InvalidMode, "createBuffers() called multiple times");
		}

		if (!sampleRateWasAccessed) {
			// See https://github.com/dechamps/FlexASIO/issues/31
			Log() << "WARNING: ASIO host application never enquired about sample rate, and therefore cannot know we are running at " << sampleRate << " Hz!";
		}

		preparedState.emplace(*this, sampleRate, bufferInfos, numChannels, bufferSize, callbacks);
	}

	ASIO401::PreparedState::Buffers::Buffers(size_t bufferSetCount, size_t inputChannelCount, size_t outputChannelCount, size_t bufferSizeInSamples, size_t inputSampleSize, size_t outputSampleSize) :
		bufferSetCount(bufferSetCount), inputChannelCount(inputChannelCount), outputChannelCount(outputChannelCount), bufferSizeInSamples(bufferSizeInSamples), inputSampleSize(inputSampleSize), outputSampleSize(outputSampleSize),
		buffers(bufferSetCount * bufferSizeInSamples * (inputChannelCount * inputSampleSize + outputChannelCount * outputSampleSize)) {
		Log() << "Allocated "
			<< bufferSetCount << " buffer sets, "
			<< inputChannelCount << "/" << outputChannelCount << " (I/O) channels per buffer set, "
			<< bufferSizeInSamples << " samples per channel, "
			<< inputSampleSize << "/" << outputSampleSize << " (I/O) bytes per sample, memory range: "
			<< static_cast<const void*>(buffers.data()) << "-" << static_cast<const void*>(buffers.data() + buffers.size());
	}

	ASIO401::PreparedState::Buffers::~Buffers() {
		Log() << "Destroying buffers";
	}

	ASIO401::PreparedState::PreparedState(ASIO401& asio401, ASIOSampleRate sampleRate, ASIOBufferInfo* asioBufferInfos, long numChannels, long bufferSizeInSamples, ASIOCallbacks* callbacks) :
		asio401(asio401), sampleRate(sampleRate), callbacks(*callbacks),
		buffers(
			2,
			GetBufferInfosChannelCount(asioBufferInfos, numChannels, true), GetBufferInfosChannelCount(asioBufferInfos, numChannels, false),
			bufferSizeInSamples, qa401SampleSize, qa401SampleSize),
		bufferInfos([&] {
		std::vector<ASIOBufferInfo> bufferInfos;
		bufferInfos.reserve(numChannels);
		size_t nextBuffersInputChannelIndex = 0;
		size_t nextBuffersOutputChannelIndex = 0;
		for (long channelIndex = 0; channelIndex < numChannels; ++channelIndex)
		{
			ASIOBufferInfo& asioBufferInfo = asioBufferInfos[channelIndex];
			if (asioBufferInfo.isInput)
			{
				if (asioBufferInfo.channelNum < 0 || asioBufferInfo.channelNum >= asio401.GetInputChannelCount())
					throw ASIOException(ASE_InvalidParameter, "out of bounds input channel in createBuffers() buffer info");
			}
			else
			{
				if (asioBufferInfo.channelNum < 0 || asioBufferInfo.channelNum >= asio401.GetOutputChannelCount())
					throw ASIOException(ASE_InvalidParameter, "out of bounds output channel in createBuffers() buffer info");
			}
			const auto getBuffer = asioBufferInfo.isInput ? &Buffers::GetInputBuffer : &Buffers::GetOutputBuffer;
			auto& nextBuffersChannelIndex = asioBufferInfo.isInput ? nextBuffersInputChannelIndex : nextBuffersOutputChannelIndex;
			const auto bufferSizeInBytes = asioBufferInfo.isInput ? buffers.GetInputBufferSizeInBytes() : buffers.GetOutputBufferSizeInBytes();

			uint8_t* first_half = (buffers.*getBuffer)(0, nextBuffersChannelIndex);
			uint8_t* second_half = (buffers.*getBuffer)(1, nextBuffersChannelIndex);
			++nextBuffersChannelIndex;
			asioBufferInfo.buffers[0] = first_half;
			asioBufferInfo.buffers[1] = second_half;
			Log() << "ASIO buffer #" << channelIndex << " is " << (asioBufferInfo.isInput ? "input" : "output") << " channel " << asioBufferInfo.channelNum
				<< " - first half: " << static_cast<const void*>(first_half) << "-" << static_cast<const void*>(first_half + bufferSizeInBytes)
				<< " - second half: " << static_cast<const void*>(second_half) << "-" << static_cast<const void*>(second_half + bufferSizeInBytes);
			bufferInfos.push_back(asioBufferInfo);
		}
		return bufferInfos;
	}()) {
		if (callbacks->asioMessage) ProbeHostMessages(callbacks->asioMessage);
	}

	bool ASIO401::PreparedState::IsChannelActive(bool isInput, long channel) const {
		for (const auto& buffersInfo : bufferInfos)
			if (!!buffersInfo.isInput == !!isInput && buffersInfo.channelNum == channel)
				return true;
		return false;
	}

	void ASIO401::DisposeBuffers()
	{
		if (!preparedState.has_value()) throw ASIOException(ASE_InvalidMode, "disposeBuffers() called before createBuffers()");
		preparedState.reset();
	}

	void ASIO401::GetLatencies(long* inputLatency, long* outputLatency) {
		if (!preparedState.has_value()) throw ASIOException(ASE_InvalidMode, "getLatencies() called before createBuffers()");
		return preparedState->GetLatencies(inputLatency, outputLatency);
	}

	void ASIO401::PreparedState::GetLatencies(long* inputLatency, long* outputLatency)
	{
		*inputLatency = long(buffers.bufferSizeInSamples);
		*outputLatency = long(buffers.bufferSizeInSamples) * 2;  // Because we don't support ASIOOutputReady() - see ASIO SDK docs, dechamps_ASIOUtil/BUFFERS.md
		Log() << "Returning input latency of " << *inputLatency << " samples and output latency of " << *outputLatency << " samples";
	}

	void ASIO401::Start() {
		if (!preparedState.has_value()) throw ASIOException(ASE_InvalidMode, "start() called before createBuffers()");
		return preparedState->Start();
	}

	void ASIO401::PreparedState::Start()
	{
		if (runningState != nullptr) throw ASIOException(ASE_InvalidMode, "start() called twice");
		ownedRunningState.emplace(*this);
	}

	ASIO401::PreparedState::RunningState::RunningState(PreparedState& preparedState) :
		preparedState(preparedState),
		host_supports_timeinfo([&] {
		Log() << "Checking if the host supports time info";
		const bool result = preparedState.callbacks.asioMessage &&
			Message(preparedState.callbacks.asioMessage, kAsioSelectorSupported, kAsioSupportsTimeInfo, NULL, NULL) == 1 &&
			Message(preparedState.callbacks.asioMessage, kAsioSupportsTimeInfo, 0, NULL, NULL) == 1;
		Log() << "The host " << (result ? "supports" : "does not support") << " time info";
		return result;
	}()), thread([&] { RunThread(); }) {
	}

	ASIO401::PreparedState::RunningState::~RunningState() {
		stopRequested = true;
		thread.join();
	}

	void ASIO401::PreparedState::RunningState::RunningState::RunThread() {
		std::vector<uint8_t> writeBuffer;
		if (preparedState.buffers.outputChannelCount > 0) {
			writeBuffer.resize(preparedState.buffers.bufferSizeInSamples * preparedState.asio401.GetOutputChannelCount() * preparedState.buffers.outputSampleSize);
		}
		// Note that we always read even if no input channels are enabled, because we use read operations to synchronize with the QA401 clock.
		std::vector<uint8_t> readBuffer(preparedState.buffers.bufferSizeInSamples * preparedState.asio401.GetInputChannelCount() * preparedState.buffers.inputSampleSize);
		long driverBufferIndex = 0;
		Win32HighResolutionTimer win32HighResolutionTimer;
		AvrtHighPriority avrtHighPriority;
		// Note: see ../dechamps_ASIOUtil/BUFFERS.md for an explanation of ASIO buffer management and operation order.
		size_t inputBuffersToSkip = 1;
		size_t outputQueueBufferCount = 0;
		bool started = false;
		while (!stopRequested) {
			auto currentSamplePosition = samplePosition.load();
			currentSamplePosition.timestamp = ::dechamps_ASIOUtil::Int64ToASIO<ASIOTimeStamp>(((long long int) win32HighResolutionTimer.GetTimeMilliseconds()) * 1000000);
			Log() << "Updated current timestamp: " << ::dechamps_ASIOUtil::ASIOToInt64(currentSamplePosition.timestamp);

			if (!host_supports_timeinfo) {
				Log() << "Firing ASIO bufferSwitch() callback with buffer index: " << driverBufferIndex;
				preparedState.callbacks.bufferSwitch(long(driverBufferIndex), ASIOTrue);
				Log() << "bufferSwitch() complete";
			}
			else {
				ASIOTime time = { 0 };
				time.timeInfo.flags = kSystemTimeValid | kSamplePositionValid | kSampleRateValid;
				time.timeInfo.samplePosition = currentSamplePosition.samples;
				time.timeInfo.systemTime = currentSamplePosition.timestamp;
				time.timeInfo.sampleRate = preparedState.sampleRate;
				Log() << "Firing ASIO bufferSwitchTimeInfo() callback with buffer index: " << driverBufferIndex << ", time info: (" << ::dechamps_ASIOUtil::DescribeASIOTime(time) << ")";
				const auto timeResult = preparedState.callbacks.bufferSwitchTimeInfo(&time, long(driverBufferIndex), ASIOTrue);
				Log() << "bufferSwitchTimeInfo() complete, returned time info: " << (timeResult == nullptr ? "none" : ::dechamps_ASIOUtil::DescribeASIOTime(*timeResult));
			}
			driverBufferIndex = (driverBufferIndex + 1) % 2;

			if (!writeBuffer.empty()) {
				Log() << "Writing to QA401 from buffer index " << driverBufferIndex;
				preparedState.asio401.qa401.FinishWrite();
				::dechamps_ASIOUtil::CopyToInterleavedBuffer(preparedState.bufferInfos, false, preparedState.buffers.outputSampleSize, preparedState.buffers.bufferSizeInSamples, driverBufferIndex, writeBuffer.data(), preparedState.asio401.GetOutputChannelCount());
				preparedState.asio401.qa401.StartWrite(writeBuffer.data(), writeBuffer.size());
				if (!started) ++outputQueueBufferCount;
			}

			preparedState.asio401.qa401.Ping();

			if (!started && outputQueueBufferCount == 2 || outputQueueBufferCount * preparedState.buffers.bufferSizeInSamples > qa401HardwareQueueSizeInFrames) {
				// If we already have two buffers queued, we can start streaming.
				// If we have more writes queued that the QA401 can store, we *have* to start streaming, otherwise the next FinishWrite() call will block indefinitely.
				Log() << "Starting QA401";
				preparedState.asio401.qa401.Start();
				started = true;
			}
			
			if (inputBuffersToSkip > 0) {
				--inputBuffersToSkip;
			}
			else {
				preparedState.asio401.qa401.FinishRead();
				::dechamps_ASIOUtil::CopyFromInterleavedBuffer(preparedState.bufferInfos, true,  preparedState.buffers.inputSampleSize, preparedState.buffers.bufferSizeInSamples, driverBufferIndex, readBuffer.data(), preparedState.asio401.GetInputChannelCount());
				preparedState.asio401.qa401.StartRead(readBuffer.data(), readBuffer.size());
			}
			
			currentSamplePosition.samples = ::dechamps_ASIOUtil::Int64ToASIO<ASIOSamples>(::dechamps_ASIOUtil::ASIOToInt64(currentSamplePosition.samples) + preparedState.buffers.bufferSizeInSamples);
			Log() << "Updated position: " << ::dechamps_ASIOUtil::ASIOToInt64(currentSamplePosition.samples) << " samples";
		}

		// The QA401 output will exhibit a lingering DC offset if we don't do this.
		preparedState.asio401.qa401.Reset();
	}

	void ASIO401::Stop() {
		if (!preparedState.has_value()) throw ASIOException(ASE_InvalidMode, "stop() called before createBuffers()");
		return preparedState->Stop();
	}

	void ASIO401::PreparedState::Stop()
	{
		if (runningState == nullptr) throw ASIOException(ASE_InvalidMode, "stop() called before start()");
		ownedRunningState.reset();
	}

	void ASIO401::GetSamplePosition(ASIOSamples* sPos, ASIOTimeStamp* tStamp) {
		if (!preparedState.has_value()) throw ASIOException(ASE_InvalidMode, "getSamplePosition() called before createBuffers()");
		return preparedState->GetSamplePosition(sPos, tStamp);
	}

	void ASIO401::PreparedState::GetSamplePosition(ASIOSamples* sPos, ASIOTimeStamp* tStamp)
	{
		if (runningState == nullptr) throw ASIOException(ASE_InvalidMode, "getSamplePosition() called before start()");
		return runningState->GetSamplePosition(sPos, tStamp);
	}

	void ASIO401::PreparedState::RunningState::GetSamplePosition(ASIOSamples* sPos, ASIOTimeStamp* tStamp) const
	{
		const auto currentSamplePosition = samplePosition.load();
		*sPos = currentSamplePosition.samples;
		*tStamp = currentSamplePosition.timestamp;
		Log() << "Returning: sample position " << ::dechamps_ASIOUtil::ASIOToInt64(*sPos) << ", timestamp " << ::dechamps_ASIOUtil::ASIOToInt64(*tStamp);
	}

	void ASIO401::PreparedState::RequestReset() {
		if (!callbacks.asioMessage || Message(callbacks.asioMessage, kAsioSelectorSupported, kAsioResetRequest, nullptr, nullptr) != 1)
			throw ASIOException(ASE_InvalidMode, "reset requests are not supported");
		Message(callbacks.asioMessage, kAsioResetRequest, 0, NULL, NULL);
	}

	void ASIO401::ControlPanel() {
		const auto url = std::string("https://github.com/dechamps/ASIO401/blob/") + ::dechamps_CMakeUtils_gitDescription + "/CONFIGURATION.md";
		Log() << "Opening URL: " << url;
		const auto result = ShellExecuteA(windowHandle, NULL, url.c_str(), NULL, NULL, SW_SHOWNORMAL);
		Log() << "ShellExecuteA() result: " << result;
	}

}

