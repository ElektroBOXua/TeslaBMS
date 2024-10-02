## This library is a total rework of https://github.com/collin80/TeslaBMS

Unlike the original project this is a standalone, single-header library.
It's fully abstract and have no external dependencies (except stdlib).
Thank to collin80 for his hard work in discovering various EV protocols!

## Features:
- Safety oriented. Each fault or communication inconsistancy MUST be treated as critical. **See method** ```tbms_is_ready```
- Unlike the original project, this library will try to reset itself into operable state after critical errors.
- Fully asynchronous code (no delays).
- Hardware-agnostic (it only accepts and returns RX/TX buffers).
- Independent debug layer which is fully segregated from main code.

## Notes:
- This is the first release version with minimal core features. Yet it is working as expected.
- Nothing except the serial communication protocol is implemented (and probably wont be).
- It uses computed gotos and async/yield like macros in order to achieve asynchronous flow.
  It could be replaced with automatas, but the code will probably grow in size dramatically.
  Possible use of threads, but it is not portable solution.
- This is total rework from scratch.
- Code review wanted.

## TODO:
(very low difficulty, medium time effort)
- 100% test coverage.
- Repeat failed requests (like in the original library).
- Check CRC's.
- Reset faults less often.
