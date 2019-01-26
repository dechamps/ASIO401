# ASIO401 Frequently Asked Questions

## Where is the control panel?

There isn't one. ASIO401 settings can only be changed through a [configuration
file][CONFIGURATION].

The reason for the lack of a proper control panel is because developing a
graphical user interface requires a lot of time and resources, which ASIO401
currently doesn't have.

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
 - **192 kHz sampling rate comes with extreme USB timing constraints**, and
   typically requires a highly responsive system as well as a clean,
   unencumbered USB connection.
   - This is due to the fact that the QA401 hardware queue is only 1024 samples
     long, and therefore needs to be refreshed via USB at least once every 5
     milliseconds at 192 kHz to avoid buffer underrun/overflow. If the deadline
     is missed, a glitch will result. This is true even if the ASIO buffer size
     is larger than 1024 samples.
   - For this reason, it is recommended to avoid running the QA401 at 192 kHz
     unless you absolutely need to.
 - **[ASIO401 logging][logging] is enabled**.
   - ASIO401 writes to the log using blocking file I/O from critical real-time
     code paths. This can easily lead to missed deadlines, especially with small
     buffer sizes.
   - Do not forget to disable logging when you don't need it.
   - To disable logging, simply delete or move the `ASIO401.log` file.
 - A **ASIO401 bug** (or lack of optimization). If you believe that is the case,
   please [file a report][report].

[bufferSizeSamples]: CONFIGURATION.md#option-bufferSizeSamples
[CONFIGURATION]: CONFIGURATION.md
[logging]: README.md#logging
[QuantAsylum]: https://github.com/QuantAsylum
[QuantAsylum Analyzer]: https://github.com/QuantAsylum/QA401/releases
[report]: README.md#reporting-issues-feedback-feature-requests
