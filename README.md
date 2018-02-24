[![CLA assistant](https://cla-assistant.io/readme/badge/nfrechette/acl)](https://cla-assistant.io/nfrechette/acl)
[![Build status](https://ci.appveyor.com/api/projects/status/8h1jwmhumqh9ie3h?svg=true)](https://ci.appveyor.com/project/nfrechette/acl)
[![Build Status](https://travis-ci.org/nfrechette/acl.svg?branch=develop)](https://travis-ci.org/nfrechette/acl)
[![GitHub (pre-)release](https://img.shields.io/github/release/nfrechette/acl/all.svg)](https://github.com/nfrechette/acl/releases)
[![GitHub license](https://img.shields.io/badge/license-MIT-blue.svg)](https://raw.githubusercontent.com/nfrechette/acl/master/LICENSE)

# Animation Compression Library

Animation compression is a fundamental aspect of modern video game engines. Not only is it important to keep the memory footprint down but it is also critical to keep the animation clip sampling performance fast.

The more memory an animation clip consumes, the slower it will be to sample it and extract a character pose at runtime. For these reasons, any game that attempts to push the boundaries of what the hardware can achieve will at some point need to implement some form of animation compression.

While some degree of compression can easily be achieved with simple tricks, achieving high compression ratios, fast decompression, while simultaneously not compromising the accuracy of the resulting compressed animation requires a great deal of care.

## Goals

This library has four primary goals:

*  Implement state of the art and production ready animation compression algorithms
*  Be easy to integrate into modern video game engines
*  Serve as a benchmark to compare various techniques against one another
*  Document what works and doesn't work

Algorithms are optimized with a focus on (in this particular order):

*  Minimizing the compression artifacts in order to reach high cinematographic quality
*  Fast decompression on all our supported hardware
*  A small memory footprint to lower memory pressure at runtime as well as reducing disk and network usage

Decompression speed will not be sacrificed for a smaller memory footprint nor will accuracy be compromised under any circumstances.

## Philosophy

Much thought was put into designing the library for it to be as flexible and powerful as possible. To this end, the following decisions were made:

*  The library consists of **100% C++11** header files and is thus easy to integrate in any game engine
*  [An intermediary clip format](./docs/the_acl_file_format.md) is supported in order to facilitate debugging and bug reporting
*  All allocations use a [game provided allocator](./includes/acl/core/iallocator.h)
*  All asserts use a [game provided macro](./includes/acl/core/error.h)

## Supported platforms

The library aims to support the most common platforms for the most common use cases. There is very little platform specific code as such it should work nearly everywhere.

The math library is not yet fully optimized for every platform. The overwhelming majority of the math heavy code executes when compressing, not decompressing.
Decompression is typically very simple and light in order to be fast. Very little math is involved beyond interpolating values.

*  Compression and decompression:
   *  Windows (VS2015, VS2017) x86 and x64
   *  Linux (gcc5, gcc6, gcc7, clang4, clang5) x86 and x64
   *  OS X (Xcode 8.3, Xcode 9.2) x86 and x64
*  Decompression only: Android (NVIDIA CodeWorks)

## External dependencies

There are none! You don't need anything else to get started: everything is self contained.
See [here](./external) for details on the ones we do include.

## Getting up and running

### Windows, Linux, and OS X

1. Install CMake 3.2 or higher, Python 3, and the proper compiler for your platform
2. Generate the IDE solution with: `python make.py`  
   The solution is generated under `./build`  
   Note that if you do not have CMake in your `PATH`, you should define the `ACL_CMAKE_HOME` environment variable to something like `C:\Program Files\CMake`.
3. Build the IDE solution with: `python make.py -build`
4. Run the unit tests with: `python make.py -unit_test`
5. Run the regression tests with: `python make.py -regression_test`

## Performance metrics

*  [Carnegie-Mellon University database performance](./docs/cmu_performance.md)
*  [Paragon database performance](./docs/paragon_performance.md)
*  [Matinee fight scene performance](./docs/fight_scene_performance.md)

## License, copyright, and code of conduct

This project uses the [MIT license](LICENSE).

Copyright (c) 2017 Nicholas Frechette & Animation Compression Library contributors

Please note that this project is released with a [Contributor Code of Conduct](CODE_OF_CONDUCT.md). By participating in this project you agree to abide by its terms.

