# RemoteCWKeyer-esp32

A small ESP32-S3 box that lets you work a remote station in CW with your own paddle.

It speaks **CWNet**, the network protocol of the
[Remote CW Keyer](https://www.qsl.net/dl4yhf/Remote_CW_Keyer/Remote_CW_Keyer.htm)
by Wolfgang Buescher, **DL4YHF** — so it connects to an existing Remote CW Keyer
server the same way his client does. All credit for the protocol, and for the
program this project is built to be compatible with, goes to him.

## Why

Today both ends of a CWNet link are a Windows PC. This project replaces the
operator's end with a dedicated box: you plug in a paddle and headphones and
operate, with no PC in the middle, no RS-232 to wire and nothing to configure
at the station end.

Compatibility with DL4YHF's program is the point of the project, not a
side effect — behaviour is proven against that reference rather than made to
look similar.

## Status

Early, and honest about it: the CW keyer and the CWNet client work, the
protocol is still being tightened against the reference, and the remote
operating experience is not finished. It is ready when it is ready.

## Hardware

- **Target**: ESP32-S3
- **Paddle input**: GPIO with internal pull-up
- **Audio output**: I2S DAC for the sidetone
- **TX output**: GPIO for keying the transmitter

## License

[GPL-3.0](LICENSE).

## Contributing

Contributions must comply with [ARCHITECTURE.md](ARCHITECTURE.md).
Non-compliant PRs will not be merged.

## SAST Tools

[PVS-Studio](https://pvs-studio.com/en/pvs-studio/?utm_source=website&utm_medium=github&utm_campaign=open_source) — static analyzer for C, C++, C#, and Java code.
