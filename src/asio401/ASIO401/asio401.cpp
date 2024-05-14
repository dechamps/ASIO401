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

		void CopyToQA40xBuffer(const std::vector<ASIOBufferInfo>& bufferInfos, const size_t bufferSizeInFrames, const long doubleBufferIndex, const std::span<std::byte> qa40xBuffer, const long channelCount, const size_t sampleSizeInBytes) {
			const auto calculateDestinationOffset = [&](size_t channelOffset, size_t sampleCount) {
				return (channelCount * sampleCount + channelOffset) * sampleSizeInBytes;
			};
			assert(calculateDestinationOffset(0, bufferSizeInFrames) == qa40xBuffer.size());
			for (const auto& bufferInfo : bufferInfos) {
				if (bufferInfo.isInput) continue;

				const auto channelNum = bufferInfo.channelNum;
				assert(channelNum < channelCount);
				const auto channelOffset = (channelNum + 1) % channelCount;  // Both the QA401 and QA403 have their output channels swapped.
				const auto buffer = static_cast<std::byte*>(bufferInfo.buffers[doubleBufferIndex]);

				for (size_t sampleCount = 0; sampleCount < bufferSizeInFrames; ++sampleCount) {
					memcpy(qa40xBuffer.data() + calculateDestinationOffset(channelOffset, sampleCount), buffer + sampleCount * sampleSizeInBytes, sampleSizeInBytes);
				}
			}
		}

		void CopyFromQA40xBuffer(const std::vector<ASIOBufferInfo>& bufferInfos, const size_t bufferSizeInFrames, const long doubleBufferIndex, const std::span<const std::byte> qa40xBuffer, const long channelCount, const size_t sampleSizeInBytes, const bool swapChannels) {
			const auto calculateSourceOffset = [&](size_t channelOffset, size_t sampleCount) {
				return (channelCount * sampleCount + channelOffset) * sampleSizeInBytes;
			};
			assert(calculateSourceOffset(0, bufferSizeInFrames) == qa40xBuffer.size());
			for (const auto& bufferInfo : bufferInfos) {
				if (!bufferInfo.isInput) continue;

				const auto channelNum = bufferInfo.channelNum;
				assert(channelNum < channelCount);
				const auto channelOffset = swapChannels ? (channelNum + 1) % channelCount : channelNum;
				const auto buffer = static_cast<std::byte*>(bufferInfo.buffers[doubleBufferIndex]);

				for (size_t sampleCount = 0; sampleCount < bufferSizeInFrames; ++sampleCount) {
					memcpy(buffer + sampleCount * sampleSizeInBytes, qa40xBuffer.data() + calculateSourceOffset(channelOffset, sampleCount), sampleSizeInBytes);
				}
			}
		}

		void SwapEndianness(void* const buffer, size_t bytes, const size_t sampleSizeInBytes) {
			assert(sampleSizeInBytes == 4);

			// Potential optimization opportunity: there are probably faster ways to do this, e.g. using a bswap intrinsic.
			auto byteBuffer = static_cast<std::byte*>(buffer);
			while (bytes > 0) {
				std::swap(byteBuffer[0], byteBuffer[3]);
				std::swap(byteBuffer[1], byteBuffer[2]);

				byteBuffer += sampleSizeInBytes;
				bytes -= sampleSizeInBytes;
			}
		}

		void ConvertASIOBufferEndianness(const std::vector<ASIOBufferInfo>& bufferInfos, const bool isInput, const long doubleBufferIndex, const size_t bufferSizeInFrames, const size_t sampleSizeInBytes, ::dechamps_cpputil::Endianness deviceSampleEndianness) {
			if (::dechamps_cpputil::endianness == deviceSampleEndianness) return;
			for (const auto& bufferInfo : bufferInfos) {
				if (!!bufferInfo.isInput != isInput) continue;
				SwapEndianness(bufferInfo.buffers[doubleBufferIndex], bufferSizeInFrames * 4, sampleSizeInBytes);
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

		std::optional<QA403::SampleRate> GetQA403SampleRate(ASIOSampleRate sampleRate) {
			return ::dechamps_cpputil::Find(sampleRate, std::initializer_list<std::pair<ASIOSampleType, QA403::SampleRate>>{
				{ 48000, QA403::SampleRate::KHZ48 },
				{ 96000, QA403::SampleRate::KHZ96 },
				{ 192000, QA403::SampleRate::KHZ192 },
				{ 384000, QA403::SampleRate::KHZ384 },
			});
		}

		QA401::AttenuatorState GetQA401AttenuatorState(const Config& config) {
			const auto fullScaleInputLevelDBV = config.fullScaleInputLevelDBV.value_or(+26.0);
			const auto attenuatorState = ::dechamps_cpputil::Find(
				fullScaleInputLevelDBV,
				std::initializer_list<std::pair<double, QA401::AttenuatorState>>{
					{ +6.0, QA401::AttenuatorState::DISENGAGED },
					{ +26.0, QA401::AttenuatorState::ENGAGED },
				}
			);
			if (!attenuatorState.has_value())
				throw std::runtime_error("Full scale input level of " + std::to_string(fullScaleInputLevelDBV) + " dBV is not supported by the QA401. Valid values for the QA401 are +6.0 and +26.0");
			return *attenuatorState;
		}

		void ValidateQA401FullScaleOutputLevel(const Config& config) {
			const auto fullScaleOutputLevelDBV = config.fullScaleOutputLevelDBV;
			if (fullScaleOutputLevelDBV.has_value() && *fullScaleOutputLevelDBV != +5.5)
				throw std::runtime_error("Full scale output level of " + std::to_string(*fullScaleOutputLevelDBV) + " dBV is not supported by the QA401. The only valid value for the QA401 is +5.5");
		}

		QA403::FullScaleInputLevel GetQA403FullScaleInputLevel(const Config& config) {
			const auto fullScaleInputLevelDBV = config.fullScaleInputLevelDBV.value_or(+42.0);
			const auto fullScaleInputLevel = ::dechamps_cpputil::Find(
				fullScaleInputLevelDBV,
				std::initializer_list<std::pair<double, QA403::FullScaleInputLevel>>{
					{ 0.0, QA403::FullScaleInputLevel::DBV0 },
					{ +6.0, QA403::FullScaleInputLevel::DBV6 },
					{ +12.0, QA403::FullScaleInputLevel::DBV12 },
					{ +18.0, QA403::FullScaleInputLevel::DBV18 },
					{ +24.0, QA403::FullScaleInputLevel::DBV24 },
					{ +30.0, QA403::FullScaleInputLevel::DBV30 },
					{ +36.0, QA403::FullScaleInputLevel::DBV36 },
					{ +42.0, QA403::FullScaleInputLevel::DBV42 },
			}
			);
			if (!fullScaleInputLevel.has_value())
				throw std::runtime_error("Full scale input level of " + std::to_string(fullScaleInputLevelDBV) + " dBV is not supported by the QA403/QA402. Valid values for the QA403/QA402 are 0.0, +6.0, +12.0, +18.0, +24.0, +30.0, +36.0 and +42.0");
			return *fullScaleInputLevel;
		}

		QA403::FullScaleOutputLevel GetQA403FullScaleOutputLevel(const Config& config) {
			const auto fullScaleOutputLevelDBV = config.fullScaleOutputLevelDBV.value_or(-12.0);
			const auto fullScaleOutputLevel = ::dechamps_cpputil::Find(
				fullScaleOutputLevelDBV,
				std::initializer_list<std::pair<double, QA403::FullScaleOutputLevel>>{
					{ -12.0, QA403::FullScaleOutputLevel::DBVn12 },
					{ -2.0, QA403::FullScaleOutputLevel::DBVn2 },
					{ +8.0, QA403::FullScaleOutputLevel::DBV8 },
					{ +18.0, QA403::FullScaleOutputLevel::DBV18 },
			}
			);
			if (!fullScaleOutputLevel.has_value())
				throw std::runtime_error("Full scale output level of " + std::to_string(fullScaleOutputLevelDBV) + " dBV is not supported by the QA403/QA402. Valid values for the QA403/QA402 are -12.0, -2.0, +8.0 and +18.0");
			return *fullScaleOutputLevel;
		}

		template <typename Integer> void NegateIntegerBuffer(Integer* buffer, size_t count) {
			std::replace(buffer, buffer + count, (std::numeric_limits<Integer>::min)(), (std::numeric_limits<Integer>::min)() + 1);
			std::transform(buffer, buffer + count, buffer, std::negate());
		}

		void PreProcessASIOOutputBuffers(const std::vector<ASIOBufferInfo>& bufferInfos, const long doubleBufferIndex, const size_t bufferSizeInFrames, const size_t sampleSizeInBytes, ::dechamps_cpputil::Endianness deviceSampleEndianness, const bool invertPolarity) {
			for (const auto& bufferInfo : bufferInfos) {
				if (bufferInfo.isInput) continue;

				if (invertPolarity) NegateIntegerBuffer(static_cast<NativeSampleType*>(bufferInfo.buffers[doubleBufferIndex]), bufferSizeInFrames);
			}

			ConvertASIOBufferEndianness(bufferInfos, false, doubleBufferIndex, bufferSizeInFrames, sampleSizeInBytes, deviceSampleEndianness);
		}

		void PostProcessASIOInputBuffers(const std::vector<ASIOBufferInfo>& bufferInfos, const long doubleBufferIndex, const size_t bufferSizeInFrames, const size_t sampleSizeInBytes, ::dechamps_cpputil::Endianness deviceSampleEndianness) {
			ConvertASIOBufferEndianness(bufferInfos, true, doubleBufferIndex, bufferSizeInFrames, sampleSizeInBytes, deviceSampleEndianness);

			for (const auto& bufferInfo : bufferInfos) {
				if (!bufferInfo.isInput) continue;

				// Invert polarity of the right input channel. See https://github.com/dechamps/ASIO401/issues/14
				if (bufferInfo.channelNum == 1) NegateIntegerBuffer(static_cast<NativeSampleType*>(bufferInfo.buffers[doubleBufferIndex]), bufferSizeInFrames);
			}
		}

	}

	ASIO401::Device ASIO401::GetDevice() {
		const auto qa401DevicesPaths = GetDevicesPaths({ 0xFDA49C5C, 0x7006, 0x4EE9, { 0x88, 0xB2, 0xA0, 0xF8, 0x06, 0x50, 0x81, 0x50 } });
		const auto qa402DevicesPaths = GetDevicesPaths({ 0x2232825c, 0x1e52, 0x447a, { 0x83, 0xbd, 0xc8, 0x4d, 0xa7, 0xc1, 0x88, 0x59 } });
		const auto qa403DevicesPaths = GetDevicesPaths({ 0x5512825c, 0x1e52, 0x447a, { 0x83, 0xbd, 0xc8, 0x4d, 0xa7, 0xc1, 0x82, 0x13 } });
		if (qa401DevicesPaths.size() + qa402DevicesPaths.size() + qa403DevicesPaths.size() > 1) throw ASIOException(ASE_NotPresent, "more than one QA40x device was found. Multiple devices are not supported.");
		if (!qa401DevicesPaths.empty()) {
			Log() << "Found QA401 device";
			return Device(std::in_place_type<QA401>, *qa401DevicesPaths.begin());
		}
		if (!qa402DevicesPaths.empty()) {
			Log() << "Found QA402 device";
			return Device(std::in_place_type<QA403>, *qa402DevicesPaths.begin());
		}
		if (!qa403DevicesPaths.empty()) {
			Log() << "Found QA403 device";
			return Device(std::in_place_type<QA403>, *qa403DevicesPaths.begin());
		}
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
		ValidateConfig();
	}

	void ASIO401::ValidateConfig() const {
		WithDevice(
			[&](const QA401&) {
				GetQA401AttenuatorState(config);
				ValidateQA401FullScaleOutputLevel(config);
			},
			[&](const QA403&) {
				GetQA403FullScaleInputLevel(config);
				GetQA403FullScaleOutputLevel(config);
			});
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

			// QA40x devices have a minimum write granularity, under which the DAC output is garbled.
			// We don't know if the user actually intends to use output channels at this point, but let's err on the safe side.
			bufferSizes.granularity = long(GetDeviceWriteGranularityInFrames());
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
		return WithDevice(
			[&](QA401&) { return GetQA401SampleRate(sampleRate).has_value(); },
			[&](QA403&) { return GetQA403SampleRate(sampleRate).has_value(); });
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
		bool hasOutput = false;
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
				hasOutput = true;
			}
			const auto getBuffer = asioBufferInfo.isInput ? &Buffers::GetInputBuffer : &Buffers::GetOutputBuffer;
			auto& nextBuffersChannelIndex = asioBufferInfo.isInput ? nextBuffersInputChannelIndex : nextBuffersOutputChannelIndex;
			const auto bufferSizeInBytes = asioBufferInfo.isInput ? buffers.GetInputBufferSizeInBytes() : buffers.GetOutputBufferSizeInBytes();

			std::byte* first_half = (buffers.*getBuffer)(0, nextBuffersChannelIndex);
			std::byte* second_half = (buffers.*getBuffer)(1, nextBuffersChannelIndex);
			++nextBuffersChannelIndex;
			asioBufferInfo.buffers[0] = first_half;
			asioBufferInfo.buffers[1] = second_half;
			Log() << "ASIO buffer #" << channelIndex << " is " << (asioBufferInfo.isInput ? "input" : "output") << " channel " << asioBufferInfo.channelNum
				<< " - first half: " << static_cast<const void*>(first_half) << "-" << static_cast<const void*>(first_half + bufferSizeInBytes)
				<< " - second half: " << static_cast<const void*>(second_half) << "-" << static_cast<const void*>(second_half + bufferSizeInBytes);
			bufferInfos.push_back(asioBufferInfo);
		}

		if (hasOutput) {
			const auto requiredGranularityInFrames = asio401.GetDeviceWriteGranularityInFrames();
			if (bufferSizeInFrames % requiredGranularityInFrames != 0)
				throw ASIOException(ASE_InvalidMode, "Buffer size must be a multiple of " + std::to_string(requiredGranularityInFrames) + " when output channels are used");
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
			// In full duplex mode, buffer switches are delayed by the time it takes to do a read. We start blocking
			// on reads as soon as 2 buffers are sent, and once a read completes we immediately provide it to the host
			// through a bufferSwitch() call. So, right before the beforeSwitch() call there is only 1 ASIO buffer size
			// buffered in total; and right after the call we immediately top it off to 2 (assuming the call returns
			// instantaneously). We never expect to block on writes - ASIO buffers are transferred to a write buffer
			// and queued for write immediately.
			// In contrast, in output-only mode we fill up all write buffers, wait for writes to block, and only *then*
			// do we ask the host for more data through a bufferSwitch() call. So, right before the bufferSwitch() call
			// there are *2* ASIO buffer sizes buffered in total, in addition to the hardware queue; and right after the
			// call there will be one more buffer waiting, which is actually the shared ASIO host buffer itself - that
			// one will NOT be transferred to a write buffer and queued right away; instead, it will only be sent *after*
			// another write completes and frees up a write buffer. End result: the ASIO output buffer will have to wait
			// behind 2 other buffer writes, plus the hardware queue, before actually starting to play.
			const auto additionalOutputLatencyInFrames = bufferSizeInFrames + GetHardwareQueueSizeInFrames();
			Log() << additionalOutputLatencyInFrames << " samples added to output latency due to write-only mode";
			*outputLatency += long(additionalOutputLatencyInFrames);
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
		if (runningState.has_value()) throw ASIOException(ASE_InvalidMode, "start() called twice");
		runningState.emplace(*this);
		runningState->Start();
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
	}()) {	}

	ASIO401::PreparedState::RunningState::~RunningState() {
		stopRequested = true;
		// Stop inflight I/O. If RunThread() is currently in an `Await()` call, it will immediately
		// see ABORTED and exit faster than it would if it waited for the I/O to complete.
		// If there is no inflight I/O, these are no-ops. We don't check first because that would require extra thread
		// safety mechanisms - instead we just piggyback on the (assumed?) thread safety of the underlying I/O abort mechanism.
		// This could end up racing against the same abort calls in the thread exit logic(), but this shouldn't be of any
		// practical consequence.
		Abort();
		thread.join();
	}

	template <QA40x::ChannelType channelType>
	ASIO401::PreparedState::RunningState::QA40xBuffer<channelType>::QA40xBuffer(size_t size) : buffer(size) {
		assert(!buffer.empty());
	}

	template <QA40x::ChannelType channelType>
	std::span<std::byte> ASIO401::PreparedState::RunningState::QA40xBuffer<channelType>::data() {
		assert(!ioSlot.HasPending());
		return buffer;
	}

	template <QA40x::ChannelType channelType>
	std::span<const std::byte> ASIO401::PreparedState::RunningState::QA40xBuffer<channelType>::data() const {
		assert(!ioSlot.HasPending());
		return buffer;
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
		const auto mustPlay = preparedState.buffers.outputChannelCount > 0;
		const auto mustRecord = preparedState.buffers.inputChannelCount > 0;
		const auto mustRead = mustRecord || preparedState.asio401.config.forceRead;
		const auto mustMaintainSync = mustPlay && mustRead;
		const auto initialInputGarbageInFrames = preparedState.asio401.WithDevice(
			[&](QA401&) {
				// As described in https://github.com/dechamps/ASIO401/issues/5, the QA401 will initially replay the last 64 frames of input.
				// After that, the QA401 produces about 1000 frames of silence, regardless of sample rate.
				// (Note the read still takes about the same amount of time to complete, so time sync appears to bemaintained
				// throughout - it's as if we're actually recording, but the data gets mangled before it's delivered to us.)
				return 1056;
			},
			[&](QA403&) { return 0; }
		);
		const auto outputQueueStartThresholdInFrames = preparedState.asio401.WithDevice(
			[&](QA401&) { return 1; }, // The QA401 will start as soon as at least 1 frame is written to it.
			[&](QA403&) { return QA403::hardwareQueueSizeInFrames; } // The QA403 will only start once its internal queue has been filled.
		);
		const auto initialGarbageToSkipFrames = mustRecord ? initialInputGarbageInFrames : 0;
		const auto steadyStateWriteSizeInFrames = mustPlay ? preparedState.buffers.bufferSizeInFrames : 0;
		const auto steadyStateReadSizeInFrames = mustRead ? preparedState.buffers.bufferSizeInFrames : 0;
		const auto firstWriteSizeInFrames = [&] {
			auto firstWriteSizeInFrames = (mustMaintainSync ? initialGarbageToSkipFrames : 0) + steadyStateWriteSizeInFrames;
			// At the beginning we send two buffers before waiting, so the total initial playback queue is the sum of both the initial buffer and that additional buffer.
			const auto initialPlaybackQueueInFrames = firstWriteSizeInFrames + steadyStateWriteSizeInFrames;
			// Make sure the initial playback queue is enough to trigger the hardware to start; otherwise, we'll want to pad it with silence until it does.
			// Technically we could keep asking the host application for more buffers until we fill the queue, but that would likely make the logic vastly
			// more complex, and things would likely become awkward if things don't align with the ASIO buffer size. Also, it's atypical for an ASIO driver
			// to ask for more then 2 buffers before starting.
			if (outputQueueStartThresholdInFrames > initialPlaybackQueueInFrames) firstWriteSizeInFrames += outputQueueStartThresholdInFrames - initialPlaybackQueueInFrames;
			return firstWriteSizeInFrames;
		}();
		const auto firstReadSizeInFrames = mustRead ? (std::max)(initialInputGarbageInFrames + steadyStateReadSizeInFrames, mustMaintainSync ? firstWriteSizeInFrames : 0) : 0;
		assert(firstWriteSizeInFrames >= steadyStateWriteSizeInFrames);
		assert(firstReadSizeInFrames >= steadyStateReadSizeInFrames);

		// QA40x (more technically, WinUSB) supports multiple concurrent I/O requests on a given channel. The requests are serviced in the order they are started.
		// We use this capability to try to keep two buffers in flight to/from the hardware at any given time.
		// Compared to only using one buffer per channel, this is a performance optimization. If we only used one buffer, then
		// when an I/O completes there would be nothing in flight on the USB bus. This means the only buffer preventing an underrun/overflow
		// would be the QA40x internal hardware buffer, which is quite small: only 2.7 ms at 384 kHz. This in turn means that when an I/O
		// completes, we only have a small amount of time to issue the next one before the buffer runs out. This puts severe scheduling constraints
		// on this thread, which is not ideal. (This is true even with arbitrarily large ASIO buffer sizes - these don't factor into this discussion.)
		// In contrast, if we start the next I/O before the current one completes, then when the current I/O eventually completes the WinUSB stack can
		// directly send the next one without having to get back to this code first. (In practice, it has been observed that the process doesn't even
		// get woken up when that happens, suggesting the round-trip happens completely in kernel mode, perhaps even in the USB host hardware itself.)
		std::array<std::optional<QA40xBuffer<QA40x::ChannelType::WRITE>>, 2> writeBuffers;
		std::array<std::optional<QA40xBuffer<QA40x::ChannelType::READ>>, 2> readBuffers;
		{
			const auto maybeAllocateBuffer = [&](auto& optionalBuffer, size_t size) {
				if (size > 0) optionalBuffer.emplace(size);
			};
			maybeAllocateBuffer(writeBuffers.front(), (std::max)(firstWriteSizeInFrames, steadyStateWriteSizeInFrames) * writeFrameSizeInBytes);
			maybeAllocateBuffer(writeBuffers.back(), steadyStateWriteSizeInFrames * writeFrameSizeInBytes);
			maybeAllocateBuffer(readBuffers.front(), (std::max)(firstReadSizeInFrames, steadyStateReadSizeInFrames) * readFrameSizeInBytes);
			maybeAllocateBuffer(readBuffers.back(), steadyStateReadSizeInFrames * readFrameSizeInBytes);
		}
		assert(!writeBuffers.back().has_value() || writeBuffers.front().has_value());
		assert(!readBuffers.back().has_value() || readBuffers.front().has_value());
		assert(!writeBuffers.back().has_value() || mustPlay);
		assert((!readBuffers.front().has_value() && !readBuffers.back().has_value()) || mustRead);
		assert(std::ranges::all_of(readBuffers, [&](const std::optional<QA40xBuffer<QA40x::ChannelType::READ>>& buffer) { return buffer.has_value() == mustRead; }));

		size_t writeBufferIndex = 0, readBufferIndex = 0;

		struct StopRequested final {};
		// We abuse exception handling to process stop requests - this is a bit shameful but it does make the code more straightforward.
		const auto checkStopRequested = [&] {
			if (stopRequested) {
				Log() << "Stop was requested, aborting";
				throw StopRequested();
			}
		};

		const auto awaitQa40xOperation = [&](auto& buffers, size_t bufferIndex, std::string_view operationName) {
			if (IsLoggingEnabled()) Log() << "Awaiting " << operationName << " I/O slot index " << readBufferIndex;
			// We may have been asked to stop before this I/O was started. In that case `Await()` will unnecessarily block instead of immediately returning ABORTED.
			checkStopRequested();
			if (buffers[bufferIndex]->GetIoSlot().Await() == QA40x::AwaitResult::ABORTED) {
				checkStopRequested();
				throw new std::runtime_error("QA40x I/O was unexpectedly aborted");
			}
		};
		const auto awaitQa40xWrite = [&] { return awaitQa40xOperation(writeBuffers, writeBufferIndex, "write"); };
		const auto awaitQa40xRead = [&] { return awaitQa40xOperation(readBuffers, readBufferIndex, "read"); };
		const auto startQa40xOperation = [&](auto& buffers, size_t& bufferIndex, size_t nextSizeInBytes, auto channel, std::string_view operationName) {
			if (IsLoggingEnabled()) Log() << "Starting new " << operationName << " I/O of size " << nextSizeInBytes << " bytes in slot index " << bufferIndex;
			auto& buffer = *buffers[bufferIndex];;
			buffer.GetIoSlot().Start(channel, std::span(buffer.data()).first(nextSizeInBytes));
			bufferIndex = (bufferIndex + 1) % buffers.size();
		};
		const auto startQa40xWrite = [&](size_t sizeInBytes) {
			return startQa40xOperation(writeBuffers, writeBufferIndex, sizeInBytes, preparedState.asio401.WithDevice([&](auto& device) { return device.GetWriteChannel(); }), "write");
		};
		const auto startQa40xRead = [&](size_t sizeInBytes) {
			return startQa40xOperation(readBuffers, readBufferIndex, sizeInBytes, preparedState.asio401.WithDevice([&](auto& device) { return device.GetReadChannel(); }), "read");
		};

		Win32HighResolutionTimer win32HighResolutionTimer;
		// Note: Reset() calls are done under high priority, because the internal timing of the reset procedure is somewhat important to avoid https://github.com/dechamps/ASIO401/issues/9
		AvrtHighPriority avrtHighPriority;

		try {
			SetupDevice();

			// Note: see ../dechamps_ASIOUtil/BUFFERS.md for an explanation of ASIO buffer management and operation order.
			const auto asioBufferSizeInBytes = preparedState.buffers.bufferSizeInFrames * writeFrameSizeInBytes;
			bool firstWriteStarted = false, firstReadStarted = false, recordedFirstBuffer = false, primed = false;
			size_t withheldOutputBuffers = 0;
			SamplePosition currentSamplePosition;

			const auto recordTimestamp = [&] {
				currentSamplePosition.timestamp = ::dechamps_ASIOUtil::Int64ToASIO<ASIOTimeStamp>(((long long int) win32HighResolutionTimer.GetTimeMilliseconds()) * 1000000);
			};
			const auto startSending = [&] {
				if (IsLoggingEnabled()) Log() << "Starting a write from QA40x buffer index " << writeBufferIndex;
				const auto sizeInFrames = firstWriteStarted ? asioBufferSizeInBytes : firstWriteSizeInFrames * writeFrameSizeInBytes;
				assert(sizeInFrames % preparedState.asio401.GetDeviceWriteGranularityInFrames() == 0);
				startQa40xWrite(sizeInFrames);
				firstWriteStarted = true;
			};
			const auto finishSending = [&] {
				if (IsLoggingEnabled()) Log() << "Waiting for QA40x write buffer index " << writeBufferIndex << " to complete";
				awaitQa40xWrite();
				if (!mustRead) {
					// If we can't use reads to get timing information, write completion events are the next best thing.
					recordTimestamp();
				}
			};
			const auto startReceiving = [&] {
				if (IsLoggingEnabled()) Log() << "Starting a read into QA40x buffer index " << readBufferIndex;
				assert(mustRead);
				startQa40xRead(firstReadStarted ? asioBufferSizeInBytes : firstReadSizeInFrames * readFrameSizeInBytes);
				firstReadStarted = true;
			};
			const auto finishReceiving = [&] {
				if (IsLoggingEnabled()) Log() << "Waiting for read into buffer index " << readBufferIndex << " to complete";
				assert(mustRead);
				awaitQa40xRead();
				// The most precise timing is given by the read completion event, so record the current time before we do anything else.
				recordTimestamp();
			};

			if (mustRead) {
				// We can set up the initial reads at any time up until we actually need the data.
				// These reads will not complete until the hardware actually starts (i.e.
				// `outputQueueStartThresholdInFrames` frames have been written), so might as well
				// set this up now and we'll be ready when that happens.
				if (IsLoggingEnabled()) Log() << "Starting initial reads";
				for (const auto& readBuffer : readBuffers) startReceiving();
			}
			recordTimestamp();
			for (long asioBufferIndex = 0; ; asioBufferIndex = (asioBufferIndex + 1) % 2) {
				const auto asioToQa40xWithheld = [&] {
					// The loop is structured in such a way that the ASIO buffer that is ready to send is the
					// *opposite* buffer from the one given by `asioBufferIndex`.
					const auto outputAsioBufferIndex = (asioBufferIndex + 1) % 2;
					assert(withheldOutputBuffers < writeBuffers.size());
					const bool firstWrite = !firstWriteStarted && withheldOutputBuffers == 0;
					const auto bufferIndex = (writeBufferIndex + withheldOutputBuffers) % 2;
					++withheldOutputBuffers;
					if (IsLoggingEnabled()) Log() << "About to copy data from ASIO buffer index " << outputAsioBufferIndex << " to QA40x write buffer index " << bufferIndex << (firstWrite ? " (first write)" : "");
					assert(mustPlay);
					const bool invertPolarity = preparedState.asio401.WithDevice(
						[&](const QA401&) { return true; }, // https://github.com/dechamps/ASIO401/issues/14
						[&](const QA403&) { return false; }
					);
					PreProcessASIOOutputBuffers(preparedState.bufferInfos, outputAsioBufferIndex, preparedState.buffers.bufferSizeInFrames, preparedState.asio401.GetDeviceSampleSizeInBytes(), preparedState.asio401.GetDeviceSampleEndianness(), invertPolarity);
					auto& writeBuffer = *writeBuffers[bufferIndex];
					if (writeBuffer.GetIoSlot().HasPending()) {
						assert(bufferIndex == writeBufferIndex);
						finishSending();
					}
					const auto data = writeBuffer.data();
					CopyToQA40xBuffer(
						preparedState.bufferInfos,
						preparedState.buffers.bufferSizeInFrames,
						outputAsioBufferIndex,
						firstWrite ? data.last(asioBufferSizeInBytes) : data.first(asioBufferSizeInBytes),
						preparedState.asio401.GetDeviceOutputChannelCount(),
						preparedState.asio401.GetDeviceSampleSizeInBytes());
				};
				const auto writeWithheldOutputBuffers = [&] {
					if (IsLoggingEnabled()) Log() << "Issuing " << withheldOutputBuffers << " withheld writes";
					for (; withheldOutputBuffers > 0; --withheldOutputBuffers) startSending();
				};

				const auto qa40xToAsio = [&] {
					if (IsLoggingEnabled()) Log() << "About to copy data from QA40x read buffer index " << readBufferIndex << " to ASIO buffer index " << asioBufferIndex << (recordedFirstBuffer ? "" : " (first read)");
					assert(mustRecord);
					finishReceiving();
					const bool swapChannels = preparedState.asio401.WithDevice(
						[&](const QA401&) { return true; }, // https://github.com/dechamps/ASIO401/issues/13
						[&](const QA403&) { return false; });
					const auto data = readBuffers[readBufferIndex]->data();
					CopyFromQA40xBuffer(
						preparedState.bufferInfos,
						preparedState.buffers.bufferSizeInFrames,
						asioBufferIndex,
						recordedFirstBuffer ? data.first(asioBufferSizeInBytes) : data.last(asioBufferSizeInBytes),
						preparedState.asio401.GetDeviceInputChannelCount(),
						preparedState.asio401.GetDeviceSampleSizeInBytes(),
						swapChannels);
					startReceiving();
					PostProcessASIOInputBuffers(preparedState.bufferInfos, asioBufferIndex, preparedState.buffers.bufferSizeInFrames, preparedState.asio401.GetDeviceSampleSizeInBytes(), preparedState.asio401.GetDeviceSampleEndianness());
					recordedFirstBuffer = true;
				};

				if (mustPlay && hostSupportsOutputReady) {
					// We only wait for OutputReady() after we've called bufferSwitch() at least once. In theory it *may*
					// be pedentically correct to require the host application to call OutputReady() after Start() returns
					// but before the first bufferSwitch() call is made, but in practice it's likely many applications
					// won't do that.
					if (!firstWriteStarted) {
						std::unique_lock outputReadyLock(outputReadyMutex);
						if (!outputReady) {
							if (IsLoggingEnabled()) Log() << "Waiting for the ASIO Host Application to signal OutputReady";
							outputReadyCondition.wait(outputReadyLock, [&] { return outputReady; });
						}
					}
					asioToQa40xWithheld();
				}

				if (!primed && (
					!mustPlay // In read-only mode we are in steady state from the first iteration - there are no output buffers, therefore no priming necessary
					|| withheldOutputBuffers == writeBuffers.size() // We are entering steady-state because we have accumulated enough initial output data
				)) {
					if (IsLoggingEnabled()) Log() << "We are now primed";
					if (!mustPlay) {
						assert(withheldOutputBuffers == 0);
						assert(!firstWriteStarted);
						// Even if we don't want to play anything, we still have to do at least one write to start the hardware,
						// otherwise the first read will just hang forever.
						// Note we won't wait for this write - it will stay pending until we stop streaming. This should be fine.
						startSending();
					}
					primed = true;
				}

				if (primed) {
					// During priming, writes are "withheld", i.e. we collect the output data from the app and store it in
					// QA40x-facing write buffers, but we don't actually send them. This is to ensure the QA40x doesn't
					// actually start streaming before priming is done.
					// In the first steady-state iteration, we issue all withheld writes. On subsequent steady-state iterations,
					// this will send a single write per iteration as writes will not spend any time in a withheld state.
					writeWithheldOutputBuffers();

					if (mustRecord) {
						qa40xToAsio();
					}
					else if (mustRead) {
						finishReceiving();
						startReceiving();
					}
				}

				BufferSwitch(asioBufferIndex, currentSamplePosition);
				currentSamplePosition.samples = ::dechamps_ASIOUtil::Int64ToASIO<ASIOSamples>(::dechamps_ASIOUtil::ASIOToInt64(currentSamplePosition.samples) + preparedState.buffers.bufferSizeInFrames);

				if (mustPlay && !hostSupportsOutputReady) asioToQa40xWithheld();

				preparedState.asio401.WithDevice(
					[&](QA401& qa401) { qa401.Ping(); },
					[&](auto&) {});
			}
		}
		catch (StopRequested) {
			Log() << "Streaming successfully stopped; tearing down device";
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
			// ~RunningState() may already be calling `Abort()` at the same time, but that shouldn't
			// matter - whomever gets there first will trigger the abort and the second call should
			// be a no-op.
			Abort();
			for (auto& readBuffer : readBuffers) if (readBuffer.has_value() && readBuffer->GetIoSlot().HasPending()) (void) readBuffer->GetIoSlot().Await();
			for (auto& writeBuffer : writeBuffers) if (writeBuffer.has_value() && writeBuffer->GetIoSlot().HasPending()) (void) writeBuffer->GetIoSlot().Await();
			TearDownDevice();
		}
		catch (const std::exception& exception) {
			Log() << "Fatal error occurred while attempting to tear down the QA40x: " << exception.what();
			requestReset();
		}
		catch (...) {
			Log() << "Unknown fatal error occurred while attempting to tear down the QA40x";
			requestReset();
		}
	}

	void ASIO401::PreparedState::RunningState::RunningState::SetupDevice() {
		preparedState.asio401.WithDevice(
			[&](QA401& qa401) {
				// Note: the input high pass filter is not configurable, because there's no clear use case for disabling it.
				// If you can think of one, feel free to reopen https://github.com/dechamps/ASIO401/issues/7.
				qa401.Reset(
					QA401::InputHighPassFilterState::ENGAGED,
					GetQA401AttenuatorState(preparedState.asio401.config),
					*GetQA401SampleRate(sampleRate)
				);
			},
			[&](QA403& qa403) {
				qa403.Reset(
					GetQA403FullScaleInputLevel(preparedState.asio401.config),
					GetQA403FullScaleOutputLevel(preparedState.asio401.config),
					*GetQA403SampleRate(sampleRate));
				qa403.Start();
			});
	}

	void ASIO401::PreparedState::RunningState::RunningState::TearDownDevice() {
		preparedState.asio401.WithDevice([&](QA401& qa401) {
			// The QA401 output will exhibit a lingering DC offset if we don't reset it. Also, (re-)engage the attenuator just to be safe.
			qa401.Reset(
				QA401::InputHighPassFilterState::ENGAGED, QA401::AttenuatorState::ENGAGED, *GetQA401SampleRate(sampleRate)
			);
			},
			[&](QA403& qa403) {
				// Re-engage the attenuators just to be safe.
				qa403.Reset(QA403::FullScaleInputLevel::DBV42, QA403::FullScaleOutputLevel::DBVn12, QA403::SampleRate::KHZ48);
			});
	}

	void ASIO401::PreparedState::RunningState::RunningState::BufferSwitch(long driverBufferIndex, SamplePosition currentSamplePosition) {
		{
			std::unique_lock outputReadyLock(outputReadyMutex);
			outputReady = false;
		}
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
	}

	void ASIO401::Stop() {
		if (!preparedState.has_value()) throw ASIOException(ASE_InvalidMode, "stop() called before createBuffers()");
		return preparedState->Stop();
	}

	void ASIO401::PreparedState::Stop()
	{
		if (!runningState.has_value()) throw ASIOException(ASE_InvalidMode, "stop() called before start()");
		runningState.reset();
	}

	void ASIO401::GetSamplePosition(ASIOSamples* sPos, ASIOTimeStamp* tStamp) {
		if (!preparedState.has_value()) throw ASIOException(ASE_InvalidMode, "getSamplePosition() called before createBuffers()");
		return preparedState->GetSamplePosition(sPos, tStamp);
	}

	void ASIO401::PreparedState::GetSamplePosition(ASIOSamples* sPos, ASIOTimeStamp* tStamp)
	{
		if (!runningState.has_value()) throw ASIOException(ASE_InvalidMode, "getSamplePosition() called before start()");
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
		if (runningState.has_value()) runningState->OutputReady();
	}

	void ASIO401::PreparedState::RunningState::OutputReady() {
		{
			std::scoped_lock outputReadyLock(outputReadyMutex);
			outputReady = true;
		}
		outputReadyCondition.notify_all();
	}

	void ASIO401::PreparedState::RunningState::Abort() {
		preparedState.asio401.WithDevice([&](auto& device) {
			device.GetReadChannel().Abort();
			device.GetWriteChannel().Abort();
		});
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

