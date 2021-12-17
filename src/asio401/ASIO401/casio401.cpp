#include "casio401.h"

#include "asio401.h"
#include "asio401.rc.h"
#include "asio401_h.h"

#include "log.h"

#include <dechamps_ASIOUtil/asiosdk/iasiodrv.h>
#include <dechamps_ASIOUtil/asio.h>

#pragma warning(push)
#pragma warning(disable:28204 6001 6011 6387)
#include <atlbase.h>
#pragma warning(pop)
#include <atlcom.h>

#include <cstdlib>
#include <string_view>

// Provide a definition for the ::CASIO401 class declaration that the MIDL compiler generated.
// The actual implementation is in a derived class in an anonymous namespace, as it should be.
//
// Note: ASIO doesn't use COM properly, and doesn't define a proper interface.
// Instead, it uses the CLSID to create an instance and then blindfully casts it to IASIO, giving the finger to QueryInterface() and to sensible COM design in general.
// Of course, since this is a blind cast, the order of inheritance below becomes critical: if IASIO is not first, the cast is likely to produce a wrong vtable offset, crashing the whole thing. What a nice design.
class CASIO401 : public IASIO, public IASIO401 {};

namespace asio401 {
	namespace {

		class CASIO401 :
			public ::CASIO401,
			public CComObjectRootEx<CComMultiThreadModel>,
			public CComCoClass<CASIO401, &__uuidof(::CASIO401)>
		{
			BEGIN_COM_MAP(CASIO401)
				COM_INTERFACE_ENTRY(IASIO401)

				// To add insult to injury, ASIO mistakes the CLSID for an IID when calling CoCreateInstance(). Yuck.
				COM_INTERFACE_ENTRY(::CASIO401)

				// IASIO doesn't have an IID (see above), which is why it doesn't appear here.
			END_COM_MAP()

			DECLARE_REGISTRY_RESOURCEID(IDR_ASIO401)

		public:
			CASIO401() throw() { Enter("CASIO401()", [] {}); }
			~CASIO401() throw() { Enter("~CASIO401()", [] {}); }

			// IASIO implementation

			ASIOBool init(void* sysHandle) throw() final {
				return (Enter("init()", [&] {
					if (asio401.has_value()) throw ASIOException(ASE_InvalidMode, "init() called more than once");
					asio401.emplace(sysHandle);
				}) == ASE_OK) ? ASIOTrue : ASIOFalse;
			}
			void getDriverName(char* name) throw() final {
				Enter("getDriverName()", [&] {
					strcpy_s(name, 32, "ASIO401");
				});
			}
			long getDriverVersion() throw() final {
				Enter("getDriverVersion()", [] {});
				return 0;
			}
			void getErrorMessage(char* string) throw() final {
				Enter("getErrorMessage()", [&] {
					std::string_view error(lastError);
					constexpr auto maxSize = 123;
					if (error.size() > maxSize) error.remove_suffix(error.size() - maxSize);
					std::copy(error.begin(), error.end(), string);
					string[error.size()] = '\0';
				});
			}
			ASIOError getClockSources(ASIOClockSource* clocks, long* numSources) throw() final;
			ASIOError setClockSource(long reference) throw() final;
			ASIOError getBufferSize(long* minSize, long* maxSize, long* preferredSize, long* granularity) throw() final {
				return EnterWithMethod("getBufferSize()", &ASIO401::GetBufferSize, minSize, maxSize, preferredSize, granularity);
			}

			ASIOError getChannels(long* numInputChannels, long* numOutputChannels) throw() final {
				return EnterWithMethod("getChannels()", &ASIO401::GetChannels, numInputChannels, numOutputChannels);
			}
			ASIOError getChannelInfo(ASIOChannelInfo* info) throw() final {
				return EnterWithMethod("getChannelInfo()", &ASIO401::GetChannelInfo, info);
			}
			ASIOError canSampleRate(ASIOSampleRate sampleRate) throw() final {
				bool result;
				const auto error = EnterInitialized("canSampleRate()", [&] {
					result = asio401->CanSampleRate(sampleRate);
				});
				if (error != ASE_OK) return error;
				return result ? ASE_OK : ASE_NoClock;
			}
			ASIOError setSampleRate(ASIOSampleRate sampleRate) throw() final {
				return EnterWithMethod("setSampleRate()", &ASIO401::SetSampleRate, sampleRate);
			}
			ASIOError getSampleRate(ASIOSampleRate* sampleRate) throw() final {
				return EnterWithMethod("getSampleRate()", &ASIO401::GetSampleRate, sampleRate);
			}

			ASIOError createBuffers(ASIOBufferInfo* bufferInfos, long numChannels, long bufferSize, ASIOCallbacks* callbacks) throw() final {
				return EnterWithMethod("createBuffers()", &ASIO401::CreateBuffers, bufferInfos, numChannels, bufferSize, callbacks);
			}
			ASIOError disposeBuffers() throw() final {
				return EnterWithMethod("disposeBuffers()", &ASIO401::DisposeBuffers);
			}
			ASIOError getLatencies(long* inputLatency, long* outputLatency) throw() final {
				return EnterWithMethod("getLatencies()", &ASIO401::GetLatencies, inputLatency, outputLatency);
			}

			ASIOError start() throw() final {
				return EnterWithMethod("start()", &ASIO401::Start);
			}
			ASIOError stop() throw() final {
				return EnterWithMethod("stop()", &ASIO401::Stop);
			}
			ASIOError getSamplePosition(ASIOSamples* sPos, ASIOTimeStamp* tStamp) throw() final {
				return EnterWithMethod("getSamplePosition()", &ASIO401::GetSamplePosition, sPos, tStamp);
			}

			ASIOError controlPanel() throw() final {
				return EnterWithMethod("controlPanel()", &ASIO401::ControlPanel);
			}
			ASIOError future(long selector, void *) throw() final {
				return Enter("future()", [&] {
					Log() << "Requested future selector: " << ::dechamps_ASIOUtil::GetASIOFutureSelectorString(selector);
					throw ASIOException(ASE_InvalidParameter, "future() is not supported");
				});
			}

			ASIOError outputReady() throw() final {
				return EnterWithMethod("outputReady()", &ASIO401::OutputReady);
			}

		private:
			std::string lastError;
			std::optional<ASIO401> asio401;

			template <typename Functor> ASIOError Enter(std::string_view context, Functor functor);
			template <typename... Args> ASIOError EnterInitialized(std::string_view context, Args&&... args);
			template <typename Method, typename... Args> ASIOError EnterWithMethod(std::string_view context, Method method, Args&&... args);
		};

		OBJECT_ENTRY_AUTO(__uuidof(::CASIO401), CASIO401);

		template <typename Functor> ASIOError CASIO401::Enter(std::string_view context, Functor functor) {
			if (IsLoggingEnabled()) Log() << "--- ENTERING CONTEXT: " << context;
			ASIOError result;
			try {
				functor();
				result = ASE_OK;
			}
			catch (const ASIOException& exception) {
				lastError = exception.what();
				result = exception.GetASIOError();
			}
			catch (const std::exception& exception) {
				lastError = exception.what();
				result = ASE_HWMalfunction;
			}
			catch (...) {
				lastError = "unknown exception";
				result = ASE_HWMalfunction;
			}
			if (result == ASE_OK) {
				if (IsLoggingEnabled()) Log() << "--- EXITING CONTEXT: " << context << " [OK]";
			}
			else {
				if (IsLoggingEnabled()) Log() << "--- EXITING CONTEXT: " << context << " (" << ::dechamps_ASIOUtil::GetASIOErrorString(result) << " " << lastError << ")";
			}
			return result;
		}

		template <typename... Args> ASIOError CASIO401::EnterInitialized(std::string_view context, Args&&... args) {
			if (!asio401.has_value()) {
				throw ASIOException(ASE_InvalidMode, std::string("entered ") + std::string(context) + " but uninitialized state");
			}
			return Enter(context, std::forward<Args>(args)...);
		}

		template <typename Method, typename... Args> ASIOError CASIO401::EnterWithMethod(std::string_view context, Method method, Args&&... args) {
			return EnterInitialized(context, [&] { return ((*asio401).*method)(std::forward<Args>(args)...); });
		}

		ASIOError CASIO401::getClockSources(ASIOClockSource* clocks, long* numSources) throw()
		{
			return Enter("getClockSources()", [&] {
				if (!clocks || !numSources || *numSources < 1)
					throw ASIOException(ASE_InvalidParameter, "invalid parameters to getClockSources()");

				clocks->index = 0;
				clocks->associatedChannel = -1;
				clocks->associatedGroup = -1;
				clocks->isCurrentSource = ASIOTrue;
				strcpy_s(clocks->name, 32, "Internal");
				*numSources = 1;
			});
		}

		ASIOError CASIO401::setClockSource(long reference) throw()
		{
			return Enter("setClockSource()", [&] {
				Log() << "reference = " << reference;
				if (reference != 0) throw ASIOException(ASE_InvalidParameter, "setClockSource() parameter out of bounds");
			});
		}

	}
}

IASIO* CreateASIO401() {
	::CASIO401* asio401 = nullptr;
	if (::asio401::CASIO401::CreateInstance(&asio401) != S_OK) abort();
	if (asio401 == nullptr) abort();
	return asio401;
}

void ReleaseASIO401(IASIO* const iASIO) {
	if (iASIO == nullptr) abort();
	iASIO->Release();
}
