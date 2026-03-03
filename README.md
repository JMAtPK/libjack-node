# libjack-node

Native JACK audio client bindings for Node.js. Connects to [PipeWire](https://pipewire.org/) via its JACK compatibility layer, or to a standalone JACK server.

## Why

Existing Node.js audio packages (audify, naudiodon) use RtAudio or PortAudio, which go through ALSA on Linux. ALSA can't do full-duplex reliably on PipeWire. This package links directly against libjack, which PipeWire handles natively.

## System Dependencies

### Debian / Ubuntu
```bash
sudo apt install pipewire-jack libjack-jackd2-dev cmake build-essential
```

### Fedora
```bash
sudo dnf install pipewire-jack-audio-connection-kit-devel cmake gcc-c++
```

### Arch
```bash
sudo pacman -S pipewire-jack cmake base-devel
```

`pipewire-jack` provides PipeWire's `libjack.so` — the build links against it directly. `libjack-jackd2-dev` provides the JACK C headers. `cmake` is needed to compile the native addon.

## Install

```bash
bun add libjack-node
# or
npm install libjack-node
```

The native addon compiles on install via cmake-js.

## Usage

```typescript
import { JackClient } from 'libjack-node'

const client = new JackClient('my-app')
console.log(`Sample rate: ${client.getSampleRate()}`)

// Register ports
client.registerPort('out_L', false)  // false = output
client.registerPort('out_R', false)

// Allocate shared buffers (call after registering ports)
const buffers = client.setProcessBuffer()
const output = new Float32Array(buffers.outputBuffer!)

// Fill with audio data (channel-strided: [ch0_frame0..ch0_frameN, ch1_frame0..ch1_frameN])
// The RT thread copies this to JACK ports each cycle.

// Start processing
client.activate()

// Connect to physical playback
const playback = client.getPorts({ isPhysical: true, isInput: true })
client.connect('my-app:out_L', playback[0])
client.connect('my-app:out_R', playback[1])

// ... later
client.deactivate()
client.close()
```

## API

### `new JackClient(name?: string)`
Open a JACK client. Throws if no JACK/PipeWire server is running. Pass `JackNoStartServer` — won't auto-launch a server.

### `getSampleRate(): number`
Server sample rate in Hz.

### `getBufferSize(): number`
Audio buffer size in frames per cycle.

### `registerPort(name: string, isInput: boolean): number`
Register an audio port. Returns the port index. Call before `setProcessBuffer()`.

### `setProcessBuffer(): ProcessBuffers`
Allocate shared memory buffers and install the RT process callback. Returns `ArrayBuffer` objects backed by the same memory the audio thread reads/writes.

- **Input ports**: RT thread copies captured audio into `inputBuffer`
- **Output ports**: RT thread copies `outputBuffer` to JACK

Buffer layout is channel-strided: `[ch0_samples..., ch1_samples..., ...]` with `maxFrames` samples per channel.

### `activate() / deactivate()`
Start/stop audio processing.

### `connect(source, destination): number`
Connect two JACK ports by full name (e.g. `"my-app:out_L"` to `"Speaker:playback_FL"`).

### `disconnect(source, destination): number`
Disconnect two ports.

### `getPorts(query?): string[]`
List JACK ports. Filter by `{ pattern?, type?, isInput?, isOutput?, isPhysical? }`.

### `close()`
Disconnect and release all resources.

## Architecture

The native addon (C++, ~200 lines) does three things:
1. Opens a JACK client via `libjack`
2. Registers audio ports
3. Runs a process callback on the JACK real-time thread that copies between JACK port buffers and shared `ArrayBuffer` memory

JavaScript writes output audio and reads input audio through the shared `ArrayBuffer`. No serialization, no IPC — just direct memory access to the same buffers the RT thread uses.

## License

MIT
