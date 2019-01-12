# ASIO401 Frequently Asked Questions

## Where is the control panel?

There isn't one. ASIO401 settings can only be changed through a [configuration
file][CONFIGURATION].

The reason for the lack of a proper control panel is because developing a
graphical user interface requires a lot of time and resources, which ASIO401
currently doesn't have.

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
[report]: README.md#reporting-issues-feedback-feature-requests
