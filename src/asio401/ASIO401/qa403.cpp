#include "QA403.h"

#include "log.h"

namespace asio401 {

	QA403::QA403(std::string_view devicePath) :
		qa40x(devicePath, /*registerPipeId*/0x01, /*writePipeId*/0x02, /*readPipeId*/0x82, /*requiresApp*/false) {}

	void QA403::Reset(FullScaleInputLevel fullScaleInputLevel, FullScaleOutputLevel fullScaleOutputLevel, SampleRate sampleRate) {
		Log() << "Resetting QA403";

		// Reset the hardware. This is especially important in case of a previous unclean stop,
		// where the hardware could be left in an inconsistent state.
		WriteRegister(8, 0);
		WriteRegister(5, uint32_t(fullScaleInputLevel));
		WriteRegister(6, uint32_t(fullScaleOutputLevel));
		// QuantAsylum did not publicly document sample rate setting, this is from private correspondence with them.
		WriteRegister(9, uint32_t(sampleRate));
		// Wait for a bit before setting the register again, otherwise it looks like the hardware
		// "skips past" the zero state (some kind of ABA problem?)
		::Sleep(50);

		Log() << "QA403 is reset";
	}

	void QA403::Start() {
		WriteRegister(8, 5);
	}

}