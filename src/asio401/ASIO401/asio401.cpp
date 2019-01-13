#include "asio401.h"

#include "devices.h"

#include <algorithm>
#include <memory>
#include <mutex>
#include <string>
#include <sstream>
#include <string_view>
#include <vector>

#include <dechamps_cpputil/endian.h>
#include <dechamps_cpputil/string.h>

#include <dechamps_ASIOUtil/asio.h>

#include <dechamps_CMakeUtils/version.h>

#include "log.h"

namespace asio401 {

	ASIO401::Win32HighResolutionTimer::Win32HighResolutionTimer() {
		Log() << "Starting high resolution timer";
		timeBeginPeriod(1);
	}
	ASIO401::Win32HighResolutionTimer::~Win32HighResolutionTimer() {
		Log() << "Stopping high resolution timer";
		timeEndPeriod(1);
	}

	DWORD ASIO401::Win32HighResolutionTimer::GetTimeMilliseconds() const { return timeGetTime(); }

	namespace {

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

	}

	ASIO401::ASIO401(void* sysHandle) :
		windowHandle(reinterpret_cast<decltype(windowHandle)>(sysHandle)),
		config([&] {
		const auto config = LoadConfig();
		if (!config.has_value()) throw ASIOException(ASE_HWMalfunction, "could not load ASIO401 configuration. See ASIO401 log for details.");
		return *config;
	}()), winUsbHandle(WinUsbOpen([&] {
		const auto devicePath = GetDevicePath(qa401DeviceGUID);
		if (!devicePath.has_value()) {
			throw ASIOException(ASE_NotPresent, "QA401 USB device not found. Is it connected?");
		}
		return *devicePath;
	}())) {
		Log() << "sysHandle = " << sysHandle;
	}

	void ASIO401::GetBufferSize(long* minSize, long* maxSize, long* preferredSize, long* granularity)
	{
		if (config.bufferSizeSamples.has_value()) {
			Log() << "Using buffer size " << *config.bufferSizeSamples << " from configuration";
			*minSize = *maxSize = *preferredSize = long(*config.bufferSizeSamples);
			*granularity = 0;
		}
		else {
			Log() << "Calculating default buffer size based on " << sampleRate << " Hz sample rate";
			*minSize = long(sampleRate * 0.001); // 1 ms, there's basically no chance we'll get glitch-free streaming below this
			*maxSize = long(sampleRate); // 1 second, more would be silly
			*preferredSize = long(sampleRate * 0.02); // 20 ms
			*granularity = 1; // Don't care
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
		info->type = ASIOSTFloat64LSB;
		std::stringstream channel_string;
		channel_string << (info->isInput ? "IN" : "OUT") << " " << getChannelName(info->channel);
		strcpy_s(info->name, 32, channel_string.str().c_str());
		Log() << "Returning: " << info->name << ", " << (info->isActive ? "active" : "inactive") << ", group " << info->channelGroup << ", type " << ::dechamps_ASIOUtil::GetASIOSampleTypeString(info->type);
	}

	bool ASIO401::CanSampleRate(ASIOSampleRate sampleRate)
	{
		Log() << "Checking for sample rate: " << sampleRate;
		return true;
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

		if (!(requestedSampleRate > 0 && requestedSampleRate < (std::numeric_limits<ASIOSampleRate>::max)())) {
			throw ASIOException(ASE_InvalidParameter, "setSampleRate() called with an invalid sample rate");
		}

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
			bufferSizeInSamples, sizeof(double), sizeof(double)),
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
		*outputLatency = long(buffers.bufferSizeInSamples);
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
		our_buffer_index(0),
		host_supports_timeinfo([&] {
		Log() << "Checking if the host supports time info";
		const bool result = preparedState.callbacks.asioMessage &&
			Message(preparedState.callbacks.asioMessage, kAsioSelectorSupported, kAsioSupportsTimeInfo, NULL, NULL) == 1 &&
			Message(preparedState.callbacks.asioMessage, kAsioSupportsTimeInfo, 0, NULL, NULL) == 1;
		Log() << "The host " << (result ? "supports" : "does not support") << " time info";
		return result;
	}()) {}

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

