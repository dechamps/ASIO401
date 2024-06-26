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
fullScaleInputLevelDBV = +18.0
fullScaleOutputLevelDBV = -2.0
bufferSizeSamples = 512
```

## Options reference

### Option `fullScaleInputLevelDBV`

*Floating point*-typed option that determines the QA40x input voltage that maps
to full scale sample values (i.e. the maximum input level), in dBV. Also
controls the hardware input attenuator.

Lower values of this setting increase input gain (i.e. make the recorded signal
louder), and vice-versa.

**Note:** the purpose of this option is to adjust input gain and attenuator
settings. The value entered here is NOT calibrated. The true full scale input
voltage may deviate from this setting by several dB, and therefore should not be
used for accurate absolute input voltage readings. If that's what you're after,
you will want to do your own separate calibration.

**QA403/QA402 only:** the allowed values are `0.0`, `+6.0`, `+12.0`, `+18.0`,
`+24.0`, `+30.0`, `+36.0` and `+42.0`. Values below `+24.0` will disengage the
hardware input attenuator.

**QA401 only:** the allowed values are `+6.0` and `+26.0`. A value of `+6.0`
will disengage the attenuator. **CAUTION:** do not apply high voltages to the
QA401 inputs while the attenuator is bypassed. See the QA401 User Manual for
details.

Example:

```toml
fullScaleInputLevelDBV = +6.0
```

The default value is `+42.0` for the QA403/QA402 and `+26.0` for the QA401.

### Option `fullScaleOutputLevelDBV`

*Floating point*-typed option that determines which QA40x output voltage full
scale sample values map to (i.e. the maximum output level), in dBV.

Higher values of this setting increase output gain (i.e. make the output
louder), and vice-versa.

**Note:** the purpose of this option is to adjust output gain and attenuator
settings. The value entered here is NOT calibrated. The true full scale output
voltage may deviate from this setting by several dB, and therefore should not be
used for accurate absolute output voltage readings. If that's what you're after,
you will want to do your own separate calibration.

**QA403/QA402 only:** the allowed values are `-12.0`, `-2.0`, `+8.0` and
`+18.0`.

**QA401 only:** the only allowed value is `+5.5`.

Example:

```toml
fullScaleOutputLevelDBV = +8.0
```

The default value is `-12.0` for the QA403/QA402 and `+5.5` for the QA401.

### Option `bufferSizeSamples`

*Integer*-typed option that determines which ASIO buffer size (in samples)
ASIO401 will suggest to the ASIO Host application.

This option can have a major impact on reliability and latency. Smaller buffers
will reduce latency but will increase the likelihood of glitches/discontinuities
(buffer overflow/underrun) if the audio pipeline is not fast enough.

The QA40x device can store up to 1024 samples in the hardware itself.

On the output (playback) side, ASIO401 always keeps one or two buffers inflight
at any given time; if the buffer size is larger than half the hardware queue
size, the excess data is queued on the computer side of the USB connection and
is streamed progressively as space becomes available in the QA40x hardware
queue.

On the input (recording) side, ASIO401 always keeps a read request inflight such
that the QA40x hardware queue is always being drained, and hopefully stays
nearly empty at all times.

At 48 kHz, the best tradeoff between reliability and latency is likely to be
1024 samples; this ensures the QA40x hardware queue is always full (under the
assumption that buffer switches are instant). Smaller buffers only make sense if
you care about latency. Larger buffers will always improve reliability and
efficiency by relaxing scheduling constraints and allowing for more data to be
preloaded, but come with diminishing returns.

At higher sample rates, it is recommended to use a larger buffer size due to the
tigher buffer processing deadlines that could otherwise result in
discontinuities (glitches).

Note that some host applications might already provide a user-controlled buffer
size setting; in this case, there should be no need to use this option. It is
useful only when the application does not provide a way to customize the buffer
size.

Note that playback requires buffer sizes to be multiples of 64 (for the QA403)
or 32 (for the QA401), otherwise the output is garbled. To prevent mistakes,
ASIO401 will refuse to proceed if the host application attempts to use output
channels while specifying a buffer size that violates this rule. Input-only
streams are not affected.

Example:

```toml
bufferSizeSamples = 512 # ~10.7 ms at 48 kHz
```

The default behaviour is to advertise minimum, preferred and maximum buffer
sizes of 64, 1024 and 32768 samples, respectively. If the application selects a
sample rate higher than 48 kHz, the preferred buffer size is increased
proportionally.

### Option `forceRead`

*Boolean*-typed option that determines if ASIO401 will fetch audio data from the
QA40x for clock synchronization purposes even if no input channels are enabled.

This option only has an effect if the ASIO Host Application exclusively uses
ASIO401 in the output direction (i.e. playback only, not recording). The value
of this option is ignored if any input channels are used, since in that case
ASIO401 has to read data from the QA40x anyway.

If the option is set to `true` (or the ASIO Host application enables any
input channels), then ASIO401 will use read operations to monitor the progress
of the QA40x clock, and will use that information to issue writes at the
appropriate time. This decreases output latency.

If the option is set to `false` (and the ASIO Host application does not enable
any input channels), then ASIO401 will not issue any reads. Instead, it will use
write pushback (backpressure) to synchronize with the QA40x clock. In practice
this means that ASIO401 will let all the playback buffers throughout the entire
chain fill up: the QA40x hardware queue, ASIO401's two internal buffers (each
the same size as the ASIO buffer), and the ASIO buffer itself. Avoiding reads
reduces USB load, which improves improves efficiency and relaxes performance
constraints. On top of that, the likelihood of glitches (discontinuities) from
missed deadlines is greatly reduced due to the additional buffering. The
downside is the output latency is increased by one ASIO buffer size, plus the
length of the QA40x output queue, i.e. 1024 samples.

In general, it does not make sense to enable this option unless you care about
latency.

Example:

```toml
forceRead = true
```

The default value is `false`.

### (DEPRECATED) Option `attenuator`

**Deprecated, use `maxInputLevelDBV` instead.**

*Boolean*-typed option that is equivalent to setting `maxInputLevelDBV` to
`+6.0` if set to `false`, or `+26.0` if set to `true`.

---

*ASIO is a trademark and software of Steinberg Media Technologies GmbH*

[bufferSizeSamples]: #option-bufferSizeSamples
[configuration file]: https://en.wikipedia.org/wiki/Configuration_file
[GUI]: https://en.wikipedia.org/wiki/Graphical_user_interface
[INI files]: https://en.wikipedia.org/wiki/INI_file
[logging]: README.md#logging
[official TOML documentation]: https://github.com/toml-lang/toml#toml
[TOML]: https://en.wikipedia.org/wiki/TOML
