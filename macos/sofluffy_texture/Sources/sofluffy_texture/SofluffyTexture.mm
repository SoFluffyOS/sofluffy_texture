// Copyright 2026 SoFluffy. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// IOSurfaces and pixel buffers as a Flutter texture. Each surface is wrapped
// as a pixel buffer once, and Flutter is handed the buffer of the latest
// frame whenever it asks.
//
// No fence is waited on: the caller presents a buffer only once its GPU has
// drawn it, and a buffer goes back to the caller once the next one is
// bound, by when the drawing that read it last was submitted a frame
// earlier.
#import "sofluffy_texture/sofluffy_texture.h"

#include <memory>
#include <mutex>
#include <vector>

namespace {

// One set of buffers as the caller described it, each retained; null where
// a surface could not be wrapped.
struct BufferSet {
  uint32_t generation = 0;
  std::vector<CVPixelBufferRef> buffers;

  ~BufferSet() {
    for (CVPixelBufferRef buffer : buffers) {
      if (buffer != nullptr) CVPixelBufferRelease(buffer);
    }
  }
};

}  // namespace

// ---- The texture ---------------------------------------------------------

@interface SftFlutterTexture : NSObject <FlutterTexture>
@property(nonatomic, readonly) int64_t textureId;
- (instancetype)initWithReleased:(SftBufferReleased)released userData:(void*)userData;
- (BOOL)registerWith:(id<FlutterTextureRegistry>)registry;
- (void)takeSet:(std::unique_ptr<BufferSet>)set;
- (void)showGeneration:(uint32_t)generation index:(int32_t)index;
- (void)showPixelBuffer:(CVPixelBufferRef)buffer;
- (void)shutDown;
@end

@implementation SftFlutterTexture {
  std::mutex _mutex;
  // The set waiting to be shown, and the one shown; the raster thread takes
  // pending over when Flutter asks for a buffer.
  std::unique_ptr<BufferSet> _pending;
  std::unique_ptr<BufferSet> _current;
  // The frame named last, -1 for none.
  uint32_t _shownGeneration;
  int32_t _shownIndex;
  // The buffer of a set handed to Flutter last, -1 for none: given back
  // once another is.
  uint32_t _boundGeneration;
  int32_t _boundIndex;
  // The buffer handed to Flutter last, retained: shown again while the
  // frame named is not of the set at hand, rather than one of the set's
  // buffers nobody named.
  CVPixelBufferRef _boundBuffer;
  // A buffer presented outside any set and not yet handed to Flutter,
  // retained.
  CVPixelBufferRef _direct;
  SftBufferReleased _released;
  void* _userData;
  // Disposed: a set that comes after is dropped, nothing is handed out and
  // no callback runs. Under the mutex.
  bool _disposed;
  // Main queue only.
  __weak id<FlutterTextureRegistry> _registry;
  BOOL _registered;
}

- (instancetype)initWithReleased:(SftBufferReleased)released userData:(void*)userData {
  self = [super init];
  if (self) {
    _shownGeneration = 0;
    _shownIndex = -1;
    _boundGeneration = 0;
    _boundIndex = -1;
    _boundBuffer = nullptr;
    _direct = nullptr;
    _released = released;
    _userData = userData;
    _disposed = false;
    _registered = NO;
    _textureId = -1;
  }
  return self;
}

- (void)dealloc {
  if (_boundBuffer != nullptr) CVPixelBufferRelease(_boundBuffer);
  if (_direct != nullptr) CVPixelBufferRelease(_direct);
}

- (BOOL)registerWith:(id<FlutterTextureRegistry>)registry {
  _registry = registry;
  _textureId = [registry registerTexture:self];
  _registered = YES;
  return YES;
}

// Gives buffer |index| of |generation| back to the caller. Mutex held.
- (void)giveBack:(uint32_t)generation index:(int32_t)index {
  if (_released != nullptr && !_disposed) _released(_userData, generation, index);
}

// Tells Flutter to ask for a buffer again, on the main queue, where the
// texture is unregistered.
- (void)markFrameAvailable {
  dispatch_block_t mark = ^{
    id<FlutterTextureRegistry> registry = self->_registry;
    if (self->_registered && registry != nil) {
      [registry textureFrameAvailable:self->_textureId];
    }
  };
  if (NSThread.isMainThread) {
    mark();
    return;
  }
  dispatch_async(dispatch_get_main_queue(), mark);
}

- (CVPixelBufferRef)copyPixelBuffer {
  std::lock_guard<std::mutex> lock(_mutex);
  if (_disposed) return nullptr;
  if (_direct != nullptr) {
    const uint32_t beforeGeneration = _boundGeneration;
    const int32_t beforeIndex = _boundIndex;
    if (_boundBuffer != nullptr) CVPixelBufferRelease(_boundBuffer);
    _boundBuffer = _direct;
    _direct = nullptr;
    _boundIndex = -1;
    // The set's buffer bound before was last read by the frame drawn
    // before this one: the caller's again.
    if (beforeIndex >= 0) [self giveBack:beforeGeneration index:beforeIndex];
    return CVPixelBufferRetain(_boundBuffer);
  }
  if (_pending) _current = std::move(_pending);
  // The frame named is not of the set at hand: the set changed on one
  // thread and the names on another. Buffer 0 of a new set may be the one
  // being drawn, or never drawn, and of an old set stale; the frame shown
  // last stays until one of this set is named.
  const bool named = _current && _shownIndex >= 0 &&
                     _shownGeneration == _current->generation &&
                     static_cast<size_t>(_shownIndex) < _current->buffers.size() &&
                     _current->buffers[_shownIndex] != nullptr;
  if (!named) {
    return _boundBuffer == nullptr ? nullptr : CVPixelBufferRetain(_boundBuffer);
  }
  const BufferSet& set = *_current;
  const bool rebound = _boundIndex != _shownIndex || _boundGeneration != set.generation;
  const uint32_t beforeGeneration = _boundGeneration;
  const int32_t beforeIndex = _boundIndex;
  _boundGeneration = set.generation;
  _boundIndex = _shownIndex;
  CVPixelBufferRef buffer = set.buffers[_shownIndex];
  if (buffer != _boundBuffer) {
    if (_boundBuffer != nullptr) CVPixelBufferRelease(_boundBuffer);
    _boundBuffer = CVPixelBufferRetain(buffer);
  }
  // The buffer bound before was last read by the frame drawn before this
  // one: the caller's again.
  if (rebound && beforeIndex >= 0) [self giveBack:beforeGeneration index:beforeIndex];
  return CVPixelBufferRetain(buffer);
}

// A frame named but never bound (the raster thread did not get to it) was
// never read: the caller's again at once. Without this a texture nobody
// draws (off screen, a hidden tab) would keep every buffer it is given,
// and whoever fills the set would run out. Mutex held.
- (void)giveBackUnbound {
  const bool unbound = _shownIndex != _boundIndex || _shownGeneration != _boundGeneration;
  if (_shownIndex >= 0 && unbound) [self giveBack:_shownGeneration index:_shownIndex];
}

- (void)showGeneration:(uint32_t)generation index:(int32_t)index {
  {
    std::lock_guard<std::mutex> lock(_mutex);
    if (_disposed) return;
    if (_direct != nullptr) {
      CVPixelBufferRelease(_direct);
      _direct = nullptr;
    }
    const bool superseded = _shownIndex != index || _shownGeneration != generation;
    if (superseded) [self giveBackUnbound];
    _shownGeneration = generation;
    _shownIndex = index;
  }
  [self markFrameAvailable];
}

- (void)showPixelBuffer:(CVPixelBufferRef)buffer {
  {
    std::lock_guard<std::mutex> lock(_mutex);
    if (_disposed) return;
    [self giveBackUnbound];
    _shownIndex = -1;
    if (_direct != nullptr) CVPixelBufferRelease(_direct);
    _direct = CVPixelBufferRetain(buffer);
  }
  [self markFrameAvailable];
}

- (void)takeSet:(std::unique_ptr<BufferSet>)set {
  bool named;
  {
    std::lock_guard<std::mutex> lock(_mutex);
    if (_disposed) return;
    named = _shownIndex >= 0 && set->generation == _shownGeneration;
    _pending = std::move(set);
  }
  // The frame was named before its set came: Flutter asked for it then and
  // keeps what it got, so it must ask again, or a still picture stays as
  // it was.
  if (named) [self markFrameAvailable];
}

- (void)shutDown {
  {
    std::lock_guard<std::mutex> lock(_mutex);
    _disposed = true;
    _pending.reset();
    _current.reset();
    if (_boundBuffer != nullptr) {
      CVPixelBufferRelease(_boundBuffer);
      _boundBuffer = nullptr;
    }
    if (_direct != nullptr) {
      CVPixelBufferRelease(_direct);
      _direct = nullptr;
    }
  }
  dispatch_block_t unregister = ^{
    if (!self->_registered) return;
    self->_registered = NO;
    [self->_registry unregisterTexture:self->_textureId];
  };
  if (NSThread.isMainThread) {
    unregister();
    return;
  }
  dispatch_async(dispatch_get_main_queue(), unregister);
}

@end

// ---- The C API -----------------------------------------------------------

struct SftTexture {
  SftFlutterTexture* object;
  int64_t texture_id;
};

SftTexture* sft_texture_create(id<FlutterTextureRegistry> registry,
                               SftBufferReleased released,
                               void* user_data) {
  if (registry == nil) return nullptr;
  SftFlutterTexture* object = [[SftFlutterTexture alloc] initWithReleased:released
                                                                 userData:user_data];
  [object registerWith:registry];
  return new SftTexture{object, object.textureId};
}

int64_t sft_texture_id(const SftTexture* texture) {
  return texture == nullptr ? -1 : texture->texture_id;
}

bool sft_texture_set_pixel_buffers(SftTexture* texture,
                                   uint32_t generation,
                                   CVPixelBufferRef const* buffers,
                                   size_t count) {
  if (texture == nullptr || buffers == nullptr || count == 0) return false;
  auto set = std::make_unique<BufferSet>();
  set->generation = generation;
  bool any = false;
  for (size_t i = 0; i < count; ++i) {
    CVPixelBufferRef buffer = buffers[i];
    set->buffers.push_back(buffer == nullptr ? nullptr : CVPixelBufferRetain(buffer));
    any = any || buffer != nullptr;
  }
  if (!any) return false;
  [texture->object takeSet:std::move(set)];
  return true;
}

bool sft_texture_set_surfaces(SftTexture* texture,
                              uint32_t generation,
                              IOSurfaceRef const* surfaces,
                              size_t count) {
  if (texture == nullptr || surfaces == nullptr || count == 0) return false;
  auto set = std::make_unique<BufferSet>();
  set->generation = generation;
  bool any = false;
  for (size_t i = 0; i < count; ++i) {
    CVPixelBufferRef buffer = nullptr;
    // The pixel buffer keeps the surface.
    if (surfaces[i] == nullptr ||
        CVPixelBufferCreateWithIOSurface(kCFAllocatorDefault, surfaces[i], nullptr, &buffer) !=
            kCVReturnSuccess) {
      buffer = nullptr;
    }
    set->buffers.push_back(buffer);
    any = any || buffer != nullptr;
  }
  if (!any) return false;
  [texture->object takeSet:std::move(set)];
  return true;
}

void sft_texture_present(SftTexture* texture, uint32_t generation, int32_t index) {
  if (texture == nullptr || index < 0) return;
  [texture->object showGeneration:generation index:index];
}

void sft_texture_present_pixel_buffer(SftTexture* texture, CVPixelBufferRef buffer) {
  if (texture == nullptr || buffer == nullptr) return;
  [texture->object showPixelBuffer:buffer];
}

void sft_texture_dispose(SftTexture* texture) {
  if (texture == nullptr) return;
  [texture->object shutDown];
  delete texture;
}

// ---- The plugin ----------------------------------------------------------

@implementation SofluffyTexturePlugin

+ (void)registerWithRegistrar:(id<FlutterPluginRegistrar>)registrar {
}

@end
