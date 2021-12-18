# ASIO401 Developer Information

See `LICENSE.txt` for licensing information. In particular, do note that
specific license terms apply to the ASIO trademark and ASIO SDK.

## Building

ASIO401 is designed to be built using CMake within the Microsoft Visual C++
2019/2022 toolchain native CMake support.

ASIO401 uses a CMake "superbuild" system (in `/src`) to automatically build the
dependencies before building ASIO401 itself. These dependencies are pulled in as
git submodules.

It is strongly recommended to use the superbuild system. Providing dependencies
manually is quite tedious because ASIO401 uses a highly modular structure that
relies on many small subprojects.

Note that the ASIOUtil build system will download the [ASIO SDK][] for you
automatically at configure time.

## Packaging

The following command will generate the installer package for you:

```
cmake -P installer.cmake
```

Note that for this command to work:

 -  You need to have [Inno Setup][] installed.
 -  You need to have built ASIO401 in the `x64-Release` and `x86-Release`
    Visual Studio configurations first.

---

*ASIO is a trademark and software of Steinberg Media Technologies GmbH*

[ASIO SDK]: http://www.steinberg.net/en/company/developer.html
[Inno Setup]: http://www.jrsoftware.org/isdl.php
[tinytoml]: https://github.com/mayah/tinytoml
