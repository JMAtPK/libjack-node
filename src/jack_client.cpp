#include <napi.h>
#include <jack/jack.h>
#include <cmath>
#include <cstring>
#include <vector>
#include <string>
#include <atomic>
#include <mutex>

// ─── Per-port state visible to the RT thread ────────────────────
struct PortInfo {
  jack_port_t* port = nullptr;
  std::string name;
  bool is_input = false;
};

// ─── Client wrapper ─────────────────────────────────────────────
class JackClient : public Napi::ObjectWrap<JackClient> {
public:
  static Napi::Object Init(Napi::Env env, Napi::Object exports);
  JackClient(const Napi::CallbackInfo& info);
  ~JackClient();

private:
  // JS methods
  Napi::Value GetSampleRate(const Napi::CallbackInfo& info);
  Napi::Value GetBufferSize(const Napi::CallbackInfo& info);
  Napi::Value GetApi(const Napi::CallbackInfo& info);
  Napi::Value RegisterPort(const Napi::CallbackInfo& info);
  Napi::Value Activate(const Napi::CallbackInfo& info);
  Napi::Value Deactivate(const Napi::CallbackInfo& info);
  Napi::Value Connect(const Napi::CallbackInfo& info);
  Napi::Value Disconnect(const Napi::CallbackInfo& info);
  Napi::Value GetPorts(const Napi::CallbackInfo& info);
  Napi::Value Close(const Napi::CallbackInfo& info);
  Napi::Value SetProcessBuffer(const Napi::CallbackInfo& info);

  // JACK callbacks (static, called from RT thread)
  static int ProcessCallback(jack_nframes_t nframes, void* arg);
  static void ShutdownCallback(void* arg);

  jack_client_t* client_ = nullptr;
  std::vector<PortInfo> ports_;
  std::atomic<bool> active_{false};
  std::atomic<bool> shutdown_{false};

  // Shared memory for audio I/O between RT thread and JS
  // Buffers are owned by V8 ArrayBuffers (not C++ new[]) to avoid
  // "External buffers are not allowed" in Node.js 22+.
  float* input_buffer_ = nullptr;   // RT writes, JS reads
  float* output_buffer_ = nullptr;  // JS writes, RT reads
  Napi::Reference<Napi::ArrayBuffer> input_ab_ref_;
  Napi::Reference<Napi::ArrayBuffer> output_ab_ref_;
  uint32_t input_channels_ = 0;
  uint32_t output_channels_ = 0;
  uint32_t max_frames_ = 0;
};

// ─── Init ───────────────────────────────────────────────────────

Napi::Object JackClient::Init(Napi::Env env, Napi::Object exports) {
  Napi::Function func = DefineClass(env, "JackClient", {
    InstanceMethod("getSampleRate", &JackClient::GetSampleRate),
    InstanceMethod("getBufferSize", &JackClient::GetBufferSize),
    InstanceMethod("getApi", &JackClient::GetApi),
    InstanceMethod("registerPort", &JackClient::RegisterPort),
    InstanceMethod("activate", &JackClient::Activate),
    InstanceMethod("deactivate", &JackClient::Deactivate),
    InstanceMethod("connect", &JackClient::Connect),
    InstanceMethod("disconnect", &JackClient::Disconnect),
    InstanceMethod("getPorts", &JackClient::GetPorts),
    InstanceMethod("close", &JackClient::Close),
    InstanceMethod("setProcessBuffer", &JackClient::SetProcessBuffer),
  });

  Napi::FunctionReference* constructor = new Napi::FunctionReference();
  *constructor = Napi::Persistent(func);
  env.SetInstanceData(constructor);

  exports.Set("JackClient", func);
  return exports;
}

// ─── Constructor ────────────────────────────────────────────────

JackClient::JackClient(const Napi::CallbackInfo& info)
    : Napi::ObjectWrap<JackClient>(info) {
  Napi::Env env = info.Env();

  std::string name = "libjack-node";
  if (info.Length() > 0 && info[0].IsString()) {
    name = info[0].As<Napi::String>().Utf8Value();
  }

  jack_status_t status;
  client_ = jack_client_open(name.c_str(), JackNoStartServer, &status);

  if (!client_) {
    std::string err = "Failed to connect to JACK server (status: " +
                      std::to_string(status) + ")";
    Napi::Error::New(env, err).ThrowAsJavaScriptException();
    return;
  }

  jack_on_shutdown(client_, ShutdownCallback, this);

  max_frames_ = jack_get_buffer_size(client_);
}

JackClient::~JackClient() {
  if (client_) {
    if (active_) jack_deactivate(client_);
    jack_client_close(client_);
    client_ = nullptr;
  }
  // Buffers are owned by V8 ArrayBuffers — released when refs are dropped
  input_buffer_ = nullptr;
  output_buffer_ = nullptr;
}

// ─── Properties ─────────────────────────────────────────────────

Napi::Value JackClient::GetSampleRate(const Napi::CallbackInfo& info) {
  if (!client_) return info.Env().Undefined();
  return Napi::Number::New(info.Env(), jack_get_sample_rate(client_));
}

Napi::Value JackClient::GetBufferSize(const Napi::CallbackInfo& info) {
  if (!client_) return info.Env().Undefined();
  return Napi::Number::New(info.Env(), jack_get_buffer_size(client_));
}

Napi::Value JackClient::GetApi(const Napi::CallbackInfo& info) {
  return Napi::String::New(info.Env(), "JACK");
}

// ─── Port management ────────────────────────────────────────────

Napi::Value JackClient::RegisterPort(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();
  if (!client_) {
    Napi::Error::New(env, "Client not connected").ThrowAsJavaScriptException();
    return env.Undefined();
  }

  std::string portName = info[0].As<Napi::String>().Utf8Value();
  bool isInput = info[1].As<Napi::Boolean>().Value();

  unsigned long flags = isInput ? JackPortIsInput : JackPortIsOutput;
  jack_port_t* port = jack_port_register(
    client_, portName.c_str(), JACK_DEFAULT_AUDIO_TYPE, flags, 0
  );

  if (!port) {
    Napi::Error::New(env, "Failed to register port: " + portName)
      .ThrowAsJavaScriptException();
    return env.Undefined();
  }

  PortInfo pi;
  pi.port = port;
  pi.name = portName;
  pi.is_input = isInput;
  ports_.push_back(pi);

  // Track channel counts
  if (isInput) input_channels_++;
  else output_channels_++;

  return Napi::Number::New(env, (int)ports_.size() - 1);
}

// ─── Shared buffer setup ────────────────────────────────────────

Napi::Value JackClient::SetProcessBuffer(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();

  // Release previous buffer references
  input_buffer_ = nullptr;
  output_buffer_ = nullptr;
  input_ab_ref_.Reset();
  output_ab_ref_.Reset();

  Napi::Object result = Napi::Object::New(env);

  // Allocate buffers through V8 (not C++ new[]) to avoid
  // "External buffers are not allowed" in Node.js 22+.
  // V8-owned ArrayBuffers can be safely wrapped in Float32Array.
  if (input_channels_ > 0) {
    size_t byteLen = input_channels_ * max_frames_ * sizeof(float);
    auto ab = Napi::ArrayBuffer::New(env, byteLen);
    input_buffer_ = static_cast<float*>(ab.Data());
    memset(input_buffer_, 0, byteLen);
    input_ab_ref_ = Napi::Persistent(ab);  // prevent GC
    result.Set("inputBuffer", ab);
    result.Set("inputChannels", Napi::Number::New(env, input_channels_));
  }

  if (output_channels_ > 0) {
    size_t byteLen = output_channels_ * max_frames_ * sizeof(float);
    auto ab = Napi::ArrayBuffer::New(env, byteLen);
    output_buffer_ = static_cast<float*>(ab.Data());
    memset(output_buffer_, 0, byteLen);
    output_ab_ref_ = Napi::Persistent(ab);  // prevent GC
    result.Set("outputBuffer", ab);
    result.Set("outputChannels", Napi::Number::New(env, output_channels_));
  }

  result.Set("maxFrames", Napi::Number::New(env, max_frames_));

  // Set the process callback now that buffers are ready
  jack_set_process_callback(client_, ProcessCallback, this);

  return result;
}

// ─── Activate / Deactivate ──────────────────────────────────────

Napi::Value JackClient::Activate(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();
  if (!client_) {
    Napi::Error::New(env, "Client not connected").ThrowAsJavaScriptException();
    return env.Undefined();
  }

  int result = jack_activate(client_);
  if (result != 0) {
    Napi::Error::New(env, "Failed to activate JACK client")
      .ThrowAsJavaScriptException();
    return env.Undefined();
  }

  active_ = true;
  return env.Undefined();
}

Napi::Value JackClient::Deactivate(const Napi::CallbackInfo& info) {
  if (client_ && active_) {
    jack_deactivate(client_);
    active_ = false;
  }
  return info.Env().Undefined();
}

// ─── Connect / Disconnect ───────────────────────────────────────

Napi::Value JackClient::Connect(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();
  if (!client_) return env.Undefined();

  std::string src = info[0].As<Napi::String>().Utf8Value();
  std::string dst = info[1].As<Napi::String>().Utf8Value();
  int result = jack_connect(client_, src.c_str(), dst.c_str());

  return Napi::Number::New(env, result);
}

Napi::Value JackClient::Disconnect(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();
  if (!client_) return env.Undefined();

  std::string src = info[0].As<Napi::String>().Utf8Value();
  std::string dst = info[1].As<Napi::String>().Utf8Value();
  int result = jack_disconnect(client_, src.c_str(), dst.c_str());

  return Napi::Number::New(env, result);
}

// ─── Port listing ───────────────────────────────────────────────

Napi::Value JackClient::GetPorts(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();
  if (!client_) return Napi::Array::New(env, 0);

  std::string pattern = "";
  std::string typePattern = "";
  unsigned long flags = 0;

  if (info.Length() > 0 && info[0].IsObject()) {
    Napi::Object opts = info[0].As<Napi::Object>();
    if (opts.Has("pattern"))
      pattern = opts.Get("pattern").As<Napi::String>().Utf8Value();
    if (opts.Has("type"))
      typePattern = opts.Get("type").As<Napi::String>().Utf8Value();
    if (opts.Has("isInput") && opts.Get("isInput").As<Napi::Boolean>().Value())
      flags |= JackPortIsInput;
    if (opts.Has("isOutput") && opts.Get("isOutput").As<Napi::Boolean>().Value())
      flags |= JackPortIsOutput;
    if (opts.Has("isPhysical") && opts.Get("isPhysical").As<Napi::Boolean>().Value())
      flags |= JackPortIsPhysical;
  }

  const char** ports = jack_get_ports(
    client_,
    pattern.empty() ? nullptr : pattern.c_str(),
    typePattern.empty() ? nullptr : typePattern.c_str(),
    flags
  );

  Napi::Array result = Napi::Array::New(env);
  if (ports) {
    for (int i = 0; ports[i]; i++) {
      result.Set(i, Napi::String::New(env, ports[i]));
    }
    jack_free(ports);
  }

  return result;
}

// ─── Close ──────────────────────────────────────────────────────

Napi::Value JackClient::Close(const Napi::CallbackInfo& info) {
  if (client_) {
    if (active_) {
      jack_deactivate(client_);
      active_ = false;
    }
    jack_client_close(client_);
    client_ = nullptr;
  }
  return info.Env().Undefined();
}

// ─── RT process callback ────────────────────────────────────────

int JackClient::ProcessCallback(jack_nframes_t nframes, void* arg) {
  JackClient* self = static_cast<JackClient*>(arg);
  if (!self || self->shutdown_) return 0;

  uint32_t in_ch = 0;
  uint32_t out_ch = 0;

  for (size_t i = 0; i < self->ports_.size(); i++) {
    const PortInfo& pi = self->ports_[i];
    float* jack_buf = (float*)jack_port_get_buffer(pi.port, nframes);
    if (!jack_buf) continue;

    if (pi.is_input) {
      // Copy from JACK port into our input buffer (channel-strided)
      if (self->input_buffer_) {
        float* dst = self->input_buffer_ + in_ch * self->max_frames_;
        memcpy(dst, jack_buf, nframes * sizeof(float));
      }
      in_ch++;
    } else {
      // Copy from our output buffer into JACK port
      if (self->output_buffer_) {
        float* src = self->output_buffer_ + out_ch * self->max_frames_;
        memcpy(jack_buf, src, nframes * sizeof(float));
      } else {
        memset(jack_buf, 0, nframes * sizeof(float));
      }
      out_ch++;
    }
  }

  return 0;
}

void JackClient::ShutdownCallback(void* arg) {
  JackClient* self = static_cast<JackClient*>(arg);
  if (self) {
    self->shutdown_ = true;
    self->active_ = false;
    self->client_ = nullptr; // JACK has already closed it
  }
}

// ─── Module init ────────────────────────────────────────────────

Napi::Object Init(Napi::Env env, Napi::Object exports) {
  return JackClient::Init(env, exports);
}

NODE_API_MODULE(libjack_node, Init)
