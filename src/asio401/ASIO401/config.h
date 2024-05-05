#pragma once

#include <optional>
#include <string>

namespace asio401 {

	struct Config {
		std::optional<double> fullScaleInputLevelDBV;
		std::optional<double> fullScaleOutputLevelDBV;
		std::optional<int64_t> bufferSizeSamples;
		bool forceRead = false;
	};

	std::optional<Config> LoadConfig();

}