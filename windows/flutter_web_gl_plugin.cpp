#include "include/flutter_web_gl/flutter_web_gl_plugin.h"

#include "include/egl.h"
#include "include/gl32.h"

// This must be included before many other Windows headers.
#include <windows.h>

// For getPlatformVersion; remove unless needed for your plugin implementation.
#include <VersionHelpers.h>
#include <flutter/method_channel.h>
#include <flutter/plugin_registrar_windows.h>
#include <flutter/standard_method_codec.h>

#include <iostream>
#include <map>
#include <memory>
#include <sstream>
#include <thread>

namespace {
using flutter::EncodableMap;
using flutter::EncodableValue;

class OpenGLException {
 public:
  OpenGLException(char* message, int error);
  GLint error = 0;
  char* message = "";
};

OpenGLException::OpenGLException(char* message, int error) {
  this->error = error;
  message = message;
}

class FlutterGLTexture;

typedef std::map<int64_t, std::unique_ptr<FlutterGLTexture>> TextureMap;

class FlutterWebGlPlugin : public flutter::Plugin {
 public:
  static void RegisterWithRegistrar(flutter::PluginRegistrarWindows* registrar);

  FlutterWebGlPlugin(flutter::TextureRegistrar* textures);

  virtual ~FlutterWebGlPlugin();

  static flutter::TextureRegistrar* textureRegistrar;

 private:
  // Called when a method is called on this plugin's channel from Dart.
  void HandleMethodCall(
      const flutter::MethodCall<flutter::EncodableValue>& method_call,
      std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>> result);

  TextureMap flutterGLTextures;  // stores all created Textures
};

flutter::TextureRegistrar* FlutterWebGlPlugin::textureRegistrar;

class FlutterGLTexture {
 public:
  FlutterGLTexture(GLsizei width, GLsizei height);
  virtual ~FlutterGLTexture();
  const FlutterDesktopPixelBuffer* CopyPixelBuffer(size_t width, size_t height);

  std::unique_ptr<FlutterDesktopPixelBuffer> buffer;
  GLuint fbo;
  GLuint rbo;
  //GLuint fbo2;
  int64_t flutterTextureId;
  std::unique_ptr<flutter::TextureVariant> flutterTexture;

 private:
  std::unique_ptr<uint8_t> pixels;
  size_t request_count_ = 0;
};

FlutterGLTexture::FlutterGLTexture(GLsizei width, GLsizei height) {
  int64_t size = width * height * 4;

  pixels.reset(new uint8_t[size]);

  buffer = std::make_unique<FlutterDesktopPixelBuffer>();
  buffer->buffer = pixels.get();
  buffer->width = width;
  buffer->height = height;
  memset(pixels.get(), 0xFF, size);

/*
  // MSAA readback begin
  GLuint readbackTextureId;
  glGenTextures(1, &readbackTextureId);
  glBindTexture(GL_TEXTURE_2D, readbackTextureId);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
                  GL_LINEAR_MIPMAP_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA,
               GL_UNSIGNED_BYTE, 0);
  glBindTexture(GL_TEXTURE_2D, 0);

  glGenFramebuffers(1, &fbo2);
  glBindFramebuffer(GL_FRAMEBUFFER, fbo2);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                         readbackTextureId, 0);
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  */

  // msaa readback end

  /*
glGenRenderbuffers(1, &rbo);
glBindRenderbuffer(GL_RENDERBUFFER, rbo);
glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT, width, height);
glBindRenderbuffer(GL_RENDERBUFFER, 0);




*/

  glGenFramebuffers(1, &fbo);
  glBindFramebuffer(GL_FRAMEBUFFER, fbo);

  glGenRenderbuffers(1, &rbo);
  glBindRenderbuffer(GL_RENDERBUFFER, rbo);

   glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA8, width, height);
  //glRenderbufferStorageMultisample(GL_RENDERBUFFER, 4, GL_RGBA8, width, height);
  auto error = glGetError();
  if (error != GL_NO_ERROR) {
    std::cerr << "GlError while allocating Renderbuffer" << error << std::endl;
    throw new OpenGLException("GlError while allocating Renderbuffer", error);
  }
  glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                            GL_RENDERBUFFER, rbo);
  auto frameBufferCheck = glCheckFramebufferStatus(GL_FRAMEBUFFER);
  if (frameBufferCheck != GL_FRAMEBUFFER_COMPLETE) {
    std::cerr << "Framebuffer error" << frameBufferCheck << std::endl;
    throw new OpenGLException("Framebuffer Error while creating Texture",
                              frameBufferCheck);
  }

  error = glGetError();
  if (error != GL_NO_ERROR) {
    std::cerr << "GlError" << error << std::endl;
  }

  flutterTexture =
      std::make_unique<flutter::TextureVariant>(flutter::PixelBufferTexture(
          [this](size_t width,
                 size_t height) -> const FlutterDesktopPixelBuffer* {
            return CopyPixelBuffer(width, height);
          }));

  flutterTextureId = FlutterWebGlPlugin::textureRegistrar->RegisterTexture(
      flutterTexture.get());
}

const FlutterDesktopPixelBuffer* FlutterGLTexture::CopyPixelBuffer(
    size_t width, size_t height) {
  return buffer.get();
}

FlutterGLTexture::~FlutterGLTexture() {
  FlutterWebGlPlugin::textureRegistrar->UnregisterTexture(flutterTextureId);
  glDeleteRenderbuffers(1, &rbo);
  glDeleteFramebuffers(1, &fbo);
  pixels.reset();
  buffer.reset();
}

// static
void FlutterWebGlPlugin::RegisterWithRegistrar(
    flutter::PluginRegistrarWindows* registrar) {
  auto channel =
      std::make_unique<flutter::MethodChannel<flutter::EncodableValue>>(
          registrar->messenger(), "flutter_web_gl",
          &flutter::StandardMethodCodec::GetInstance());

  auto plugin =
      std::make_unique<FlutterWebGlPlugin>(registrar->texture_registrar());

  channel->SetMethodCallHandler(
      [plugin_pointer = plugin.get()](const auto& call, auto result) {
        plugin_pointer->HandleMethodCall(call, std::move(result));
      });
  registrar->AddPlugin(std::move(plugin));
}

FlutterWebGlPlugin::FlutterWebGlPlugin(flutter::TextureRegistrar* textures) {
  textureRegistrar = textures;
}

FlutterWebGlPlugin::~FlutterWebGlPlugin() {}

void FlutterWebGlPlugin::HandleMethodCall(
    const flutter::MethodCall<flutter::EncodableValue>& method_call,
    std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>> result) {
  const auto* arguments = std::get_if<EncodableMap>(method_call.arguments());

  if (method_call.method_name().compare("getPlatformVersion") == 0) {
    std::ostringstream version_stream;
    version_stream << "Windows ";
    if (IsWindows10OrGreater()) {
      version_stream << "10+";
    } else if (IsWindows8OrGreater()) {
      version_stream << "8";
    } else if (IsWindows7OrGreater()) {
      version_stream << "7";
    }
    result->Success(flutter::EncodableValue(version_stream.str()));
  } else if (method_call.method_name().compare("initOpenGL") == 0) {
    static EGLContext context;
    static int64_t dummySurfaceForDartSide;
    static int64_t configId;

    if (context != nullptr) {
      auto response = flutter::EncodableValue(flutter::EncodableMap{
          {flutter::EncodableValue("context"),
           flutter::EncodableValue((int64_t)context)},
          {flutter::EncodableValue("dummySurface"),
           flutter::EncodableValue(dummySurfaceForDartSide)},
          {flutter::EncodableValue("eglConfigId"),
           flutter::EncodableValue(configId)}});
      result->Success(response);
      return;
    }

    auto display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    EGLint major;
    EGLint minor;
    auto initializeResult = eglInitialize(display, &major, &minor);
    if (initializeResult != 1) {
      result->Error("EGL InitError", "eglInit failed");
      return;
    }

    std::cerr << "EGL version in native plugin" << major << "." << minor
              << std::endl;

    const EGLint attribute_list[] = {EGL_RENDERABLE_TYPE,
                                     EGL_OPENGL_ES3_BIT,
                                     EGL_RED_SIZE,
                                     8,
                                     EGL_GREEN_SIZE,
                                     8,
                                     EGL_BLUE_SIZE,
                                     8,
                                     EGL_ALPHA_SIZE,
                                     8,
                                     EGL_DEPTH_SIZE,
                                     16,
                                     EGL_NONE};

    EGLint num_config;
    EGLConfig config;
    auto chooseConfigResult =
        eglChooseConfig(display, attribute_list, &config, 1, &num_config);
    if (chooseConfigResult != 1) {
      result->Error("EGL InitError", "eglChooseConfig failed");
      return;
    }

    EGLint configId2;
    eglGetConfigAttrib(display, config, EGL_CONFIG_ID, &configId2);
    configId = (int64_t)configId2;

    const EGLint surfaceAttributes[] = {EGL_WIDTH, 16, EGL_HEIGHT, 16,
                                        EGL_NONE};

    const EGLint contextAttributes[] = {EGL_CONTEXT_CLIENT_VERSION, 3,
                                        EGL_NONE};
    context =
        eglCreateContext(display, config, EGL_NO_CONTEXT, contextAttributes);

    // This is just a dummy surface that it needed to make an OpenGL context
    // current (bind it to this thread)
    auto dummySurface =
        eglCreatePbufferSurface(display, config, surfaceAttributes);
    dummySurfaceForDartSide =
        (int64_t)eglCreatePbufferSurface(display, config, surfaceAttributes);

    eglMakeCurrent(display, dummySurface, dummySurface, context);

    auto v = glGetString(GL_VENDOR);
    int error = glGetError();
    if (error != GL_NO_ERROR) {
      std::cerr << "GlError" << error << std::endl;
    }
    auto r = glGetString(GL_RENDERER);
    auto v2 = glGetString(GL_VERSION);

    std::cerr << v << std::endl << r << std::endl << v2 << std::endl;

    /// we send back the context. This might look a bit strange, but is
    /// necessary to allow this function to be called from Dart Isolates.

    auto response = flutter::EncodableValue(flutter::EncodableMap{
        {flutter::EncodableValue("context"),
         flutter::EncodableValue((int64_t)context)},
        {flutter::EncodableValue("dummySurface"),
         flutter::EncodableValue(dummySurfaceForDartSide)},
        {flutter::EncodableValue("eglConfigId"),
         flutter::EncodableValue(configId)}});
    result->Success(response);
    return;
  } else if (method_call.method_name().compare("createTexture") == 0) {
    int width = 0;
    int height = 0;
    if (arguments) {
      auto texture_width = arguments->find(EncodableValue("width"));
      if (texture_width != arguments->end()) {
        width = std::get<std::int32_t>(texture_width->second);
      } else {
        result->Error("no texture width", "no texture width");
        return;
      }
      auto texture_height = arguments->find(EncodableValue("height"));
      if (texture_height != arguments->end()) {
        height = std::get<std::int32_t>(texture_height->second);
      } else {
        result->Error("no texture height", "no texture height");
        return;
      }
    } else {
      result->Error("no texture texture height and width",
                    "no texture width and height");
      return;
    }

    std::unique_ptr<FlutterGLTexture> flutterGLTexture;

    try {
      flutterGLTexture = std::make_unique<FlutterGLTexture>(width, height);
    } catch (OpenGLException ex) {
      result->Error(ex.message + ':' + std::to_string(ex.error));
    }
    auto rbo = (int64_t)flutterGLTexture->rbo;

    auto response = flutter::EncodableValue(flutter::EncodableMap{
        {flutter::EncodableValue("textureId"),
         flutter::EncodableValue(flutterGLTexture->flutterTextureId)},
        {flutter::EncodableValue("rbo"), flutter::EncodableValue(rbo)}});

    flutterGLTextures.insert(TextureMap::value_type(
        flutterGLTexture->flutterTextureId, std::move(flutterGLTexture)));

    result->Success(response);
    std::cerr << "Created a new texture " << width << "x" << height
              << "openGL ID" << rbo << std::endl;
  } else if (method_call.method_name().compare("updateTexture") == 0) {
    int64_t textureId = 0;
    if (arguments) {
      auto findResult = arguments->find(EncodableValue("textureId"));
      if (findResult != arguments->end()) {
        textureId = std::get<std::int64_t>(findResult->second);
      }
    } else {
      result->Error("no texture id", "no texture id");
      return;
    }

    // Check if the received ID is registered
    if (flutterGLTextures.find(textureId) == flutterGLTextures.end()) {
      result->Error("Invalid texture ID",
                    "Invalid Texture ID: " + std::to_string(textureId));
      return;
    }

    auto currentTexture = flutterGLTextures[textureId].get();
    auto width = (GLsizei)currentTexture->buffer->width;
    auto height = (GLsizei)currentTexture->buffer->height;

    /*
    glBindFramebuffer(GL_READ_FRAMEBUFFER, currentTexture->fbo);   // msaa
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, currentTexture->fbo2);  // normal

    glBlitFramebuffer(0, 0, width, height,  // src rect
                      0, 0, width, height,  // dst rect
                      GL_COLOR_BUFFER_BIT,  // buffer mask
                      GL_LINEAR);

    glBindFramebuffer(GL_FRAMEBUFFER, currentTexture->fbo2);
    */

       glBindFramebuffer(GL_FRAMEBUFFER, currentTexture->fbo);

/*
    glReadPixels(0, 0, (GLsizei)currentTexture->buffer->width,
                 (GLsizei)currentTexture->buffer->height, GL_RGBA,
                 GL_UNSIGNED_BYTE, (void*)currentTexture->buffer->buffer);
                 */
    //  textureRegistrar->MarkTextureFrameAvailable(textureId);

    glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE,
     (void*)currentTexture->buffer->buffer);
    textureRegistrar->MarkTextureFrameAvailable(textureId);

    result->Success();
  } else if (method_call.method_name().compare("deleteTexture") == 0) {
    int64_t textureId = 0;
    if (arguments) {
      auto id_iterator = arguments->find(EncodableValue("textureId"));
      if (id_iterator != arguments->end()) {
        textureId = std::get<std::int64_t>(id_iterator->second);
      }
    } else {
      result->Error("no texture id", "no texture id");
      return;
    }

    auto findResult = flutterGLTextures.find(textureId);
    // Check if the received ID is registered
    if (findResult == flutterGLTextures.end()) {
      result->Error("Invalid texture ID",
                    "Invalid Texture ID: " + std::to_string(textureId));
      return;
    }

    flutterGLTextures[textureId].release();
    flutterGLTextures.erase(textureId);

    result->Success();
  } else {
    result->NotImplemented();
  }
}

}  // namespace

void FlutterWebGlPluginRegisterWithRegistrar(
    FlutterDesktopPluginRegistrarRef registrar) {
  FlutterWebGlPlugin::RegisterWithRegistrar(
      flutter::PluginRegistrarManager::GetInstance()
          ->GetRegistrar<flutter::PluginRegistrarWindows>(registrar));
}
