# ASIO401 Configuration

ASIO401 does not provide a graphical interface ([GUI][]) to adjust its
settings. This is because developing a GUI typically requires a
significant amount of developer time that ASIO401, sadly, doesn't have.
This explains why nothing happens when you click on the ASIO driver
"configure" or "settings" button in your application.

Instead, ASIO401 settings can be specified using a
[configuration file][]. ASIO401 will search for a file named
`ASIO401.toml` directly inside your Windows user profile folder; for
example: `C:\Users\Your Name\ASIO401.toml`.

If the file is missing, this is equivalent to supplying an empty file,
and as a result ASIO401 will use default values for everything.

Configuration changes will only take effect after ASIO401 is reinitialized.
Depending on the ASIO host application, this might require the application to be
restarted.

The configuration file is a text file that can be edited using any text editor,
such as Notepad. The file follows the [TOML][] syntax, which is very similar to
the syntax used for [INI files][]. Every feature described in the [official TOML documentation] should be supported.

ASIO401 will silently ignore attempts to set options that don't exist,
so beware of typos. However, if an existing option is set to an invalid
value (which includes using the wrong type or missing quotes), ASIO401
will *fail to initialize*. The [ASIO401 log][logging] will contain details
about what went wrong.

## Example configuration file

```toml
attenuator = false
bufferSizeSamples = 512
```

## Options reference

### Option `attenuator`

*Boolean*-typed option that determines if the QA401 hardware attenuator should
be engaged or not.

If the option is set to `true`, the QA401 attenuator will stay engaged.

If the option is set to `false`, the QA401 attenuator will be bypassed during
audio streaming.

Note that, contrary to the QuantAsylum Analyzer application, ASIO401 will not
adjust sample values in any way to account for the presence or absence of the
attenuator. In other words, defeating the attenuator has the effect of adding
20 dB to the samples coming from ASIO401.

**CAUTION:** do not apply high voltages to the QA401 inputs while the attenuator
is bypassed. See the QA401 User Manual for details.

Example:

```toml
attenuator = false
```

The default behaviour is to keep the attenuator engaged.

### Option `bufferSizeSamples`

*Integer*-typed option that determines which ASIO buffer size (in samples)
ASIO401 will suggest to the ASIO Host application.

This option can have a major impact on reliability and latency. Smaller buffers
will reduce latency but will increase the likelihood of glitches/discontinuities
(buffer overflow/underrun) if the audio pipeline is not fast enough.

The QA401 device can store up to 1024 samples in the hardware itself.

On the output (playback) side, ASIO401 always keeps one or two buffers inflight
at any given time; if the buffer size is larger than half the hardware queue
size, the excess data is queued on the computer side of the USB connection and
is streamed progressively as space becomes available in the QA401 hardware
queue.

On the input (recording) side, ASIO401 always keeps a read request inflight such
that the QA401 hardware queue is always being drained, and hopefully stays
nearly empty at all times.

At 48 kHz, the best tradeoff between reliability and latency is likely to be
1024 samples; this ensures there is always some data queued on the USB host side
to keep the QA401 hardware output queue nearly filled at all times. Smaller
buffers only make sense if you care about latency. Larger buffers will always
improve reliability and efficiency by relaxing scheduling constraints and
allowing for more data to be preloaded, but come with diminishing returns.

At 192 kHz, it is recommended to use a larger buffer size because 1024 samples
implies a ~5 ms buffer processing deadline, which is likely too tight for most
systems, resulting in discontinuities (glitches).

Note that some host applications might already provide a user-controlled buffer
size setting; in this case, there should be no need to use this option. It is
useful only when the application does not provide a way to customize the buffer
size.

Example:

```toml
bufferSizeSamples = 512 # ~10.7 ms at 48 kHz
```

The default behaviour is to advertise minimum, preferred and maximum buffer
sizes of 64, 1024 and 32768 samples, respectively. If the application selects
a 192 kHz sample rate, the preferred buffer size becomes 4096 samples.

[bufferSizeSamples]: #option-bufferSizeSamples
[configuration file]: https://en.wikipedia.org/wiki/Configuration_file
[GUI]: https://en.wikipedia.org/wiki/Graphical_user_interface
[INI files]: https://en.wikipedia.org/wiki/INI_file
[logging]: README.md#logging
[official TOML documentation]: https://github.com/toml-lang/toml#toml
[TOML]: https://en.wikipedia.org/wiki/TOML
