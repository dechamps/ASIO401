#include "qa401.h"

#include "log.h"

namespace asio401 {

	QA401::QA401(std::string_view devicePath) :
		qa40x(devicePath, /*registerPipeId*/0x02, /*writePipeId*/0x04, /*readPipeId*/0x88, /*requiresApp*/true) {}

	QA401::~QA401() {
		AbortPing();
	}

	void QA401::Reset(InputHighPassFilterState inputHighPassFilterState, AttenuatorState attenuatorState, SampleRate sampleRate) {
		Log() << "Resetting QA401 with attenuator " << (attenuatorState == AttenuatorState::DISENGAGED ? "disengaged" : "engaged") << " and sample rate " << (sampleRate == SampleRate::KHZ48 ? "48 kHz" : "192 kHz");

		AbortPing();

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

	void QA401::Ping() {
		if (pinging && qa40x.FinishWriteRegister() == QA40x::FinishResult::ABORTED) throw std::runtime_error("QA401 ping register write was unexpectedly aborted");
		// Black magic incantation provided by QuantAsylum. It's not clear what this is for; it only seems to keep the "Link" LED on during streaming.
		qa40x.StartWriteRegister(7, 3);
		pinging = true;
	}

	void QA401::AbortPing() {
		if (!pinging) return;
		Log() << "Aborting QA401 ping";
		qa40x.AbortWriteRegister();
		(void)qa40x.FinishWriteRegister();
		pinging = false;
	}

}