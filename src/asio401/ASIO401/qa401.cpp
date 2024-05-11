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
		WriteRegister(4, 1);
		WriteRegister(4, 0);
		WriteRegister(4, 3);
		WriteRegister(4, 1);
		WriteRegister(4, 3);
		WriteRegister(4, 0);
		// Note: according to QuantAsylum these parameters can be changed at any time, except the sample rate, which can only be changed on reset.
		WriteRegister(5,
			(inputHighPassFilterState == InputHighPassFilterState::ENGAGED ? 0x01 : 0) |
			(attenuatorState == AttenuatorState::DISENGAGED ? 0x02 : 0) |
			(sampleRate == SampleRate::KHZ48 ? 0x04 : 0)
		);
		WriteRegister(6, 4);
		::Sleep(10);
		WriteRegister(6, 6);
		WriteRegister(6, 0);
		WriteRegister(4, 5);

		Log() << "QA401 is reset";
	}

	void QA401::Ping() {
		if (pinging && registerIOSlot.Await() == QA40x::RegisterChannel::Pending::AwaitResult::ABORTED) throw std::runtime_error("QA401 ping register write was unexpectedly aborted");
		// Black magic incantation provided by QuantAsylum. It's not clear what this is for; it only seems to keep the "Link" LED on during streaming.
		registerIOSlot.Start(QA40x::RegisterChannel(qa40x), 7, 3);
		pinging = true;
	}

	void QA401::AbortPing() {
		if (!pinging) return;
		Log() << "Aborting QA401 ping";
		QA40x::RegisterChannel(qa40x).Abort();
		(void)registerIOSlot.Await();
		pinging = false;
	}

}