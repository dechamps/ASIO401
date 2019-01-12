# ASIO401, the flexible universal ASIO driver
*Brought to you by [Etienne Dechamps][] - [GitHub][]*

**If you are looking for an installer, see the
[GitHub releases page][releases].**

## Description

ASIO401 is a *universal [ASIO][] driver*, meaning that it is not tied to
specific audio hardware. Other examples of universal ASIO drivers include
[ASIO4ALL][], [ASIO2KS][], [ASIO2WASAPI][].

Universal ASIO drivers use hardware-agnostic audio interfaces provided by the
operating system to produce and consume sound. The typical use case for such a
driver is to make ASIO usable with audio hardware that doesn't come with its own
ASIO drivers, or where the bundled ASIO drivers don't provide the desired
functionality.

While ASIO4ALL and ASIO2KS use a low-level Windows audio API known as
*[Kernel Streaming]* (also called "DirectKS", "WDM-KS") to operate, and
ASIO2WASAPI uses [WASAPI][] (in exclusive mode only), ASIO401 differentiates
itself by using an intermediate library called [PortAudio][] that itself
supports a large number of operating system sound APIs, which includes Kernel
Streaming and WASAPI (in shared *and* exclusive mode), but also the more mundane
APIs [MME][] and [DirectSound][]. Thus ASIO401 can be used to interface with
*any* sound API available on a Windows system. For more information, see the
[backends documentation][BACKENDS].

Among other things, this makes it possible to emulate a typical Windows
application that opens an audio device in *shared mode*. This means other
applications can use the same audio devices at the same time, with the
Windows audio engine mixing the various audio streams. Other universal ASIO
drivers do not offer this functionality as they always open audio devices in
*exclusive mode*.

## Requirements

 - Windows Vista or later
 - Compatible with 32-bit and 64-bit ASIO Host Applications

## Usage

After running the [installer][releases], ASIO401 should appear in the ASIO
driver list of any ASIO Host Application (e.g. Cubase, Sound Forge, Room EQ
Wizard).

The default settings are as follows:

 - DirectSound [backend][BACKENDS]
 - Uses the Windows default recording and playback audio devices
 - 32-bit float sample type
 - 20 ms "preferred" buffer size

All of the above can be customized using a [configuration file][CONFIGURATION].

For more advanced use cases, such as low-latency operation and bit-perfect
streaming, see the [FAQ][].

## Troubleshooting

The [FAQ][] provides information on how to deal with common issues. Otherwise,
ASIO401 provides a number of troubleshooting tools described below.

### Logging

ASIO401 includes a logging system that describes everything that is
happening within the driver in an excruciating amount of detail. It is
especially useful for troubleshooting driver initialization failures and
other issues. It can also be used for verification (e.g. to double-check
that ASIO401 is using the device and audio format that you expect).

To enable logging, simply create an empty file (e.g. with Notepad) named
`ASIO401.log` directly under your user directory (e.g.
`C:\Users\Your Name Here\ASIO401.log`). Then restart your ASIO Host
Application. ASIO401 will notice the presence of the file and start
logging to it.

Note that the contents of the log file are intended for consumption by
developers. That said, grave errors should stick out in an obvious way
(especially if you look towards the end of the log). If you are having
trouble interpreting the contents of the log, feel free to
[ask for help][report].

*Do not forget to remove the logfile once you're done with it* (or move
it elsewhere). Indeed, logging slows down ASIO401, which can lead to
discontinuities (audio glitches). The logfile can also grow to a very
large size over time.

### Device list program

ASIO401 includes a program that can be used to get the list of all the audio
devices that PortAudio (and therefore ASIO401) knows about, as well as detailed
information about each device.

The program is called `PortAudioDevices.exe` and can be found in the `x64`
(64-bit) or `x86` (32-bit) subfolder in the ASIO401 installation
folder. It is a console program that should be run from the command line. It
doesn't matter much which one you use.

### Test program

ASIO401 includes a rudimentary self-test program that can help diagnose
issues in some cases. It attempts to emulate what a basic ASIO host
application would do in a controlled, easily reproducible environment.

The program is called `ASIO401Test.exe` and can be found in the `x64`
(64-bit) or `x86` (32-bit) subfolder in the ASIO401 installation
folder. It is a console program that should be run from the command
line.

It is a good idea to have [logging][] enabled while running the test.

Note that a successful test run does not necessarily mean ASIO401 is
not at fault. Indeed it might be that the ASIO host application that
you're using is triggering a pathological case in ASIO401. If you
suspect that's the case, please feel free to [ask for help][report].

## Reporting issues, feedback, feature requests

ASIO401 welcomes feedback. Feel free to [file an issue][] in the
[GitHub issue tracker][], if there isn't one already.

When asking for help, it is strongly recommended to [produce a log][logging]
while the problem is occurring, and attach it to your report. The output of
[`ASIO401Test`][test], along with its log output, might also help.

[ASIO]: http://en.wikipedia.org/wiki/Audio_Stream_Input/Output
[ASIO2KS]: http://www.asio2ks.de/
[ASIO2WASAPI]: https://github.com/levmin/ASIO2WASAPI
[ASIO4ALL]: http://www.asio4all.org/
[BACKENDS]: BACKENDS.md
[CONFIGURATION]: CONFIGURATION.md
[DirectSound]: https://en.wikipedia.org/wiki/DirectSound
[Etienne Dechamps]: mailto:etienne@edechamps.fr
[FAQ]: FAQ.md
[file an issue]: https://github.com/dechamps/ASIO401/issues/new
[GitHub]: https://github.com/dechamps/ASIO401
[GitHub issue tracker]: https://github.com/dechamps/ASIO401/issues
[logging]: #logging
[MME]: https://en.wikipedia.org/wiki/Windows_legacy_audio_components#Multimedia_Extensions_(MME)
[Kernel Streaming]: https://en.wikipedia.org/wiki/Windows_legacy_audio_components#Kernel_Streaming
[PortAudio]: http://www.portaudio.com/
[releases]: https://github.com/dechamps/ASIO401/releases
[report]: #reporting-issues-feedback-feature-requests
[test]: #test-program
[WASAPI]: https://docs.microsoft.com/en-us/windows/desktop/coreaudio/wasapi
