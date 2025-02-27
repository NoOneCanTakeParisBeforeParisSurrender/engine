// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "flutter/fml/logging.h"

#include "impeller/core/device_buffer_descriptor.h"
#include "impeller/core/formats.h"
#include "impeller/core/host_buffer.h"
#include "impeller/core/sampler_descriptor.h"
#include "impeller/fixtures/array.frag.h"
#include "impeller/fixtures/array.vert.h"
#include "impeller/fixtures/box_fade.frag.h"
#include "impeller/fixtures/box_fade.vert.h"
#include "impeller/fixtures/colors.frag.h"
#include "impeller/fixtures/colors.vert.h"
#include "impeller/fixtures/impeller.frag.h"
#include "impeller/fixtures/impeller.vert.h"
#include "impeller/fixtures/inactive_uniforms.frag.h"
#include "impeller/fixtures/inactive_uniforms.vert.h"
#include "impeller/fixtures/instanced_draw.frag.h"
#include "impeller/fixtures/instanced_draw.vert.h"
#include "impeller/fixtures/mipmaps.frag.h"
#include "impeller/fixtures/mipmaps.vert.h"
#include "impeller/geometry/path_builder.h"
#include "impeller/playground/playground_test.h"
#include "impeller/renderer/command.h"
#include "impeller/renderer/command_buffer.h"
#include "impeller/renderer/pipeline_builder.h"
#include "impeller/renderer/pipeline_library.h"
#include "impeller/renderer/render_pass.h"
#include "impeller/renderer/render_target.h"
#include "impeller/renderer/renderer.h"
#include "impeller/renderer/vertex_buffer_builder.h"
#include "impeller/tessellator/tessellator.h"
#include "third_party/imgui/imgui.h"

// TODO(zanderso): https://github.com/flutter/flutter/issues/127701
// NOLINTBEGIN(bugprone-unchecked-optional-access)

namespace impeller {
namespace testing {

using RendererTest = PlaygroundTest;
INSTANTIATE_PLAYGROUND_SUITE(RendererTest);

TEST_P(RendererTest, CanCreateBoxPrimitive) {
  using VS = BoxFadeVertexShader;
  using FS = BoxFadeFragmentShader;
  auto context = GetContext();
  ASSERT_TRUE(context);
  using BoxPipelineBuilder = PipelineBuilder<VS, FS>;
  auto desc = BoxPipelineBuilder::MakeDefaultPipelineDescriptor(*context);
  ASSERT_TRUE(desc.has_value());
  desc->SetSampleCount(SampleCount::kCount4);
  desc->SetStencilAttachmentDescriptors(std::nullopt);

  // Vertex buffer.
  VertexBufferBuilder<VS::PerVertexData> vertex_builder;
  vertex_builder.SetLabel("Box");
  vertex_builder.AddVertices({
      {{100, 100, 0.0}, {0.0, 0.0}},  // 1
      {{800, 100, 0.0}, {1.0, 0.0}},  // 2
      {{800, 800, 0.0}, {1.0, 1.0}},  // 3
      {{100, 100, 0.0}, {0.0, 0.0}},  // 1
      {{800, 800, 0.0}, {1.0, 1.0}},  // 3
      {{100, 800, 0.0}, {0.0, 1.0}},  // 4
  });
  auto bridge = CreateTextureForFixture("bay_bridge.jpg");
  auto boston = CreateTextureForFixture("boston.jpg");
  ASSERT_TRUE(bridge && boston);
  auto sampler = context->GetSamplerLibrary()->GetSampler({});
  ASSERT_TRUE(sampler);

  auto host_buffer = HostBuffer::Create(context->GetResourceAllocator());
  SinglePassCallback callback = [&](RenderPass& pass) {
    ImGui::Begin("Controls", nullptr, ImGuiWindowFlags_AlwaysAutoResize);
    static bool wireframe;
    ImGui::Checkbox("Wireframe", &wireframe);
    ImGui::End();

    desc->SetPolygonMode(wireframe ? PolygonMode::kLine : PolygonMode::kFill);
    auto pipeline = context->GetPipelineLibrary()->GetPipeline(desc).Get();

    assert(pipeline && pipeline->IsValid());

    pass.SetCommandLabel("Box");
    pass.SetPipeline(pipeline);
    pass.SetVertexBuffer(
        vertex_builder.CreateVertexBuffer(*context->GetResourceAllocator()));

    VS::UniformBuffer uniforms;
    EXPECT_EQ(pass.GetOrthographicTransform(),
              Matrix::MakeOrthographic(pass.GetRenderTargetSize()));
    uniforms.mvp =
        pass.GetOrthographicTransform() * Matrix::MakeScale(GetContentScale());
    VS::BindUniformBuffer(pass, host_buffer->EmplaceUniform(uniforms));

    FS::FrameInfo frame_info;
    frame_info.current_time = GetSecondsElapsed();
    frame_info.cursor_position = GetCursorPosition();
    frame_info.window_size.x = GetWindowSize().width;
    frame_info.window_size.y = GetWindowSize().height;

    FS::BindFrameInfo(pass, host_buffer->EmplaceUniform(frame_info));
    FS::BindContents1(pass, boston, sampler);
    FS::BindContents2(pass, bridge, sampler);

    host_buffer->Reset();
    return pass.Draw().ok();
  };
  OpenPlaygroundHere(callback);
}

TEST_P(RendererTest, CanRenderPerspectiveCube) {
  using VS = ColorsVertexShader;
  using FS = ColorsFragmentShader;
  auto context = GetContext();
  ASSERT_TRUE(context);
  auto desc = PipelineBuilder<VS, FS>::MakeDefaultPipelineDescriptor(*context);
  ASSERT_TRUE(desc.has_value());
  desc->SetCullMode(CullMode::kBackFace);
  desc->SetWindingOrder(WindingOrder::kCounterClockwise);
  desc->SetSampleCount(SampleCount::kCount4);
  desc->SetStencilAttachmentDescriptors(std::nullopt);
  auto pipeline =
      context->GetPipelineLibrary()->GetPipeline(std::move(desc)).Get();
  ASSERT_TRUE(pipeline);

  struct Cube {
    VS::PerVertexData vertices[8] = {
        // -Z
        {{-1, -1, -1}, Color::Red()},
        {{1, -1, -1}, Color::Yellow()},
        {{1, 1, -1}, Color::Green()},
        {{-1, 1, -1}, Color::Blue()},
        // +Z
        {{-1, -1, 1}, Color::Green()},
        {{1, -1, 1}, Color::Blue()},
        {{1, 1, 1}, Color::Red()},
        {{-1, 1, 1}, Color::Yellow()},
    };
    uint16_t indices[36] = {
        1, 5, 2, 2, 5, 6,  // +X
        4, 0, 7, 7, 0, 3,  // -X
        4, 5, 0, 0, 5, 1,  // +Y
        3, 2, 7, 7, 2, 6,  // -Y
        5, 4, 6, 6, 4, 7,  // +Z
        0, 1, 3, 3, 1, 2,  // -Z
    };
  } cube;

  VertexBuffer vertex_buffer;
  {
    auto device_buffer = context->GetResourceAllocator()->CreateBufferWithCopy(
        reinterpret_cast<uint8_t*>(&cube), sizeof(cube));
    vertex_buffer.vertex_buffer = {
        .buffer = device_buffer,
        .range = Range(offsetof(Cube, vertices), sizeof(Cube::vertices))};
    vertex_buffer.index_buffer = {
        .buffer = device_buffer,
        .range = Range(offsetof(Cube, indices), sizeof(Cube::indices))};
    vertex_buffer.vertex_count = 36;
    vertex_buffer.index_type = IndexType::k16bit;
  }

  auto sampler = context->GetSamplerLibrary()->GetSampler({});
  ASSERT_TRUE(sampler);

  Vector3 euler_angles;
  auto host_buffer = HostBuffer::Create(context->GetResourceAllocator());
  SinglePassCallback callback = [&](RenderPass& pass) {
    static Degrees fov_y(60);
    static Scalar distance = 10;

    ImGui::Begin("Controls", nullptr, ImGuiWindowFlags_AlwaysAutoResize);
    ImGui::SliderFloat("Field of view", &fov_y.degrees, 0, 180);
    ImGui::SliderFloat("Camera distance", &distance, 0, 30);
    ImGui::End();

    pass.SetCommandLabel("Perspective Cube");
    pass.SetPipeline(pipeline);
    pass.SetVertexBuffer(vertex_buffer);

    VS::UniformBuffer uniforms;
    Scalar time = GetSecondsElapsed();
    euler_angles = Vector3(0.19 * time, 0.7 * time, 0.43 * time);

    uniforms.mvp =
        Matrix::MakePerspective(fov_y, pass.GetRenderTargetSize(), 0, 10) *
        Matrix::MakeTranslation({0, 0, distance}) *
        Matrix::MakeRotationX(Radians(euler_angles.x)) *
        Matrix::MakeRotationY(Radians(euler_angles.y)) *
        Matrix::MakeRotationZ(Radians(euler_angles.z));
    VS::BindUniformBuffer(pass, host_buffer->EmplaceUniform(uniforms));

    host_buffer->Reset();
    return pass.Draw().ok();
  };
  OpenPlaygroundHere(callback);
}

TEST_P(RendererTest, CanRenderMultiplePrimitives) {
  using VS = BoxFadeVertexShader;
  using FS = BoxFadeFragmentShader;
  auto context = GetContext();
  ASSERT_TRUE(context);
  using BoxPipelineBuilder = PipelineBuilder<VS, FS>;
  auto desc = BoxPipelineBuilder::MakeDefaultPipelineDescriptor(*context);
  ASSERT_TRUE(desc.has_value());
  desc->SetSampleCount(SampleCount::kCount4);
  desc->SetStencilAttachmentDescriptors(std::nullopt);
  auto box_pipeline =
      context->GetPipelineLibrary()->GetPipeline(std::move(desc)).Get();
  ASSERT_TRUE(box_pipeline);

  // Vertex buffer.
  VertexBufferBuilder<VS::PerVertexData> vertex_builder;
  vertex_builder.SetLabel("Box");
  vertex_builder.AddVertices({
      {{100, 100, 0.0}, {0.0, 0.0}},  // 1
      {{800, 100, 0.0}, {1.0, 0.0}},  // 2
      {{800, 800, 0.0}, {1.0, 1.0}},  // 3
      {{100, 100, 0.0}, {0.0, 0.0}},  // 1
      {{800, 800, 0.0}, {1.0, 1.0}},  // 3
      {{100, 800, 0.0}, {0.0, 1.0}},  // 4
  });
  auto vertex_buffer =
      vertex_builder.CreateVertexBuffer(*context->GetResourceAllocator());
  ASSERT_TRUE(vertex_buffer);

  auto bridge = CreateTextureForFixture("bay_bridge.jpg");
  auto boston = CreateTextureForFixture("boston.jpg");
  ASSERT_TRUE(bridge && boston);
  auto sampler = context->GetSamplerLibrary()->GetSampler({});
  ASSERT_TRUE(sampler);

  auto host_buffer = HostBuffer::Create(context->GetResourceAllocator());
  SinglePassCallback callback = [&](RenderPass& pass) {
    for (size_t i = 0; i < 1; i++) {
      for (size_t j = 0; j < 1; j++) {
        pass.SetCommandLabel("Box");
        pass.SetPipeline(box_pipeline);
        pass.SetVertexBuffer(vertex_buffer);

        FS::FrameInfo frame_info;
        frame_info.current_time = GetSecondsElapsed();
        frame_info.cursor_position = GetCursorPosition();
        frame_info.window_size.x = GetWindowSize().width;
        frame_info.window_size.y = GetWindowSize().height;

        FS::BindFrameInfo(pass, host_buffer->EmplaceUniform(frame_info));
        FS::BindContents1(pass, boston, sampler);
        FS::BindContents2(pass, bridge, sampler);

        VS::UniformBuffer uniforms;
        EXPECT_EQ(pass.GetOrthographicTransform(),
                  Matrix::MakeOrthographic(pass.GetRenderTargetSize()));
        uniforms.mvp = pass.GetOrthographicTransform() *
                       Matrix::MakeScale(GetContentScale()) *
                       Matrix::MakeTranslation({i * 50.0f, j * 50.0f, 0.0f});
        VS::BindUniformBuffer(pass, host_buffer->EmplaceUniform(uniforms));
        if (!pass.Draw().ok()) {
          return false;
        }
      }
    }

    host_buffer->Reset();
    return true;
  };
  OpenPlaygroundHere(callback);
}

TEST_P(RendererTest, CanRenderToTexture) {
  using VS = BoxFadeVertexShader;
  using FS = BoxFadeFragmentShader;
  auto context = GetContext();
  ASSERT_TRUE(context);
  using BoxPipelineBuilder = PipelineBuilder<VS, FS>;
  auto pipeline_desc =
      BoxPipelineBuilder::MakeDefaultPipelineDescriptor(*context);
  pipeline_desc->SetSampleCount(SampleCount::kCount1);

  ASSERT_TRUE(pipeline_desc.has_value());
  auto box_pipeline =
      context->GetPipelineLibrary()->GetPipeline(pipeline_desc).Get();
  ASSERT_TRUE(box_pipeline);
  auto host_buffer = HostBuffer::Create(context->GetResourceAllocator());

  VertexBufferBuilder<VS::PerVertexData> vertex_builder;
  vertex_builder.SetLabel("Box");
  vertex_builder.AddVertices({
      {{100, 100, 0.0}, {0.0, 0.0}},  // 1
      {{800, 100, 0.0}, {1.0, 0.0}},  // 2
      {{800, 800, 0.0}, {1.0, 1.0}},  // 3
      {{100, 100, 0.0}, {0.0, 0.0}},  // 1
      {{800, 800, 0.0}, {1.0, 1.0}},  // 3
      {{100, 800, 0.0}, {0.0, 1.0}},  // 4
  });
  auto vertex_buffer =
      vertex_builder.CreateVertexBuffer(*context->GetResourceAllocator());
  ASSERT_TRUE(vertex_buffer);

  auto bridge = CreateTextureForFixture("bay_bridge.jpg");
  auto boston = CreateTextureForFixture("boston.jpg");
  ASSERT_TRUE(bridge && boston);
  auto sampler = context->GetSamplerLibrary()->GetSampler({});
  ASSERT_TRUE(sampler);
  std::shared_ptr<RenderPass> r2t_pass;
  auto cmd_buffer = context->CreateCommandBuffer();
  ASSERT_TRUE(cmd_buffer);
  {
    ColorAttachment color0;
    color0.load_action = LoadAction::kClear;
    color0.store_action = StoreAction::kStore;

    TextureDescriptor texture_descriptor;
    ASSERT_NE(pipeline_desc->GetColorAttachmentDescriptor(0u), nullptr);
    texture_descriptor.format =
        pipeline_desc->GetColorAttachmentDescriptor(0u)->format;
    texture_descriptor.storage_mode = StorageMode::kHostVisible;
    texture_descriptor.size = {400, 400};
    texture_descriptor.mip_count = 1u;
    texture_descriptor.usage =
        static_cast<TextureUsageMask>(TextureUsage::kRenderTarget);

    color0.texture =
        context->GetResourceAllocator()->CreateTexture(texture_descriptor);

    ASSERT_TRUE(color0.IsValid());

    color0.texture->SetLabel("r2t_target");

    StencilAttachment stencil0;
    stencil0.load_action = LoadAction::kClear;
    stencil0.store_action = StoreAction::kDontCare;
    TextureDescriptor stencil_texture_desc;
    stencil_texture_desc.storage_mode = StorageMode::kDeviceTransient;
    stencil_texture_desc.size = texture_descriptor.size;
    stencil_texture_desc.format = PixelFormat::kS8UInt;
    stencil_texture_desc.usage =
        static_cast<TextureUsageMask>(TextureUsage::kRenderTarget);
    stencil0.texture =
        context->GetResourceAllocator()->CreateTexture(stencil_texture_desc);

    RenderTarget r2t_desc;
    r2t_desc.SetColorAttachment(color0, 0u);
    r2t_desc.SetStencilAttachment(stencil0);
    r2t_pass = cmd_buffer->CreateRenderPass(r2t_desc);
    ASSERT_TRUE(r2t_pass && r2t_pass->IsValid());
  }

  r2t_pass->SetCommandLabel("Box");
  r2t_pass->SetPipeline(box_pipeline);
  r2t_pass->SetVertexBuffer(vertex_buffer);

  FS::FrameInfo frame_info;
  frame_info.current_time = GetSecondsElapsed();
  frame_info.cursor_position = GetCursorPosition();
  frame_info.window_size.x = GetWindowSize().width;
  frame_info.window_size.y = GetWindowSize().height;

  FS::BindFrameInfo(*r2t_pass, host_buffer->EmplaceUniform(frame_info));
  FS::BindContents1(*r2t_pass, boston, sampler);
  FS::BindContents2(*r2t_pass, bridge, sampler);

  VS::UniformBuffer uniforms;
  uniforms.mvp = Matrix::MakeOrthographic(ISize{1024, 768}) *
                 Matrix::MakeTranslation({50.0f, 50.0f, 0.0f});
  VS::BindUniformBuffer(*r2t_pass, host_buffer->EmplaceUniform(uniforms));
  ASSERT_TRUE(r2t_pass->Draw().ok());
  ASSERT_TRUE(r2t_pass->EncodeCommands());
}

TEST_P(RendererTest, CanRenderInstanced) {
  if (GetParam() == PlaygroundBackend::kOpenGLES) {
    GTEST_SKIP_("Instancing is not supported on OpenGL.");
  }
  using VS = InstancedDrawVertexShader;
  using FS = InstancedDrawFragmentShader;

  VertexBufferBuilder<VS::PerVertexData> builder;

  ASSERT_EQ(Tessellator::Result::kSuccess,
            Tessellator{}.Tessellate(
                PathBuilder{}
                    .AddRect(Rect::MakeXYWH(10, 10, 100, 100))
                    .TakePath(FillType::kPositive),
                1.0f,
                [&builder](const float* vertices, size_t vertices_count,
                           const uint16_t* indices, size_t indices_count) {
                  for (auto i = 0u; i < vertices_count * 2; i += 2) {
                    VS::PerVertexData data;
                    data.vtx = {vertices[i], vertices[i + 1]};
                    builder.AppendVertex(data);
                  }
                  for (auto i = 0u; i < indices_count; i++) {
                    builder.AppendIndex(indices[i]);
                  }
                  return true;
                }));

  ASSERT_NE(GetContext(), nullptr);
  auto pipeline =
      GetContext()
          ->GetPipelineLibrary()
          ->GetPipeline(PipelineBuilder<VS, FS>::MakeDefaultPipelineDescriptor(
                            *GetContext())
                            ->SetSampleCount(SampleCount::kCount4)
                            .SetStencilAttachmentDescriptors(std::nullopt))

          .Get();
  ASSERT_TRUE(pipeline && pipeline->IsValid());

  static constexpr size_t kInstancesCount = 5u;
  VS::InstanceInfo<kInstancesCount> instances;
  for (size_t i = 0; i < kInstancesCount; i++) {
    instances.colors[i] = Color::Random();
  }

  auto host_buffer = HostBuffer::Create(GetContext()->GetResourceAllocator());
  ASSERT_TRUE(OpenPlaygroundHere([&](RenderPass& pass) -> bool {
    pass.SetPipeline(pipeline);
    pass.SetCommandLabel("InstancedDraw");

    VS::FrameInfo frame_info;
    EXPECT_EQ(pass.GetOrthographicTransform(),
              Matrix::MakeOrthographic(pass.GetRenderTargetSize()));
    frame_info.mvp =
        pass.GetOrthographicTransform() * Matrix::MakeScale(GetContentScale());
    VS::BindFrameInfo(pass, host_buffer->EmplaceUniform(frame_info));
    VS::BindInstanceInfo(pass, host_buffer->EmplaceStorageBuffer(instances));
    pass.SetVertexBuffer(builder.CreateVertexBuffer(*host_buffer));

    pass.SetInstanceCount(kInstancesCount);
    pass.Draw();

    host_buffer->Reset();
    return true;
  }));
}

TEST_P(RendererTest, CanBlitTextureToTexture) {
  if (GetBackend() == PlaygroundBackend::kOpenGLES) {
    GTEST_SKIP() << "Mipmap test shader not supported on GLES.";
  }
  auto context = GetContext();
  ASSERT_TRUE(context);

  using VS = MipmapsVertexShader;
  using FS = MipmapsFragmentShader;
  auto desc = PipelineBuilder<VS, FS>::MakeDefaultPipelineDescriptor(*context);
  ASSERT_TRUE(desc.has_value());
  desc->SetSampleCount(SampleCount::kCount4);
  desc->SetStencilAttachmentDescriptors(std::nullopt);
  auto mipmaps_pipeline =
      context->GetPipelineLibrary()->GetPipeline(std::move(desc)).Get();
  ASSERT_TRUE(mipmaps_pipeline);

  TextureDescriptor texture_desc;
  texture_desc.storage_mode = StorageMode::kHostVisible;
  texture_desc.format = PixelFormat::kR8G8B8A8UNormInt;
  texture_desc.size = {800, 600};
  texture_desc.mip_count = 1u;
  texture_desc.usage =
      static_cast<TextureUsageMask>(TextureUsage::kRenderTarget) |
      static_cast<TextureUsageMask>(TextureUsage::kShaderRead);
  auto texture = context->GetResourceAllocator()->CreateTexture(texture_desc);
  ASSERT_TRUE(texture);

  auto bridge = CreateTextureForFixture("bay_bridge.jpg");
  auto boston = CreateTextureForFixture("boston.jpg");
  ASSERT_TRUE(bridge && boston);
  auto sampler = context->GetSamplerLibrary()->GetSampler({});
  ASSERT_TRUE(sampler);

  // Vertex buffer.
  VertexBufferBuilder<VS::PerVertexData> vertex_builder;
  vertex_builder.SetLabel("Box");
  auto size = Point(boston->GetSize());
  vertex_builder.AddVertices({
      {{0, 0}, {0.0, 0.0}},            // 1
      {{size.x, 0}, {1.0, 0.0}},       // 2
      {{size.x, size.y}, {1.0, 1.0}},  // 3
      {{0, 0}, {0.0, 0.0}},            // 1
      {{size.x, size.y}, {1.0, 1.0}},  // 3
      {{0, size.y}, {0.0, 1.0}},       // 4
  });
  auto vertex_buffer =
      vertex_builder.CreateVertexBuffer(*context->GetResourceAllocator());
  ASSERT_TRUE(vertex_buffer);

  auto host_buffer = HostBuffer::Create(context->GetResourceAllocator());
  Renderer::RenderCallback callback = [&](RenderTarget& render_target) {
    auto buffer = context->CreateCommandBuffer();
    if (!buffer) {
      return false;
    }
    buffer->SetLabel("Playground Command Buffer");

    {
      auto pass = buffer->CreateBlitPass();
      if (!pass) {
        return false;
      }
      pass->SetLabel("Playground Blit Pass");

      if (render_target.GetColorAttachments().empty()) {
        return false;
      }

      // Blit `bridge` to the top left corner of the texture.
      pass->AddCopy(bridge, texture);

      if (!pass->EncodeCommands(context->GetResourceAllocator())) {
        return false;
      }
    }

    {
      auto pass = buffer->CreateRenderPass(render_target);
      if (!pass) {
        return false;
      }
      pass->SetLabel("Playground Render Pass");
      {
        pass->SetCommandLabel("Image");
        pass->SetPipeline(mipmaps_pipeline);
        pass->SetVertexBuffer(vertex_buffer);

        VS::FrameInfo frame_info;
        EXPECT_EQ(pass->GetOrthographicTransform(),
                  Matrix::MakeOrthographic(pass->GetRenderTargetSize()));
        frame_info.mvp = pass->GetOrthographicTransform() *
                         Matrix::MakeScale(GetContentScale());
        VS::BindFrameInfo(*pass, host_buffer->EmplaceUniform(frame_info));

        FS::FragInfo frag_info;
        frag_info.lod = 0;
        FS::BindFragInfo(*pass, host_buffer->EmplaceUniform(frag_info));

        auto sampler = context->GetSamplerLibrary()->GetSampler({});
        FS::BindTex(*pass, texture, sampler);

        pass->Draw();
      }
      pass->EncodeCommands();
    }

    if (!buffer->SubmitCommands()) {
      return false;
    }
    host_buffer->Reset();
    return true;
  };
  OpenPlaygroundHere(callback);
}

TEST_P(RendererTest, CanBlitTextureToBuffer) {
  if (GetBackend() == PlaygroundBackend::kOpenGLES) {
    GTEST_SKIP() << "Mipmap test shader not supported on GLES.";
  }
  auto context = GetContext();
  ASSERT_TRUE(context);

  using VS = MipmapsVertexShader;
  using FS = MipmapsFragmentShader;
  auto desc = PipelineBuilder<VS, FS>::MakeDefaultPipelineDescriptor(*context);
  ASSERT_TRUE(desc.has_value());
  desc->SetSampleCount(SampleCount::kCount4);
  desc->SetStencilAttachmentDescriptors(std::nullopt);
  auto mipmaps_pipeline =
      context->GetPipelineLibrary()->GetPipeline(std::move(desc)).Get();
  ASSERT_TRUE(mipmaps_pipeline);

  auto bridge = CreateTextureForFixture("bay_bridge.jpg");
  auto boston = CreateTextureForFixture("boston.jpg");
  ASSERT_TRUE(bridge && boston);
  auto sampler = context->GetSamplerLibrary()->GetSampler({});
  ASSERT_TRUE(sampler);

  TextureDescriptor texture_desc;
  texture_desc.storage_mode = StorageMode::kHostVisible;
  texture_desc.format = PixelFormat::kR8G8B8A8UNormInt;
  texture_desc.size = bridge->GetTextureDescriptor().size;
  texture_desc.mip_count = 1u;
  texture_desc.usage =
      static_cast<TextureUsageMask>(TextureUsage::kRenderTarget) |
      static_cast<TextureUsageMask>(TextureUsage::kShaderWrite) |
      static_cast<TextureUsageMask>(TextureUsage::kShaderRead);
  DeviceBufferDescriptor device_buffer_desc;
  device_buffer_desc.storage_mode = StorageMode::kHostVisible;
  device_buffer_desc.size =
      bridge->GetTextureDescriptor().GetByteSizeOfBaseMipLevel();
  auto device_buffer =
      context->GetResourceAllocator()->CreateBuffer(device_buffer_desc);

  // Vertex buffer.
  VertexBufferBuilder<VS::PerVertexData> vertex_builder;
  vertex_builder.SetLabel("Box");
  auto size = Point(boston->GetSize());
  vertex_builder.AddVertices({
      {{0, 0}, {0.0, 0.0}},            // 1
      {{size.x, 0}, {1.0, 0.0}},       // 2
      {{size.x, size.y}, {1.0, 1.0}},  // 3
      {{0, 0}, {0.0, 0.0}},            // 1
      {{size.x, size.y}, {1.0, 1.0}},  // 3
      {{0, size.y}, {0.0, 1.0}},       // 4
  });
  auto vertex_buffer =
      vertex_builder.CreateVertexBuffer(*context->GetResourceAllocator());
  ASSERT_TRUE(vertex_buffer);

  auto host_buffer = HostBuffer::Create(context->GetResourceAllocator());
  Renderer::RenderCallback callback = [&](RenderTarget& render_target) {
    {
      auto buffer = context->CreateCommandBuffer();
      if (!buffer) {
        return false;
      }
      buffer->SetLabel("Playground Command Buffer");
      auto pass = buffer->CreateBlitPass();
      if (!pass) {
        return false;
      }
      pass->SetLabel("Playground Blit Pass");

      if (render_target.GetColorAttachments().empty()) {
        return false;
      }

      // Blit `bridge` to the top left corner of the texture.
      pass->AddCopy(bridge, device_buffer);

      pass->EncodeCommands(context->GetResourceAllocator());

      if (!buffer->SubmitCommands()) {
        return false;
      }
    }

    {
      auto buffer = context->CreateCommandBuffer();
      if (!buffer) {
        return false;
      }
      buffer->SetLabel("Playground Command Buffer");

      auto pass = buffer->CreateRenderPass(render_target);
      if (!pass) {
        return false;
      }
      pass->SetLabel("Playground Render Pass");
      {
        pass->SetCommandLabel("Image");
        pass->SetPipeline(mipmaps_pipeline);
        pass->SetVertexBuffer(vertex_buffer);

        VS::FrameInfo frame_info;
        EXPECT_EQ(pass->GetOrthographicTransform(),
                  Matrix::MakeOrthographic(pass->GetRenderTargetSize()));
        frame_info.mvp = pass->GetOrthographicTransform() *
                         Matrix::MakeScale(GetContentScale());
        VS::BindFrameInfo(*pass, host_buffer->EmplaceUniform(frame_info));

        FS::FragInfo frag_info;
        frag_info.lod = 0;
        FS::BindFragInfo(*pass, host_buffer->EmplaceUniform(frag_info));

        auto sampler = context->GetSamplerLibrary()->GetSampler({});
        auto buffer_view = DeviceBuffer::AsBufferView(device_buffer);
        auto texture =
            context->GetResourceAllocator()->CreateTexture(texture_desc);
        if (!texture->SetContents(device_buffer->OnGetContents(),
                                  buffer_view.range.length)) {
          VALIDATION_LOG << "Could not upload texture to device memory";
          return false;
        }
        FS::BindTex(*pass, texture, sampler);

        pass->Draw().ok();
      }
      pass->EncodeCommands();
      if (!buffer->SubmitCommands()) {
        return false;
      }
    }
    host_buffer->Reset();
    return true;
  };
  OpenPlaygroundHere(callback);
}

TEST_P(RendererTest, CanGenerateMipmaps) {
  if (GetBackend() == PlaygroundBackend::kOpenGLES) {
    GTEST_SKIP() << "Mipmap test shader not supported on GLES.";
  }
  auto context = GetContext();
  ASSERT_TRUE(context);

  using VS = MipmapsVertexShader;
  using FS = MipmapsFragmentShader;
  auto desc = PipelineBuilder<VS, FS>::MakeDefaultPipelineDescriptor(*context);
  ASSERT_TRUE(desc.has_value());
  desc->SetSampleCount(SampleCount::kCount4);
  desc->SetStencilAttachmentDescriptors(std::nullopt);
  auto mipmaps_pipeline =
      context->GetPipelineLibrary()->GetPipeline(std::move(desc)).Get();
  ASSERT_TRUE(mipmaps_pipeline);

  auto boston = CreateTextureForFixture("boston.jpg", true);
  ASSERT_TRUE(boston);

  // Vertex buffer.
  VertexBufferBuilder<VS::PerVertexData> vertex_builder;
  vertex_builder.SetLabel("Box");
  auto size = Point(boston->GetSize());
  vertex_builder.AddVertices({
      {{0, 0}, {0.0, 0.0}},            // 1
      {{size.x, 0}, {1.0, 0.0}},       // 2
      {{size.x, size.y}, {1.0, 1.0}},  // 3
      {{0, 0}, {0.0, 0.0}},            // 1
      {{size.x, size.y}, {1.0, 1.0}},  // 3
      {{0, size.y}, {0.0, 1.0}},       // 4
  });
  auto vertex_buffer =
      vertex_builder.CreateVertexBuffer(*context->GetResourceAllocator());
  ASSERT_TRUE(vertex_buffer);

  bool first_frame = true;
  auto host_buffer = HostBuffer::Create(context->GetResourceAllocator());
  Renderer::RenderCallback callback = [&](RenderTarget& render_target) {
    const char* mip_filter_names[] = {"Nearest", "Linear"};
    const MipFilter mip_filters[] = {MipFilter::kNearest, MipFilter::kLinear};
    const char* min_filter_names[] = {"Nearest", "Linear"};
    const MinMagFilter min_filters[] = {MinMagFilter::kNearest,
                                        MinMagFilter::kLinear};

    // UI state.
    static int selected_mip_filter = 1;
    static int selected_min_filter = 0;
    static float lod = 4.5;

    ImGui::Begin("Controls", nullptr, ImGuiWindowFlags_AlwaysAutoResize);
    ImGui::Combo("Mip filter", &selected_mip_filter, mip_filter_names,
                 sizeof(mip_filter_names) / sizeof(char*));
    ImGui::Combo("Min filter", &selected_min_filter, min_filter_names,
                 sizeof(min_filter_names) / sizeof(char*));
    ImGui::SliderFloat("LOD", &lod, 0, boston->GetMipCount() - 1);
    ImGui::End();

    auto buffer = context->CreateCommandBuffer();
    if (!buffer) {
      return false;
    }
    buffer->SetLabel("Playground Command Buffer");

    if (first_frame) {
      auto pass = buffer->CreateBlitPass();
      if (!pass) {
        return false;
      }
      pass->SetLabel("Playground Blit Pass");

      pass->GenerateMipmap(boston, "Boston Mipmap");

      pass->EncodeCommands(context->GetResourceAllocator());
    }

    first_frame = false;

    {
      auto pass = buffer->CreateRenderPass(render_target);
      if (!pass) {
        return false;
      }
      pass->SetLabel("Playground Render Pass");
      {
        pass->SetCommandLabel("Image LOD");
        pass->SetPipeline(mipmaps_pipeline);
        pass->SetVertexBuffer(vertex_buffer);

        VS::FrameInfo frame_info;
        EXPECT_EQ(pass->GetOrthographicTransform(),
                  Matrix::MakeOrthographic(pass->GetRenderTargetSize()));
        frame_info.mvp = pass->GetOrthographicTransform() *
                         Matrix::MakeScale(GetContentScale());
        VS::BindFrameInfo(*pass, host_buffer->EmplaceUniform(frame_info));

        FS::FragInfo frag_info;
        frag_info.lod = lod;
        FS::BindFragInfo(*pass, host_buffer->EmplaceUniform(frag_info));

        SamplerDescriptor sampler_desc;
        sampler_desc.mip_filter = mip_filters[selected_mip_filter];
        sampler_desc.min_filter = min_filters[selected_min_filter];
        auto sampler = context->GetSamplerLibrary()->GetSampler(sampler_desc);
        FS::BindTex(*pass, boston, sampler);

        pass->Draw();
      }
      pass->EncodeCommands();
    }

    if (!buffer->SubmitCommands()) {
      return false;
    }
    host_buffer->Reset();
    return true;
  };
  OpenPlaygroundHere(callback);
}

TEST_P(RendererTest, TheImpeller) {
  using VS = ImpellerVertexShader;
  using FS = ImpellerFragmentShader;

  auto context = GetContext();
  auto pipeline_descriptor =
      PipelineBuilder<VS, FS>::MakeDefaultPipelineDescriptor(*context);
  ASSERT_TRUE(pipeline_descriptor.has_value());
  pipeline_descriptor->SetSampleCount(SampleCount::kCount4);
  pipeline_descriptor->SetStencilAttachmentDescriptors(std::nullopt);
  auto pipeline =
      context->GetPipelineLibrary()->GetPipeline(pipeline_descriptor).Get();
  ASSERT_TRUE(pipeline && pipeline->IsValid());

  auto blue_noise = CreateTextureForFixture("blue_noise.png");
  SamplerDescriptor noise_sampler_desc;
  noise_sampler_desc.width_address_mode = SamplerAddressMode::kRepeat;
  noise_sampler_desc.height_address_mode = SamplerAddressMode::kRepeat;
  auto noise_sampler =
      context->GetSamplerLibrary()->GetSampler(noise_sampler_desc);

  auto cube_map = CreateTextureCubeForFixture(
      {"table_mountain_px.png", "table_mountain_nx.png",
       "table_mountain_py.png", "table_mountain_ny.png",
       "table_mountain_pz.png", "table_mountain_nz.png"});
  auto cube_map_sampler = context->GetSamplerLibrary()->GetSampler({});
  auto host_buffer = HostBuffer::Create(context->GetResourceAllocator());

  SinglePassCallback callback = [&](RenderPass& pass) {
    auto size = pass.GetRenderTargetSize();

    pass.SetPipeline(pipeline);
    pass.SetCommandLabel("Impeller SDF scene");
    VertexBufferBuilder<VS::PerVertexData> builder;
    builder.AddVertices({{Point()},
                         {Point(0, size.height)},
                         {Point(size.width, 0)},
                         {Point(size.width, 0)},
                         {Point(0, size.height)},
                         {Point(size.width, size.height)}});
    pass.SetVertexBuffer(builder.CreateVertexBuffer(*host_buffer));

    VS::FrameInfo frame_info;
    EXPECT_EQ(pass.GetOrthographicTransform(), Matrix::MakeOrthographic(size));
    frame_info.mvp = pass.GetOrthographicTransform();
    VS::BindFrameInfo(pass, host_buffer->EmplaceUniform(frame_info));

    FS::FragInfo fs_uniform;
    fs_uniform.texture_size = Point(size);
    fs_uniform.time = GetSecondsElapsed();
    FS::BindFragInfo(pass, host_buffer->EmplaceUniform(fs_uniform));
    FS::BindBlueNoise(pass, blue_noise, noise_sampler);
    FS::BindCubeMap(pass, cube_map, cube_map_sampler);

    pass.Draw().ok();
    host_buffer->Reset();
    return true;
  };
  OpenPlaygroundHere(callback);
}

TEST_P(RendererTest, ArrayUniforms) {
  using VS = ArrayVertexShader;
  using FS = ArrayFragmentShader;

  auto context = GetContext();
  auto pipeline_descriptor =
      PipelineBuilder<VS, FS>::MakeDefaultPipelineDescriptor(*context);
  ASSERT_TRUE(pipeline_descriptor.has_value());
  pipeline_descriptor->SetSampleCount(SampleCount::kCount4);
  pipeline_descriptor->SetStencilAttachmentDescriptors(std::nullopt);
  auto pipeline =
      context->GetPipelineLibrary()->GetPipeline(pipeline_descriptor).Get();
  ASSERT_TRUE(pipeline && pipeline->IsValid());

  auto host_buffer = HostBuffer::Create(context->GetResourceAllocator());
  SinglePassCallback callback = [&](RenderPass& pass) {
    auto size = pass.GetRenderTargetSize();

    pass.SetPipeline(pipeline);
    pass.SetCommandLabel("Google Dots");
    VertexBufferBuilder<VS::PerVertexData> builder;
    builder.AddVertices({{Point()},
                         {Point(0, size.height)},
                         {Point(size.width, 0)},
                         {Point(size.width, 0)},
                         {Point(0, size.height)},
                         {Point(size.width, size.height)}});
    pass.SetVertexBuffer(builder.CreateVertexBuffer(*host_buffer));

    VS::FrameInfo frame_info;
    EXPECT_EQ(pass.GetOrthographicTransform(), Matrix::MakeOrthographic(size));
    frame_info.mvp =
        pass.GetOrthographicTransform() * Matrix::MakeScale(GetContentScale());
    VS::BindFrameInfo(pass, host_buffer->EmplaceUniform(frame_info));

    auto time = GetSecondsElapsed();
    auto y_pos = [&time](float x) {
      return 400 + 10 * std::cos(time * 5 + x / 6);
    };

    FS::FragInfo fs_uniform = {
        .circle_positions = {Point(430, y_pos(0)), Point(480, y_pos(1)),
                             Point(530, y_pos(2)), Point(580, y_pos(3))},
        .colors = {Color::MakeRGBA8(66, 133, 244, 255),
                   Color::MakeRGBA8(219, 68, 55, 255),
                   Color::MakeRGBA8(244, 180, 0, 255),
                   Color::MakeRGBA8(15, 157, 88, 255)},
    };
    FS::BindFragInfo(pass, host_buffer->EmplaceUniform(fs_uniform));

    pass.Draw();
    host_buffer->Reset();
    return true;
  };
  OpenPlaygroundHere(callback);
}

TEST_P(RendererTest, InactiveUniforms) {
  using VS = InactiveUniformsVertexShader;
  using FS = InactiveUniformsFragmentShader;

  auto context = GetContext();
  auto pipeline_descriptor =
      PipelineBuilder<VS, FS>::MakeDefaultPipelineDescriptor(*context);
  ASSERT_TRUE(pipeline_descriptor.has_value());
  pipeline_descriptor->SetSampleCount(SampleCount::kCount4);
  pipeline_descriptor->SetStencilAttachmentDescriptors(std::nullopt);
  auto pipeline =
      context->GetPipelineLibrary()->GetPipeline(pipeline_descriptor).Get();
  ASSERT_TRUE(pipeline && pipeline->IsValid());

  auto host_buffer = HostBuffer::Create(context->GetResourceAllocator());
  SinglePassCallback callback = [&](RenderPass& pass) {
    auto size = pass.GetRenderTargetSize();

    pass.SetPipeline(pipeline);
    pass.SetCommandLabel("Inactive Uniform");

    VertexBufferBuilder<VS::PerVertexData> builder;
    builder.AddVertices({{Point()},
                         {Point(0, size.height)},
                         {Point(size.width, 0)},
                         {Point(size.width, 0)},
                         {Point(0, size.height)},
                         {Point(size.width, size.height)}});
    pass.SetVertexBuffer(builder.CreateVertexBuffer(*host_buffer));

    VS::FrameInfo frame_info;
    EXPECT_EQ(pass.GetOrthographicTransform(), Matrix::MakeOrthographic(size));
    frame_info.mvp =
        pass.GetOrthographicTransform() * Matrix::MakeScale(GetContentScale());
    VS::BindFrameInfo(pass, host_buffer->EmplaceUniform(frame_info));

    FS::FragInfo fs_uniform = {.unused_color = Color::Red(),
                               .color = Color::Green()};
    FS::BindFragInfo(pass, host_buffer->EmplaceUniform(fs_uniform));

    pass.Draw().ok();
    host_buffer->Reset();
    return true;
  };
  OpenPlaygroundHere(callback);
}

TEST_P(RendererTest, CanCreateCPUBackedTexture) {
  if (GetParam() == PlaygroundBackend::kOpenGLES) {
    GTEST_SKIP_("CPU backed textures are not supported on OpenGLES.");
  }

  auto context = GetContext();
  auto allocator = context->GetResourceAllocator();
  size_t dimension = 2;

  do {
    ISize size(dimension, dimension);
    TextureDescriptor texture_descriptor;
    texture_descriptor.storage_mode = StorageMode::kHostVisible;
    texture_descriptor.format = PixelFormat::kR8G8B8A8UNormInt;
    texture_descriptor.size = size;
    auto row_bytes =
        std::max(static_cast<uint16_t>(size.width * 4),
                 allocator->MinimumBytesPerRow(texture_descriptor.format));
    auto buffer_size = size.height * row_bytes;

    DeviceBufferDescriptor buffer_descriptor;
    buffer_descriptor.storage_mode = StorageMode::kHostVisible;
    buffer_descriptor.size = buffer_size;

    auto buffer = allocator->CreateBuffer(buffer_descriptor);

    ASSERT_TRUE(buffer);

    auto texture = buffer->AsTexture(*allocator, texture_descriptor, row_bytes);

    ASSERT_TRUE(texture);
    ASSERT_TRUE(texture->IsValid());

    dimension *= 2;
  } while (dimension <= 8192);
}

TEST_P(RendererTest, DefaultIndexSize) {
  using VS = BoxFadeVertexShader;

  // Default to 16bit index buffer size, as this is a reasonable default and
  // supported on all backends without extensions.
  VertexBufferBuilder<VS::PerVertexData> vertex_builder;
  vertex_builder.AppendIndex(0u);
  ASSERT_EQ(vertex_builder.GetIndexType(), IndexType::k16bit);
}

TEST_P(RendererTest, DefaultIndexBehavior) {
  using VS = BoxFadeVertexShader;

  // Do not create any index buffer if no indices were provided.
  VertexBufferBuilder<VS::PerVertexData> vertex_builder;
  ASSERT_EQ(vertex_builder.GetIndexType(), IndexType::kNone);
}

TEST_P(RendererTest, VertexBufferBuilder) {
  // Does not create index buffer if one is provided.
  using VS = BoxFadeVertexShader;
  VertexBufferBuilder<VS::PerVertexData> vertex_builder;
  vertex_builder.SetLabel("Box");
  vertex_builder.AddVertices({
      {{100, 100, 0.0}, {0.0, 0.0}},  // 1
      {{800, 100, 0.0}, {1.0, 0.0}},  // 2
      {{800, 800, 0.0}, {1.0, 1.0}},  // 3
      {{100, 800, 0.0}, {0.0, 1.0}},  // 4
  });
  vertex_builder.AppendIndex(0);
  vertex_builder.AppendIndex(1);
  vertex_builder.AppendIndex(2);
  vertex_builder.AppendIndex(1);
  vertex_builder.AppendIndex(2);
  vertex_builder.AppendIndex(3);

  ASSERT_EQ(vertex_builder.GetIndexCount(), 6u);
  ASSERT_EQ(vertex_builder.GetVertexCount(), 4u);
}

class CompareFunctionUIData {
 public:
  CompareFunctionUIData() {
    labels_.push_back("Never");
    functions_.push_back(CompareFunction::kNever);
    labels_.push_back("Always");
    functions_.push_back(CompareFunction::kAlways);
    labels_.push_back("Less");
    functions_.push_back(CompareFunction::kLess);
    labels_.push_back("Equal");
    functions_.push_back(CompareFunction::kEqual);
    labels_.push_back("LessEqual");
    functions_.push_back(CompareFunction::kLessEqual);
    labels_.push_back("Greater");
    functions_.push_back(CompareFunction::kGreater);
    labels_.push_back("NotEqual");
    functions_.push_back(CompareFunction::kNotEqual);
    labels_.push_back("GreaterEqual");
    functions_.push_back(CompareFunction::kGreaterEqual);
    assert(labels_.size() == functions_.size());
  }

  const char* const* labels() const { return &labels_[0]; }

  int size() const { return labels_.size(); }

  int IndexOf(CompareFunction func) const {
    for (size_t i = 0; i < functions_.size(); i++) {
      if (functions_[i] == func) {
        return i;
      }
    }
    FML_UNREACHABLE();
    return -1;
  }

  CompareFunction FunctionOf(int index) const { return functions_[index]; }

 private:
  std::vector<const char*> labels_;
  std::vector<CompareFunction> functions_;
};

static const CompareFunctionUIData& CompareFunctionUI() {
  static CompareFunctionUIData data;
  return data;
}

TEST_P(RendererTest, StencilMask) {
  using VS = BoxFadeVertexShader;
  using FS = BoxFadeFragmentShader;
  auto context = GetContext();
  ASSERT_TRUE(context);
  using BoxFadePipelineBuilder = PipelineBuilder<VS, FS>;
  auto desc = BoxFadePipelineBuilder::MakeDefaultPipelineDescriptor(*context);
  ASSERT_TRUE(desc.has_value());

  // Vertex buffer.
  VertexBufferBuilder<VS::PerVertexData> vertex_builder;
  vertex_builder.SetLabel("Box");
  vertex_builder.AddVertices({
      {{100, 100, 0.0}, {0.0, 0.0}},  // 1
      {{800, 100, 0.0}, {1.0, 0.0}},  // 2
      {{800, 800, 0.0}, {1.0, 1.0}},  // 3
      {{100, 100, 0.0}, {0.0, 0.0}},  // 1
      {{800, 800, 0.0}, {1.0, 1.0}},  // 3
      {{100, 800, 0.0}, {0.0, 1.0}},  // 4
  });
  auto vertex_buffer =
      vertex_builder.CreateVertexBuffer(*context->GetResourceAllocator());
  ASSERT_TRUE(vertex_buffer);

  desc->SetSampleCount(SampleCount::kCount4);
  desc->SetStencilAttachmentDescriptors(std::nullopt);

  auto bridge = CreateTextureForFixture("bay_bridge.jpg");
  auto boston = CreateTextureForFixture("boston.jpg");
  ASSERT_TRUE(bridge && boston);
  auto sampler = context->GetSamplerLibrary()->GetSampler({});
  ASSERT_TRUE(sampler);

  static bool mirror = false;
  static int stencil_reference_write = 0xFF;
  static int stencil_reference_read = 0x1;
  std::vector<uint8_t> stencil_contents;
  static int last_stencil_contents_reference_value = 0;
  static int current_front_compare =
      CompareFunctionUI().IndexOf(CompareFunction::kLessEqual);
  static int current_back_compare =
      CompareFunctionUI().IndexOf(CompareFunction::kLessEqual);

  auto host_buffer = HostBuffer::Create(context->GetResourceAllocator());
  Renderer::RenderCallback callback = [&](RenderTarget& render_target) {
    auto buffer = context->CreateCommandBuffer();
    if (!buffer) {
      return false;
    }
    buffer->SetLabel("Playground Command Buffer");

    {
      // Configure the stencil attachment for the test.
      RenderTarget::AttachmentConfig stencil_config;
      stencil_config.load_action = LoadAction::kLoad;
      stencil_config.store_action = StoreAction::kDontCare;
      stencil_config.storage_mode = StorageMode::kHostVisible;
      auto render_target_allocator =
          RenderTargetAllocator(context->GetResourceAllocator());
      render_target.SetupDepthStencilAttachments(
          *context, render_target_allocator,
          render_target.GetRenderTargetSize(), true, "stencil", stencil_config);
      // Fill the stencil buffer with an checkerboard pattern.
      const auto target_width = render_target.GetRenderTargetSize().width;
      const auto target_height = render_target.GetRenderTargetSize().height;
      const size_t target_size = target_width * target_height;
      if (stencil_contents.size() != target_size ||
          last_stencil_contents_reference_value != stencil_reference_write) {
        stencil_contents.resize(target_size);
        last_stencil_contents_reference_value = stencil_reference_write;
        for (int y = 0; y < target_height; y++) {
          for (int x = 0; x < target_width; x++) {
            const auto index = y * target_width + x;
            const auto kCheckSize = 64;
            const auto value =
                (((y / kCheckSize) + (x / kCheckSize)) % 2 == 0) *
                stencil_reference_write;
            stencil_contents[index] = value;
          }
        }
      }
      if (!render_target.GetStencilAttachment()->texture->SetContents(
              stencil_contents.data(), stencil_contents.size(), 0, false)) {
        VALIDATION_LOG << "Could not upload stencil contents to device memory";
        return false;
      }
      auto pass = buffer->CreateRenderPass(render_target);
      if (!pass) {
        return false;
      }
      pass->SetLabel("Stencil Buffer");
      ImGui::Begin("Controls", nullptr, ImGuiWindowFlags_AlwaysAutoResize);
      ImGui::SliderInt("Stencil Write Value", &stencil_reference_write, 0,
                       0xFF);
      ImGui::SliderInt("Stencil Compare Value", &stencil_reference_read, 0,
                       0xFF);
      ImGui::Checkbox("Back face mode", &mirror);
      ImGui::ListBox("Front face compare function", &current_front_compare,
                     CompareFunctionUI().labels(), CompareFunctionUI().size());
      ImGui::ListBox("Back face compare function", &current_back_compare,
                     CompareFunctionUI().labels(), CompareFunctionUI().size());
      ImGui::End();

      StencilAttachmentDescriptor front;
      front.stencil_compare =
          CompareFunctionUI().FunctionOf(current_front_compare);
      StencilAttachmentDescriptor back;
      back.stencil_compare =
          CompareFunctionUI().FunctionOf(current_back_compare);
      desc->SetStencilAttachmentDescriptors(front, back);
      auto pipeline = context->GetPipelineLibrary()->GetPipeline(desc).Get();

      assert(pipeline && pipeline->IsValid());

      pass->SetCommandLabel("Box");
      pass->SetPipeline(pipeline);
      pass->SetStencilReference(stencil_reference_read);
      pass->SetVertexBuffer(vertex_buffer);

      VS::UniformBuffer uniforms;
      EXPECT_EQ(pass->GetOrthographicTransform(),
                Matrix::MakeOrthographic(pass->GetRenderTargetSize()));
      uniforms.mvp = pass->GetOrthographicTransform() *
                     Matrix::MakeScale(GetContentScale());
      if (mirror) {
        uniforms.mvp = Matrix::MakeScale(Vector2(-1, 1)) * uniforms.mvp;
      }
      VS::BindUniformBuffer(*pass, host_buffer->EmplaceUniform(uniforms));

      FS::FrameInfo frame_info;
      frame_info.current_time = GetSecondsElapsed();
      frame_info.cursor_position = GetCursorPosition();
      frame_info.window_size.x = GetWindowSize().width;
      frame_info.window_size.y = GetWindowSize().height;

      FS::BindFrameInfo(*pass, host_buffer->EmplaceUniform(frame_info));
      FS::BindContents1(*pass, boston, sampler);
      FS::BindContents2(*pass, bridge, sampler);
      if (!pass->Draw().ok()) {
        return false;
      }
      pass->EncodeCommands();
    }

    if (!buffer->SubmitCommands()) {
      return false;
    }
    host_buffer->Reset();
    return true;
  };
  OpenPlaygroundHere(callback);
}

TEST_P(RendererTest, CanPreAllocateCommands) {
  auto context = GetContext();
  auto cmd_buffer = context->CreateCommandBuffer();
  auto render_target_cache = std::make_shared<RenderTargetAllocator>(
      GetContext()->GetResourceAllocator());

  auto render_target = RenderTarget::CreateOffscreen(
      *context, *render_target_cache, {100, 100}, /*mip_count=*/1);
  auto render_pass = cmd_buffer->CreateRenderPass(render_target);

  render_pass->ReserveCommands(100u);

  EXPECT_EQ(render_pass->GetCommands().capacity(), 100u);
}

TEST_P(RendererTest, CanLookupRenderTargetProperties) {
  auto context = GetContext();
  auto cmd_buffer = context->CreateCommandBuffer();
  auto render_target_cache = std::make_shared<RenderTargetAllocator>(
      GetContext()->GetResourceAllocator());

  auto render_target = RenderTarget::CreateOffscreen(
      *context, *render_target_cache, {100, 100}, /*mip_count=*/1);
  auto render_pass = cmd_buffer->CreateRenderPass(render_target);

  EXPECT_EQ(render_pass->GetSampleCount(), render_target.GetSampleCount());
  EXPECT_EQ(render_pass->GetRenderTargetPixelFormat(),
            render_target.GetRenderTargetPixelFormat());
  EXPECT_EQ(render_pass->HasStencilAttachment(),
            render_target.GetStencilAttachment().has_value());
  EXPECT_EQ(render_pass->GetRenderTargetSize(),
            render_target.GetRenderTargetSize());
}

TEST_P(RendererTest,
       RenderTargetCreateOffscreenMSAASetsDefaultDepthStencilFormat) {
  auto context = GetContext();
  auto render_target_cache = std::make_shared<RenderTargetAllocator>(
      GetContext()->GetResourceAllocator());

  RenderTarget render_target = RenderTarget::CreateOffscreenMSAA(
      *context, *render_target_cache, {100, 100}, /*mip_count=*/1);
  EXPECT_EQ(render_target.GetDepthAttachment()
                ->texture->GetTextureDescriptor()
                .format,
            GetContext()->GetCapabilities()->GetDefaultDepthStencilFormat());
}

}  // namespace testing
}  // namespace impeller

// NOLINTEND(bugprone-unchecked-optional-access)
