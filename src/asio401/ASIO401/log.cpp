#include "log.h"

#include <shlobj.h>
#include <windows.h>

#include <dechamps_CMakeUtils/version.h>

#include "../ASIO401Util/shell.h"

namespace asio401 {

	namespace {

		class ASIO401LogSink final : public ::dechamps_cpplog::LogSink {
			public:
				static std::unique_ptr<ASIO401LogSink> Open() {
					const auto userDirectory = GetUserDirectory();
					if (!userDirectory.has_value()) return nullptr;

					std::filesystem::path path(*userDirectory);
					path.append("ASIO401.log");

					if (!std::filesystem::exists(path)) return nullptr;

					return std::make_unique<ASIO401LogSink>(path);
				}

				static ASIO401LogSink* Get() {
					static const auto output = Open();
					return output.get();
				}

				ASIO401LogSink(const std::filesystem::path& path) : file_sink(path) {
					::dechamps_cpplog::Logger(this) << "ASIO401 " << BUILD_CONFIGURATION << " " << BUILD_PLATFORM << " " << ::dechamps_CMakeUtils_gitDescriptionDirty << " built on " << ::dechamps_CMakeUtils_buildTime;
				}

				void Write(const std::string_view str) override { return preamble_sink.Write(str); }

			private:
				::dechamps_cpplog::FileLogSink file_sink;
				::dechamps_cpplog::ThreadSafeLogSink thread_safe_sink{ file_sink };
				::dechamps_cpplog::PreambleLogSink preamble_sink{ thread_safe_sink };
		};

	}

	::dechamps_cpplog::Logger Log() { return ::dechamps_cpplog::Logger(ASIO401LogSink::Get()); }

}