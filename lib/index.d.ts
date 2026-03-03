export interface PortQuery {
  pattern?: string;
  type?: string;
  isInput?: boolean;
  isOutput?: boolean;
  isPhysical?: boolean;
}

export interface ProcessBuffers {
  inputBuffer?: ArrayBuffer;
  inputChannels?: number;
  outputBuffer?: ArrayBuffer;
  outputChannels?: number;
  maxFrames: number;
}

export declare class JackClient {
  /**
   * Open a JACK client connection.
   * @param name Client name visible in JACK/PipeWire (default: "libjack-node")
   * @throws If JACK server is not running
   */
  constructor(name?: string);

  /** Server sample rate in Hz */
  getSampleRate(): number;

  /** Audio buffer size in frames */
  getBufferSize(): number;

  /** Returns "JACK" */
  getApi(): string;

  /**
   * Register an audio port.
   * @param name Port name (e.g. "in_1", "out_L")
   * @param isInput true for capture, false for playback
   * @returns Port index
   */
  registerPort(name: string, isInput: boolean): number;

  /**
   * Allocate shared buffers and set the process callback.
   * Call after registering all ports, before activate().
   *
   * Returns ArrayBuffers backed by the same memory the RT thread uses.
   * - inputBuffer: RT thread writes captured audio here (channel-strided)
   * - outputBuffer: Write audio here for playback (channel-strided)
   */
  setProcessBuffer(): ProcessBuffers;

  /** Start audio processing */
  activate(): void;

  /** Stop audio processing */
  deactivate(): void;

  /**
   * Connect two JACK ports by their full names.
   * @returns 0 on success, non-zero on error
   */
  connect(source: string, destination: string): number;

  /**
   * Disconnect two JACK ports.
   * @returns 0 on success
   */
  disconnect(source: string, destination: string): number;

  /**
   * List available JACK ports matching the query.
   */
  getPorts(query?: PortQuery): string[];

  /** Close the client and release resources */
  close(): void;
}
