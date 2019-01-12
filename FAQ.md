# ASIO401 Frequently Asked Questions

## What is PortAudio?

[PortAudio][] is an open-source audio input/output library. It unifies many
different audio APIs provided by operating systems (e.g. Windows) behind a
single simplified API, making it easy to write applications (such as ASIO401)
that can leverage the variety of audio features provided by operating systems.

In ASIO401, the PortAudio library handles most of the business logic of finding
audio devices, setting them up in the correct way, and transferring audio data.
In fact, it would be fair to describe ASIO401 as a mere "wrapper" or "glue
layer", providing a bridge between the ASIO API and the PortAudio library.

Because ASIO401 does relatively little work compared to PortAudio, most of
ASIO401's behaviour, features, limitations, and quirks are ultimately a
reflection of PortAudio's.

**Note:** ASIO401 is not affiliated with PortAudio in any way. It is merely
using it as a library.

## Where is the control panel?

There isn't one. ASIO401 settings can only be changed through a [configuration
file][CONFIGURATION].

The reason for the lack of a proper control panel is because developing a
graphical user interface requires a lot of time and resources, which ASIO401
currently doesn't have.

## Why does ASIO401 fail to initialize?

There are many reasons why ASIO401 might refuse to initialize. Sadly, the ASIO
API doesn't provide many ways of surfacing error details to applications, and
many applications don't display them anyway. The best way to shed light on what
might be going on is to inspect the [ASIO401 log][logging].

Otherwise, initialization failures can usually be traced back to problematic
settings. Here are some common issues:

 - **Invalid values for configuration options** (e.g. wrong type, typos in
   backend names or device names) will result in initialization failures.
 - **When using WASAPI Shared…**
   - **Only one sample rate is supported**: the one configured in the Windows
     audio device settings for the input *and* output devices.
     - If the ASIO Host Application uses any other sample rate, ASIO401 will
       fail to initialize.
     - If the selected input and output devices are configured with different
       sample rates in the Windows audio settings, ASIO401 will fail to
       initialize.
   - **Only one channel count is supported**: the one configured in the Windows
     audio device settings for the selected device.
     - ASIO401 will fail to initialize if it is configured to use any other
       channel count.
   - These limitations are [inherent to WASAPI itself][wasapisr].
 - **When using an exclusive backend (i.e. WASAPI Exclusive, WDM-KS)…**
   - The **sample rate** selected in the ASIO Host Application must be natively
     supported by the hardware audio device.
   - The **channel count** that ASIO401 is configured to use must be natively
     supported by the hardware audio device.
 - **WDM-KS will fail to initialize if the selected device is already in use**
   by any other application, even if no audio is actually playing. This means
   that WDM-KS is unlikely to initialize successfully on the Windows default
   devices; this can be worked around using the
   [`device` configuration option][device].
 - A **ASIO401 (or PortAudio) bug**. If you believe that is the case, please
   [file a report][report].
   - In particular, please do file a report if ASIO401 fails to initialize with
     the default configuration, as the defaults are always supposed to work on
     all systems.

## Why am I getting "glitches" (cracks, pops) in the audio?

A more technical term for these is *discontinuities*. They are often caused by
*buffer overflows* (input) or *buffer underruns* (output). These in turn have
two typical causes:

 - **When the input and output are not provided by a single hardware device**
   (e.g. using two sound cards):
   - It is unlikely that the two devices will use the same clock.
   - Therefore, they will run at slightly different speeds.
   - The ASIO API does not handle this case well, as it can only use a single
     speed and the same streaming sequence is used for both the input and
     output.
   - For this reason, discontinuities are pretty much guaranteed to occur at one
     point or another.
   - This is an inherent limitation of ASIO. The only way to work around it
     would be to adjust for clock drift on-the-fly using sample rate conversion,
     but that is not supported by ASIO401 and it would have a number of other
     downsides anyway.
 - **Expensive processing** is being done in the critical real-time audio
   streaming loop, or the ASIO Host Application real-time streaming path is
   poorly optimized, and the pipeline is unable to keep up.
 - **Overly tight scheduling constraints**, which make it impossible to run the
   audio streaming event code (callback) in time.
   - This is especially likely to occur when using very small buffer sizes
     (smaller than the default values). See the
     [`bufferSizeSamples`][bufferSizeSamples] and
     [`suggestedLatencySeconds`][suggestedLatencySeconds] options.
   - Small buffer sizes require audio streaming events to fire with very tight
     deadlines, which can put too much pressure on the Windows thread scheduler
     or other parts of the system, especially when combined with expensive
     processing (see above).
   - Scheduling constraints are tighter when using both input and output
     devices (full duplex mode), even if both devices are backed by the same
     hardware. Problems are less likely to occur when using only the input, or
     only the output (half duplex mode).
 - **[ASIO401 logging][logging] is enabled**.
   - ASIO401 writes to the log using blocking file I/O from critical real-time
     code paths. This can easily lead to missed deadlines, especially with small
     buffer sizes.
   - Do not forget to disable logging when you don't need it.
   - To disable logging, simply delete or move the `ASIO401.log` file.
 - A **ASIO401 (or PortAudio) bug** (or lack of optimization). If you believe
   that is the case, please [file a report][report].

## How to improve the latency?

The default ASIO401 settings are optimized for reliability and ease of use, not
latency. Worse, the default backend (DirectSound) is known to underestimate its
own latency, which makes the number misleading. To improve the latency, you will
need to make some adjustments to the ASIO401 [configuration][].

The best latency (and latency reporting) is provided by the **WASAPI**
(especially in **exclusive mode**) and **WDM-KS** [backends][]. This is because
these backends offer the most direct path to the hardware.

Aside from the [`backend` option][backend], the ASIO buffer size should also be
made as small as possible (but watch out for discontinuities - see above). Some
ASIO Host Applications make it possible to change the ASIO Buffer Size in the
application itself. For those that don't, use the
[`bufferSizeSamples` option][bufferSizeSamples].

Finally, the [`suggestedLatencySeconds`][suggestedLatencySeconds] option should
be set to the smallest possible value that works.

In the end, a typical low-latency configuration might look something like this:

```toml
backend = "Windows WASAPI"
bufferSizeSamples = 480 # 10 ms at 48 kHz

[input]
suggestedLatencySeconds = 0.0
wasapiExclusiveMode = true

[output]
suggestedLatencySeconds = 0.0
wasapiExclusiveMode = true
```

## How reliable are the latency numbers reported by ASIO401?

It depends on the [backend][backends]:

 - The default backend, **DirectSound**, as well as **MME**, are blind to most
   sources of latency (including the Windows audio pipeline itself), and can
   therefore grossly underestimate the actual latency.
   - For example, both have been observed to report latencies below 20 ms, which
     is not technically possible because that's the buffer size of the Windows
     audio pipeline itself.
 - The **WASAPI** and **WDM-KS** backends have been observed to report more
   plausible latency numbers.

There are also a couple of things to keep in mind when interpreting latency
numbers:

 - The reported latency **can be higher than the ASIO buffer size**.
   - This is perfectly normal in most configurations, because some backends
     will add additional buffering on top of the ASIO buffer itself.
 - The reported latency usually **does not take the underlying hardware into
   account**.
   - For example, when playing audio over Bluetooth, the reported latency will
     not take the Bluetooth stack into account, and will therefore be grossly
     underestimated.

## How to achieve "bit-perfect" audio streaming?

In this context, *bit-perfect streaming* describes a setup in which the audio
data generated by the ASIO Host Application is delivered to the audio hardware
as-is, bit-for-bit, with no alteration whatsoever. In particular, no *sample
rate conversions* nor *bit depth conversions* are made.

The default ASIO401 settings are optimized for reliability and ease of use, not
bit-perfect streaming. To reach that goal, you will need to make some
adjustments to the ASIO401 [configuration][]:

- You will want to **avoid *shared* [backends][]** because these go through the
  Windows audio pipeline, which is never bit-perfect (unless perhaps your
  hardware is capable of handing 32-bit float samples natively, which is
  atypical).
  - That means you should either use the *WASAPI Exclusive* backend or the
    *WDM-KS* backend.
- Even then, you will want to make sure no **sample type conversions** (e.g.
  32-bit float to 16-bit integer) are taking place.
  - This is where it gets tricky, because PortAudio does sample type conversions
    automatically, and there is no way to disable that behaviour.
  - The only way to ensure no conversions are done is to set things up such that
    the **ASIO sample type** matches the native sample type of the hardware;
    this way, PortAudio will not have to do any conversion.
    - In WASAPI Exclusive mode, ASIO401 will try to do that automatically by
      guessing the hardware native sample type.
    - Otherwise, the [`sampleType` option][sampleType] can be used to override
      ASIO401's choice. This is useful when ASIO401 guesses wrong, when using
      WDM-KS, or when the hardware supports more than one native sample type.
  - Sadly, PortAudio does not make it obvious that a sample type conversion is
    taking place. To verify that no conversion is done, you might need to
    examine the ASIO401 log.

A typical bit-perfect configuration might look something like this:

```toml
backend = "Windows WASAPI"

[input]
wasapiExclusiveMode = true

[output]
wasapiExclusiveMode = true
```

[backend]: CONFIGURATION.md#option-backend
[backends]: BACKENDS.md
[bufferSizeSamples]: CONFIGURATION.md#option-bufferSizeSamples
[channels]: CONFIGURATION.md#option-channels
[device]: CONFIGURATION.md#option-device
[CONFIGURATION]: CONFIGURATION.md
[logging]: README.md#logging
[issue #3]: https://github.com/dechamps/ASIO401/issues/3
[PortAudio]: http://www.portaudio.com/
[report]: README.md#reporting-issues-feedback-feature-requests
[sampleType]: CONFIGURATION.md#option-sampleType
[suggestedLatencySeconds]: CONFIGURATION.md#option-suggestedLatencySeconds
[wasapisr]: https://docs.microsoft.com/windows/desktop/CoreAudio/device-formats
