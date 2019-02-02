#pragma once

#include <optional>
#include <string>

namespace asio401 {

	struct Config {
		bool attenuator = true;
		std::optional<int64_t> bufferSizeSamples;
		bool forceRead = false;
	};

	std::optional<Config> LoadConfig();

}