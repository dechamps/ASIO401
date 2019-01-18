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
bufferSizeSamples = 480
```

## Options reference

### Option `bufferSizeSamples`

*Integer*-typed option that determines which ASIO buffer size (in samples)
ASIO401 will suggest to the ASIO Host application.

This option can have a major impact on reliability and latency. Smaller buffers
will reduce latency but will increase the likelihood of glitches/discontinuities
(buffer overflow/underrun) if the audio pipeline is not fast enough.

Reads and writes to the QA401 happen once per buffer length, and the maximum
queue size of the QA401 is 1024 samples. For this reason, a buffer size of 1024
samples or more is unlikely to work well, as the record queue would not be
drained in time to avoid buffer overflow.

Note that some host applications might already provide a user-controlled buffer
size setting; in this case, there should be no need to use this option. It is
useful only when the application does not provide a way to customize the buffer
size.

Example:

```toml
bufferSizeSamples = 256 # 5.3 ms at 48 kHz
```

The default behaviour is to advertise minimum, preferred and maximum buffer
sizes of 512 samples.

[bufferSizeSamples]: #option-bufferSizeSamples
[configuration file]: https://en.wikipedia.org/wiki/Configuration_file
[GUI]: https://en.wikipedia.org/wiki/Graphical_user_interface
[INI files]: https://en.wikipedia.org/wiki/INI_file
[logging]: README.md#logging
[official TOML documentation]: https://github.com/toml-lang/toml#toml
[TOML]: https://en.wikipedia.org/wiki/TOML
