#include <ASIOTest/test.h>

#include "..\ASIO401\casio401.h"

#include <cstdlib>

int main(int argc, char** argv) {
	auto* const asioDriver = CreateASIO401();
	if (asioDriver == nullptr) abort();

	const auto result = ::ASIOTest_RunTest(asioDriver, argc, argv);

	ReleaseASIO401(asioDriver);
	return result;
}
