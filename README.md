# ASIO401, the unofficial QA401 ASIO driver
*Brought to you by [Etienne Dechamps][] - [GitHub][]*

**If you are looking for an installer, see the
[GitHub releases page][releases].**

## Description

This is an unofficial [ASIO][] audio driver for the [QuantAsylum][] [QA401][]
audio analyzer USB device. This makes it possible to use the QA401 in any
audio application that supports ASIO, including third-party audio measurement
software (e.g. [REW][]).

This is an early prototype that is still somewhat unreliable.

**DISCLAIMER :** while this driver was developed with the help of QuantAsylum
(with my thanks to Matt Taylor), it is not officially endorsed nor supported by
QuantAsylum in any way. Please direct any support requests [to ASIO401][report],
not QuantAsylum.

## Requirements

 - A [QA401][] audio analyzer USB device
 - Windows Vista or later
 - Compatible with 32-bit and 64-bit ASIO Host Applications

## Usage

[Install][releases] ASIO401, then:

1. Connect the QA401 to your computer.
2. Open the [QuantAsylum Analyzer][] application.
3. Wait for the *Configuring hardware...* message to disappear.
4. Close the QuantAsylum Analyzer application.
5. Run your ASIO Host Application. ASIO401 should appear in the ASIO driver
   list.

You will need to repeat these steps every time you power cycle the QA401.

## Troubleshooting

The [FAQ][] provides information on how to deal with common issues. Otherwise,
ASIO401 provides a number of troubleshooting tools described below.

### Logging

ASIO401 includes a logging system that describes everything that is
happening within the driver in an excruciating amount of detail. It is
especially useful for troubleshooting driver initialization failures and
other issues.

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
[CONFIGURATION]: CONFIGURATION.md
[Etienne Dechamps]: mailto:etienne@edechamps.fr
[FAQ]: FAQ.md
[file an issue]: https://github.com/dechamps/ASIO401/issues/new
[GitHub]: https://github.com/dechamps/ASIO401
[GitHub issue tracker]: https://github.com/dechamps/ASIO401/issues
[logging]: #logging
[QuantAsylum]: https://quantasylum.com/
[QuantAsylum Analyzer]: https://github.com/QuantAsylum/QA401/releases
[QA401]: https://quantasylum.com/products/qa401-audio-analyzer
[releases]: https://github.com/dechamps/ASIO401/releases
[report]: #reporting-issues-feedback-feature-requests
[REW]: https://www.roomeqwizard.com/
[test]: #test-program
