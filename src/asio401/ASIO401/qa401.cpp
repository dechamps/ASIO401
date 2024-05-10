#include "qa401.h"

#include "log.h"

namespace asio401 {

	QA401::QA401(std::string_view devicePath) :
		qa40x(devicePath, /*registerPipeId*/0x02, /*writePipeId*/0x04, /*readPipeId*/0x88, /*requiresApp*/true) {}

	void QA401::Reset(InputHighPassFilterState inputHighPassFilterState, AttenuatorState attenuatorState, SampleRate sampleRate) {
		Log() << "Resetting QA401 with attenuator " << (attenuatorState == AttenuatorState::DISENGAGED ? "disengaged" : "engaged") << " and sample rate " << (sampleRate == SampleRate::KHZ48 ? "48 kHz" : "192 kHz");

		qa40x.AbortIO();
		pinging = false;

		// Black magic incantations provided by QuantAsylum.
		qa40x.WriteRegister(4, 1);
		qa40x.WriteRegister(4, 0);
		qa40x.WriteRegister(4, 3);
		qa40x.WriteRegister(4, 1);
		qa40x.WriteRegister(4, 3);
		qa40x.WriteRegister(4, 0);
		// Note: according to QuantAsylum these parameters can be changed at any time, except the sample rate, which can only be changed on reset.
		qa40x.WriteRegister(5,
			(inputHighPassFilterState == InputHighPassFilterState::ENGAGED ? 0x01 : 0) |
			(attenuatorState == AttenuatorState::DISENGAGED ? 0x02 : 0) |
			(sampleRate == SampleRate::KHZ48 ? 0x04 : 0)
		);
		qa40x.WriteRegister(6, 4);
		::Sleep(10);
		qa40x.WriteRegister(6, 6);
		qa40x.WriteRegister(6, 0);
		qa40x.WriteRegister(4, 5);

		Log() << "QA401 is reset";
	}

	void QA401::StartWrite(const std::byte* buffer, size_t size) {
		qa40x.StartWrite(buffer, size);
	}

	void QA401::FinishWrite() {
		qa40x.FinishWrite();
	}

	void QA401::StartRead(std::byte* buffer, size_t size) {
		qa40x.StartRead(buffer, size);
	}
	
	void QA401::FinishRead() {
		qa40x.FinishRead();
	}

	void QA401::Ping() {
		if (pinging) qa40x.FinishWriteRegister();
		// Black magic incantation provided by QuantAsylum. It's not clear what this is for; it only seems to keep the "Link" LED on during streaming.
		qa40x.StartWriteRegister(7, 3);
		pinging = true;
	}

}