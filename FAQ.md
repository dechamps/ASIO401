# ASIO401 Frequently Asked Questions

## Where is the control panel?

There isn't one. ASIO401 settings can only be changed through a [configuration
file][CONFIGURATION].

The reason for the lack of a proper control panel is because developing a
graphical user interface requires a lot of time and resources, which ASIO401
currently doesn't have.

## Why is ASIO401 ignoring my configuration file and/or logfile?

You can double-check that the files are located in the correct folder and are
named properly by running the following in a command-line prompt:

```
dir %userprofile%\ASIO401*
```

The output should list `ASIO401.toml` and/or `ASIO401.log`.

**Note:** it is typical for Windows and some text editors to automatically add a
`.txt` extension to file names. This results in files named `ASIO401.toml.txt`
or `ASIO401.log.txt`, which won't work. To confuse matters further, Windows
*hides* `.txt` extensions by default. Use the aforementioned command to reveal
the true file name.

## Why does ASIO401 fail to initialize?

ASIO401 can fail to initialize for a variety of reasons. Sadly, the ASIO API
doesn't provide many ways of surfacing error details to applications, and many
applications don't display them anyway. The best way to shed light on what might
be going on is to inspect the [ASIO401 log][logging].

Remember that **ASIO401 will only work after the [QuantAsylum Analyzer][]
application has configured the hardware first**. ASIO401 does not know how to do
that by itself, and relies on the QuantAsylum-provided application to prepare
the hardware. You will need to do that again every time you power cycle the
QA401.

If the [QuantAsylum Analyzer][] application itself doesn't work, you might want
to double-check that the QA401 is connected. You might want to read the
Troubleshooting section of the QA401 User Manual. If all else fails, you might
want to ask [QuantAsylum][] themselves for help, but note that QuantAsylum will
only provide support for their applications, not ASIO401.

If the QuantAsylum Analyzer application works, but ASIO401 doesn't, you might
have found a bug in ASIO401. Feel free to [file a report][report].

## Why am I getting "glitches" (cracks, pops) in the audio?

A more technical term for these is *discontinuities*. They are often caused by
*buffer overflows* (input) or *buffer underruns* (output). These in turn have
two typical causes:

 - **USB performance issues**: contrary to most audio USB devices, the QA401
   uses bulk, not isochronous, USB transfers, which means it does not reserve
   USB bandwidth and is therefore prone from interference from other USB devices
   sharing the same bus.
   - It is recommended to keep the QA401 on a dedicated USB root all to itself,
     with no over devices competing for bandwidth and interrupts.
   - At **192 kHz** the USB timing constraints become quite severe because the
     QA401 hardware queue is only 1024 samples long, and therefore needs to be
     refreshed at least once every 5 milliseconds at 192 kHz. This is true even
     if the ASIO buffer size is larger than 1024 samples. Stick to 48 kHz if you
     can.
 - **Expensive processing** is being done in the critical real-time audio
   streaming loop, or the ASIO Host Application real-time streaming path is
   poorly optimized, and the pipeline is unable to keep up.
 - **Overly tight scheduling constraints**, which make it impossible to run the
   audio streaming event code (callback) in time.
   - This is especially likely to occur when using very small buffer sizes
     (smaller than the default values). See the
     [`bufferSizeSamples`][bufferSizeSamples] option.
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
 - A **ASIO401 bug** (or lack of optimization). If you believe that is the case,
   please [file a report][report].

## What's the deal with DC?

Here are a few things that are worth noting about ASIO401 and [DC][]:

 - **The output (playback) path is DC-coupled end-to-end**, and ASIO401 supports
   arbitrary DC offsets in that path.
   - This means that if the ASIO Host Application gives ASIO401 samples that
     contain a DC offset, that DC offset will appear as-is on the QA401 outputs.
   - ASIO401 does not provide a way to configure a fixed DC offset; it assumes
     that the ASIO Host Application will handle that. If this is something you
     think would be useful to have as an ASIO401 configuration option, feel free
     to [file a feature request][report].
 - **A DC offset can appear on the QA401 outputs if an ASIO401 streaming session
   is ended abruptly**, e.g. the ASIO Host Application crashes. This is a
   [known issue][issue6].
   - If streaming is suddenly interrupted without ASIO401 being given a chance
     to clean up, the QA401 outputs will "latch on" to the last sample that was
     played, thereby producing DC equal to the value of that last sample.
   - DC will be produced continuously until the QA401 is reset by ASIO401 or
     another QA401 application, or the QA401 is powered off.
   - To avoid this issue, please ensure that the ASIO Host Application closes
     the ASIO stream properly. This can be verified by looking for the presence
     of a `stop()`  call near the end of the [ASIO401 log][logging].
 - **The input (record) path is AC-coupled** in the QA401 hardware itself.
   - In other words, DC offsets on the input cannot be measured using ASIO401.
     This is a hardware limitation.
   - Note, however, that a very small amount of DC (less than -60 dBFS) will
     typically linger in the input signal for about 20 seconds after streaming
     starts if the attenuator is disengaged. This is a [known issue][issue17].
 - **Be careful about applying a large DC offset to the QA401 inputs, as it can
   damage the hardware.**
   - This is especially true if the attenuator is disengaged. Make sure the
     [`attenuator`][attenuator] option is set to true if there is any risk that
     the QA401 inputs will be exposed to DC.
   - See the QA401 User Manual from QuantAsylum for details.

---

*ASIO is a trademark and software of Steinberg Media Technologies GmbH*

[attenuator]: CONFIGURATION.md#option-attenuator
[bufferSizeSamples]: CONFIGURATION.md#option-bufferSizeSamples
[CONFIGURATION]: CONFIGURATION.md
[DC]: https://en.wikipedia.org/wiki/Direct_current
[issue6]: https://github.com/dechamps/ASIO401/issues/6
[issue17]: https://github.com/dechamps/ASIO401/issues/17
[logging]: README.md#logging
[QuantAsylum]: https://github.com/QuantAsylum
[QuantAsylum Analyzer]: https://github.com/QuantAsylum/QA401/releases
[report]: README.md#reporting-issues-feedback-feature-requests
