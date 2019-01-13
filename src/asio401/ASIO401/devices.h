#pragma once

#include <rpc.h>

#include <optional>
#include <string>

namespace asio401 {
	std::optional<std::string> GetDevicePath(const GUID& guid);
}
