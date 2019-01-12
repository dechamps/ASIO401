#pragma once

#include <optional>
#include <string>

namespace asio401 {

	struct Config {
		std::optional<int64_t> bufferSizeSamples;
	};

	std::optional<Config> LoadConfig();

}