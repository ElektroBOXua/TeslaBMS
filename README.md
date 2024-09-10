## This library is a total rework of https://github.com/collin80/TeslaBMS

Unlike the original project this is a standalone, single-header library.
It's fully abstract and have no external dependencies (except stdlib).
Thank to collin80 for his hard work in discovering different EV protocols!

## Features:
- Fully asynchronous code (no delays).
- Hardware-agnostic (it only accepts and returns RX/TX buffers).
- Independent debug layer which is fully segregated from main code.

## Notes:
- It is not yet finished (no examples, yet lacks common functionality, etc).
- Nothing except the serial communication protocol is implemented (and probably wont be).
- It uses computed gotos and async/yield like macros in order to achieve asynchronous flow.
- This is total rework from scratch.
