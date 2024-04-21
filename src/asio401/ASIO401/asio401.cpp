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
#include <dechamps_cpputil/find.h>
#include <dechamps_cpputil/string.h>

#include <dechamps_ASIOUtil/asio.h>

#include <dechamps_CMakeUtils/version.h>

#include "../ASIO401Util/windows_error.h"

#include "log.h"

namespace asio401 {

	namespace {

		template<class... Functors>
		struct overloaded final : Functors... { using Functors::operator()...; };
		template<class... Functors>
		overloaded(Functors...) -> overloaded<Functors...>;

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

		void CopyToQA40xBuffer(const std::vector<ASIOBufferInfo>& bufferInfos, const size_t bufferSizeInFrames, const long doubleBufferIndex, void* const qa40xBuffer, const long channelCount, const size_t sampleSizeInBytes) {
			for (const auto& bufferInfo : bufferInfos) {
				if (bufferInfo.isInput) continue;

				const auto channelNum = bufferInfo.channelNum;
				assert(channelNum < channelCount);
				const auto channelOffset = (channelNum + 1) % channelCount;  // https://github.com/dechamps/ASIO401/issues/13
				const auto buffer = static_cast<uint8_t*>(bufferInfo.buffers[doubleBufferIndex]);

				for (size_t sampleCount = 0; sampleCount < bufferSizeInFrames; ++sampleCount)
					memcpy(static_cast<uint8_t*>(qa40xBuffer) + (channelCount * sampleCount + channelOffset) * sampleSizeInBytes, buffer + sampleCount * sampleSizeInBytes, sampleSizeInBytes);
			}
		}

		void CopyFromQA40xBuffer(const std::vector<ASIOBufferInfo>& bufferInfos, const size_t bufferSizeInFrames, const long doubleBufferIndex, const void* const qa40xBuffer, const long channelCount, const size_t sampleSizeInBytes) {
			for (const auto& bufferInfo : bufferInfos) {
				if (!bufferInfo.isInput) continue;

				const auto channelNum = bufferInfo.channelNum;
				assert(channelNum < channelCount);
				const auto channelOffset = (channelNum + 1) % channelCount;  // https://github.com/dechamps/ASIO401/issues/13
				const auto buffer = static_cast<uint8_t*>(bufferInfo.buffers[doubleBufferIndex]);

				for (size_t sampleCount = 0; sampleCount < bufferSizeInFrames; ++sampleCount)
					memcpy(buffer + sampleCount * sampleSizeInBytes, static_cast<const uint8_t*>(qa40xBuffer) + (channelCount * sampleCount + channelOffset) * sampleSizeInBytes, sampleSizeInBytes);
			}
		}

		void ConvertQA40xEndianness(void* const buffer, size_t bytes, const size_t sampleSizeInBytes) {
			if constexpr (::dechamps_cpputil::endianness == ::dechamps_cpputil::Endianness::BIG) return;

			assert(sampleSizeInBytes == 4);

			// Potential optimization opportunity: there are probably faster ways to do this, e.g. using a bswap intrinsic.
			uint8_t* byteBuffer = static_cast<uint8_t*>(buffer);
			while (bytes > 0) {
				std::swap(byteBuffer[0], byteBuffer[3]);
				std::swap(byteBuffer[1], byteBuffer[2]);

				byteBuffer += sampleSizeInBytes;
				bytes -= sampleSizeInBytes;
			}
		}

		void ConvertASIOBufferEndianness(const std::vector<ASIOBufferInfo>& bufferInfos, const bool isInput, const long doubleBufferIndex, const size_t bufferSizeInFrames, const size_t sampleSizeInBytes) {
			for (const auto& bufferInfo : bufferInfos) {
				if (!!bufferInfo.isInput != isInput) continue;
				ConvertQA40xEndianness(bufferInfo.buffers[doubleBufferIndex], bufferSizeInFrames * 4, sampleSizeInBytes);
			}
		}

		constexpr ASIOSampleType sampleType = ::dechamps_cpputil::endianness == ::dechamps_cpputil::Endianness::BIG ? ASIOSTInt32MSB : ASIOSTInt32LSB;
		using NativeSampleType = int32_t;

		std::optional<QA401::SampleRate> GetQA401SampleRate(ASIOSampleRate sampleRate) {
			return ::dechamps_cpputil::Find(sampleRate, std::initializer_list<std::pair<ASIOSampleType, QA401::SampleRate>>{
				{48000, QA401::SampleRate::KHZ48},
				{192000, QA401::SampleRate::KHZ192}
			});
		}

		constexpr std::pair<ASIOSampleRate, QA401::SampleRate> supportedSampleRates[] = {
			{48000, QA401::SampleRate::KHZ48},
			{192000, QA401::SampleRate::KHZ192},
		};

		template <typename Integer> void NegateIntegerBuffer(Integer* buffer, size_t count) {
			std::replace(buffer, buffer + count, (std::numeric_limits<Integer>::min)(), (std::numeric_limits<Integer>::min)() + 1);
			std::transform(buffer, buffer + count, buffer, std::negate());
		}

		void PreProcessASIOOutputBuffers(const std::vector<ASIOBufferInfo>& bufferInfos, const long doubleBufferIndex, const size_t bufferSizeInFrames, const size_t sampleSizeInBytes) {
			for (const auto& bufferInfo : bufferInfos) {
				if (bufferInfo.isInput) continue;

				// Invert polarity of all output channels. See https://github.com/dechamps/ASIO401/issues/14
				NegateIntegerBuffer(static_cast<NativeSampleType*>(bufferInfo.buffers[doubleBufferIndex]), bufferSizeInFrames);
			}

			ConvertASIOBufferEndianness(bufferInfos, false, doubleBufferIndex, bufferSizeInFrames, sampleSizeInBytes);
		}

		void PostProcessASIOInputBuffers(const std::vector<ASIOBufferInfo>& bufferInfos, const long doubleBufferIndex, const size_t bufferSizeInFrames, const size_t sampleSizeInBytes) {
			ConvertASIOBufferEndianness(bufferInfos, true, doubleBufferIndex, bufferSizeInFrames, sampleSizeInBytes);

			for (const auto& bufferInfo : bufferInfos) {
				if (!bufferInfo.isInput) continue;

				// Invert polarity of the right input channel. See https://github.com/dechamps/ASIO401/issues/14
				if (bufferInfo.channelNum == 1) NegateIntegerBuffer(static_cast<NativeSampleType*>(bufferInfo.buffers[doubleBufferIndex]), bufferSizeInFrames);
			}
		}

	}

	ASIO401::Device ASIO401::GetDevice() {
		const auto qa401DevicesPaths = GetDevicesPaths({ 0xFDA49C5C, 0x7006, 0x4EE9, { 0x88, 0xB2, 0xA0, 0xF8, 0x06, 0x50, 0x81, 0x50 } });
		const auto qa403DevicesPaths = GetDevicesPaths({ 0x5512825c, 0x1e52, 0x447a, { 0x83, 0xbd, 0xc8, 0x4d, 0xa7, 0xc1, 0x82, 0x13 } });
		if (qa401DevicesPaths.size() + qa403DevicesPaths.size() > 1) throw ASIOException(ASE_NotPresent, "more than one QA40x device was found. Multiple devices are not supported.");
		if (!qa401DevicesPaths.empty()) return Device(std::in_place_type<QA401>, *qa401DevicesPaths.begin());
		if (!qa403DevicesPaths.empty()) return Device(std::in_place_type<QA403>, *qa403DevicesPaths.begin());
		throw ASIOException(ASE_NotPresent, "QA40x USB device not found. Is it connected?");
	}

	ASIO401::ASIO401(void* sysHandle) :
		windowHandle(reinterpret_cast<decltype(windowHandle)>(sysHandle)),
		config([&] {
		const auto config = LoadConfig();
		if (!config.has_value()) throw ASIOException(ASE_HWMalfunction, "could not load ASIO401 configuration. See ASIO401 log for details.");
		return *config;
	}()), device(GetDevice()) {
		Log() << "sysHandle = " << sysHandle;
	}

	ASIO401::BufferSizes ASIO401::ComputeBufferSizes() const
	{
		BufferSizes bufferSizes;
		if (config.bufferSizeSamples.has_value()) {
			Log() << "Using buffer size " << *config.bufferSizeSamples << " from configuration";
			bufferSizes.minimum = bufferSizes.maximum = bufferSizes.preferred = long(*config.bufferSizeSamples);
			bufferSizes.granularity = 0;
		}
		else {
			// Mostly arbitrary; based on the size of a single USB bulk transfer packet
			bufferSizes.minimum = 64;

			// At 48 kHz, keep the QA40x hardware queue filled at all times; good tradeoff between reliability and latency
			// Above 48 kHz, increase the suggested buffer size proportionally in an attempt to alleviate scheduling/processing timing constraints
			bufferSizes.preferred = long(GetHardwareQueueSizeInFrames() * (std::max)(sampleRate / 48000, 1.0));

			// Technically there doesn't seem to be any limit on the size of a WinUSB transfer, but let's be reasonable
			bufferSizes.maximum = 32768;

			// Mostly arbitrary; based on the size of a single USB bulk transfer packet
			bufferSizes.granularity = 64;
		}
		return bufferSizes;
	}

	void ASIO401::GetBufferSize(long* minSize, long* maxSize, long* preferredSize, long* granularity)
	{
		const auto bufferSizes = ComputeBufferSizes();
		*minSize = bufferSizes.minimum;
		*maxSize = bufferSizes.maximum;
		*preferredSize = bufferSizes.preferred;
		*granularity = bufferSizes.granularity;
		Log() << "Returning: min buffer size " << *minSize << ", max buffer size " << *maxSize << ", preferred buffer size " << *preferredSize << ", granularity " << *granularity;
	}

	long ASIO401::GetDeviceInputChannelCount() const { return std::visit([](const auto& device) { return device.inputChannelCount; }, device);  }
	long ASIO401::GetDeviceOutputChannelCount() const { return std::visit([](const auto& device) { return device.outputChannelCount; }, device); }
	size_t ASIO401::GetDeviceSampleSizeInBytes() const { return std::visit([](const auto& device) { return device.sampleSizeInBytes; }, device); }
	size_t ASIO401::GetHardwareQueueSizeInFrames() const { return std::visit([](const auto& device) { return device.hardwareQueueSizeInFrames; }, device); }

	void ASIO401::GetChannels(long* numInputChannels, long* numOutputChannels)
	{
		*numInputChannels = GetDeviceInputChannelCount();
		*numOutputChannels = GetDeviceOutputChannelCount();
		Log() << "Returning " << *numInputChannels << " input channels and " << *numOutputChannels << " output channels";
	}

	void ASIO401::GetChannelInfo(ASIOChannelInfo* info)
	{
		Log() << "CASIO401::getChannelInfo()";

		Log() << "Channel info requested for " << (info->isInput ? "input" : "output") << " channel " << info->channel;
		if (info->isInput)
		{
			if (info->channel < 0 || info->channel >= GetDeviceInputChannelCount()) throw ASIOException(ASE_InvalidParameter, "no such input channel");
		}
		else
		{
			if (info->channel < 0 || info->channel >= GetDeviceOutputChannelCount()) throw ASIOException(ASE_InvalidParameter, "no such output channel");
		}

		info->isActive = preparedState.has_value() && preparedState->IsChannelActive(info->isInput, info->channel);
		info->channelGroup = 0;
		info->type = sampleType;
		std::stringstream channel_string;
		channel_string << (info->isInput ? "IN" : "OUT") << " " << info->channel;
		switch (info->channel) {
		case 0: channel_string << " Left"; break;
		case 1: channel_string << " Right"; break;
		}
		strcpy_s(info->name, 32, channel_string.str().c_str());
		Log() << "Returning: " << info->name << ", " << (info->isActive ? "active" : "inactive") << ", group " << info->channelGroup << ", type " << ::dechamps_ASIOUtil::GetASIOSampleTypeString(info->type);
	}

	bool ASIO401::CanSampleRate(ASIOSampleRate sampleRate)
	{
		Log() << "Checking for sample rate: " << sampleRate;
		return std::visit(overloaded{
			[&](QA401&) { return GetQA401SampleRate(sampleRate).has_value(); },
			[&](QA403&) { return sampleRate == QA403::sampleRate; }
		}, device);
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
		if (preparedState.has_value() && preparedState->IsRunning())
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

		preparedState.emplace(*this, bufferInfos, numChannels, bufferSize, callbacks);
	}

	ASIO401::PreparedState::Buffers::Buffers(size_t bufferSetCount, size_t inputChannelCount, size_t outputChannelCount, size_t bufferSizeInFrames, size_t inputSampleSizeInBytes, size_t outputSampleSizeInBytes) :
		bufferSetCount(bufferSetCount), inputChannelCount(inputChannelCount), outputChannelCount(outputChannelCount), bufferSizeInFrames(bufferSizeInFrames), inputSampleSizeInBytes(inputSampleSizeInBytes), outputSampleSizeInBytes(outputSampleSizeInBytes),
		buffers(bufferSetCount * bufferSizeInFrames * (inputChannelCount * inputSampleSizeInBytes + outputChannelCount * outputSampleSizeInBytes)) {
		Log() << "Allocated "
			<< bufferSetCount << " buffer sets, "
			<< inputChannelCount << "/" << outputChannelCount << " (I/O) channels per buffer set, "
			<< bufferSizeInFrames << " samples per channel, "
			<< inputSampleSizeInBytes << "/" << outputSampleSizeInBytes << " (I/O) bytes per sample, memory range: "
			<< static_cast<const void*>(buffers.data()) << "-" << static_cast<const void*>(buffers.data() + buffers.size());
	}

	ASIO401::PreparedState::Buffers::~Buffers() {
		Log() << "Destroying buffers";
	}

	ASIO401::PreparedState::PreparedState(ASIO401& asio401, ASIOBufferInfo* asioBufferInfos, long numChannels, long bufferSizeInFrames, ASIOCallbacks* callbacks) :
		asio401(asio401), callbacks(*callbacks),
		buffers(
			2,
			GetBufferInfosChannelCount(asioBufferInfos, numChannels, true), GetBufferInfosChannelCount(asioBufferInfos, numChannels, false),
			bufferSizeInFrames, asio401.GetDeviceSampleSizeInBytes(), asio401.GetDeviceSampleSizeInBytes()),
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
				if (asioBufferInfo.channelNum < 0 || asioBufferInfo.channelNum >= asio401.GetDeviceInputChannelCount())
					throw ASIOException(ASE_InvalidParameter, "out of bounds input channel in createBuffers() buffer info");
			}
			else
			{
				if (asioBufferInfo.channelNum < 0 || asioBufferInfo.channelNum >= asio401.GetDeviceOutputChannelCount())
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
		if (preparedState.has_value()) {
			preparedState->GetLatencies(inputLatency, outputLatency);
		}
		else {
			// A GetLatencies() call before CreateBuffers() puts us in a difficult situation,
			// but according to the ASIO SDK we have to come up with a number and some
			// applications rely on it - see https://github.com/dechamps/FlexASIO/issues/122.
			Log() << "GetLatencies() called before CreateBuffers() - assuming preferred buffer size, full duplex";
			ComputeLatencies(inputLatency, outputLatency, ComputeBufferSizes().preferred, /*outputOnly=*/false);
		}
	}

	void ASIO401::ComputeLatencies(long* const inputLatency, long* const outputLatency, long bufferSizeInFrames, bool outputOnly) const
	{
		*inputLatency = *outputLatency = bufferSizeInFrames;
		if (!hostSupportsOutputReady) {
			Log() << bufferSizeInFrames << " samples added to output latency due to the ASIO Host Application not supporting OutputReady";
			*outputLatency += bufferSizeInFrames;
		}
		if (outputOnly && !config.forceRead) {
			const auto hardwareQueueSizeInFrames = GetHardwareQueueSizeInFrames();
			Log() << hardwareQueueSizeInFrames << " samples added to output latency due to write-only mode";
			*outputLatency += long(hardwareQueueSizeInFrames);
		}
		Log() << "Returning input latency of " << *inputLatency << " samples and output latency of " << *outputLatency << " samples";
	}

	void ASIO401::PreparedState::GetLatencies(long* inputLatency, long* outputLatency)
	{
		asio401.ComputeLatencies(inputLatency, outputLatency, long(buffers.bufferSizeInFrames), /*outputOnly=*/buffers.inputChannelCount == 0);
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
		sampleRate(preparedState.asio401.sampleRate),
		hostSupportsOutputReady(preparedState.asio401.hostSupportsOutputReady),
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

	void ASIO401::PreparedState::RunningState::RunningState::RunThread() noexcept {
		bool resetRequestIssued = false;
		auto requestReset = [&]() noexcept {
			resetRequestIssued = true;
			try {
				preparedState.RequestReset();
			} catch (...) {}
		};

		const auto writeFrameSizeInBytes = preparedState.asio401.GetDeviceOutputChannelCount() * preparedState.buffers.outputSampleSizeInBytes;
		const auto readFrameSizeInBytes = preparedState.asio401.GetDeviceInputChannelCount() * preparedState.buffers.inputSampleSizeInBytes;
		const auto firstWriteBufferSizeInBytes = 1 * writeFrameSizeInBytes;
		const auto firstReadBufferSizeInBytes = preparedState.asio401.GetHardwareQueueSizeInFrames() * readFrameSizeInBytes;
		const auto writeBufferSizeInBytes = preparedState.buffers.outputChannelCount > 0 ? preparedState.buffers.bufferSizeInFrames * writeFrameSizeInBytes : 0;
		const auto readBufferSizeInBytes = preparedState.buffers.bufferSizeInFrames * readFrameSizeInBytes;
		// Out of the try/catch scope because these can still be inflight even after an exception is thrown.
		std::vector<uint8_t> writeBuffer((std::max)(firstWriteBufferSizeInBytes, writeBufferSizeInBytes));
		std::vector<uint8_t> readBuffer((std::max)(firstReadBufferSizeInBytes, readBufferSizeInBytes));

		Win32HighResolutionTimer win32HighResolutionTimer;
		// Note: Reset() calls are done under high priority, because the internal timing of the reset procedure is somewhat important to avoid https://github.com/dechamps/ASIO401/issues/9
		AvrtHighPriority avrtHighPriority;

		try {
			std::visit(overloaded{
				[&](QA401& qa401) {
					// Note: the input high pass filter is not configurable, because there's no clear use case for disabling it.
					// If you can think of one, feel free to reopen https://github.com/dechamps/ASIO401/issues/7.
					qa401.Reset(
						QA401::InputHighPassFilterState::ENGAGED,
						preparedState.asio401.config.attenuator ? QA401::AttenuatorState::ENGAGED : QA401::AttenuatorState::DISENGAGED,
						*GetQA401SampleRate(sampleRate)
					);
				},
				[&](QA403& qa403) {
					// TODO: surface the fact that the QA403 does not support the same options as the QA401
					qa403.Reset();
				}
			}, preparedState.asio401.device);

			if (preparedState.buffers.inputChannelCount > 0) {
				std::visit([&](auto& device) {
					// TODO: this logic was designed for the QA401 - verify that it still makes sense for the QA403.

					// The first frames read from the QA40x shortly after Reset() will always be silence, so throw them away.
					// (Note: this does not mean that the hardware input queue is initially filled with silence. The read duration is consistent with its size - the QA40x is actually recording silence in real time.)
					// Note that sleep(20 ms) would pretty much achieve the same result, but we time this using a QA40x read instead for two reasons:
					//  - Using the QA40x clock for this is probably more accurate and more reliable than the CPU clock.
					//  - As a nice side effect this will also throw away the "ghost" frames from the previous stream (if any) at the same time (see https://github.com/dechamps/ASIO401/issues/5)
					device.StartRead(readBuffer.data(), firstReadBufferSizeInBytes);
					// We want to wait on the read now; waiting on this read in the first FinishRead() call of the processing loop below would be a bad idea as it would kill the time budget for the first bufferSwitch() call.
					// This means we have to start the hardware, otherwise the read will block forever. Which is why we do a minuscule 1-frame write. Indeed the QA40x will not start until we do at least one write (see https://github.com/dechamps/ASIO401/issues/10).
					// Starting streaming that way as a few interesting consequences:
					//  - On the write side this is guaranteed to result in a buffer underrun, since the output queue will drain instantaneously. We work around this by sending silence as the next buffer (see below).
					//  - On the read side we are fine because the next read will occur very shortly afterwards, and we should be well within the time budget that the QA40x input queue gives us (otherwise we would have a much bigger problem, anyway).
					device.StartWrite(writeBuffer.data(), firstWriteBufferSizeInBytes);
					device.FinishWrite();
					device.FinishRead();
				}, preparedState.asio401.device);
			}

			const auto mustRead = preparedState.buffers.inputChannelCount > 0 || preparedState.asio401.config.forceRead;

			// Note: see ../dechamps_ASIOUtil/BUFFERS.md for an explanation of ASIO buffer management and operation order.
			bool firstIteration = true;
			long driverBufferIndex = 0;
			SamplePosition currentSamplePosition;
			while (!stopRequested) {
				// If no output channels are enabled, then skip writing to increase efficiency and reliability.
				// Note: we can skip this because we already did a dummy write above, which started the hardware. Otherwise the reads would block forever.
				if (preparedState.buffers.outputChannelCount > 0) {
					// On the first iteration, we can't start playing a real signal just yet because the QA40x write queue is empty at this point (see above), which means it could underrun (glitch) while the write is taking place.
					// So instead we just send a buffer of silence, which will "hide" any glitches; that will guarantee that on the next iteration the QA40x write queue will be in a stable state and we can queue a real signal behind the silence.
					if (!firstIteration) {
						if (hostSupportsOutputReady) {
							std::unique_lock outputReadyLock(outputReadyMutex);
							if (!outputReady) {
								if (IsLoggingEnabled()) Log() << "Waiting for the ASIO Host Application to signal OutputReady";
								outputReadyCondition.wait(outputReadyLock, [&]{ return outputReady; });
							}
							outputReady = false;
						}

						PreProcessASIOOutputBuffers(preparedState.bufferInfos, driverBufferIndex, preparedState.buffers.bufferSizeInFrames, preparedState.asio401.GetDeviceSampleSizeInBytes());
						std::visit([&](auto& device) { device.FinishWrite(); }, preparedState.asio401.device);
						if (IsLoggingEnabled()) Log() << "Sending data from buffer index " << driverBufferIndex << " to QA40x";
						CopyToQA40xBuffer(preparedState.bufferInfos, preparedState.buffers.bufferSizeInFrames, driverBufferIndex, writeBuffer.data(), preparedState.asio401.GetDeviceOutputChannelCount(), preparedState.asio401.GetDeviceSampleSizeInBytes());
					}
					std::visit([&](auto& device) { device.StartWrite(writeBuffer.data(), writeBufferSizeInBytes); }, preparedState.asio401.device);
				}

				if (hostSupportsOutputReady) driverBufferIndex = (driverBufferIndex + 1) % 2;
				
				if (!firstIteration && mustRead) std::visit([&](auto& device) { device.FinishRead(); }, preparedState.asio401.device);
				currentSamplePosition.timestamp = ::dechamps_ASIOUtil::Int64ToASIO<ASIOTimeStamp>(((long long int) win32HighResolutionTimer.GetTimeMilliseconds()) * 1000000);
				if (!firstIteration) {
					if (preparedState.buffers.inputChannelCount > 0) {
						if (IsLoggingEnabled()) Log() << "Received data from QA40x for buffer index " << driverBufferIndex;
						CopyFromQA40xBuffer(preparedState.bufferInfos, preparedState.buffers.bufferSizeInFrames, driverBufferIndex, readBuffer.data(), preparedState.asio401.GetDeviceInputChannelCount(), preparedState.asio401.GetDeviceSampleSizeInBytes());
					}
				}
				if (mustRead) {
					if (IsLoggingEnabled()) Log() << "Reading from QA40x";
					std::visit([&](auto& device) { device.StartRead(readBuffer.data(), readBufferSizeInBytes); }, preparedState.asio401.device);
					if (!firstIteration && preparedState.buffers.inputChannelCount > 0) PostProcessASIOInputBuffers(preparedState.bufferInfos, driverBufferIndex, preparedState.buffers.bufferSizeInFrames, preparedState.asio401.GetDeviceSampleSizeInBytes());
				}

				// If the host supports OutputReady then we only need to write one buffer in advance, not two.
				// Therefore, we can wait for the initial silent buffer to make it through and buffer 1 to start playing before we ask the application to start generating buffer 0.
				if (!(firstIteration && hostSupportsOutputReady)) {
					if (IsLoggingEnabled()) Log() << "Updating position: " << ::dechamps_ASIOUtil::ASIOToInt64(currentSamplePosition.samples) << " samples, timestamp " << ::dechamps_ASIOUtil::ASIOToInt64(currentSamplePosition.timestamp);
					samplePosition = currentSamplePosition;

					if (!host_supports_timeinfo) {
						if (IsLoggingEnabled()) Log() << "Firing ASIO bufferSwitch() callback with buffer index: " << driverBufferIndex;
						preparedState.callbacks.bufferSwitch(long(driverBufferIndex), ASIOTrue);
						if (IsLoggingEnabled()) Log() << "bufferSwitch() complete";
					}
					else {
						ASIOTime time = { 0 };
						time.timeInfo.flags = kSystemTimeValid | kSamplePositionValid | kSampleRateValid;
						time.timeInfo.samplePosition = currentSamplePosition.samples;
						time.timeInfo.systemTime = currentSamplePosition.timestamp;
						time.timeInfo.sampleRate = sampleRate;
						if (IsLoggingEnabled()) Log() << "Firing ASIO bufferSwitchTimeInfo() callback with buffer index: " << driverBufferIndex << ", time info: (" << ::dechamps_ASIOUtil::DescribeASIOTime(time) << ")";
						const auto timeResult = preparedState.callbacks.bufferSwitchTimeInfo(&time, long(driverBufferIndex), ASIOTrue);
						if (IsLoggingEnabled()) Log() << "bufferSwitchTimeInfo() complete, returned time info: " << (timeResult == nullptr ? "none" : ::dechamps_ASIOUtil::DescribeASIOTime(*timeResult));
					}

					currentSamplePosition.samples = ::dechamps_ASIOUtil::Int64ToASIO<ASIOSamples>(::dechamps_ASIOUtil::ASIOToInt64(currentSamplePosition.samples) + preparedState.buffers.bufferSizeInFrames);
				}

				if (!hostSupportsOutputReady) driverBufferIndex = (driverBufferIndex + 1) % 2;

				std::visit(overloaded{
					[&](QA401& qa401) { qa401.Ping(); },
					[&](auto&) {}
				}, preparedState.asio401.device);
				firstIteration = false;
			}
		}
		catch (const std::exception& exception) {
			Log() << "Fatal error occurred in streaming thread: " << exception.what();
			requestReset();
		}
		catch (...) {
			Log() << "Unknown fatal error occurred in streaming thread";
			requestReset();
		}

		try {
			std::visit(overloaded{
				[&](QA401& qa401) {
					// The QA401 output will exhibit a lingering DC offset if we don't reset it. Also, (re-)engage the attenuator just to be safe.
					qa401.Reset(
						QA401::InputHighPassFilterState::ENGAGED, QA401::AttenuatorState::ENGAGED, *GetQA401SampleRate(sampleRate)
					);
				},
				[&](QA403& qa403) { qa403.Reset();  }
			}, preparedState.asio401.device);
		}
		catch (const std::exception& exception) {
			Log() << "Fatal error occurred while attempting to reset the QA40x: " << exception.what();
			requestReset();
		}
		catch (...) {
			Log() << "Unknown fatal error occurred while attempting to reset the QA40x";
			requestReset();
		}
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
		if (IsLoggingEnabled()) Log() << "Returning: sample position " << ::dechamps_ASIOUtil::ASIOToInt64(*sPos) << ", timestamp " << ::dechamps_ASIOUtil::ASIOToInt64(*tStamp);
	}

	void ASIO401::OutputReady() {
		if (!hostSupportsOutputReady) {
			Log() << "Host supports OutputReady";
			hostSupportsOutputReady = true;
		}
		if (preparedState.has_value()) preparedState->OutputReady();
	}

	void ASIO401::PreparedState::OutputReady() {
		if (runningState != nullptr) runningState->OutputReady();
	}

	void ASIO401::PreparedState::RunningState::OutputReady() {
		{
			std::scoped_lock outputReadyLock(outputReadyMutex);
			outputReady = true;
		}
		outputReadyCondition.notify_all();
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

