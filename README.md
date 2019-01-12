# ASIO401
*Brought to you by [Etienne Dechamps][] - [GitHub][]*

**If you are looking for an installer, see the
[GitHub releases page][releases].**

## Description

TODO

## Requirements

 - Windows Vista or later
 - Compatible with 32-bit and 64-bit ASIO Host Applications

## Usage

TODO

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
[releases]: https://github.com/dechamps/ASIO401/releases
[report]: #reporting-issues-feedback-feature-requests
[test]: #test-program
