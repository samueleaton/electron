// Copyright (c) 2014 GitHub, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "atom/browser/api/event.h"

#include <vector>

#include "atom/common/api/api_messages.h"
#include "atom/common/native_mate_converters/string16_converter.h"
#include "base/threading/thread_restrictions.h"
#include "content/public/browser/render_process_host.h"
#include "content/public/browser/web_contents.h"
#include "native_mate/object_template_builder.h"

#include "atom/common/node_includes.h"

namespace mate {

namespace {

v8::Persistent<v8::ObjectTemplate> template_;

}  // namespace

Event::Event()
    : sender_(nullptr),
      message_(nullptr) {
}

Event::~Event() {
}

ObjectTemplateBuilder Event::GetObjectTemplateBuilder(v8::Isolate* isolate) {
  if (template_.IsEmpty())
    template_.Reset(isolate, ObjectTemplateBuilder(isolate)
        .SetMethod("preventDefault", &Event::PreventDefault)
        .SetMethod("sendReply", &Event::SendReply)
        .Build());

  return ObjectTemplateBuilder(
      isolate, v8::Local<v8::ObjectTemplate>::New(isolate, template_));
}

void Event::SetSenderAndMessage(content::WebContents* sender,
                                IPC::Message* message) {
  DCHECK(!sender_);
  DCHECK(!message_);
  sender_ = sender;
  message_ = message;

  Observe(sender);
}

void Event::WebContentsDestroyed() {
  sender_ = nullptr;
  message_ = nullptr;
}

void Event::PreventDefault(v8::Isolate* isolate) {
  GetWrapper(isolate)->Set(StringToV8(isolate, "defaultPrevented"),
                           v8::True(isolate));
}

bool Event::SendReply(const base::string16& json, mate::Arguments* args) {
  if (!message_ || !sender_)
    return false;

  SyncMessageReply reply = { json };
  std::vector<v8::Local<v8::Value>> buffers;
  if (args->GetNext(&buffers)) {
    base::ThreadRestrictions::ScopedAllowIO allow_io;
    reply.handles.reserve(buffers.size());
    reply.sizes.reserve(buffers.size());

    // Converting Buffer to SharedMemory.
    for (v8::Local<v8::Value> buffer : buffers) {
      if (node::Buffer::HasInstance(buffer)) {
        base::SharedMemory memory;
        size_t size = node::Buffer::Length(buffer);
        if (memory.CreateAndMapAnonymous(size)) {
          memcpy(memory.memory(), node::Buffer::Data(buffer), size);
          auto process = sender_->GetRenderProcessHost()->GetHandle();
          auto handle = base::SharedMemory::NULLHandle();
          if (memory.GiveToProcess(process, &handle)) {
            reply.handles.push_back(handle);
            reply.sizes.push_back(size);
            continue;
          }
        }
      }

      // Push back NULLHandle as fallback.
      reply.handles.push_back(base::SharedMemory::NULLHandle());
      reply.sizes.push_back(0);
    }
  }

  AtomViewHostMsg_Message_Sync::WriteReplyParams(message_, reply);
  return sender_->Send(message_);
}

// static
Handle<Event> Event::Create(v8::Isolate* isolate) {
  return CreateHandle(isolate, new Event);
}

}  // namespace mate
