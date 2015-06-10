// Copyright (c) 2013 GitHub, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "atom/common/api/api_messages.h"
#include "atom/common/native_mate_converters/string16_converter.h"
#include "atom/common/native_mate_converters/value_converter.h"
#include "content/public/renderer/render_view.h"
#include "native_mate/dictionary.h"
#include "third_party/WebKit/public/web/WebLocalFrame.h"
#include "third_party/WebKit/public/web/WebView.h"

#include "atom/common/node_includes.h"

using content::RenderView;
using blink::WebLocalFrame;
using blink::WebView;

namespace {

RenderView* GetCurrentRenderView() {
  WebLocalFrame* frame = WebLocalFrame::frameForCurrentContext();
  if (!frame)
    return NULL;

  WebView* view = frame->view();
  if (!view)
    return NULL;  // can happen during closing.

  return RenderView::FromWebView(view);
}

v8::Local<v8::Array> ConvertToBuffers(v8::Isolate* isolate,
                                      const SyncMessageReply& reply) {
  v8::Local<v8::Array> buffers = v8::Array::New(isolate, reply.handles.size());
  for (size_t i = 0; i < reply.handles.size(); ++i) {
    auto handle = reply.handles[i];
    auto size = reply.sizes[i];

    // Convert SharedMemory to Buffer.
    if (base::SharedMemory::IsHandleValid(handle)) {
      base::SharedMemory buffer(handle, true);
      if (buffer.Map(size)) {
        buffers->Set(i, node::Buffer::New(isolate,
                                          static_cast<char*>(buffer.memory()),
                                          size));
        continue;
      }
    }

    // Set an empty Buffer as fallback.
    buffers->Set(i, node::Buffer::New(isolate, nullptr, 0));
  }

  return buffers;
}

void Send(const base::string16& channel, const base::ListValue& arguments) {
  RenderView* render_view = GetCurrentRenderView();
  if (render_view == NULL)
    return;

  bool success = render_view->Send(new AtomViewHostMsg_Message(
      render_view->GetRoutingID(), channel, arguments));

  if (!success)
    node::ThrowError("Unable to send AtomViewHostMsg_Message");
}

mate::Dictionary SendSync(v8::Isolate* isolate,
                          const base::string16& channel,
                          const base::ListValue& arguments) {
  mate::Dictionary result(isolate, v8::Object::New(isolate));

  RenderView* render_view = GetCurrentRenderView();
  if (!render_view) {
    result.Set("error", "Unable to get RenderView");
    return result;
  }

  SyncMessageReply reply;
  IPC::SyncMessage* message = new AtomViewHostMsg_Message_Sync(
      render_view->GetRoutingID(), channel, arguments, &reply);
  // Enable the UI thread in browser to receive messages.
  message->EnableMessagePumping();
  bool success = render_view->Send(message);

  if (!success) {
    result.Set("error", "Unable to send AtomViewHostMsg_Message_Sync");
    return result;
  }

  v8::Local<v8::Array> buffers = ConvertToBuffers(isolate, reply);
  v8::Local<v8::String> json = mate::StringToV8(isolate, reply.json);

  // JSON.parse(reply.json)
  v8::MaybeLocal<v8::Value> parsed = v8::JSON::Parse(isolate, json);
  if (result.IsEmpty()) {
    result.Set("error", "Invalid JSON string");
    return result;
  }

  result.Set("value", parsed.ToLocalChecked());
  result.Set("buffers", buffers);
  return result;
}

void Initialize(v8::Local<v8::Object> exports, v8::Local<v8::Value> unused,
                v8::Local<v8::Context> context, void* priv) {
  mate::Dictionary dict(context->GetIsolate(), exports);
  dict.SetMethod("send", &Send);
  dict.SetMethod("sendSync", &SendSync);
}

}  // namespace

NODE_MODULE_CONTEXT_AWARE_BUILTIN(atom_renderer_ipc, Initialize)
