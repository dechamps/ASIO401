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
# Use WASAPI as the PortAudio host API backend.
backend = "Windows WASAPI"

[input]
# Disable the input. It is strongly recommended to do this if you only want to
# stream audio in one direction.
device = ""

[output]
# Select the output device. The name comes from the output of the
# PortAudioDevices program.
device = "Speakers (Realtek High Definition Audio)"

# Open the hardware output device with 6 channels. This is only required if you
# are unhappy with the default channel count.
channels = 6

# Set the output to WASAPI Exclusive Mode.
wasapiExclusiveMode = true
```

Experimentally, the following set of options has been shown to be a good
starting point for low latency operation:

```toml
backend = "Windows WASAPI"
bufferSizeSamples = 480

[input]
suggestedLatencySeconds = 0.0
wasapiExclusiveMode = true

[output]
suggestedLatencySeconds = 0.0
wasapiExclusiveMode = true
```

## Options reference

### Global section

These options are outside of any section ("table" in TOML parlance), and affect
both input and output streams.

#### Option `backend`

*String*-typed option that determines which audio backend ASIO401 will attempt
to use. In PortAudio parlance, this is called the *host API*. ASIO401 uses the
term "backend" to avoid potential confusion with the term "ASIO host".

This is by far the most important option in ASIO401. Changing the backend can
have wide-ranging consequences on the operation of the entire audio pipeline.
For more information, see [BACKENDS][].

The value of the option is matched against PortAudio host API names, as
shown in the output of the [`PortAudioDevices` program][PortAudioDevices]. If
the specified name doesn't match any host API, ASIO401 will fail to initialize.

In practice, PortAudio will recognize the following names: `MME`,
`Windows DirectSound`, `Windows WASAPI` and `Windows WDM-KS`.

Example:

```toml
backend = "Windows WASAPI"
```

The default behaviour is to use DirectSound.

#### Option `bufferSizeSamples`

*Integer*-typed option that determines which ASIO buffer size (in samples)
ASIO401 will suggest to the ASIO Host application.

This option, in combination with
[`suggestedLatencySeconds`][suggestedLatencySeconds],
can have a major impact on reliability and latency. Smaller buffers will reduce
latency but will increase the likelihood of glitches/discontinuities (buffer
overflow/underrun) if the audio pipeline is not fast enough.

Note that some host applications might already provide a user-controlled buffer
size setting; in this case, there should be no need to use this option. It is
useful only when the application does not provide a way to customize the buffer
size.

The ASIO buffer size is also used as the PortAudio "front" (user) buffer size,
as ASIO401 bridges the two. Note that, for various technical reasons and
depending on the backend and settings used (especially the
[`suggestedLatencySeconds` option][suggestedLatencySeconds]), there are many
scenarios where additional buffers will be inserted in the audio pipeline
(either by PortAudio or by Windows itself), *in addition* to the ASIO buffer.
This can result in overall latency being higher than what the ASIO buffer size
alone would suggest.

Example:

```toml
bufferSizeSamples = 1920 # 40 ms at 48 kHz
```

The default behaviour is to advertise minimum, preferred and maximum buffer
sizes of 1 ms, 20 ms and 1 s, respectively. The resulting sizes in samples are
computed based on whatever sample rate the driver is set to when the application
enquires.

### `[input]` and `[output]` sections

Options in this section only apply to the *input* (capture, recording) audio
stream or to the *output* (rendering, playback) audio stream, respectively.

#### Option `device`

*String*-typed option that determines which hardware audio device ASIO401 will
attempt to use.

The value of the option is the *full name* of the device. The list of available
device names is shown by the [`PortAudioDevices` program][PortAudioDevices]. The
value of this option must exactly match the "Device name" shown by
`PortAudioDevices`, including any text in parentheses.

**Note:** only devices that match the selected [*backend*][BACKENDS] will be
considered. In other words, the "Host API name" as shown in the output of
`PortAudioDevices` has to match the value of the [`backend` option][backend].
Beware that a given hardware device will not necessarily have the same name
under different backends.

If the specified name doesn't match any device under the selected backend,
ASIO401 will fail to initialize.

If the option is set to the empty string (`""`), no device will be used; that
is, the input or output side of the stream will be disabled, and all other
options in the section will be ignored. Making your ASIO Host Application
unselect all input channels or all output channels will achieve the same result.

**Note:** using both input and output devices (full duplex mode) puts additional
constraints on the [backend][BACKENDS] due to the need to synchronize buffer
delivery. It makes discontinuities (glitches) more likely and increases the
lowest achievable latency. It is recommended to only use a single device (half
duplex mode) if possible.

Example:

```toml
[input]
device = ""

[output]
device = "Speakers (Realtek High Definition Audio)"
```

The default behaviour is to use the default device for the selected backend.
`PortAudioDevices` will show which device that is. Typically, this would be the
device set as default in the Windows audio control panel.

#### Option `channels`

*Integer*-typed option that determines how many channels ASIO401 will open the
hardware audio device with. This is the number of channels the ASIO Host
Application will see.

**Note:** even if the ASIO Host Application only decides to use a subset of the
available channels, the hardware audio device will still be opened with the
number of channels configured here. In other words, the host application has no
control over the hardware channel configuration. The only exception is if the
application does not request any input channels, or any output channels; in this
case the input or output device (respectively) won't be opened at all.

If the requested channel count doesn't match what the audio device is configured
for, the resulting behaviour depends on the backend. Some backends will accept
any channel count, upmixing or downmixing as necessary. Other backends might
refuse to initialize.

The value of this option must be strictly positive. To completely disable the
input or output, set the [`device` option][device] to the empty string.

**Note:** with the WASAPI backend, setting this option has the side effect of
disabling channel masks. This means channel names will not be shown, and the
backend might behave differently with regard to channel routing.

Example:

```toml
[input]
channels = 2

[output]
channels = 6
```

The default behaviour is to use the maximum channel count for the selected
device as reported by PortAudio. This information is shown in the output of the
[`PortAudioDevices` program][PortAudioDevices]. Sadly, PortAudio often gets the
channel count wrong, so setting this option explicitly might be necessary for
correct operation.

#### Option `sampleType`

*String*-typed option that determines which sample format ASIO401 will use with
this device.

ASIO401 itself doesn't do any kind of sample type conversion; therefore, this
option determines the type of samples on the ASIO side as well as the PortAudio
side.

**Note:** however, PortAudio *does* support transparent sample type conversion
internally. If this option is set to a sample type that the device cannot be
opened with, PortAudio will *automatically* and *implicitly* convert to the
"closest" type that works. Sadly, this cannot be disabled, which means it's
impossible to be sure what sample type is actually used in the PortAudio
backend, aside from examining the [ASIO401 log][logging].

The valid values are:

 - `Float32`: 32-bit IEEE floating point
 - `Int32`: 32-bit signed integer
 - `Int24`: 24-bit signed integer
 - `Int16`: 16-bit signed integer

**Note:** it makes sense to choose a specific type when using a
[backend][BACKENDS] that goes directly to the hardware, bypassing the Windows
audio engine (e.g. WASAPI Exclusive, WDM-KS). In other cases, it usually does
not make sense to choose a type other than 32-bit float, because that's what the
Windows audio pipeline uses internally, so any other type would just get
converted to 32-bit float eventually.

Example:

```toml
[output]
sampleType = "Int16"
```

The default value is `Float32`, except in WASAPI Exclusive mode, where ASIO401
will try to guess the native sample type of the hardware and use that as the
default.

#### Option `suggestedLatencySeconds`

*Floating-point*-typed option that determines the amount of audio latency (in
seconds) that ASIO401 will "suggest" to PortAudio. Typically, this has the
effect of increasing the amount of additional buffering that PortAudio will
introduce in the audio pipeline in addition to the ASIO buffer itself (see
[`bufferSizeSamples`][bufferSizeSamples]). As a result, this option can have a
major impact on reliability and latency.

The value of this option is only a hint; the resulting latency can be very
different from the value of this option. PortAudio [backends][BACKENDS]
interpret this setting in complicated and confusing ways, and it interacts
strongly with the ASIO buffer size, so it is recommended to experiment with
various values.

Setting this option to `0.0` will request the lowest possible latency that
PortAudio can provide for the selected buffer size.

**Note:** using both input and output devices (full duplex mode) puts more
buffering constraints on the backend due to synchronization requirements. Using
a low suggested latency value in this case is likely to cause audio
discontinuities (glitches). This is less of a problem when using a single device
(half duplex mode).

Example:

```toml
[output]
suggestedLatencySeconds = 0.050 # 50 ms
```

The default value is 3 times the ASIO buffer length.

#### Option `wasapiExclusiveMode`

*Boolean*-typed option that determines if the stream should be opened in
*WASAPI Shared* or in *WASAPI Exclusive* mode. For more information, see
the [WASAPI backend documentation][WASAPI].

This option is ignored if the backend is not WASAPI. See the
[`backend` option][backend].

Example:

```toml
backend = "Windows WASAPI"

[input]
wasapiExclusiveMode = true
```

The default behaviour is to open the stream in *shared* mode.

[backend]: #option-backend
[BACKENDS]: BACKENDS.md
[bufferSizeSamples]: #option-bufferSizeSamples
[configuration file]: https://en.wikipedia.org/wiki/Configuration_file
[device]: #option-device
[GUI]: https://en.wikipedia.org/wiki/Graphical_user_interface
[INI files]: https://en.wikipedia.org/wiki/INI_file
[logging]: README.md#logging
[official TOML documentation]: https://github.com/toml-lang/toml#toml
[portaudio287]: https://app.assembla.com/spaces/portaudio/tickets/287-wasapi-interprets-a-zero-suggestedlatency-in-surprising-ways
[PortAudioDevices]: README.md#device-list-program
[suggestedLatencySeconds]: #option-suggestedLatencySeconds
[TOML]: https://en.wikipedia.org/wiki/TOML
[WASAPI]: BACKENDS.md#wasapi-backend
