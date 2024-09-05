## This library is a total rework of https://github.com/collin80/TeslaBMS

Unlike the original project this is a standalone, single-header library.
It's fully abstract and have no external dependencies (except stdlib).
It also has nothing except the protocol implementation.

## It features:
- Fully asynchronous code (no delays).
- Hardware-agnostic (it only accepts and returns RX-TX buffers)
- Independent debug layer which can be fully segregated from main code.

## Notes:
- This project is not yet finished.
- TODO Add examples.
