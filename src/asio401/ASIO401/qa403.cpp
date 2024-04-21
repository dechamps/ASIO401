#include "QA403.h"

#include "log.h"

namespace asio401 {

	QA403::QA403(std::string_view devicePath) :
		qa40x(devicePath, /*registerPipeId*/0x01, /*writePipeId*/0x02, /*readPipeId*/0x82) {}

	void QA403::Reset() {
		Log() << "Resetting QA403";

		qa40x.AbortIO();
		qa40x.WriteRegister(8, 5);

		Log() << "QA403 is reset";
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