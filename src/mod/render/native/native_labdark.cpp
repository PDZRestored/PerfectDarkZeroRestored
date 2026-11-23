// Native renderer: the Laboratory's local darkness (native_labdark.h).
//
// Rare's light pre-pass draws the hemisphere ambient (and the sun) with one full-screen draw per view, pixel shader
// a95c4b61c177a282: texture slot 2 is the scene depth, and with c0 = (1 / P00, 1 / P11, 1 - P22, P32) the shader
// rebuilds the view-space position of each pixel as z = c0.w / (depth + c0.z), xy = ndc.xy * c0.xy * z (pixels
// with depth + c0.z <= 0 hold no geometry and stay unlit). This pass repeats that for the same depth texture and
// constant, and multiplies the light buffer (blend ZERO / SRC_COLOR) by the ambient kept there: the product over
// the lamps that are out (view space, from lab_dark's hook on that pass) of lerp(1, strength, weight x reach), reach
// 1 inside the lamp's radius falling smoothly to 0 at 1.5 x the radius, never below the strength
// (lab_dark_patches.cpp KeepAt is the same rule for the light measure). The lamps drawn after it add their light as
// before; nothing else of the frame is touched.
//
// Root signature: [0] CBV b0 (the constants below, frame upload memory)   [1] SRV table t0 (the depth texture)
// static sampler s0 (point, clamp). One PSO per light buffer format, built at the first ambient draw while local
// darkness is wanted (the Laboratory with the switch on), so the first lamp going out compiles nothing.
#include "native_labdark.h"

#include <cstring>
#include <unordered_map>

#include <rex/logging.h>

#include "../../lab_dark_patches.h"
#include "native_textures.h"

namespace pdz::native::labdark
{
  namespace
  {
    const char kSource[] = R"HLSL(
cbuffer Constants : register(b0)
{
  float4 c0;        // Rare's light pass constant: 1 / P00, 1 / P11, 1 - P22, P32
  float4 viewport;  // x, y, 1 / width, 1 / height (host pixels)
  float4 params;    // strength, falloff (x the radius), lamp count, 0
  float4 lamp[32];  // view-space position, radius
  float4 weight[32]; // x: how far the lamp has gone out (0..1)
};
Texture2D<float4> t0 : register(t0);
SamplerState s0 : register(s0);

float4 vs_main(uint id : SV_VertexID) : SV_Position
{
  float2 p = float2((id << 1) & 2, id & 2);
  return float4(p * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
}

float4 ps_main(float4 pos : SV_Position) : SV_Target
{
  float2 uv = (pos.xy - viewport.xy) * viewport.zw;
  float d = t0.SampleLevel(s0, uv, 0.0).x + c0.z;
  if (!(d > 0.0))
  {
    return float4(1.0, 1.0, 1.0, 1.0); // no geometry: Rare's pass leaves it unlit too
  }
  float z = c0.w / d;
  float2 ndc = float2(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0);
  float3 p = float3(ndc * c0.xy * z, z);
  float keep = 1.0;
  uint count = uint(params.z);
  for (uint i = 0u; i < count; ++i)
  {
    float3 v = p - lamp[i].xyz;
    float r = lamp[i].w;
    float reach = 1.0 - smoothstep(r, r * params.y, length(v));
    keep *= 1.0 + (params.x - 1.0) * (weight[i].x * reach);
  }
  keep = max(keep, params.x);
  return float4(keep, keep, keep, keep);
}
)HLSL";

    struct Constants
    {
      float c0[4];
      float viewport[4];
      float params[4];
      float lamp[lab_dark::kVisualMaxLamps][4];
      float weight[lab_dark::kVisualMaxLamps][4];
    };
    static_assert(lab_dark::kVisualMaxLamps == 32, "the shader's arrays hold 32 lamps");
    static_assert(sizeof(Constants) == (3 + 64) * 16);

    struct State
    {
      bool init_tried = false;
      bool ok = false;
      ID3D12RootSignature *root = nullptr;
      ID3DBlob *vs = nullptr;
      ID3DBlob *ps = nullptr;
      std::unordered_map<uint32_t, ID3D12PipelineState *> psos; // by the light buffer's DXGI_FORMAT
      uint64_t passes = 0;
      bool logged_first = false;
    };
    State g_state;

    ID3DBlob *Compile(NativeDevice &dev, const char *entry, const char *target)
    {
      ID3DBlob *code = nullptr;
      ID3DBlob *errors = nullptr;
      const HRESULT hr = dev.d3d_compile()(kSource, sizeof(kSource) - 1, "pdz_native_labdark", nullptr, nullptr, entry,
                                           target, 0, 0, &code, &errors);
      if (FAILED(hr))
      {
        REXLOG_ERROR("pdz_native: lab dark D3DCompile({}, {}) failed: 0x{:08X} {}", entry, target, uint32_t(hr),
                     errors ? static_cast<const char *>(errors->GetBufferPointer()) : "");
        code = nullptr;
      }
      if (errors)
      {
        errors->Release();
      }
      return code;
    }

    bool Init(NativeDevice &dev)
    {
      State &s = g_state;
      if (s.init_tried)
      {
        return s.ok;
      }
      s.init_tried = true;
      if (!dev.d3d_compile() || !dev.serialize_root_signature())
      {
        return false;
      }
      D3D12_DESCRIPTOR_RANGE srv_range{};
      srv_range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
      srv_range.NumDescriptors = 1;
      srv_range.BaseShaderRegister = 0;
      srv_range.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
      D3D12_ROOT_PARAMETER params[2]{};
      params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
      params[0].Descriptor.ShaderRegister = 0;
      params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
      params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
      params[1].DescriptorTable.NumDescriptorRanges = 1;
      params[1].DescriptorTable.pDescriptorRanges = &srv_range;
      params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
      D3D12_STATIC_SAMPLER_DESC sampler{};
      sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
      sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
      sampler.MaxLOD = D3D12_FLOAT32_MAX;
      sampler.ShaderRegister = 0;
      sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
      D3D12_ROOT_SIGNATURE_DESC rs{};
      rs.NumParameters = 2;
      rs.pParameters = params;
      rs.NumStaticSamplers = 1;
      rs.pStaticSamplers = &sampler;
      rs.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;
      ID3DBlob *blob = nullptr;
      ID3DBlob *errors = nullptr;
      HRESULT hr = dev.serialize_root_signature()(&rs, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &errors);
      if (FAILED(hr))
      {
        REXLOG_ERROR("pdz_native: lab dark root signature serialisation failed: 0x{:08X} {}", uint32_t(hr),
                     errors ? static_cast<const char *>(errors->GetBufferPointer()) : "");
        if (errors)
        {
          errors->Release();
        }
        return false;
      }
      hr = dev.device()->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&s.root));
      blob->Release();
      if (FAILED(hr))
      {
        REXLOG_ERROR("pdz_native: lab dark CreateRootSignature failed: 0x{:08X}", uint32_t(hr));
        return false;
      }
      s.vs = Compile(dev, "vs_main", "vs_5_0");
      s.ps = Compile(dev, "ps_main", "ps_5_0");
      s.ok = s.vs && s.ps;
      if (s.ok)
      {
        REXLOG_INFO("pdz_native: lab dark pass ready (local darkness around lamps that are out)");
      }
      return s.ok;
    }

    ID3D12PipelineState *Pso(NativeDevice &dev, DXGI_FORMAT format)
    {
      State &s = g_state;
      auto it = s.psos.find(uint32_t(format));
      if (it != s.psos.end())
      {
        return it->second;
      }
      D3D12_GRAPHICS_PIPELINE_STATE_DESC d{};
      d.pRootSignature = s.root;
      d.VS.pShaderBytecode = s.vs->GetBufferPointer();
      d.VS.BytecodeLength = s.vs->GetBufferSize();
      d.PS.pShaderBytecode = s.ps->GetBufferPointer();
      d.PS.BytecodeLength = s.ps->GetBufferSize();
      // target = target x the ambient kept (colour and the specular alpha alike).
      D3D12_RENDER_TARGET_BLEND_DESC &b = d.BlendState.RenderTarget[0];
      b.BlendEnable = TRUE;
      b.SrcBlend = D3D12_BLEND_ZERO;
      b.DestBlend = D3D12_BLEND_SRC_COLOR;
      b.BlendOp = D3D12_BLEND_OP_ADD;
      b.SrcBlendAlpha = D3D12_BLEND_ZERO;
      b.DestBlendAlpha = D3D12_BLEND_SRC_ALPHA;
      b.BlendOpAlpha = D3D12_BLEND_OP_ADD;
      b.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
      d.SampleMask = UINT_MAX;
      d.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
      d.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
      d.RasterizerState.DepthClipEnable = TRUE;
      d.DepthStencilState.DepthEnable = FALSE;
      d.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
      d.NumRenderTargets = 1;
      d.RTVFormats[0] = format;
      d.SampleDesc.Count = 1;
      ID3D12PipelineState *pso = nullptr;
      const HRESULT hr = dev.device()->CreateGraphicsPipelineState(&d, IID_PPV_ARGS(&pso));
      if (FAILED(hr))
      {
        REXLOG_ERROR("pdz_native: lab dark PSO for format {} failed: 0x{:08X}", uint32_t(format), uint32_t(hr));
        pso = nullptr;
      }
      s.psos.emplace(uint32_t(format), pso); // null cached too: one failure log per format
      return pso;
    }

    float LoadBe(const uint8_t *p)
    {
      const uint32_t v = (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3];
      float f;
      std::memcpy(&f, &v, 4);
      return f;
    }
  } // namespace

  bool AfterAmbientDraw(NativeDevice &dev, const shadow::DrawRecord &r, const uint8_t *ps_constants,
                        HostSurface *target, const D3D12_VIEWPORT &viewport, const D3D12_RECT &scissor,
                        uint32_t draws_before)
  {
    if (!lab_dark::VisualArmed() || !target || !target->view.ptr || !ps_constants)
    {
      return false;
    }
    // The pipeline first (the level's first frames), then the lamps of this draw's view, if any are out.
    if (!Init(dev))
    {
      return false;
    }
    ID3D12PipelineState *pso = Pso(dev, target->view_format);
    lab_dark::VisualFrame frame;
    if (!pso || !lab_dark::VisualFrameFor(r.sequence, frame) || frame.count == 0)
    {
      return false;
    }
    if (!(viewport.Width > 0.0f) || !(viewport.Height > 0.0f))
    {
      return false;
    }
    // The depth texture the ambient pass samples (its texture slot 2), as that draw bound it.
    const FetchDecoded fetch = DecodeFetch(r.fetch_be + 2 * shadow::dev::kFetchConstantBytes);
    const TextureView depth = Textures::Get().Lookup(fetch);
    if (!depth.texture || !depth.srv.ptr)
    {
      return false;
    }
    UploadSlice cb = dev.Upload(sizeof(Constants), 256);
    Descriptor table = dev.AllocSrv(1);
    if (!cb.cpu || !table.cpu.ptr)
    {
      return false;
    }
    dev.CopySrv(table.cpu, depth.srv);

    Constants c{}; // built in cached memory, copied up whole
    for (int i = 0; i < 4; ++i)
    {
      c.c0[i] = LoadBe(ps_constants + 4 * i);
    }
    c.viewport[0] = viewport.TopLeftX;
    c.viewport[1] = viewport.TopLeftY;
    c.viewport[2] = 1.0f / viewport.Width;
    c.viewport[3] = 1.0f / viewport.Height;
    c.params[0] = frame.strength;
    c.params[1] = lab_dark::kVisualFalloff;
    c.params[2] = float(frame.count);
    for (uint32_t i = 0; i < frame.count; ++i)
    {
      std::memcpy(c.lamp[i], frame.lamp[i], sizeof(c.lamp[i]));
      c.weight[i][0] = frame.weight[i];
    }
    std::memcpy(cb.cpu, &c, sizeof(c));

    RecList *list = dev.list();
    dev.ListStateChanged(); // the draw path re-binds its own root signature / PSO after this pass
    list->SetPipelineState(pso);
    list->SetGraphicsRootSignature(g_state.root);
    list->SetGraphicsRootConstantBufferView(0, cb.gpu);
    list->SetGraphicsRootDescriptorTable(1, table.gpu);
    list->OMSetRenderTargets(1, &target->view, FALSE, nullptr);
    Surfaces::Get().ForgetBoundTargets(); // the next BindTargets sets the draw path's targets again
    list->RSSetViewports(1, &viewport);
    list->RSSetScissorRects(1, &scissor);
    list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    list->DrawInstanced(3, 1, 0, 0);
    ++g_state.passes;
    if (!g_state.logged_first)
    {
      g_state.logged_first = true;
      REXLOG_INFO("pdz_native: lab dark pass: first use (draw #{}, {} draw(s) into the light buffer before the "
                  "ambient draw, {} lamp(s), strength {:.2f}, light buffer {}x{} format {}, depth texture {:08X} {}x{}, "
                  "c0 {:.4f} {:.4f} {:.4f} {:.2f})",
                  r.sequence, draws_before, frame.count, frame.strength, target->host_width, target->host_height,
                  uint32_t(target->view_format), fetch.base_address, depth.texture->width, depth.texture->height, c.c0[0],
                  c.c0[1], c.c0[2], c.c0[3]);
    }
    return true;
  }

  uint64_t PassCount() { return g_state.passes; }
} // namespace pdz::native::labdark
