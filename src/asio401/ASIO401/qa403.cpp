#include "QA403.h"

#include "log.h"

namespace asio401 {

	QA403::QA403(std::string_view devicePath) :
		qa40x(devicePath, /*registerPipeId*/0x01, /*writePipeId*/0x02, /*readPipeId*/0x82) {}

	void QA403::Reset(FullScaleInputLevel fullScaleInputLevel, FullScaleOutputLevel fullScaleOutputLevel) {
		Log() << "Resetting QA403";

		qa40x.AbortIO();

		// Reset the hardware. This is especially important in case of a previous unclean stop,
		// where the hardware could be left in an inconsistent state.
		qa40x.WriteRegister(8, 0);
		qa40x.WriteRegister(5, uint32_t(fullScaleInputLevel));
		qa40x.WriteRegister(6, uint32_t(fullScaleOutputLevel));
		// Wait for a bit before setting the register again, otherwise it looks like the hardware
		// "skips past" the zero state (some kind of ABA problem?)
		::Sleep(50);

		Log() << "QA403 is reset";
	}

	void QA403::Start() {
		qa40x.WriteRegister(8, 5);
	}

	void QA403::StartWrite(const void* buffer, size_t size) {
		qa40x.StartWrite(buffer, size);
	}

	void QA403::FinishWrite() {
		qa40x.FinishWrite();
	}

	void QA403::StartRead(void* buffer, size_t size) {
		qa40x.StartRead(buffer, size);
	}
	
	void QA403::FinishRead() {
		qa40x.FinishRead();
	}

}