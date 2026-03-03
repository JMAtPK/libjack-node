/**
 * Test: connect to JACK via PipeWire, play a 440Hz sine for 3 seconds.
 *
 * Run: bun test/sine.ts
 */
import { JackClient } from '../lib'

const client = new JackClient('libjack-test')
console.log(`Connected — Sample rate: ${client.getSampleRate()}, Buffer size: ${client.getBufferSize()}`)

// Register stereo output
const outL = client.registerPort('out_L', false)
const outR = client.registerPort('out_R', false)
console.log(`Ports registered: out_L (${outL}), out_R (${outR})`)

// Allocate shared buffers
const buffers = client.setProcessBuffer()
console.log(`Process buffers: ${buffers.outputChannels} out channels, ${buffers.maxFrames} frames`)

// Fill output buffer with sine wave (channel-strided layout)
const outBuf = new Float32Array(buffers.outputBuffer!)
const sampleRate = client.getSampleRate()
const freq = 440
const maxFrames = buffers.maxFrames

const phaseInc = (2 * Math.PI * freq) / sampleRate
for (let ch = 0; ch < buffers.outputChannels!; ch++) {
  const offset = ch * maxFrames
  for (let i = 0; i < maxFrames; i++) {
    outBuf[offset + i] = Math.sin(phaseInc * i) * 0.3
  }
}

// Activate
client.activate()
console.log('Client activated.')

// Auto-connect to physical playback ports
const playback = client.getPorts({ isPhysical: true, isInput: true })
console.log('Physical playback ports:', playback)

if (playback[0]) {
  client.connect('libjack-test:out_L', playback[0])
  console.log(`Connected out_L -> ${playback[0]}`)
}
if (playback[1]) {
  client.connect('libjack-test:out_R', playback[1])
  console.log(`Connected out_R -> ${playback[1]}`)
}

console.log('Playing 440Hz sine for 3 seconds...')

setTimeout(() => {
  client.deactivate()
  client.close()
  console.log('Done.')
  process.exit(0)
}, 3000)
