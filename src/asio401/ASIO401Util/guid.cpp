#include "guid.h"

#include <cstdio>
#include <cstdlib>

namespace asio401 {

	std::string GetGUIDString(const GUID& guid) {
		std::string str(36, 0);
		// Shamelessly stolen from https://stackoverflow.com/a/18114061/172594
		const auto result = ::snprintf(str.data(), str.size() + 1, "%08lX-%04hX-%04hX-%02hhX%02hhX-%02hhX%02hhX%02hhX%02hhX%02hhX%02hhX",
			guid.Data1, guid.Data2, guid.Data3,
			guid.Data4[0], guid.Data4[1], guid.Data4[2], guid.Data4[3],
			guid.Data4[4], guid.Data4[5], guid.Data4[6], guid.Data4[7]
		);
		if (result != int(str.size())) abort();
		return str;
	}

}