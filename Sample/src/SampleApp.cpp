#include "SampleApp.h"

// imgui
#include "imgui.h"
#include "backends/imgui_impl_win32.h"
#include "backends/imgui_impl_dx12.h"

// stl
#include <sstream>

// DirectX libraries
#include <DirectXMath.h>
#include <CommonStates.h>
#include <DirectXHelpers.h>

// Framework
#include "FileUtil.h"
#include "Logger.h"
#include "Mesh.h"
#include "Material.h"
#include "InlineUtil.h"
#include "RootSignature.h"
#include "ScopedTimer.h"

// Sponzaは、ライティングをIBLでなくハードコーディングで配置したライトを使うなど特別な処理を多くやっているので分岐する
#define RENDER_SPONZA true

using namespace DirectX::SimpleMath;

namespace
{
	static constexpr float CAMERA_FOV_Y_DEGREE = 37.5f;
	static constexpr float CAMERA_NEAR = 0.1f;
	static constexpr float CAMERA_FAR = 100.0f;

	static constexpr uint32_t DIRECTIONAL_LIGHT_SHADOW_MAP_SIZE = 2048; // TODO:ModelViewerを参考にした
	static constexpr uint32_t SPOT_LIGHT_SHADOW_MAP_SIZE = 512; // TODO:ModelViewerを参考にした

	static constexpr uint32_t SKY_TRANSMITTANCE_LUT_WIDTH = 256; // UEを参考にした
	static constexpr uint32_t SKY_TRANSMITTANCE_LUT_HEIGHT = 64; // UEを参考にした

	static constexpr uint32_t HCB_MAX_NUM_OUTPUT_MIP = 5; // UEを参考にした
	static constexpr uint32_t HZB_MAX_NUM_OUTPUT_MIP = 4; // UEを参考にした

	static constexpr uint32_t VOLUMETRIC_FOG_GRID_PIXEL_SIZE = 8; // UEを参考にした
	static constexpr uint32_t VOLUMETRIC_FOG_GRID_SIZE_Z = 128; // UEを参考にした

	static constexpr uint32_t TEMPORAL_AA_SAMPLES = 8; // UEを参考にした
	static constexpr uint32_t TEMPORAL_AA_NUM_PLUS_SAMPLE = 5; // UEを参考にした
	static constexpr uint32_t TEMPORAL_AA_NUM_PLUS_VECTOR4 = (TEMPORAL_AA_NUM_PLUS_SAMPLE + 3) / 4; // 切り上げ

	static constexpr float TEMPORAL_AA_NEIGHBORHOOD_SAMPLE_OFFSETS[TEMPORAL_AA_NUM_PLUS_SAMPLE][2] =
	{
		{0, -1}, // top
		{-1, 0}, // left
		{0, 0}, // center
		{1, 0}, // right
		{0, 1}, // bottom
	};

	// シェーダ側のマクロ定数と同じ値である必要がある
	// 定数バッファ内配列のfloat4へのパッキングルールがあるので4の倍数である必要がある
	static constexpr uint32_t GAUSSIAN_FILTER_SAMPLES = 32;

	// カメラのフラスタムカリングにかからないようnear以上far以下になるように注意が必要
	static constexpr float SKY_BOX_HALF_EXTENT = 50.0f;

	enum COLOR_SPACE_TYPE
	{
		COLOR_SPACE_BT709,
		COLOR_SPACE_BT2100_PQ,
	};

	enum TONEMAP_TYPE
	{
		TONEMAP_NONE = 0,
		TONEMAP_REINHARD,
		TONEMAP_GT,
		TONEMAP_KHRONOS_PBR_NEUTRAL,
	};

	enum DEBUG_VIEW_RENDER_TARGET
	{
		DEBUG_VIEW_NONE = 0,
		DEBUG_VIEW_DEPTH,
		DEBUG_VIEW_NORMAL,
		DEBUG_VIEW_VELOCITY,
		DEBUG_VIEW_SSAO_FULL_RES,
		DEBUG_VIEW_SSAO_HALF_RES,
		DEBUG_VIEW_SSGI,
	};

	struct alignas(256) CbMesh
	{
		Matrix World;
	};

	struct alignas(256) CbTransform
	{
		Matrix ViewProj;
		Matrix ModelToDirLightShadowMap;
		Matrix WorldToSpotLight1ShadowMap;
		Matrix WorldToSpotLight2ShadowMap;
		Matrix WorldToSpotLight3ShadowMap;
	};

	struct alignas(256) CbDirectionalLight
	{
		Vector3 LightColor;
		float LightIntensity;
		Vector3 LightForward;
		float Padding[1];
		Vector2 ShadowMapSize; // x is pixel size, y is texel size on UV.
		float Padding2[2];
	};

	struct alignas(256) CbPointLight
	{
		Vector3 LightPosition;
		float LightInvSqrRadius;
		Vector3 LightColor;
		float LightIntensity;
	};

	struct alignas(256) CbSpotLight
	{
		Vector3 LightPosition;
		float LightInvSqrRadius;
		Vector3 LightColor;
		float LightIntensity;
		Vector3 LightForward;
		float LightAngleScale;
		float LightAngleOffset;
		int LightType;
		Vector2 ShadowMapSize; // x is pixel size, y is texel size on UV.
	};

	struct alignas(256) CbCamera
	{
		Vector3 CameraPosition;
		float Padding[1];
	};

	struct alignas(256) CbMaterial
	{
		Vector3 BaseColorFactor;
		float MetallicFactor;
		float RoughnessFactor;
		Vector3 EmissiveFactor;
		float AlphaCutoff;
		int bExistEmissiveTex;
		int bExistAOTex;
		float Padding[1];
	};

	// TODO: Width/Heightは多くのSSシェーダで定数バッファにしているので共通化したい
	struct alignas(256) CbHZB
	{
		int DstMip0Width;
		int DstMip0Height;
		float HeightScale;
		int NumOutputMip;
	};

	struct alignas(256) CbObjectVelocity
	{
		Matrix CurWVPWithJitter;
		Matrix CurWVPNoJitter;
		Matrix PrevWVPNoJitter;
	};

	struct alignas(256) CbCameraVelocity
	{
		Matrix ClipToPrevClip;
	};

	// TODO: Width/Heightは多くのSSシェーダで定数バッファにしているので共通化したい
	struct alignas(256) CbSSAOSetup
	{
		int Width;
		int Height;
		float Near;
		float Far;
	};

	struct alignas(256) CbSSAO
	{
		Matrix ViewMatrix;
		Matrix InvProjMatrix;
		int Width;
		int Height;
		Vector2 RandomationSize;
		Vector2 TemporalOffset;
		float Near;
		float Far;
		float InvTanHalfFov;
		int bHalfRes;
		float Contrast;
		float Intensity;
	};

	// TODO: Width/Heightは多くのSSシェーダで定数バッファにしているので共通化したい
	struct alignas(256) CbSSGI
	{
		Matrix ProjMatrix;
		Matrix VRotPMatrix;
		Matrix InvVRotPMatrix;
		float Near;
		float Far;
		int Width;
		int Height;
		int FrameSampleIndex;
		float Intensity;
		float Padding[2];
	};

	// TODO: Width/Heightは多くのSSシェーダで定数バッファにしているので共通化したい
	struct alignas(256) CbSSGI_Denoise
	{
		int Width;
		int Height;
		float Padding[2];
	};

	struct alignas(256) CbSSR
	{
		Matrix ProjMatrix;
		Matrix VRotPMatrix;
		Matrix InvVRotPMatrix;
		float Near;
		float Far;
		int Width;
		int Height;
		int FrameSampleIndex;
		float Intensity;
		int bDebugViewSSR;
		float Padding;
	};

	struct alignas(256) CbVolumetricFog
	{
		Matrix InvVRotPMatrix;
		Matrix ClipToPrevClip;
		int GridSizeX;
		int GridSizeY;
		int GridSizeZ;
		float Near;
		float Far;
		Vector3 FrameJitterOffsetValue;
		float DirectionalLightScatteringIntensity;
		float SpotLightScatteringIntensity;
		float Padding[2];
	};

	struct alignas(256) CbTemporalAA
	{
		Vector4 PlusWeights[TEMPORAL_AA_NUM_PLUS_VECTOR4];
		int Width;
		int Height;
		int bEnableTemporalAA;
		float Padding;
	};

	// TODO: Width/Heightは多くのSSシェーダで定数バッファにしているので共通化したい
	struct alignas(256) CbMotionBlur
	{
		int Width;
		int Height;
		float Scale;
		float Padding[1];
	};

	struct alignas(256) CbTonemap
	{
		int Type;
		int ColorSpace;
		float BaseLuminance;
		float MaxLuminance;
		float BloomIntensity;
		float Padding[3];
	};

	// TODO: Width/Heightは多くのSSシェーダで定数バッファにしているので共通化したい
	struct alignas(256) CbFXAA
	{
		int Width;
		int Height;
		int bEnableFXAA;
		int bEnableFXAAHighQuality;
	};

	struct alignas(256) CbDownsample
	{
		int SrcWidth;
		int SrcHeight;
		float Padding[2];
	};

	struct alignas(256) CbFilter
	{
		Vector4 SampleOffsets[GAUSSIAN_FILTER_SAMPLES / 2];
		Vector4 SampleWeights[GAUSSIAN_FILTER_SAMPLES]; // RGBAそれぞれでウェイトをもてるようにしている
		int NumSample;
		int bEnableAdditveTexture;
		float Padding[2]; // TODO:GAUSSIAN_FILTER_SAMPLESが4の倍数なのを前提としている
	};

	struct alignas(256) CbSampleTexture
	{
		int bOnlyRedChannel;
		float Contrast;
		float Scale;
		float Bias;
	};

	struct alignas(256) CbIBL
	{
		float TextureSize;
		float MipCount;
		float LightIntensity;
		float Padding0;
	};

	uint32_t DivideAndRoundUp(uint32_t dividend, uint32_t divisor)
	{
		return (dividend + divisor - 1) / divisor;
	}

	UINT16 inline GetChromaticityCoord(double value)
	{
		return UINT16(value * 50000);
	}

	CbPointLight ComputePointLight(const Vector3& pos, float radius, const Vector3& color, float intensity)
	{
		CbPointLight result;
		result.LightPosition = pos;
		result.LightInvSqrRadius = 1.0f / (radius * radius);
		result.LightColor = color;
		result.LightIntensity = intensity;
		return result;
	}

	CbSpotLight ComputeSpotLight
	(
		int lightType,
		const Vector3& dir,
		const Vector3& pos,
		float radius,
		const Vector3& color,
		float intensity,
		float innerAngle,
		float outerAngle,
		uint32_t shadowMapSize
	)
	{
		float cosInnerAngle = cosf(innerAngle);
		float cosOuterAngle = cosf(outerAngle);

		CbSpotLight result;
		result.LightPosition = pos;
		result.LightInvSqrRadius = 1.0f / (radius * radius);
		result.LightColor = color;
		result.LightIntensity = intensity;
		Vector3 normalizedDir = dir;
		normalizedDir.Normalize();
		result.LightForward = normalizedDir;
		// 0除算が発生しないよう、cosInnerとcosOuterの差は下限を0.001に設定しておく
		result.LightAngleScale = 1.0f / DirectX::XMMax(0.001f, (cosInnerAngle - cosOuterAngle));
		result.LightAngleOffset = -cosOuterAngle * result.LightAngleScale;
		result.ShadowMapSize = Vector2((float)shadowMapSize, 1.0f / shadowMapSize);
		result.LightType = lightType;
		return result;
	}

	Matrix ComputeSpotLightViewProj
	(
		const Vector3& dir,
		const Vector3& pos,
		float radius,
		float outerAngle
	)
	{
		Vector3 normalizedDir = dir;
		normalizedDir.Normalize();
		const Matrix& spotLightShadowView = Matrix::CreateLookAt(pos, pos + normalizedDir * radius, Vector3::UnitY);
		const Matrix& spotLightShadowProj = Matrix::CreatePerspectiveFieldOfView(outerAngle * 2.0f, 1.0f, radius * 0.05f, radius * 1.0f); // パラメータはModelViewerを参考にした
		return spotLightShadowView * spotLightShadowProj; // 行ベクトル形式の順序で乗算するのがXMMatrixMultiply()
	}

	//TODO: UEはより最適化された実装だが、ここでは可読性を重視する
	uint32_t RoundDownToPowerOfTwo(uint32_t value)
	{
		assert(value > 0);

		// 1の値になっている最大の桁。一番右は0とするので1の場合は0。
		uint32_t maxDigit = 0;

		for (; value > 1; value >>= 1)
		{
			maxDigit++;
		}

		assert(maxDigit < 32);
		return 1 << maxDigit;
	}

	// @param x assumed to be in this range: -1..1
	// @return 0..255
	uint8_t Quantize8SignedByte(float x)
	{
		// -1..1 -> 0..1
		float y = x * 0.5f + 0.5f;

		uint32_t ret = (uint32_t)(y * 255.0f + 0.5f);
		return (uint8_t)ret;
	}

	// [ Halton 1964, "Radical-inverse quasi-random point sequence" ]
	float Halton(uint32_t index, uint32_t base)
	{
		float result = 0.0f;
		float invBase = 1.0f / float(base);
		float fraction = invBase;

		while (index > 0)
		{
			result += float(index % base) * fraction;
			index /= base;
			fraction *= invBase;
		}

		return result;
	}

	Vector3 VolumetricFogTemporalRandom(uint32_t frameNumber)
	{
		return Vector3(Halton(frameNumber & 1023, 2), Halton(frameNumber & 1023, 3), Halton(frameNumber & 1023, 5));
	}

	// Refered UE's SceneVisibility.cpp
	void CalculateTemporalJitterPixels(uint32_t temporalAASampleIndex, float& sampleX, float& sampleY)
	{
		float u1 = Halton(temporalAASampleIndex + 1, 2);
		float u2 = Halton(temporalAASampleIndex + 1, 3);

		// Generates samples in normal distribution
		// exp( x^2 / Sigma^2 )

		// Scale distribution to set non-unit variance
		// Variance = Sigma^2
		float sigma = 0.47f;

		// Window to [-0.5, 0.5] output
		// Without windowing we could generate samples far away on the infinite tails.
		float outWindow = 0.5f;
		float inWindow = expf(-0.5f * sqrt(outWindow * sigma));

		// Box-Muller transform
		float theta = 2.0f * DirectX::XM_PI * u2;
		float r = sigma * sqrt(-2.0f * log((1.0f - u1) * inWindow + u1));

		sampleX = r * cos(theta);
		sampleY = r * sin(theta);
	}

	uint32_t Compute1DGaussianFilterKernel(uint32_t kernelRadius, float outOffsets[GAUSSIAN_FILTER_SAMPLES], float outWeights[GAUSSIAN_FILTER_SAMPLES])
	{
		int32_t clampedKernelRadius = kernelRadius;
		if (clampedKernelRadius > GAUSSIAN_FILTER_SAMPLES - 1)
		{
			clampedKernelRadius = GAUSSIAN_FILTER_SAMPLES - 1;
		}

		const float CLIP_SCALE_BY_KERNEL_RADIUS_WINDOW = -16.7f;

		uint32_t sampleCount = 0;
		float weightSum = 0.0f;
		for (int32_t i = -clampedKernelRadius; i <= clampedKernelRadius; i += 2)
		{
			float dx = fabsf((float)i);
			float invSigma = 1.0f / clampedKernelRadius;
			float gaussian = expf(CLIP_SCALE_BY_KERNEL_RADIUS_WINDOW * dx * dx * invSigma * invSigma);

			outOffsets[sampleCount] = i * 0.5f; // ループが2ずつインクリメントしているので
			outWeights[sampleCount] = gaussian;
			weightSum += gaussian;

			sampleCount++;
		}

		for (uint32_t i = 0; i < sampleCount; i++)
		{
			outWeights[i] /= weightSum;
		}

		return sampleCount;
	}
}

SampleApp::SampleApp(uint32_t width, uint32_t height)
: App(width, height, DXGI_FORMAT_R10G10B10A2_UNORM)
, m_TonemapType(TONEMAP_GT)
, m_ColorSpace(COLOR_SPACE_BT709)
, m_BaseLuminance(100.0f)
, m_MaxLuminance(100.0f)
, m_PrevCursorX(0)
, m_PrevCursorY(0)
, m_RotateAngle(0.0f)
, m_DirLightShadowMapViewport()
, m_DirLightShadowMapScissor()
, m_FrameNumber(0)
, m_TemporalAASampleIndex(0)
, m_PrevWorldForMovable(Matrix::Identity)
, m_PrevViewProjNoJitter(Matrix::Identity)
, m_directionalLightIntensity(10.0f)
, m_pointLightIntensity(100.0f)
, m_spotLightIntensity(1000.0f)
, m_enableVelocity(true)
, m_SSAO_Contrast(1.0f)
, m_SSAO_Intensity(0.5f)
, m_SSGI_Intensity(0.0f)
, m_debugViewContrast(1.0f)
, m_SSR_Intensity(1.0f)
, m_debugViewSSR(false)
, m_BloomIntensity(1.0f)
, m_motionBlurScale(1.0f)
, m_moveFlowerVase(false)
, m_directionalLightVolumetricFogScatteringIntensity(1000.0f)
, m_spotLightVolumetricFogScatteringIntensity(10000.0f)
, m_enableTemporalAA(true)
, m_enableFXAA(false)
, m_enableFXAA_HighQuality(true)
, m_debugViewRenderTarget(DEBUG_VIEW_NONE)
{
}

SampleApp::~SampleApp()
{
}

bool SampleApp::OnInit(HWND hWnd)
{
	m_Camera.Reset();

	// imgui初期化
	{
		// https://github.com/ocornut/imgui/wiki/Getting-Started#example-if-you-are-using-raw-win32-api--directx12を参考にしている
		IMGUI_CHECKVERSION();
		ImGui::CreateContext();
		ImGuiIO& io = ImGui::GetIO();
		io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls
		io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;      // Enable Gamepad Controls

		if (!ImGui_ImplWin32_Init(hWnd))
		{
			ELOG("Error : ImGui_ImplWin32_Init() Failed.");
			return false;
		}

		DescriptorHandle* pHandleSRV = m_pPool[POOL_TYPE_RES]->AllocHandle();
		if (pHandleSRV == nullptr)
		{
			ELOG("Error : DescriptorPool::AllocHandle() Failed.");
			return false;
		}

		if (!ImGui_ImplDX12_Init(m_pDevice.Get(), 1, m_BackBufferFormat, m_pPool[POOL_TYPE_RES]->GetHeap(), pHandleSRV->HandleCPU, pHandleSRV->HandleGPU))
		{
			ELOG("Error : ImGui_ImplDX12_Init() Failed.");
			return false;
		}
	}

	// テクスチャがないがドローコールで必要とされたときのためのダミーテクスチャを用意する
	{
		ID3D12GraphicsCommandList* pCmd = m_CommandList.Reset();

		// 法線マップのデフォルトテクスチャとして使えるように(0.5,0.5,1)にする
		// TODO:いずれ種類ごとに別のデフォルトテクスチャが必要になったら対応する
		uint32_t normalBlue = 0x00FF8080;
		if (!m_DummyTexture.InitFromData(m_pDevice.Get(), pCmd, m_pPool[POOL_TYPE_RES], 1, 1, DXGI_FORMAT_R8G8B8A8_UNORM, &normalBlue))
		{
			ELOG("Error : Texture::Init() Failed.");
			return false;
		}

		pCmd->Close();
		ID3D12CommandList* pLists[] = {pCmd};
		m_pQueue->ExecuteCommandLists(1, pLists);
		m_Fence.Wait(m_pQueue.Get(), INFINITE);
	}

	// メッシュをロード
	{
		std::wstring path;
		if (RENDER_SPONZA)
		{
			if (!SearchFilePath(L"res/SponzaKhronos/glTF/Sponza.gltf", path))
			{
				ELOG("Error : File Not Found.");
				return false;
			}
		}
		else
		{
			//if (!SearchFilePath(L"res/MetalRoughSpheres/glTF/MetalRoughSpheres.gltf", path))
			if (!SearchFilePath(L"res/DamagedHelmet/glTF/DamagedHelmet.gltf", path))
			{
				ELOG("Error : File Not Found.");
				return false;
			}
		}

		std::vector<ResMesh> resMesh;
		std::vector<ResMaterial> resMaterial;
		if (!LoadMesh(path.c_str(), resMesh, resMaterial))
		{
			ELOG("Error : Load Mesh Failed. filepath = %ls", path.c_str());
			return false;
		}

		std::vector<Mesh*> pMeshes;
		pMeshes.reserve(resMesh.size());

		for (size_t i = 0; i < resMesh.size(); i++)
		{
			Mesh* mesh = new (std::nothrow) Mesh();
			if (mesh == nullptr)
			{
				ELOG("Error : Out of memory.");
				return false;
			}

			if (!mesh->Init(m_pDevice.Get(), m_pPool[POOL_TYPE_RES], resMesh[i], sizeof(CbMesh)))
			{
				ELOG("Error : Mesh Initialize Failed.");
				delete mesh;
				return false;
			}

			for (uint32_t frameIndex = 0; frameIndex  < FRAME_COUNT; frameIndex++)
			{
				CbMesh* ptr = mesh->GetBufferPtr<CbMesh>(frameIndex);
				ptr->World = Matrix::Identity;
			}

			pMeshes.push_back(mesh);
		}

		pMeshes.shrink_to_fit();

		//TODO: Velocityのテストとして2番のメッシュをMovableとする
		if (RENDER_SPONZA)
		{
			if (pMeshes.size() > 2)
			{
				pMeshes[2]->SetMobility(Mobility::Movable);
			}
		}

		Model* model = new (std::nothrow) Model();
		if (model == nullptr)
		{
			ELOG("Error : Out of memory.");
			return false;
		}
		model->SetMeshes(pMeshes);

		std::vector<Material*> pMaterials;
		pMaterials.reserve(resMaterial.size());

		for (size_t i = 0; i < resMaterial.size(); i++)
		{
			Material* material = new (std::nothrow) Material();
			if (material == nullptr)
			{
				ELOG("Error : Out of memory.");
				return false;
			}

			if (!material->Init(m_pDevice.Get(), m_pPool[POOL_TYPE_RES], sizeof(CbMaterial), &m_DummyTexture))
			{
				ELOG("Error : Material Initialize Failed.");
				delete material;
				return false;
			}

			pMaterials.push_back(material);
		}

		pMaterials.shrink_to_fit();

		// 全マテリアルの全テクスチャでバッチ処理を走らせるために、SetTexture()を
		// Material::Init()の中で行っていない
		DirectX::ResourceUploadBatch batch(m_pDevice.Get());
		batch.Begin();

		const std::wstring& dir = GetDirectoryPath(path.c_str());
		for (size_t i = 0; i < resMaterial.size(); i++)
		{
			Material* pMaterial = pMaterials[i];
			const ResMaterial& resMat = resMaterial[i];

			pMaterial->SetTexture(Material::TEXTURE_USAGE_BASE_COLOR, dir + resMat.BaseColorMap, batch);
			pMaterial->SetTexture(Material::TEXTURE_USAGE_METALLIC_ROUGHNESS, dir + resMat.MetallicRoughnessMap, batch);
			pMaterial->SetTexture(Material::TEXTURE_USAGE_NORMAL, dir + resMat.NormalMap, batch);
			pMaterial->SetTexture(Material::TEXTURE_USAGE_EMISSIVE, dir + resMat.EmissiveMap, batch);
			pMaterial->SetTexture(Material::TEXTURE_USAGE_AMBIENT_OCCLUSION, dir + resMat.AmbientOcclusionMap, batch);

			pMaterial->SetDoubleSided(resMat.DoubleSided);

			CbMaterial* ptr = pMaterial->GetBufferPtr<CbMaterial>();
			ptr->BaseColorFactor = resMat.BaseColor;
			ptr->MetallicFactor = resMat.MetallicFactor;
			ptr->RoughnessFactor = resMat.RoughnessFactor;
			ptr->EmissiveFactor = resMat.EmissiveFactor;
			ptr->AlphaCutoff = resMat.AlphaCutoff;
			ptr->bExistEmissiveTex = resMat.EmissiveMap.empty() ? 0 : 1;
			ptr->bExistAOTex = resMat.AmbientOcclusionMap.empty() ? 0 : 1;
		}

		std::future<void> future = batch.End(m_pQueue.Get());
		future.wait();

		model->SetMaterials(pMaterials);

		m_pModels.push_back(model);
	}

	// 2つ目のメッシュをロード
	if (RENDER_SPONZA)
	{
		std::wstring path;
		//if (!SearchFilePath(L"res/MetalRoughSpheres/glTF/MetalRoughSpheres.gltf", path))
		if (!SearchFilePath(L"res/DamagedHelmet/glTF/DamagedHelmet.gltf", path))
		{
			ELOG("Error : File Not Found.");
			return false;
		}

		std::vector<ResMesh> resMesh;
		std::vector<ResMaterial> resMaterial;
		if (!LoadMesh(path.c_str(), resMesh, resMaterial))
		{
			ELOG("Error : Load Mesh Failed. filepath = %ls", path.c_str());
			return false;
		}

		std::vector<Mesh*> pMeshes;
		pMeshes.reserve(resMesh.size());

		for (size_t i = 0; i < resMesh.size(); i++)
		{
			Mesh* mesh = new (std::nothrow) Mesh();
			if (mesh == nullptr)
			{
				ELOG("Error : Out of memory.");
				return false;
			}

			if (!mesh->Init(m_pDevice.Get(), m_pPool[POOL_TYPE_RES], resMesh[i], sizeof(CbMesh)))
			{
				ELOG("Error : Mesh Initialize Failed.");
				delete mesh;
				return false;
			}

			//const Matrix& worldMat = Matrix::CreateScale(0.25f) * Matrix::CreateRotationY(DirectX::XM_PI * 0.5f) * Matrix::CreateTranslation(0, 1.5f, 0.0f);
			const Matrix& worldMat = Matrix::CreateRotationY(DirectX::XM_PI * 0.5f) * Matrix::CreateTranslation(0, 1.0f, 0.0f);

			for (uint32_t frameIndex = 0; frameIndex  < FRAME_COUNT; frameIndex++)
			{
				CbMesh* ptr = mesh->GetBufferPtr<CbMesh>(frameIndex);
				ptr->World = worldMat;
			}

			pMeshes.push_back(mesh);
		}

		pMeshes.shrink_to_fit();

		Model* model = new (std::nothrow) Model();
		if (model == nullptr)
		{
			ELOG("Error : Out of memory.");
			return false;
		}
		model->SetMeshes(pMeshes);

		std::vector<Material*> pMaterials;
		pMaterials.reserve(resMaterial.size());

		for (size_t i = 0; i < resMaterial.size(); i++)
		{
			Material* material = new (std::nothrow) Material();
			if (material == nullptr)
			{
				ELOG("Error : Out of memory.");
				return false;
			}

			if (!material->Init(m_pDevice.Get(), m_pPool[POOL_TYPE_RES], sizeof(CbMaterial), &m_DummyTexture))
			{
				ELOG("Error : Material Initialize Failed.");
				delete material;
				return false;
			}

			pMaterials.push_back(material);
		}

		pMaterials.shrink_to_fit();

		// 全マテリアルの全テクスチャでバッチ処理を走らせるために、SetTexture()を
		// Material::Init()の中で行っていない
		DirectX::ResourceUploadBatch batch(m_pDevice.Get());
		batch.Begin();

		const std::wstring& dir = GetDirectoryPath(path.c_str());
		for (size_t i = 0; i < resMaterial.size(); i++)
		{
			Material* pMaterial = pMaterials[i];
			const ResMaterial& resMat = resMaterial[i];

			pMaterial->SetTexture(Material::TEXTURE_USAGE_BASE_COLOR, dir + resMat.BaseColorMap, batch);
			pMaterial->SetTexture(Material::TEXTURE_USAGE_METALLIC_ROUGHNESS, dir + resMat.MetallicRoughnessMap, batch);
			pMaterial->SetTexture(Material::TEXTURE_USAGE_NORMAL, dir + resMat.NormalMap, batch);
			pMaterial->SetTexture(Material::TEXTURE_USAGE_EMISSIVE, dir + resMat.EmissiveMap, batch);
			pMaterial->SetTexture(Material::TEXTURE_USAGE_AMBIENT_OCCLUSION, dir + resMat.AmbientOcclusionMap, batch);

			pMaterial->SetDoubleSided(resMat.DoubleSided);

			CbMaterial* ptr = pMaterial->GetBufferPtr<CbMaterial>();
			ptr->BaseColorFactor = resMat.BaseColor;
			ptr->MetallicFactor = resMat.MetallicFactor;
			ptr->RoughnessFactor = resMat.RoughnessFactor;
			ptr->EmissiveFactor = resMat.EmissiveFactor;
			ptr->AlphaCutoff = resMat.AlphaCutoff;
			ptr->bExistEmissiveTex = resMat.EmissiveMap.empty() ? 0 : 1;
			ptr->bExistAOTex = resMat.AmbientOcclusionMap.empty() ? 0 : 1;
		}

		std::future<void> future = batch.End(m_pQueue.Get());
		future.wait();

		model->SetMaterials(pMaterials);

		m_pModels.push_back(model);
	}

	m_pModels.shrink_to_fit();

	if (RENDER_SPONZA)
	{
		// ディレクショナルライトバッファの設定
		{
			for (uint32_t i = 0u; i < FRAME_COUNT; i++)
			{
				if (!m_DirectionalLightCB[i].Init(m_pDevice.Get(), m_pPool[POOL_TYPE_RES], sizeof(CbDirectionalLight)))
				{
					ELOG("Error : ConstantBuffer::Init() Failed.");
					return false;
				}
			}
		}

		// ポイントライトバッファの設定
		{
			for (uint32_t i = 0u; i < NUM_POINT_LIGHTS; i++)
			{
				if (!m_PointLightCB[i].Init(m_pDevice.Get(), m_pPool[POOL_TYPE_RES], sizeof(CbPointLight)))
				{
					ELOG("Error : ConstantBuffer::Init() Failed.");
					return false;
				}
			}

			// ポイントライトは動かさないないので毎フレームの更新はしない

			CbPointLight* ptr = m_PointLightCB[0].GetPtr<CbPointLight>();
			// 少し黄色っぽい光
			//*ptr = ComputePointLight(Vector3(-4.95f, 1.10f, 1.15f), 5.0f, Vector3(1.0f, 1.0f, 0.5f), 100.0f);
			*ptr = ComputePointLight(Vector3(-4.95f, 1.10f, 1.15f), 20.0f, Vector3(1.0f, 1.0f, 0.5f), m_pointLightIntensity);

			ptr = m_PointLightCB[1].GetPtr<CbPointLight>();
			// 少し黄色っぽい光
			//*ptr = ComputePointLight(Vector3(-4.95f, 1.10f, -1.75f), 5.0f, Vector3(1.0f, 1.0f, 0.5f), 100.0f);
			*ptr = ComputePointLight(Vector3(-4.95f, 1.10f, -1.75f), 20.0f, Vector3(1.0f, 1.0f, 0.5f), m_pointLightIntensity);

			ptr = m_PointLightCB[2].GetPtr<CbPointLight>();
			// 少し黄色っぽい光
			//*ptr = ComputePointLight(Vector3(3.90f, 1.10f, 1.15f), 5.0f, Vector3(1.0f, 1.0f, 0.5f), 100.0f);
			*ptr = ComputePointLight(Vector3(3.90f, 1.10f, 1.15f), 20.0f, Vector3(1.0f, 1.0f, 0.5f), m_pointLightIntensity);

			ptr = m_PointLightCB[3].GetPtr<CbPointLight>();
			// 少し黄色っぽい光
			//*ptr = ComputePointLight(Vector3(3.90f, 1.10f, -1.75f), 5.0f, Vector3(1.0f, 1.0f, 0.5f), 100.0f);
			*ptr = ComputePointLight(Vector3(3.90f, 1.10f, -1.75f), 20.0f, Vector3(1.0f, 1.0f, 0.5f), m_pointLightIntensity);
		}

		// スポットライトバッファの設定
		{
			for (uint32_t i = 0u; i < NUM_SPOT_LIGHTS; i++)
			{
				if (!m_SpotLightCB[i].Init(m_pDevice.Get(), m_pPool[POOL_TYPE_RES], sizeof(CbSpotLight)))
				{
					ELOG("Error : ConstantBuffer::Init() Failed.");
					return false;
				}

				if (!m_SpotLightShadowMapTransformCB[i].Init(m_pDevice.Get(), m_pPool[POOL_TYPE_RES], sizeof(CbTransform)))
				{
					ELOG("Error : ConstantBuffer::Init() Failed.");
					return false;
				}
			}

			const Vector3& SpotLight1Dir = Vector3(-20.0f, -4.0f, 0.0f);
			const Vector3& SpotLight1Pos = Vector3(0.0f, 4.0f, 0.0f);
			CbSpotLight* ptr = m_SpotLightCB[0].GetPtr<CbSpotLight>();
			// 少し赤っぽい光
			*ptr = ComputeSpotLight(0, SpotLight1Dir, SpotLight1Pos, 20.0f, Vector3(1.0f, 0.5f, 0.5f), m_spotLightIntensity, DirectX::XMConvertToRadians(5.0f), DirectX::XMConvertToRadians(10.0f), SPOT_LIGHT_SHADOW_MAP_SIZE);
			CbTransform* tptr = m_SpotLightShadowMapTransformCB[0].GetPtr<CbTransform>();
			tptr->ViewProj = ComputeSpotLightViewProj(SpotLight1Dir, SpotLight1Pos, 20.0f, DirectX::XMConvertToRadians(10.0f));

			const Vector3& SpotLight2Dir = Vector3(0.0f, -10.0f, 2.0f);
			const Vector3& SpotLight2Pos = Vector3(0.0f, 10.0f, 0.0f);
			ptr = m_SpotLightCB[1].GetPtr<CbSpotLight>();
			// 少し緑っぽい光
			*ptr = ComputeSpotLight(0, SpotLight2Dir, SpotLight2Pos, 20.0f, Vector3(0.5f, 1.0f, 0.5f), m_spotLightIntensity, DirectX::XMConvertToRadians(5.0f), DirectX::XMConvertToRadians(10.0f), SPOT_LIGHT_SHADOW_MAP_SIZE);

			tptr = m_SpotLightShadowMapTransformCB[1].GetPtr<CbTransform>();
			tptr->ViewProj = ComputeSpotLightViewProj(SpotLight2Dir, SpotLight2Pos, 20.0f, DirectX::XMConvertToRadians(10.0f));

			const Vector3& SpotLight3Dir = Vector3(20.0f, -4.0f, 0.0f);
			const Vector3& SpotLight3Pos = Vector3(0.0f, 4.0f, 0.0f);
			ptr = m_SpotLightCB[2].GetPtr<CbSpotLight>();
			// 少し青っぽい光
			*ptr = ComputeSpotLight(0, SpotLight3Dir, SpotLight3Pos, 20.0f, Vector3(0.5f, 0.5f, 1.0f), m_spotLightIntensity, DirectX::XMConvertToRadians(5.0f), DirectX::XMConvertToRadians(10.0f), SPOT_LIGHT_SHADOW_MAP_SIZE);

			tptr = m_SpotLightShadowMapTransformCB[2].GetPtr<CbTransform>();
			tptr->ViewProj = ComputeSpotLightViewProj(SpotLight3Dir, SpotLight3Pos, 20.0f, DirectX::XMConvertToRadians(10.0f));
		}
	}

	// カメラバッファの設定
	{
		for (uint32_t i = 0u; i < FRAME_COUNT; i++)
		{
			if (!m_CameraCB[i].Init(m_pDevice.Get(), m_pPool[POOL_TYPE_RES], sizeof(CbCamera)))
			{
				ELOG("Error : ConstantBuffer::Init() Failed.");
				return false;
			}
		}
	}

	if (RENDER_SPONZA)
	{
		// ディレクショナルライト用デプスターゲットの生成
		{
			if (!m_DirLightShadowMapTarget.Init
			(
				m_pDevice.Get(),
				m_pPool[POOL_TYPE_DSV],
				m_pPool[POOL_TYPE_RES], // シャドウマップなのでSRVも作る
				DIRECTIONAL_LIGHT_SHADOW_MAP_SIZE,
				DIRECTIONAL_LIGHT_SHADOW_MAP_SIZE,
				DXGI_FORMAT_D16_UNORM, // TODO:ModelViewerを参考にした
				1.0f,
				0
			))
			{
				ELOG("Error : DepthTarget::Init() Failed.");
				return false;
			}

			{
				m_DirLightShadowMapViewport.TopLeftX = 0;
				m_DirLightShadowMapViewport.TopLeftY = 0;
				m_DirLightShadowMapViewport.Width = static_cast<float>(m_DirLightShadowMapTarget.GetDesc().Width);
				m_DirLightShadowMapViewport.Height = static_cast<float>(m_DirLightShadowMapTarget.GetDesc().Height);
				m_DirLightShadowMapViewport.MinDepth = 0.0f;
				m_DirLightShadowMapViewport.MaxDepth = 1.0f;
			}

			// TODO:ModelViewerだと内部で以下の処理がある
			//// Prevent drawing to the boundary pixels so that we don't have to worry about shadows stretching
			//m_Scissor.left = 1;
			//m_Scissor.top = 1;
			//m_Scissor.right = (LONG)Width - 2;
			//m_Scissor.bottom = (LONG)Height - 2;
			{
				m_DirLightShadowMapScissor.left = 0;
				m_DirLightShadowMapScissor.right = (LONG)m_DirLightShadowMapTarget.GetDesc().Width;
				m_DirLightShadowMapScissor.top = 0;
				m_DirLightShadowMapScissor.bottom = (LONG)m_DirLightShadowMapTarget.GetDesc().Height;
			}
		}

		// スポットライト用デプスターゲットの生成
		{
			for (uint32_t i = 0u; i < NUM_SPOT_LIGHTS; i++)
			{
				if (!m_SpotLightShadowMapTarget[i].Init
				(
					m_pDevice.Get(),
					m_pPool[POOL_TYPE_DSV],
					m_pPool[POOL_TYPE_RES], // シャドウマップなのでSRVも作る
					SPOT_LIGHT_SHADOW_MAP_SIZE,
					SPOT_LIGHT_SHADOW_MAP_SIZE,
					DXGI_FORMAT_D16_UNORM, // TODO:ModelViewerを参考にした
					1.0f,
					0
				))
				{
					ELOG("Error : DepthTarget::Init() Failed.");
					return false;
				}
			}

			{
				m_SpotLightShadowMapViewport.TopLeftX = 0;
				m_SpotLightShadowMapViewport.TopLeftY = 0;
				m_SpotLightShadowMapViewport.Width = static_cast<float>(m_SpotLightShadowMapTarget[0].GetDesc().Width);
				m_SpotLightShadowMapViewport.Height = static_cast<float>(m_SpotLightShadowMapTarget[0].GetDesc().Height);
				m_SpotLightShadowMapViewport.MinDepth = 0.0f;
				m_SpotLightShadowMapViewport.MaxDepth = 1.0f;
			}

			// TODO:ModelViewerだと内部で以下の処理がある
			//// Prevent drawing to the boundary pixels so that we don't have to worry about shadows stretching
			//m_Scissor.left = 1;
			//m_Scissor.top = 1;
			//m_Scissor.right = (LONG)Width - 2;
			//m_Scissor.bottom = (LONG)Height - 2;
			{
				m_SpotLightShadowMapScissor.left = 0;
				m_SpotLightShadowMapScissor.right = (LONG)m_SpotLightShadowMapTarget[0].GetDesc().Width;
				m_SpotLightShadowMapScissor.top = 0;
				m_SpotLightShadowMapScissor.bottom = (LONG)m_SpotLightShadowMapTarget[0].GetDesc().Height;
			}
		}
	}

	// 空の透過率LUT用カラーターゲットの生成
	{
		float clearColor[4] = {0.0f, 0.0f, 0.0f, 0.0f};

		if (!m_SkyTransmittanceLUT_Target.InitUnorderedAccessTarget
		(
			m_pDevice.Get(),
			m_pPool[POOL_TYPE_RES],
			nullptr, // RTVは作らない。クリアする必要がないので
			m_pPool[POOL_TYPE_RES],
			SKY_TRANSMITTANCE_LUT_WIDTH,
			SKY_TRANSMITTANCE_LUT_HEIGHT,
			DXGI_FORMAT_R11G11B10_FLOAT,
			clearColor
		))
		{
			ELOG("Error : ColorTarget::InitUnorderedAccessTarget() Failed.");
			return false;
		}
	}

	// シーン用カラーターゲットの生成
	{
		float clearColor[4] = {0.0f, 0.0f, 0.0f, 1.0f};

		if (!m_SceneColorTarget.InitRenderTarget
		(
			m_pDevice.Get(),
			m_pPool[POOL_TYPE_RTV],
			m_pPool[POOL_TYPE_RES],
			m_Width,
			m_Height,
			DXGI_FORMAT_R10G10B10A2_UNORM,
			clearColor
		))
		{
			ELOG("Error : ColorTarget::Init() Failed.");
			return false;
		}
	}

	// シーン用ノーマルターゲットの生成
	{
		float clearColor[4] = {0.0f, 0.0f, 0.0f, 1.0f};

		if (!m_SceneNormalTarget.InitRenderTarget
		(
			m_pDevice.Get(),
			m_pPool[POOL_TYPE_RTV],
			m_pPool[POOL_TYPE_RES],
			m_Width,
			m_Height,
			DXGI_FORMAT_R10G10B10A2_UNORM,
			clearColor
		))
		{
			ELOG("Error : NormalTarget::Init() Failed.");
			return false;
		}
	}

	// シーン用メタリックラフネスターゲットの生成
	{
		float clearColor[4] = {0.0f, 0.0f, 0.0f, 1.0f};

		if (!m_SceneMetallicRoughnessTarget.InitRenderTarget
		(
			m_pDevice.Get(),
			m_pPool[POOL_TYPE_RTV],
			m_pPool[POOL_TYPE_RES],
			m_Width,
			m_Height,
			DXGI_FORMAT_R8G8_UNORM,
			clearColor
		))
		{
			ELOG("Error : ColorTarget::Init() Failed.");
			return false;
		}
	}

	// シーン用デプスターゲットの生成
	{
		if (!m_SceneDepthTarget.Init
		(
			m_pDevice.Get(),
			m_pPool[POOL_TYPE_DSV],
			m_pPool[POOL_TYPE_RES],
			m_Width,
			m_Height,
			DXGI_FORMAT_D32_FLOAT,
			1.0f,
			0
		))
		{
			ELOG("Error : DepthTarget::Init() Failed.");
			return false;
		}
	}

	// HCB用カラーターゲットの生成
	{
		float clearColor[4] = {0.0f, 0.0f, 0.0f, 0.0f};

		// TODO:これらの計算をやっている箇所が複数あり冗長
		uint32_t mip0SizeX = RoundDownToPowerOfTwo(m_Width);
		uint32_t mip0SizeY = RoundDownToPowerOfTwo(m_Height);

		if (!m_HCB_Target.InitUnorderedAccessTarget
		(
			m_pDevice.Get(),
			m_pPool[POOL_TYPE_RES],
			nullptr, // RTVは作らない。クリアする必要がないので
			m_pPool[POOL_TYPE_RES],
			mip0SizeX,
			mip0SizeY,
			m_SceneColorTarget.GetDesc().Format,
			clearColor,
			HCB_MAX_NUM_OUTPUT_MIP
		))
		{
			ELOG("Error : ColorTarget::InitUnorderedAccessTarget() Failed.");
			return false;
		}
	}

	// HZB用ターゲットの生成と参照用にMipレベルを制限したSRVの例外的生成
	{
		float clearColor[4] = {0.0f, 0.0f, 0.0f, 0.0f};

		// TODO:これらの計算をやっている箇所が複数あり冗長
		uint32_t mip0SizeX = RoundDownToPowerOfTwo(m_Width);
		uint32_t mip0SizeY = RoundDownToPowerOfTwo(m_Height);
		uint32_t numMips = (uint32_t)log2f((float)DirectX::XMMax(mip0SizeX, mip0SizeY));

		if (!m_HZB_Target.InitUnorderedAccessTarget
		(
			m_pDevice.Get(),
			m_pPool[POOL_TYPE_RES],
			nullptr, // RTVは作らない。クリアする必要がないので
			m_pPool[POOL_TYPE_RES],
			mip0SizeX,
			mip0SizeY,
			DXGI_FORMAT_R16_FLOAT,
			clearColor,
			numMips
		))
		{
			ELOG("Error : ColorTarget::InitUnorderedAccessTarget() Failed.");
			return false;
		}

		uint32_t numDrawCall = DivideAndRoundUp(numMips, HZB_MAX_NUM_OUTPUT_MIP);

		m_pHZB_ParentMipSRVs.reserve(numDrawCall - 1);
		for (uint32_t i = 1; i < numDrawCall; i++) // 最初のドローコールはSceneDepthを使うので作らない
		{
			uint32_t minMipLevel = i * HZB_MAX_NUM_OUTPUT_MIP;

			// https://learn.microsoft.com/en-us/windows/win32/api/d3d12/ns-d3d12-d3d12_tex2d_srv
			D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc;
			srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
			srvDesc.Format = m_HZB_Target.GetDesc().Format;
			srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
			// ひとつ小さいMipレベルを参照する
			srvDesc.Texture2D.MostDetailedMip = minMipLevel - 1;
			// Mipレベル一枚だけでSRVを作る
			srvDesc.Texture2D.MipLevels = 1;
			srvDesc.Texture2D.PlaneSlice = 0;
			srvDesc.Texture2D.ResourceMinLODClamp = 0;

			DescriptorHandle* pHandleSRV = m_pPool[POOL_TYPE_RES]->AllocHandle();
			if (pHandleSRV == nullptr)
			{
				ELOG("Error : DescriptorPool::AllocHandle() Failed.");
				return false;
			}

			m_pDevice.Get()->CreateShaderResourceView(
				m_HZB_Target.GetResource(),
				&srvDesc,
				pHandleSRV->HandleCPU
			);

			m_pHZB_ParentMipSRVs.push_back(pHandleSRV);
		}
	}

	// ObjectVelocity用カラーターゲットの生成
	{
		float clearColor[4] = {0.0f, 0.0f, 0.0f, 0.0f};

		if (!m_ObjectVelocityTarget.InitRenderTarget
		(
			m_pDevice.Get(),
			m_pPool[POOL_TYPE_RTV],
			m_pPool[POOL_TYPE_RES],
			m_Width,
			m_Height,
			DXGI_FORMAT_R32G32_FLOAT,
			clearColor
		))
		{
			ELOG("Error : ColorTarget::Init() Failed.");
			return false;
		}
	}

	// CameraVelocity用カラーターゲットの生成
	{
		float clearColor[4] = {0.0f, 0.0f, 0.0f, 0.0f};

		if (!m_VelocityTarget.InitRenderTarget
		(
			m_pDevice.Get(),
			m_pPool[POOL_TYPE_RTV],
			m_pPool[POOL_TYPE_RES],
			m_Width,
			m_Height,
			DXGI_FORMAT_R32G32_FLOAT,
			clearColor
		))
		{
			ELOG("Error : ColorTarget::Init() Failed.");
			return false;
		}
	}

	// SSAO準備パス用カラーターゲットの生成
	{
		float clearColor[4] = {0.0f, 0.0f, 0.0f, 1.0f};

		if (!m_SSAOSetupTarget.InitRenderTarget
		(
			m_pDevice.Get(),
			m_pPool[POOL_TYPE_RTV],
			m_pPool[POOL_TYPE_RES],
			DivideAndRoundUp(m_Width, 2),
			DivideAndRoundUp(m_Height, 2),
			DXGI_FORMAT_R16G16B16A16_FLOAT,
			clearColor
		))
		{
			ELOG("Error : ColorTarget::Init() Failed.");
			return false;
		}
	}

	// SSAO半解像度用カラーターゲットの生成
	{
		float clearColor[4] = {1.0f, 1.0f, 1.0f, 1.0f};

		uint32_t width = DivideAndRoundUp(m_Width, 2);
		uint32_t height = DivideAndRoundUp(m_Height, 2);

		if (!m_SSAO_HalfResTarget.InitRenderTarget
		(
			m_pDevice.Get(),
			m_pPool[POOL_TYPE_RTV],
			m_pPool[POOL_TYPE_RES],
			width,
			height,
			DXGI_FORMAT_R8_UNORM,
			clearColor
		))
		{
			ELOG("Error : ColorTarget::Init() Failed.");
			return false;
		}
	}

	// SSAOフル解像度用カラーターゲットの生成
	{
		float clearColor[4] = {1.0f, 1.0f, 1.0f, 1.0f};

		if (!m_SSAO_FullResTarget.InitRenderTarget
		(
			m_pDevice.Get(),
			m_pPool[POOL_TYPE_RTV],
			m_pPool[POOL_TYPE_RES],
			m_Width,
			m_Height,
			DXGI_FORMAT_R8_UNORM,
			clearColor
		))
		{
			ELOG("Error : ColorTarget::Init() Failed.");
			return false;
		}
	}

	// SSAO用ランダム値生成用カラーターゲットの生成
	{
		ID3D12GraphicsCommandList* pCmd = m_CommandList.Reset();

		// サイズはUEのSSAORandomizationテクスチャを参考にした
		const uint32_t SSAO_RANDOMIZATIN_TEXTURE_SIZE = 64;

		const float ANGLE_OFF2 = 198;
		const float ANGLE_OFF3 = 23;
		
		uint8_t baseColors[16][2];
		uint32_t reorder[16] = { 0, 11, 7, 3, 10, 4, 15, 12, 6, 8, 1, 14, 13, 2, 9, 5 };

		for (uint32_t pos = 0; pos < 16; pos++)
		{
			uint32_t w = reorder[pos];
			float ww = w / 16.0f * DirectX::XM_PI;

			float lenm = 1.0f - (sinf(ANGLE_OFF2 * w * 0.01f) * 0.5f + 0.5f) * ANGLE_OFF3 * 0.01f;
			float s = sinf(ww) * lenm;
			float c = cosf(ww) * lenm;

			baseColors[pos][0] = Quantize8SignedByte(c); 
			baseColors[pos][1] = Quantize8SignedByte(s); 
		}

		std::vector<uint16_t> texData(SSAO_RANDOMIZATIN_TEXTURE_SIZE * SSAO_RANDOMIZATIN_TEXTURE_SIZE);
		for (uint32_t y = 0; y < SSAO_RANDOMIZATIN_TEXTURE_SIZE; y++)
		{
			for (uint32_t x = 0; x < SSAO_RANDOMIZATIN_TEXTURE_SIZE; x++)
			{
				uint8_t* dest = (uint8_t*)&texData[x + y * SSAO_RANDOMIZATIN_TEXTURE_SIZE];
				uint32_t index = (x % 4) + (y % 4) * 4;
				dest[0] = baseColors[index][0];
				dest[1] = baseColors[index][1];
			}
		}

		if (!m_SSAO_RandomizationTex.InitFromData
		(
			m_pDevice.Get(),
			pCmd,
			m_pPool[POOL_TYPE_RES],
			SSAO_RANDOMIZATIN_TEXTURE_SIZE,
			SSAO_RANDOMIZATIN_TEXTURE_SIZE,
			DXGI_FORMAT_R8G8_UNORM,
			texData.data()
		))
		{
			ELOG("Error : ColorTarget::Init() Failed.");
			return false;
		}

		pCmd->Close();

		ID3D12CommandList* pLists[] = {pCmd};
		m_pQueue->ExecuteCommandLists(1, pLists);

		// Wait command queue finishing.
		m_Fence.Wait(m_pQueue.Get(), INFINITE);
	}

	// SSGI用カラーターゲットの生成
	{
		float clearColor[4] = {0.0f, 0.0f, 0.0f, 0.0f};

		// 半解像度パスとして扱う
		uint32_t width = DivideAndRoundUp(m_Width, 2);
		uint32_t height = DivideAndRoundUp(m_Height, 2);

		if (!m_SSGI_Target.InitUnorderedAccessTarget
		(
			m_pDevice.Get(),
			m_pPool[POOL_TYPE_RES],
			nullptr, // RTVは作らない。クリアする必要がないので
			m_pPool[POOL_TYPE_RES],
			width,
			height,
			m_SceneColorTarget.GetDesc().Format,
			clearColor
		))
		{
			ELOG("Error : ColorTarget::InitUnorderedAccessTarget() Failed.");
			return false;
		}
	}

	// SSGIデノイズ用カラーターゲットの生成
	{
		float clearColor[4] = {0.0f, 0.0f, 0.0f, 0.0f};

		if (!m_SSGI_DenoiseTarget.InitUnorderedAccessTarget
		(
			m_pDevice.Get(),
			m_pPool[POOL_TYPE_RES],
			nullptr, // RTVは作らない。クリアする必要がないので
			m_pPool[POOL_TYPE_RES],
			(uint32_t)m_SSGI_Target.GetDesc().Width,
			m_SSGI_Target.GetDesc().Height,
			m_SceneColorTarget.GetDesc().Format,
			clearColor
		))
		{
			ELOG("Error : ColorTarget::InitUnorderedAccessTarget() Failed.");
			return false;
		}
	}

	// SSGI Temporal Accumulation用カラーターゲットの生成
	{
		float clearColor[4] = {0.0f, 0.0f, 0.0f, 0.0f};

		for (uint32_t i = 0u; i < FRAME_COUNT; i++)
		{
			if (!m_SSGI_TemporalAccumulationTarget[i].InitUnorderedAccessTarget
			(
				m_pDevice.Get(),
				m_pPool[POOL_TYPE_RES],
				nullptr, // RTVは作らない。クリアする必要がないので
				m_pPool[POOL_TYPE_RES],
				(uint32_t)m_SSGI_Target.GetDesc().Width,
				m_SSGI_Target.GetDesc().Height,
				m_SceneColorTarget.GetDesc().Format,
				clearColor
			))
			{
				ELOG("Error : ColorTarget::InitUnorderedAccessTarget() Failed.");
				return false;
			}
		}
	}

	// AmbientLight用カラーターゲットの生成
	{
		float clearColor[4] = {0.0f, 0.0f, 0.0f, 1.0f};

		if (!m_AmbientLightTarget.InitRenderTarget
		(
			m_pDevice.Get(),
			m_pPool[POOL_TYPE_RTV],
			m_pPool[POOL_TYPE_RES],
			m_Width,
			m_Height,
			DXGI_FORMAT_R10G10B10A2_UNORM,
			clearColor
		))
		{
			ELOG("Error : ColorTarget::Init() Failed.");
			return false;
		}
	}

	// SSR用カラーターゲットの生成
	{
		float clearColor[4] = {0.0f, 0.0f, 0.0f, 1.0f};

		if (!m_SSR_Target.InitRenderTarget
		(
			m_pDevice.Get(),
			m_pPool[POOL_TYPE_RTV],
			m_pPool[POOL_TYPE_RES],
			m_Width,
			m_Height,
			DXGI_FORMAT_R10G10B10A2_UNORM,
			clearColor
		))
		{
			ELOG("Error : ColorTarget::Init() Failed.");
			return false;
		}
	}

	const uint32_t volumetricFogGridSizeX = DivideAndRoundUp(m_Width, VOLUMETRIC_FOG_GRID_PIXEL_SIZE);
	const uint32_t volumetricFogGridSizeY = DivideAndRoundUp(m_Height, VOLUMETRIC_FOG_GRID_PIXEL_SIZE);

	// VolumetricFog Scattering用カラーターゲットの生成
	{
		float clearColor[4] = {0.0f, 0.0f, 0.0f, 0.0f};

		for (uint32_t i = 0u; i < FRAME_COUNT; i++)
		{
			if (!m_VolumetricFogScatteringTarget[i].InitUnorderedAccessTarget
			(
				m_pDevice.Get(),
				m_pPool[POOL_TYPE_RES],
				nullptr, // RTVは作らない。クリアする必要がないので
				m_pPool[POOL_TYPE_RES],
				volumetricFogGridSizeX,
				volumetricFogGridSizeY,
				DXGI_FORMAT_R16G16B16A16_FLOAT,
				clearColor,
				1,
				VOLUMETRIC_FOG_GRID_SIZE_Z
			))
			{
				ELOG("Error : InitUnorderedAccessTarget::Init() Failed.");
				return false;
			}
		}
	}

	// VolumetricFog Integration用カラーターゲットの生成
	{
		float clearColor[4] = {0.0f, 0.0f, 0.0f, 0.0f};

		if (!m_VolumetricFogIntegrationTarget.InitUnorderedAccessTarget
		(
			m_pDevice.Get(),
			m_pPool[POOL_TYPE_RES],
			nullptr, // RTVは作らない。クリアする必要がないので
			m_pPool[POOL_TYPE_RES],
			volumetricFogGridSizeX,
			volumetricFogGridSizeY,
			DXGI_FORMAT_R16G16B16A16_FLOAT,
			clearColor,
			1,
			VOLUMETRIC_FOG_GRID_SIZE_Z
		))
		{
			ELOG("Error : InitUnorderedAccessTarget::Init() Failed.");
			return false;
		}
	}

	// VolumetricFog Composition用カラーターゲットの生成
	{
		float clearColor[4] = {0.0f, 0.0f, 0.0f, 1.0f};

		if (!m_VolumetricCompositionTarget.InitRenderTarget
		(
			m_pDevice.Get(),
			m_pPool[POOL_TYPE_RTV],
			m_pPool[POOL_TYPE_RES],
			m_Width,
			m_Height,
			DXGI_FORMAT_R10G10B10A2_UNORM,
			clearColor
		))
		{
			ELOG("Error : ColorTarget::Init() Failed.");
			return false;
		}
	}

	// TemporalAA用ターゲットの生成
	{
		float clearColor[4] = {0.0f, 0.0f, 0.0f, 0.0f};

		for (uint32_t i = 0u; i < FRAME_COUNT; i++)
		{
			if (!m_TemporalAA_Target[i].InitUnorderedAccessTarget
			(
				m_pDevice.Get(),
				m_pPool[POOL_TYPE_RES],
				nullptr, // RTVは作らない。クリアする必要がないので
				m_pPool[POOL_TYPE_RES],
				m_Width,
				m_Height,
				DXGI_FORMAT_R16G16B16A16_FLOAT,
				clearColor
			))
			{
				ELOG("Error : InitUnorderedAccessTarget::Init() Failed.");
				return false;
			}
		}
	}

	// MotionBlur用カラーターゲットの生成
	{
		float clearColor[4] = {0.0f, 0.0f, 0.0f, 0.0f};

		if (!m_MotionBlurTarget.InitRenderTarget
		(
			m_pDevice.Get(),
			m_pPool[POOL_TYPE_RTV],
			m_pPool[POOL_TYPE_RES],
			m_Width,
			m_Height,
			DXGI_FORMAT_R11G11B10_FLOAT, // Aは必要ない
			clearColor
		))
		{
			ELOG("Error : ColorTarget::Init() Failed.");
			return false;
		}
	}

	// Bloom前工程用カラーターゲットの生成
	{
		float clearColor[4] = {0.0f, 0.0f, 0.0f, 0.0f};

		uint32_t width = m_Width;
		uint32_t height = m_Height;

		for (uint32_t i = 0; i < BLOOM_NUM_DOWN_SAMPLE; i++)
		{
			width = DivideAndRoundUp(width, 2);
			height = DivideAndRoundUp(height, 2);

			if (!m_BloomSetupTarget[i].InitRenderTarget
			(
				m_pDevice.Get(),
				m_pPool[POOL_TYPE_RTV],
				m_pPool[POOL_TYPE_RES],
				width,
				height,
				DXGI_FORMAT_R11G11B10_FLOAT, // Aは必要ない
				clearColor
			))
			{
				ELOG("Error : ColorTarget::Init() Failed.");
				return false;
			}
		}
	}

	// Bloom後工程用カラーターゲットの生成
	{
		float clearColor[4] = {0.0f, 0.0f, 0.0f, 0.0f};

		uint32_t width = m_Width;
		uint32_t height = m_Height;

		for (uint32_t i = 0; i < BLOOM_NUM_DOWN_SAMPLE; i++)
		{
			width = DivideAndRoundUp(width, 2);
			height = DivideAndRoundUp(height, 2);

			if (!m_BloomHorizontalTarget[i].InitRenderTarget
			(
				m_pDevice.Get(),
				m_pPool[POOL_TYPE_RTV],
				m_pPool[POOL_TYPE_RES],
				width,
				height,
				DXGI_FORMAT_R11G11B10_FLOAT, // Aは必要ない
				clearColor
			))
			{
				ELOG("Error : ColorTarget::Init() Failed.");
				return false;
			}

			if (!m_BloomVerticalTarget[i].InitRenderTarget
			(
				m_pDevice.Get(),
				m_pPool[POOL_TYPE_RTV],
				m_pPool[POOL_TYPE_RES],
				width,
				height,
				DXGI_FORMAT_R11G11B10_FLOAT, // Aは必要ない
				clearColor
			))
			{
				ELOG("Error : ColorTarget::Init() Failed.");
				return false;
			}
		}
	}

	// トーンマップ用カラーターゲットの生成
	{
		float clearColor[4] = {0.0f, 0.0f, 0.0f, 1.0f};

		if (!m_TonemapTarget.InitRenderTarget
		(
			m_pDevice.Get(),
			m_pPool[POOL_TYPE_RTV],
			m_pPool[POOL_TYPE_RES],
			m_Width,
			m_Height,
			m_ColorTarget[0].GetRTVDesc().Format,
			clearColor
		))
		{
			ELOG("Error : ColorTarget::Init() Failed.");
			return false;
		}
	}

	// FXAA用カラーターゲットの生成
	{
		float clearColor[4] = {0.0f, 0.0f, 0.0f, 1.0f};

		if (!m_FXAA_Target.InitRenderTarget
		(
			m_pDevice.Get(),
			m_pPool[POOL_TYPE_RTV],
			m_pPool[POOL_TYPE_RES],
			m_Width,
			m_Height,
			m_ColorTarget[0].GetRTVDesc().Format,
			clearColor
		))
		{
			ELOG("Error : ColorTarget::Init() Failed.");
			return false;
		}
	}

    // シーン用ルートシグニチャとパイプラインステートの生成
	if (RENDER_SPONZA)
	{
		// AlphaModeがOpaqueのシャドウマップ描画用
		std::wstring vsPath;
		if (!SearchFilePath(L"SponzaVS.cso", vsPath))
		{
			ELOG("Error : Vertex Shader Not Found");
			return false;
		}

		ComPtr<ID3DBlob> pVSBlob;
		HRESULT hr = D3DReadFileToBlob(vsPath.c_str(), pVSBlob.GetAddressOf());
		if (FAILED(hr))
		{
			ELOG("Error : D3DReadFileToBlob Failed. path = %ls", vsPath.c_str());
			return false;
		}

		// AlphaModeがOpaqueのマテリアル用
		std::wstring psPath;
		if (!SearchFilePath(L"SponzaOpaquePS.cso", psPath))
		{
			ELOG("Error : Pixel Shader Not Found");
			return false;
		}

		ComPtr<ID3DBlob> pPSBlob;
		hr = D3DReadFileToBlob(psPath.c_str(), pPSBlob.GetAddressOf());
		if (FAILED(hr))
		{
			ELOG("Error : D3DReadFileToBlob Failed. path = %ls", psPath.c_str());
			return false;
		}

		ComPtr<ID3DBlob> pRSBlob;
		hr = D3DGetBlobPart(pPSBlob->GetBufferPointer(), pPSBlob->GetBufferSize(), D3D_BLOB_ROOT_SIGNATURE, 0, &pRSBlob);
		if (FAILED(hr))
		{
			ELOG("Error : D3DGetBlobPart Failed. path = %ls", psPath.c_str());
			return false;
		}

		if (!m_SponzaRootSig.Init(m_pDevice.Get(), pRSBlob))
		{
			ELOG("Error : RootSignature::Init() Failed.");
			return false;
		}

		// シャドウマップ描画用のパイプラインステートディスクリプタ
		D3D12_GRAPHICS_PIPELINE_STATE_DESC desc = {};
		desc.InputLayout = MeshVertex::InputLayout;
		desc.pRootSignature = m_SponzaRootSig.GetPtr();
		desc.BlendState = DirectX::CommonStates::Opaque;
		desc.DepthStencilState = DirectX::CommonStates::DepthDefault;
		desc.SampleMask = UINT_MAX;
		desc.RasterizerState = DirectX::CommonStates::CullClockwise;
		desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
		desc.NumRenderTargets = 0;
		desc.DSVFormat = m_DirLightShadowMapTarget.GetDSVDesc().Format;
		desc.SampleDesc.Count = 1;
		desc.SampleDesc.Quality = 0;

		// TODO:SponzaRendererの数字を何も考えずに使っている
		desc.RasterizerState.SlopeScaledDepthBias = 1.5f;
		desc.RasterizerState.DepthBias = 100;

		desc.VS.pShaderBytecode = pVSBlob->GetBufferPointer();
		desc.VS.BytecodeLength = pVSBlob->GetBufferSize();
		// PSは実行しないので設定しない

		hr = m_pDevice->CreateGraphicsPipelineState(
			&desc,
			IID_PPV_ARGS(m_pSponzaDepthOpaquePSO.GetAddressOf())
		);
		if (FAILED(hr))
		{
			ELOG("Error : ID3D12Device::CreateGraphicsPipelineState Failed. retcode = 0x%x", hr);
			return false;
		}

		// AlphaModeがMaskのシャドウマップ描画用
		if (!SearchFilePath(L"DepthMaskPS.cso", psPath))
		{
			ELOG("Error : Pixel Shader Not Found");
			return false;
		}

		ComPtr<ID3DBlob> pDepthMaskPSBlob;
		hr = D3DReadFileToBlob(psPath.c_str(), pDepthMaskPSBlob.GetAddressOf());
		if (FAILED(hr))
		{
			ELOG("Error : D3DReadFileToBlob Failed. path = %ls", psPath.c_str());
			return false;
		}

		desc.PS.pShaderBytecode = pDepthMaskPSBlob->GetBufferPointer();
		desc.PS.BytecodeLength = pDepthMaskPSBlob->GetBufferSize();
		desc.RasterizerState = DirectX::CommonStates::CullNone;

		hr = m_pDevice->CreateGraphicsPipelineState(
			&desc,
			IID_PPV_ARGS(m_pSponzaDepthMaskPSO.GetAddressOf())
		);
		if (FAILED(hr))
		{
			ELOG("Error : ID3D12Device::CreateGraphicsPipelineState Failed. retcode = 0x%x", hr);
			return false;
		}

		// AlphaModeがOpaqueのマテリアル用
		desc.RasterizerState = DirectX::CommonStates::CullClockwise;
		desc.NumRenderTargets = 3;
		desc.RTVFormats[0] = m_SceneColorTarget.GetRTVDesc().Format;
		desc.RTVFormats[1] = m_SceneNormalTarget.GetRTVDesc().Format;
		desc.RTVFormats[2] = m_SceneMetallicRoughnessTarget.GetRTVDesc().Format;
		desc.DSVFormat = m_SceneDepthTarget.GetDSVDesc().Format;
		desc.PS.pShaderBytecode = pPSBlob->GetBufferPointer();
		desc.PS.BytecodeLength = pPSBlob->GetBufferSize();

		hr = m_pDevice->CreateGraphicsPipelineState(
			&desc,
			IID_PPV_ARGS(m_pSponzaOpaquePSO.GetAddressOf())
		);
		if (FAILED(hr))
		{
			ELOG("Error : ID3D12Device::CreateGraphicsPipelineState Failed. retcode = 0x%x", hr);
			return false;
		}

		// AlphaModeがMaskのマテリアル用
		if (!SearchFilePath(L"SponzaMaskPS.cso", psPath))
		{
			ELOG("Error : Pixel Shader Not Found");
			return false;
		}

		hr = D3DReadFileToBlob(psPath.c_str(), pPSBlob.GetAddressOf());
		if (FAILED(hr))
		{
			ELOG("Error : D3DReadFileToBlob Failed. path = %ls", psPath.c_str());
			return false;
		}

		desc.PS.pShaderBytecode = pPSBlob->GetBufferPointer();
		desc.PS.BytecodeLength = pPSBlob->GetBufferSize();
		//TODO: MaskマテリアルはDoubleSidedであるという前提にしている
		desc.RasterizerState = DirectX::CommonStates::CullNone;

		hr = m_pDevice->CreateGraphicsPipelineState(
			&desc,
			IID_PPV_ARGS(m_pSponzaMaskPSO.GetAddressOf())
		);
		if (FAILED(hr))
		{
			ELOG("Error : ID3D12Device::CreateGraphicsPipelineState Failed. retcode = 0x%x", hr);
			return false;
		}
	}
	else
	{
		// AlphaModeがOpaqueのマテリアル用
		std::wstring psPath;
		if (!SearchFilePath(L"BasePassOpaquePS.cso", psPath))
		{
			ELOG("Error : Pixel Shader Not Found");
			return false;
		}

		ComPtr<ID3DBlob> pPSBlob;
		HRESULT hr = D3DReadFileToBlob(psPath.c_str(), pPSBlob.GetAddressOf());
		if (FAILED(hr))
		{
			ELOG("Error : D3DReadFileToBlob Failed. path = %ls", psPath.c_str());
			return false;
		}

		ComPtr<ID3DBlob> pRSBlob;
		hr = D3DGetBlobPart(pPSBlob->GetBufferPointer(), pPSBlob->GetBufferSize(), D3D_BLOB_ROOT_SIGNATURE, 0, &pRSBlob);
		if (FAILED(hr))
		{
			ELOG("Error : D3DGetBlobPart Failed. path = %ls", psPath.c_str());
			return false;
		}

		if (!m_SceneRootSig.Init(m_pDevice.Get(), pRSBlob))
		{
			ELOG("Error : RootSignature::Init() Failed.");
			return false;
		}

		// シャドウマップ描画用のパイプラインステートディスクリプタ
		D3D12_GRAPHICS_PIPELINE_STATE_DESC desc = {};
		desc.InputLayout = MeshVertex::InputLayout;
		desc.pRootSignature = m_SceneRootSig.GetPtr();
		desc.BlendState = DirectX::CommonStates::Opaque;
		desc.DepthStencilState = DirectX::CommonStates::DepthDefault;
		desc.SampleMask = UINT_MAX;
		desc.RasterizerState = DirectX::CommonStates::CullClockwise;
		desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
		desc.NumRenderTargets = 0;
		desc.DSVFormat = m_DirLightShadowMapTarget.GetDSVDesc().Format;
		desc.SampleDesc.Count = 1;
		desc.SampleDesc.Quality = 0;

		// TODO:SponzaRendererの数字を何も考えずに使っている
		desc.RasterizerState.SlopeScaledDepthBias = 1.5f;
		desc.RasterizerState.DepthBias = 100;

		// AlphaModeがOpaqueのシャドウマップ描画用
		std::wstring vsPath;
		if (!SearchFilePath(L"BasePassVS.cso", vsPath))
		{
			ELOG("Error : Vertex Shader Not Found");
			return false;
		}

		ComPtr<ID3DBlob> pVSBlob;
		hr = D3DReadFileToBlob(vsPath.c_str(), pVSBlob.GetAddressOf());
		if (FAILED(hr))
		{
			ELOG("Error : D3DReadFileToBlob Failed. path = %ls", vsPath.c_str());
			return false;
		}

		desc.VS.pShaderBytecode = pVSBlob->GetBufferPointer();
		desc.VS.BytecodeLength = pVSBlob->GetBufferSize();
		// PSは実行しないので設定しない

		hr = m_pDevice->CreateGraphicsPipelineState(
			&desc,
			IID_PPV_ARGS(m_pSceneDepthOpaquePSO.GetAddressOf())
		);
		if (FAILED(hr))
		{
			ELOG("Error : ID3D12Device::CreateGraphicsPipelineState Failed. retcode = 0x%x", hr);
			return false;
		}

		// AlphaModeがMaskのシャドウマップ描画用
		if (!SearchFilePath(L"DepthMaskPS.cso", psPath))
		{
			ELOG("Error : Pixel Shader Not Found");
			return false;
		}

		ComPtr<ID3DBlob> pDepthMaskPSBlob;
		hr = D3DReadFileToBlob(psPath.c_str(), pDepthMaskPSBlob.GetAddressOf());
		if (FAILED(hr))
		{
			ELOG("Error : D3DReadFileToBlob Failed. path = %ls", psPath.c_str());
			return false;
		}

		desc.PS.pShaderBytecode = pDepthMaskPSBlob->GetBufferPointer();
		desc.PS.BytecodeLength = pDepthMaskPSBlob->GetBufferSize();
		desc.RasterizerState = DirectX::CommonStates::CullNone;

		hr = m_pDevice->CreateGraphicsPipelineState(
			&desc,
			IID_PPV_ARGS(m_pSceneDepthMaskPSO.GetAddressOf())
		);
		if (FAILED(hr))
		{
			ELOG("Error : ID3D12Device::CreateGraphicsPipelineState Failed. retcode = 0x%x", hr);
			return false;
		}

		// AlphaModeがOpaqueのマテリアル用
		desc.RasterizerState = DirectX::CommonStates::CullClockwise;
		desc.NumRenderTargets = 3;
		desc.RTVFormats[0] = m_SceneColorTarget.GetRTVDesc().Format;
		desc.RTVFormats[1] = m_SceneNormalTarget.GetRTVDesc().Format;
		desc.RTVFormats[2] = m_SceneMetallicRoughnessTarget.GetRTVDesc().Format;
		desc.DSVFormat = m_SceneDepthTarget.GetDSVDesc().Format;
		desc.PS.pShaderBytecode = pPSBlob->GetBufferPointer();
		desc.PS.BytecodeLength = pPSBlob->GetBufferSize();

		hr = m_pDevice->CreateGraphicsPipelineState(
			&desc,
			IID_PPV_ARGS(m_pSceneOpaquePSO.GetAddressOf())
		);
		if (FAILED(hr))
		{
			ELOG("Error : ID3D12Device::CreateGraphicsPipelineState Failed. retcode = 0x%x", hr);
			return false;
		}

		// AlphaModeがMaskのマテリアル用
		if (!SearchFilePath(L"BasePassMaskPS.cso", psPath))
		{
			ELOG("Error : Pixel Shader Not Found");
			return false;
		}

		hr = D3DReadFileToBlob(psPath.c_str(), pPSBlob.GetAddressOf());
		if (FAILED(hr))
		{
			ELOG("Error : D3DReadFileToBlob Failed. path = %ls", psPath.c_str());
			return false;
		}

		desc.PS.pShaderBytecode = pPSBlob->GetBufferPointer();
		desc.PS.BytecodeLength = pPSBlob->GetBufferSize();
		//TODO: MaskマテリアルはDoubleSidedであるという前提にしている
		desc.RasterizerState = DirectX::CommonStates::CullNone;

		hr = m_pDevice->CreateGraphicsPipelineState(
			&desc,
			IID_PPV_ARGS(m_pSceneMaskPSO.GetAddressOf())
		);
		if (FAILED(hr))
		{
			ELOG("Error : ID3D12Device::CreateGraphicsPipelineState Failed. retcode = 0x%x", hr);
			return false;
		}
	}

    // HCB作成パス用ルートシグニチャとパイプラインステートの生成
	{
		std::wstring csPath;

		if (!SearchFilePath(L"HCB_CS.cso", csPath))
		{
			ELOG("Error : Compute Shader Not Found");
			return false;
		}

		ComPtr<ID3DBlob> pCSBlob;

		HRESULT hr = D3DReadFileToBlob(csPath.c_str(), pCSBlob.GetAddressOf());
		if (FAILED(hr))
		{
			ELOG("Error : D3DReadFileToBlob Failed. path = %ls", csPath.c_str());
			return false;
		}

		ComPtr<ID3DBlob> pRSBlob;
		hr = D3DGetBlobPart(pCSBlob->GetBufferPointer(), pCSBlob->GetBufferSize(), D3D_BLOB_ROOT_SIGNATURE, 0, &pRSBlob);
		if (FAILED(hr))
		{
			ELOG("Error : D3DGetBlobPart Failed. path = %ls", csPath.c_str());
			return false;
		}

		if (!m_HCB_RootSig.Init(m_pDevice.Get(), pRSBlob))
		{
			ELOG("Error : RootSignature::Init() Failed.");
			return false;
		}

		D3D12_COMPUTE_PIPELINE_STATE_DESC desc = {};
		desc.pRootSignature = m_HCB_RootSig.GetPtr();
		desc.CS.pShaderBytecode = pCSBlob->GetBufferPointer();
		desc.CS.BytecodeLength = pCSBlob->GetBufferSize();
		desc.NodeMask = 0;
		desc.CachedPSO.pCachedBlob = nullptr;
		desc.CachedPSO.CachedBlobSizeInBytes = 0;
		desc.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;

		hr = m_pDevice->CreateComputePipelineState(
			&desc,
			IID_PPV_ARGS(m_pHCB_PSO.GetAddressOf())
		);
		if (FAILED(hr))
		{
			ELOG("Error : ID3D12Device::CreateComputePipelineState Failed. retcode = 0x%x", hr);
			return false;
		}
	}

    // HZB作成パス用ルートシグニチャとパイプラインステートの生成
	{
		std::wstring csPath;

		if (!SearchFilePath(L"HZB_CS.cso", csPath))
		{
			ELOG("Error : Compute Shader Not Found");
			return false;
		}

		ComPtr<ID3DBlob> pCSBlob;

		HRESULT hr = D3DReadFileToBlob(csPath.c_str(), pCSBlob.GetAddressOf());
		if (FAILED(hr))
		{
			ELOG("Error : D3DReadFileToBlob Failed. path = %ls", csPath.c_str());
			return false;
		}

		ComPtr<ID3DBlob> pRSBlob;
		hr = D3DGetBlobPart(pCSBlob->GetBufferPointer(), pCSBlob->GetBufferSize(), D3D_BLOB_ROOT_SIGNATURE, 0, &pRSBlob);
		if (FAILED(hr))
		{
			ELOG("Error : D3DGetBlobPart Failed. path = %ls", csPath.c_str());
			return false;
		}

		if (!m_HZB_RootSig.Init(m_pDevice.Get(), pRSBlob))
		{
			ELOG("Error : RootSignature::Init() Failed.");
			return false;
		}

		D3D12_COMPUTE_PIPELINE_STATE_DESC desc = {};
		desc.pRootSignature = m_HZB_RootSig.GetPtr();
		desc.CS.pShaderBytecode = pCSBlob->GetBufferPointer();
		desc.CS.BytecodeLength = pCSBlob->GetBufferSize();
		desc.NodeMask = 0;
		desc.CachedPSO.pCachedBlob = nullptr;
		desc.CachedPSO.CachedBlobSizeInBytes = 0;
		desc.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;

		hr = m_pDevice->CreateComputePipelineState(
			&desc,
			IID_PPV_ARGS(m_pHZB_PSO.GetAddressOf())
		);
		if (FAILED(hr))
		{
			ELOG("Error : ID3D12Device::CreateComputePipelineState Failed. retcode = 0x%x", hr);
			return false;
		}
	}

    // ObjectVelocity用ルートシグニチャとパイプラインステートの生成
	{
		std::wstring vsPath;
		if (!SearchFilePath(L"ObjectVelocityVS.cso", vsPath))
		{
			ELOG("Error : Vertex Shader Not Found");
			return false;
		}

		ComPtr<ID3DBlob> pVSBlob;
		HRESULT hr = D3DReadFileToBlob(vsPath.c_str(), pVSBlob.GetAddressOf());
		if (FAILED(hr))
		{
			ELOG("Error : D3DReadFileToBlob Failed. path = %ls", vsPath.c_str());
			return false;
		}

		std::wstring psPath;
		if (!SearchFilePath(L"ObjectVelocityPS.cso", psPath))
		{
			ELOG("Error : Pixel Shader Not Found");
			return false;
		}

		ComPtr<ID3DBlob> pPSBlob;
		hr = D3DReadFileToBlob(psPath.c_str(), pPSBlob.GetAddressOf());
		if (FAILED(hr))
		{
			ELOG("Error : D3DReadFileToBlob Failed. path = %ls", psPath.c_str());
			return false;
		}

		ComPtr<ID3DBlob> pRSBlob;
		hr = D3DGetBlobPart(pPSBlob->GetBufferPointer(), pPSBlob->GetBufferSize(), D3D_BLOB_ROOT_SIGNATURE, 0, &pRSBlob);
		if (FAILED(hr))
		{
			ELOG("Error : D3DGetBlobPart Failed. path = %ls", psPath.c_str());
			return false;
		}

		if (!m_ObjectVelocityRootSig.Init(m_pDevice.Get(), pRSBlob))
		{
			ELOG("Error : RootSignature::Init() Failed.");
			return false;
		}

		D3D12_GRAPHICS_PIPELINE_STATE_DESC desc = {};
		desc.InputLayout = MeshVertex::InputLayout;
		desc.pRootSignature = m_ObjectVelocityRootSig.GetPtr();
		desc.BlendState = DirectX::CommonStates::Opaque;
		desc.DepthStencilState = DirectX::CommonStates::DepthDefault;
		desc.SampleMask = UINT_MAX;
		desc.RasterizerState = DirectX::CommonStates::CullClockwise;
		desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
		desc.NumRenderTargets = 1;
		desc.RTVFormats[0] = m_ObjectVelocityTarget.GetRTVDesc().Format;
		desc.DSVFormat = m_SceneDepthTarget.GetDSVDesc().Format;
		desc.SampleDesc.Count = 1;
		desc.SampleDesc.Quality = 0;
		desc.VS.pShaderBytecode = pVSBlob->GetBufferPointer();
		desc.VS.BytecodeLength = pVSBlob->GetBufferSize();
		desc.PS.pShaderBytecode = pPSBlob->GetBufferPointer();
		desc.PS.BytecodeLength = pPSBlob->GetBufferSize();

		hr = m_pDevice->CreateGraphicsPipelineState(
			&desc,
			IID_PPV_ARGS(m_pObjectVelocityPSO.GetAddressOf())
		);
		if (FAILED(hr))
		{
			ELOG("Error : ID3D12Device::CreateGraphicsPipelineState Failed. retcode = 0x%x", hr);
			return false;
		}
	}

	// スクリーンスペース描画パス用のInputElement。解放されないようにスコープ外で定義。
	D3D12_INPUT_ELEMENT_DESC SSPassInputElements[2];
	SSPassInputElements[0].SemanticName = "POSITION";
	SSPassInputElements[0].SemanticIndex = 0;
	SSPassInputElements[0].Format = DXGI_FORMAT_R32G32_FLOAT;
	SSPassInputElements[0].InputSlot = 0;
	SSPassInputElements[0].AlignedByteOffset = 0;
	SSPassInputElements[0].InputSlotClass = D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA;
	SSPassInputElements[0].InstanceDataStepRate = 0;

	SSPassInputElements[1].SemanticName = "TEXCOORD";
	SSPassInputElements[1].SemanticIndex = 0;
	SSPassInputElements[1].Format = DXGI_FORMAT_R32G32_FLOAT;
	SSPassInputElements[1].InputSlot = 0;
	SSPassInputElements[1].AlignedByteOffset = 8;
	SSPassInputElements[1].InputSlotClass = D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA;
	SSPassInputElements[1].InstanceDataStepRate = 0;

	// スクリーンスペース描画パス用のD3D12_GRAPHICS_PIPELINE_STATE_DESCの共通項
	D3D12_GRAPHICS_PIPELINE_STATE_DESC SSPassPSODescCommon = {};
	{
		SSPassPSODescCommon.InputLayout.pInputElementDescs = SSPassInputElements;
		SSPassPSODescCommon.InputLayout.NumElements = 2;
		SSPassPSODescCommon.pRootSignature = nullptr; // 上書き必須
		SSPassPSODescCommon.VS.pShaderBytecode = nullptr; // 上書き必須。TODO:使いまわそうとしたらエラーになった。
		SSPassPSODescCommon.VS.BytecodeLength = 0; // 上書き必須。TODO:使いまわそうとしたらエラーになった。
		SSPassPSODescCommon.PS.pShaderBytecode = nullptr; // 上書き必須
		SSPassPSODescCommon.PS.BytecodeLength = 0; // 上書き必須
		SSPassPSODescCommon.RasterizerState = DirectX::CommonStates::CullCounterClockwise;
		SSPassPSODescCommon.BlendState = DirectX::CommonStates::Opaque;
		SSPassPSODescCommon.DepthStencilState = DirectX::CommonStates::DepthNone;
		SSPassPSODescCommon.SampleMask = UINT_MAX;
		SSPassPSODescCommon.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
		SSPassPSODescCommon.NumRenderTargets = 1;
		SSPassPSODescCommon.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM; // 上書き必須
		SSPassPSODescCommon.SampleDesc.Count = 1;
		SSPassPSODescCommon.SampleDesc.Quality = 0;
	}

    // CameraVelocity用ルートシグニチャとパイプラインステートの生成
	{
		std::wstring vsPath;
		std::wstring psPath;

		if (!SearchFilePath(L"QuadVS.cso", vsPath))
		{
			ELOG("Error : Vertex Shader Not Found");
			return false;
		}

		if (!SearchFilePath(L"CameraVelocityPS.cso", psPath))
		{
			ELOG("Error : Pixel Shader Not Found");
			return false;
		}

		ComPtr<ID3DBlob> pVSBlob;
		ComPtr<ID3DBlob> pPSBlob;

		HRESULT hr = D3DReadFileToBlob(vsPath.c_str(), pVSBlob.GetAddressOf());
		if (FAILED(hr))
		{
			ELOG("Error : D3DReadFileToBlob Failed. path = %ls", vsPath.c_str());
			return false;
		}

		hr = D3DReadFileToBlob(psPath.c_str(), pPSBlob.GetAddressOf());
		if (FAILED(hr))
		{
			ELOG("Error : D3DReadFileToBlob Failed. path = %ls", psPath.c_str());
			return false;
		}

		ComPtr<ID3DBlob> pRSBlob;
		hr = D3DGetBlobPart(pPSBlob->GetBufferPointer(), pPSBlob->GetBufferSize(), D3D_BLOB_ROOT_SIGNATURE, 0, &pRSBlob);
		if (FAILED(hr))
		{
			ELOG("Error : D3DGetBlobPart Failed. path = %ls", psPath.c_str());
			return false;
		}

		if (!m_CameraVelocityRootSig.Init(m_pDevice.Get(), pRSBlob))
		{
			ELOG("Error : RootSignature::Init() Failed.");
			return false;
		}

		D3D12_GRAPHICS_PIPELINE_STATE_DESC desc = SSPassPSODescCommon;
		desc.pRootSignature = m_CameraVelocityRootSig.GetPtr();
		desc.VS.pShaderBytecode = pVSBlob->GetBufferPointer();
		desc.VS.BytecodeLength = pVSBlob->GetBufferSize();
		desc.PS.pShaderBytecode = pPSBlob->GetBufferPointer();
		desc.PS.BytecodeLength = pPSBlob->GetBufferSize();
		desc.RTVFormats[0] = m_VelocityTarget.GetRTVDesc().Format;

		hr = m_pDevice->CreateGraphicsPipelineState(
			&desc,
			IID_PPV_ARGS(m_pCameraVelocityPSO.GetAddressOf())
		);
		if (FAILED(hr))
		{
			ELOG("Error : ID3D12Device::CreateGraphicsPipelineState Failed. retcode = 0x%x", hr);
			return false;
		}
	}

    // SSAO準備パス用ルートシグニチャとパイプラインステートの生成
	{
		std::wstring vsPath;
		std::wstring psPath;

		if (!SearchFilePath(L"QuadVS.cso", vsPath))
		{
			ELOG("Error : Vertex Shader Not Found");
			return false;
		}

		if (!SearchFilePath(L"SSAOSetup_PS.cso", psPath))
		{
			ELOG("Error : Pixel Shader Not Found");
			return false;
		}

		ComPtr<ID3DBlob> pVSBlob;
		ComPtr<ID3DBlob> pPSBlob;

		HRESULT hr = D3DReadFileToBlob(vsPath.c_str(), pVSBlob.GetAddressOf());
		if (FAILED(hr))
		{
			ELOG("Error : D3DReadFileToBlob Failed. path = %ls", vsPath.c_str());
			return false;
		}

		hr = D3DReadFileToBlob(psPath.c_str(), pPSBlob.GetAddressOf());
		if (FAILED(hr))
		{
			ELOG("Error : D3DReadFileToBlob Failed. path = %ls", psPath.c_str());
			return false;
		}

		ComPtr<ID3DBlob> pRSBlob;
		hr = D3DGetBlobPart(pPSBlob->GetBufferPointer(), pPSBlob->GetBufferSize(), D3D_BLOB_ROOT_SIGNATURE, 0, &pRSBlob);
		if (FAILED(hr))
		{
			ELOG("Error : D3DGetBlobPart Failed. path = %ls", psPath.c_str());
			return false;
		}

		if (!m_SSAOSetupRootSig.Init(m_pDevice.Get(), pRSBlob))
		{
			ELOG("Error : RootSignature::Init() Failed.");
			return false;
		}

		D3D12_GRAPHICS_PIPELINE_STATE_DESC desc = SSPassPSODescCommon;
		desc.pRootSignature = m_SSAOSetupRootSig.GetPtr();
		desc.VS.pShaderBytecode = pVSBlob->GetBufferPointer();
		desc.VS.BytecodeLength = pVSBlob->GetBufferSize();
		desc.PS.pShaderBytecode = pPSBlob->GetBufferPointer();
		desc.PS.BytecodeLength = pPSBlob->GetBufferSize();
		desc.RTVFormats[0] = m_SSAOSetupTarget.GetRTVDesc().Format;

		hr = m_pDevice->CreateGraphicsPipelineState(
			&desc,
			IID_PPV_ARGS(m_pSSAOSetupPSO.GetAddressOf())
		);
		if (FAILED(hr))
		{
			ELOG("Error : ID3D12Device::CreateGraphicsPipelineState Failed. retcode = 0x%x", hr);
			return false;
		}
	}

    // SSAO用ルートシグニチャとパイプラインステートの生成
	{
		std::wstring vsPath;
		std::wstring psPath;

		if (!SearchFilePath(L"QuadVS.cso", vsPath))
		{
			ELOG("Error : Vertex Shader Not Found");
			return false;
		}

		if (!SearchFilePath(L"SSAO_PS.cso", psPath))
		{
			ELOG("Error : Pixel Shader Not Found");
			return false;
		}

		ComPtr<ID3DBlob> pVSBlob;
		ComPtr<ID3DBlob> pPSBlob;

		HRESULT hr = D3DReadFileToBlob(vsPath.c_str(), pVSBlob.GetAddressOf());
		if (FAILED(hr))
		{
			ELOG("Error : D3DReadFileToBlob Failed. path = %ls", vsPath.c_str());
			return false;
		}

		hr = D3DReadFileToBlob(psPath.c_str(), pPSBlob.GetAddressOf());
		if (FAILED(hr))
		{
			ELOG("Error : D3DReadFileToBlob Failed. path = %ls", psPath.c_str());
			return false;
		}

		ComPtr<ID3DBlob> pRSBlob;
		hr = D3DGetBlobPart(pPSBlob->GetBufferPointer(), pPSBlob->GetBufferSize(), D3D_BLOB_ROOT_SIGNATURE, 0, &pRSBlob);
		if (FAILED(hr))
		{
			ELOG("Error : D3DGetBlobPart Failed. path = %ls", psPath.c_str());
			return false;
		}

		if (!m_SSAO_RootSig.Init(m_pDevice.Get(), pRSBlob))
		{
			ELOG("Error : RootSignature::Init() Failed.");
			return false;
		}

		D3D12_GRAPHICS_PIPELINE_STATE_DESC desc = SSPassPSODescCommon;
		desc.pRootSignature = m_SSAO_RootSig.GetPtr();
		desc.VS.pShaderBytecode = pVSBlob->GetBufferPointer();
		desc.VS.BytecodeLength = pVSBlob->GetBufferSize();
		desc.PS.pShaderBytecode = pPSBlob->GetBufferPointer();
		desc.PS.BytecodeLength = pPSBlob->GetBufferSize();
		desc.RTVFormats[0] = m_SSAO_FullResTarget.GetRTVDesc().Format;

		hr = m_pDevice->CreateGraphicsPipelineState(
			&desc,
			IID_PPV_ARGS(m_pSSAO_PSO.GetAddressOf())
		);
		if (FAILED(hr))
		{
			ELOG("Error : ID3D12Device::CreateGraphicsPipelineState Failed. retcode = 0x%x", hr);
			return false;
		}
	}

    // SSGI用ルートシグニチャとパイプラインステートの生成
	{
		std::wstring csPath;

		if (!SearchFilePath(L"SSGI_CS.cso", csPath))
		{
			ELOG("Error : Compute Shader Not Found");
			return false;
		}

		ComPtr<ID3DBlob> pCSBlob;

		HRESULT hr = D3DReadFileToBlob(csPath.c_str(), pCSBlob.GetAddressOf());
		if (FAILED(hr))
		{
			ELOG("Error : D3DReadFileToBlob Failed. path = %ls", csPath.c_str());
			return false;
		}

		ComPtr<ID3DBlob> pRSBlob;
		hr = D3DGetBlobPart(pCSBlob->GetBufferPointer(), pCSBlob->GetBufferSize(), D3D_BLOB_ROOT_SIGNATURE, 0, &pRSBlob);
		if (FAILED(hr))
		{
			ELOG("Error : D3DGetBlobPart Failed. path = %ls", csPath.c_str());
			return false;
		}

		if (!m_SSGI_RootSig.Init(m_pDevice.Get(), pRSBlob))
		{
			ELOG("Error : RootSignature::Init() Failed.");
			return false;
		}

		D3D12_COMPUTE_PIPELINE_STATE_DESC desc = {};
		desc.pRootSignature = m_SSGI_RootSig.GetPtr();
		desc.CS.pShaderBytecode = pCSBlob->GetBufferPointer();
		desc.CS.BytecodeLength = pCSBlob->GetBufferSize();
		desc.NodeMask = 0;
		desc.CachedPSO.pCachedBlob = nullptr;
		desc.CachedPSO.CachedBlobSizeInBytes = 0;
		desc.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;

		hr = m_pDevice->CreateComputePipelineState(
			&desc,
			IID_PPV_ARGS(m_pSSGI_PSO.GetAddressOf())
		);
		if (FAILED(hr))
		{
			ELOG("Error : ID3D12Device::CreateComputePipelineState Failed. retcode = 0x%x", hr);
			return false;
		}
	}

    // SSGIデノイズパス用ルートシグニチャとパイプラインステートの生成
	{
		std::wstring csPath;

		if (!SearchFilePath(L"SSGI_DenoiseCS.cso", csPath))
		{
			ELOG("Error : Compute Shader Not Found");
			return false;
		}

		ComPtr<ID3DBlob> pCSBlob;

		HRESULT hr = D3DReadFileToBlob(csPath.c_str(), pCSBlob.GetAddressOf());
		if (FAILED(hr))
		{
			ELOG("Error : D3DReadFileToBlob Failed. path = %ls", csPath.c_str());
			return false;
		}

		ComPtr<ID3DBlob> pRSBlob;
		hr = D3DGetBlobPart(pCSBlob->GetBufferPointer(), pCSBlob->GetBufferSize(), D3D_BLOB_ROOT_SIGNATURE, 0, &pRSBlob);
		if (FAILED(hr))
		{
			ELOG("Error : D3DGetBlobPart Failed. path = %ls", csPath.c_str());
			return false;
		}

		if (!m_SSGI_DenoiseRootSig.Init(m_pDevice.Get(), pRSBlob))
		{
			ELOG("Error : RootSignature::Init() Failed.");
			return false;
		}

		D3D12_COMPUTE_PIPELINE_STATE_DESC desc = {};
		desc.pRootSignature = m_SSGI_DenoiseRootSig.GetPtr();
		desc.CS.pShaderBytecode = pCSBlob->GetBufferPointer();
		desc.CS.BytecodeLength = pCSBlob->GetBufferSize();
		desc.NodeMask = 0;
		desc.CachedPSO.pCachedBlob = nullptr;
		desc.CachedPSO.CachedBlobSizeInBytes = 0;
		desc.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;

		hr = m_pDevice->CreateComputePipelineState(
			&desc,
			IID_PPV_ARGS(m_pSSGI_DenoisePSO.GetAddressOf())
		);
		if (FAILED(hr))
		{
			ELOG("Error : ID3D12Device::CreateComputePipelineState Failed. retcode = 0x%x", hr);
			return false;
		}
	}

    // SSGI Temporal Acclumulationパス用ルートシグニチャとパイプラインステートの生成
	{
		std::wstring csPath;

		if (!SearchFilePath(L"SSGI_TemporalAccumulationCS.cso", csPath))
		{
			ELOG("Error : Compute Shader Not Found");
			return false;
		}

		ComPtr<ID3DBlob> pCSBlob;

		HRESULT hr = D3DReadFileToBlob(csPath.c_str(), pCSBlob.GetAddressOf());
		if (FAILED(hr))
		{
			ELOG("Error : D3DReadFileToBlob Failed. path = %ls", csPath.c_str());
			return false;
		}

		ComPtr<ID3DBlob> pRSBlob;
		hr = D3DGetBlobPart(pCSBlob->GetBufferPointer(), pCSBlob->GetBufferSize(), D3D_BLOB_ROOT_SIGNATURE, 0, &pRSBlob);
		if (FAILED(hr))
		{
			ELOG("Error : D3DGetBlobPart Failed. path = %ls", csPath.c_str());
			return false;
		}

		if (!m_SSGI_TemporalAccumulationRootSig.Init(m_pDevice.Get(), pRSBlob))
		{
			ELOG("Error : RootSignature::Init() Failed.");
			return false;
		}

		D3D12_COMPUTE_PIPELINE_STATE_DESC desc = {};
		desc.pRootSignature = m_SSGI_TemporalAccumulationRootSig.GetPtr();
		desc.CS.pShaderBytecode = pCSBlob->GetBufferPointer();
		desc.CS.BytecodeLength = pCSBlob->GetBufferSize();
		desc.NodeMask = 0;
		desc.CachedPSO.pCachedBlob = nullptr;
		desc.CachedPSO.CachedBlobSizeInBytes = 0;
		desc.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;

		hr = m_pDevice->CreateComputePipelineState(
			&desc,
			IID_PPV_ARGS(m_pSSGI_TemporalAccumulationPSO.GetAddressOf())
		);
		if (FAILED(hr))
		{
			ELOG("Error : ID3D12Device::CreateComputePipelineState Failed. retcode = 0x%x", hr);
			return false;
		}
	}

    // AmbientLight用ルートシグニチャとパイプラインステートの生成
	{
		std::wstring vsPath;
		std::wstring psPath;

		if (!SearchFilePath(L"QuadVS.cso", vsPath))
		{
			ELOG("Error : Vertex Shader Not Found");
			return false;
		}

		if (!SearchFilePath(L"AmbientLightPS.cso", psPath))
		{
			ELOG("Error : Pixel Shader Not Found");
			return false;
		}

		ComPtr<ID3DBlob> pVSBlob;
		ComPtr<ID3DBlob> pPSBlob;

		HRESULT hr = D3DReadFileToBlob(vsPath.c_str(), pVSBlob.GetAddressOf());
		if (FAILED(hr))
		{
			ELOG("Error : D3DReadFileToBlob Failed. path = %ls", vsPath.c_str());
			return false;
		}

		hr = D3DReadFileToBlob(psPath.c_str(), pPSBlob.GetAddressOf());
		if (FAILED(hr))
		{
			ELOG("Error : D3DReadFileToBlob Failed. path = %ls", psPath.c_str());
			return false;
		}

		ComPtr<ID3DBlob> pRSBlob;
		hr = D3DGetBlobPart(pPSBlob->GetBufferPointer(), pPSBlob->GetBufferSize(), D3D_BLOB_ROOT_SIGNATURE, 0, &pRSBlob);
		if (FAILED(hr))
		{
			ELOG("Error : D3DGetBlobPart Failed. path = %ls", psPath.c_str());
			return false;
		}

		if (!m_AmbientLightRootSig.Init(m_pDevice.Get(), pRSBlob))
		{
			ELOG("Error : RootSignature::Init() Failed.");
			return false;
		}

		D3D12_GRAPHICS_PIPELINE_STATE_DESC desc = SSPassPSODescCommon;
		desc.pRootSignature = m_AmbientLightRootSig.GetPtr();
		desc.VS.pShaderBytecode = pVSBlob->GetBufferPointer();
		desc.VS.BytecodeLength = pVSBlob->GetBufferSize();
		desc.PS.pShaderBytecode = pPSBlob->GetBufferPointer();
		desc.PS.BytecodeLength = pPSBlob->GetBufferSize();
		desc.RTVFormats[0] = m_AmbientLightTarget.GetRTVDesc().Format;

		hr = m_pDevice->CreateGraphicsPipelineState(
			&desc,
			IID_PPV_ARGS(m_pAmbientLightPSO.GetAddressOf())
		);
		if (FAILED(hr))
		{
			ELOG("Error : ID3D12Device::CreateGraphicsPipelineState Failed. retcode = 0x%x", hr);
			return false;
		}
	}

    // SSR用ルートシグニチャとパイプラインステートの生成
	{
		std::wstring vsPath;
		std::wstring psPath;

		if (!SearchFilePath(L"QuadVS.cso", vsPath))
		{
			ELOG("Error : Vertex Shader Not Found");
			return false;
		}

		if (!SearchFilePath(L"SSR_PS.cso", psPath))
		{
			ELOG("Error : Pixel Shader Not Found");
			return false;
		}

		ComPtr<ID3DBlob> pVSBlob;
		ComPtr<ID3DBlob> pPSBlob;

		HRESULT hr = D3DReadFileToBlob(vsPath.c_str(), pVSBlob.GetAddressOf());
		if (FAILED(hr))
		{
			ELOG("Error : D3DReadFileToBlob Failed. path = %ls", vsPath.c_str());
			return false;
		}

		hr = D3DReadFileToBlob(psPath.c_str(), pPSBlob.GetAddressOf());
		if (FAILED(hr))
		{
			ELOG("Error : D3DReadFileToBlob Failed. path = %ls", psPath.c_str());
			return false;
		}

		ComPtr<ID3DBlob> pRSBlob;
		hr = D3DGetBlobPart(pPSBlob->GetBufferPointer(), pPSBlob->GetBufferSize(), D3D_BLOB_ROOT_SIGNATURE, 0, &pRSBlob);
		if (FAILED(hr))
		{
			ELOG("Error : D3DGetBlobPart Failed. path = %ls", psPath.c_str());
			return false;
		}

		if (!m_SSR_RootSig.Init(m_pDevice.Get(), pRSBlob))
		{
			ELOG("Error : RootSignature::Init() Failed.");
			return false;
		}

		D3D12_GRAPHICS_PIPELINE_STATE_DESC desc = SSPassPSODescCommon;
		desc.pRootSignature = m_SSR_RootSig.GetPtr();
		desc.VS.pShaderBytecode = pVSBlob->GetBufferPointer();
		desc.VS.BytecodeLength = pVSBlob->GetBufferSize();
		desc.PS.pShaderBytecode = pPSBlob->GetBufferPointer();
		desc.PS.BytecodeLength = pPSBlob->GetBufferSize();
		desc.RTVFormats[0] = m_SSR_Target.GetRTVDesc().Format;

		hr = m_pDevice->CreateGraphicsPipelineState(
			&desc,
			IID_PPV_ARGS(m_pSSR_PSO.GetAddressOf())
		);
		if (FAILED(hr))
		{
			ELOG("Error : ID3D12Device::CreateGraphicsPipelineState Failed. retcode = 0x%x", hr);
			return false;
		}
	}

    // VolumetricFog Scattering用ルートシグニチャとパイプラインステートの生成
	{
		std::wstring csPath;

		if (!SearchFilePath(L"VolumetricFogScatteringCS.cso", csPath))
		{
			ELOG("Error : Compute Shader Not Found");
			return false;
		}

		ComPtr<ID3DBlob> pCSBlob;

		HRESULT hr = D3DReadFileToBlob(csPath.c_str(), pCSBlob.GetAddressOf());
		if (FAILED(hr))
		{
			ELOG("Error : D3DReadFileToBlob Failed. path = %ls", csPath.c_str());
			return false;
		}

		ComPtr<ID3DBlob> pRSBlob;
		hr = D3DGetBlobPart(pCSBlob->GetBufferPointer(), pCSBlob->GetBufferSize(), D3D_BLOB_ROOT_SIGNATURE, 0, &pRSBlob);
		if (FAILED(hr))
		{
			ELOG("Error : D3DGetBlobPart Failed. path = %ls", csPath.c_str());
			return false;
		}

		if (!m_VolumetricFogScatteringRootSig.Init(m_pDevice.Get(), pRSBlob))
		{
			ELOG("Error : RootSignature::Init() Failed.");
			return false;
		}

		D3D12_COMPUTE_PIPELINE_STATE_DESC desc = {};
		desc.pRootSignature = m_VolumetricFogScatteringRootSig.GetPtr();
		desc.CS.pShaderBytecode = pCSBlob->GetBufferPointer();
		desc.CS.BytecodeLength = pCSBlob->GetBufferSize();
		desc.NodeMask = 0;
		desc.CachedPSO.pCachedBlob = nullptr;
		desc.CachedPSO.CachedBlobSizeInBytes = 0;
		desc.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;

		hr = m_pDevice->CreateComputePipelineState(
			&desc,
			IID_PPV_ARGS(m_pVolumetricFogScatteringPSO.GetAddressOf())
		);
		if (FAILED(hr))
		{
			ELOG("Error : ID3D12Device::CreateComputePipelineState Failed. retcode = 0x%x", hr);
			return false;
		}
	}

    // VolumetricFog Integration用ルートシグニチャとパイプラインステートの生成
	{
		std::wstring csPath;

		if (!SearchFilePath(L"VolumetricFogIntegrationCS.cso", csPath))
		{
			ELOG("Error : Compute Shader Not Found");
			return false;
		}

		ComPtr<ID3DBlob> pCSBlob;

		HRESULT hr = D3DReadFileToBlob(csPath.c_str(), pCSBlob.GetAddressOf());
		if (FAILED(hr))
		{
			ELOG("Error : D3DReadFileToBlob Failed. path = %ls", csPath.c_str());
			return false;
		}

		ComPtr<ID3DBlob> pRSBlob;
		hr = D3DGetBlobPart(pCSBlob->GetBufferPointer(), pCSBlob->GetBufferSize(), D3D_BLOB_ROOT_SIGNATURE, 0, &pRSBlob);
		if (FAILED(hr))
		{
			ELOG("Error : D3DGetBlobPart Failed. path = %ls", csPath.c_str());
			return false;
		}

		if (!m_VolumetricFogIntegrationRootSig.Init(m_pDevice.Get(), pRSBlob))
		{
			ELOG("Error : RootSignature::Init() Failed.");
			return false;
		}

		D3D12_COMPUTE_PIPELINE_STATE_DESC desc = {};
		desc.pRootSignature = m_VolumetricFogIntegrationRootSig.GetPtr();
		desc.CS.pShaderBytecode = pCSBlob->GetBufferPointer();
		desc.CS.BytecodeLength = pCSBlob->GetBufferSize();
		desc.NodeMask = 0;
		desc.CachedPSO.pCachedBlob = nullptr;
		desc.CachedPSO.CachedBlobSizeInBytes = 0;
		desc.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;

		hr = m_pDevice->CreateComputePipelineState(
			&desc,
			IID_PPV_ARGS(m_pVolumetricFogIntegrationPSO.GetAddressOf())
		);
		if (FAILED(hr))
		{
			ELOG("Error : ID3D12Device::CreateComputePipelineState Failed. retcode = 0x%x", hr);
			return false;
		}
	}

    // VolumetricFog Composition用パイプラインステートの生成
	{
		std::wstring vsPath;
		std::wstring psPath;

		if (!SearchFilePath(L"QuadVS.cso", vsPath))
		{
			ELOG("Error : Vertex Shader Not Found");
			return false;
		}

		if (!SearchFilePath(L"VolumetricFogCompositionPS.cso", psPath))
		{
			ELOG("Error : Pixel Shader Not Found");
			return false;
		}

		ComPtr<ID3DBlob> pVSBlob;
		ComPtr<ID3DBlob> pPSBlob;

		HRESULT hr = D3DReadFileToBlob(vsPath.c_str(), pVSBlob.GetAddressOf());
		if (FAILED(hr))
		{
			ELOG("Error : D3DReadFileToBlob Failed. path = %ls", vsPath.c_str());
			return false;
		}

		hr = D3DReadFileToBlob(psPath.c_str(), pPSBlob.GetAddressOf());
		if (FAILED(hr))
		{
			ELOG("Error : D3DReadFileToBlob Failed. path = %ls", psPath.c_str());
			return false;
		}

		ComPtr<ID3DBlob> pRSBlob;
		hr = D3DGetBlobPart(pPSBlob->GetBufferPointer(), pPSBlob->GetBufferSize(), D3D_BLOB_ROOT_SIGNATURE, 0, &pRSBlob);
		if (FAILED(hr))
		{
			ELOG("Error : D3DGetBlobPart Failed. path = %ls", psPath.c_str());
			return false;
		}

		if (!m_VolumetricFogCompositionRootSig.Init(m_pDevice.Get(), pRSBlob))
		{
			ELOG("Error : RootSignature::Init() Failed.");
			return false;
		}

		D3D12_GRAPHICS_PIPELINE_STATE_DESC desc = SSPassPSODescCommon;
		desc.pRootSignature = m_VolumetricFogCompositionRootSig.GetPtr();
		desc.VS.pShaderBytecode = pVSBlob->GetBufferPointer();
		desc.VS.BytecodeLength = pVSBlob->GetBufferSize();
		desc.PS.pShaderBytecode = pPSBlob->GetBufferPointer();
		desc.PS.BytecodeLength = pPSBlob->GetBufferSize();
		desc.RTVFormats[0] = m_VolumetricCompositionTarget.GetRTVDesc().Format;

		hr = m_pDevice->CreateGraphicsPipelineState(
			&desc,
			IID_PPV_ARGS(m_pVolumetricFogCompositionPSO.GetAddressOf())
		);
		if (FAILED(hr))
		{
			ELOG("Error : ID3D12Device::CreateGraphicsPipelineState Failed. retcode = 0x%x", hr);
			return false;
		}
	}

    // TemporalAA用ルートシグニチャとパイプラインステートの生成
	{
		std::wstring csPath;

		if (!SearchFilePath(L"TemporalAA_CS.cso", csPath))
		{
			ELOG("Error : Compute Shader Not Found");
			return false;
		}

		ComPtr<ID3DBlob> pCSBlob;

		HRESULT hr = D3DReadFileToBlob(csPath.c_str(), pCSBlob.GetAddressOf());
		if (FAILED(hr))
		{
			ELOG("Error : D3DReadFileToBlob Failed. path = %ls", csPath.c_str());
			return false;
		}

		ComPtr<ID3DBlob> pRSBlob;
		hr = D3DGetBlobPart(pCSBlob->GetBufferPointer(), pCSBlob->GetBufferSize(), D3D_BLOB_ROOT_SIGNATURE, 0, &pRSBlob);
		if (FAILED(hr))
		{
			ELOG("Error : D3DGetBlobPart Failed. path = %ls", csPath.c_str());
			return false;
		}

		if (!m_TemporalAA_RootSig.Init(m_pDevice.Get(), pRSBlob))
		{
			ELOG("Error : RootSignature::Init() Failed.");
			return false;
		}

		D3D12_COMPUTE_PIPELINE_STATE_DESC desc = {};
		desc.pRootSignature = m_TemporalAA_RootSig.GetPtr();
		desc.CS.pShaderBytecode = pCSBlob->GetBufferPointer();
		desc.CS.BytecodeLength = pCSBlob->GetBufferSize();
		desc.NodeMask = 0;
		desc.CachedPSO.pCachedBlob = nullptr;
		desc.CachedPSO.CachedBlobSizeInBytes = 0;
		desc.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;

		hr = m_pDevice->CreateComputePipelineState(
			&desc,
			IID_PPV_ARGS(m_pTemporalAA_PSO.GetAddressOf())
		);
		if (FAILED(hr))
		{
			ELOG("Error : ID3D12Device::CreateComputePipelineState Failed. retcode = 0x%x", hr);
			return false;
		}
	}

    // MotionBlur用ルートシグニチャとパイプラインステートの生成
	{
		std::wstring vsPath;
		std::wstring psPath;

		if (!SearchFilePath(L"QuadVS.cso", vsPath))
		{
			ELOG("Error : Vertex Shader Not Found");
			return false;
		}

		if (!SearchFilePath(L"MotionBlurPS.cso", psPath))
		{
			ELOG("Error : Pixel Shader Not Found");
			return false;
		}

		ComPtr<ID3DBlob> pVSBlob;
		ComPtr<ID3DBlob> pPSBlob;

		HRESULT hr = D3DReadFileToBlob(vsPath.c_str(), pVSBlob.GetAddressOf());
		if (FAILED(hr))
		{
			ELOG("Error : D3DReadFileToBlob Failed. path = %ls", vsPath.c_str());
			return false;
		}

		hr = D3DReadFileToBlob(psPath.c_str(), pPSBlob.GetAddressOf());
		if (FAILED(hr))
		{
			ELOG("Error : D3DReadFileToBlob Failed. path = %ls", psPath.c_str());
			return false;
		}

		ComPtr<ID3DBlob> pRSBlob;
		hr = D3DGetBlobPart(pPSBlob->GetBufferPointer(), pPSBlob->GetBufferSize(), D3D_BLOB_ROOT_SIGNATURE, 0, &pRSBlob);
		if (FAILED(hr))
		{
			ELOG("Error : D3DGetBlobPart Failed. path = %ls", psPath.c_str());
			return false;
		}

		if (!m_MotionBlurRootSig.Init(m_pDevice.Get(), pRSBlob))
		{
			ELOG("Error : RootSignature::Init() Failed.");
			return false;
		}

		D3D12_GRAPHICS_PIPELINE_STATE_DESC desc = SSPassPSODescCommon;
		desc.pRootSignature = m_MotionBlurRootSig.GetPtr();
		desc.VS.pShaderBytecode = pVSBlob->GetBufferPointer();
		desc.VS.BytecodeLength = pVSBlob->GetBufferSize();
		desc.PS.pShaderBytecode = pPSBlob->GetBufferPointer();
		desc.PS.BytecodeLength = pPSBlob->GetBufferSize();
		desc.RTVFormats[0] = m_MotionBlurTarget.GetRTVDesc().Format;

		hr = m_pDevice->CreateGraphicsPipelineState(
			&desc,
			IID_PPV_ARGS(m_pMotionBlurPSO.GetAddressOf())
		);
		if (FAILED(hr))
		{
			ELOG("Error : ID3D12Device::CreateGraphicsPipelineState Failed. retcode = 0x%x", hr);
			return false;
		}
	}

    // Bloom前工程用ルートシグニチャとパイプラインステートの生成
	{
		std::wstring vsPath;
		std::wstring psPath;

		if (!SearchFilePath(L"QuadVS.cso", vsPath))
		{
			ELOG("Error : Vertex Shader Not Found");
			return false;
		}

		if (!SearchFilePath(L"BloomSetupPS.cso", psPath))
		{
			ELOG("Error : Pixel Shader Not Found");
			return false;
		}

		ComPtr<ID3DBlob> pVSBlob;
		ComPtr<ID3DBlob> pPSBlob;

		HRESULT hr = D3DReadFileToBlob(vsPath.c_str(), pVSBlob.GetAddressOf());
		if (FAILED(hr))
		{
			ELOG("Error : D3DReadFileToBlob Failed. path = %ls", vsPath.c_str());
			return false;
		}

		hr = D3DReadFileToBlob(psPath.c_str(), pPSBlob.GetAddressOf());
		if (FAILED(hr))
		{
			ELOG("Error : D3DReadFileToBlob Failed. path = %ls", psPath.c_str());
			return false;
		}

		ComPtr<ID3DBlob> pRSBlob;
		hr = D3DGetBlobPart(pPSBlob->GetBufferPointer(), pPSBlob->GetBufferSize(), D3D_BLOB_ROOT_SIGNATURE, 0, &pRSBlob);
		if (FAILED(hr))
		{
			ELOG("Error : D3DGetBlobPart Failed. path = %ls", psPath.c_str());
			return false;
		}

		if (!m_BloomSetupRootSig.Init(m_pDevice.Get(), pRSBlob))
		{
			ELOG("Error : RootSignature::Init() Failed.");
			return false;
		}

		D3D12_GRAPHICS_PIPELINE_STATE_DESC desc = SSPassPSODescCommon;
		desc.pRootSignature = m_BloomSetupRootSig.GetPtr();
		desc.VS.pShaderBytecode = pVSBlob->GetBufferPointer();
		desc.VS.BytecodeLength = pVSBlob->GetBufferSize();
		desc.PS.pShaderBytecode = pPSBlob->GetBufferPointer();
		desc.PS.BytecodeLength = pPSBlob->GetBufferSize();
		desc.RTVFormats[0] = m_BloomSetupTarget[0].GetRTVDesc().Format;

		hr = m_pDevice->CreateGraphicsPipelineState(
			&desc,
			IID_PPV_ARGS(m_pBloomSetupPSO.GetAddressOf())
		);
		if (FAILED(hr))
		{
			ELOG("Error : ID3D12Device::CreateGraphicsPipelineState Failed. retcode = 0x%x", hr);
			return false;
		}
	}

    // トーンマップ用ルートシグニチャとパイプラインステートの生成
	{
		std::wstring vsPath;
		std::wstring psPath;

		if (!SearchFilePath(L"QuadVS.cso", vsPath))
		{
			ELOG("Error : Vertex Shader Not Found");
			return false;
		}

		if (!SearchFilePath(L"TonemapPS.cso", psPath))
		{
			ELOG("Error : Pixel Shader Not Found");
			return false;
		}

		ComPtr<ID3DBlob> pVSBlob;
		ComPtr<ID3DBlob> pPSBlob;

		HRESULT hr = D3DReadFileToBlob(vsPath.c_str(), pVSBlob.GetAddressOf());
		if (FAILED(hr))
		{
			ELOG("Error : D3DReadFileToBlob Failed. path = %ls", vsPath.c_str());
			return false;
		}

		hr = D3DReadFileToBlob(psPath.c_str(), pPSBlob.GetAddressOf());
		if (FAILED(hr))
		{
			ELOG("Error : D3DReadFileToBlob Failed. path = %ls", psPath.c_str());
			return false;
		}

		ComPtr<ID3DBlob> pRSBlob;
		hr = D3DGetBlobPart(pPSBlob->GetBufferPointer(), pPSBlob->GetBufferSize(), D3D_BLOB_ROOT_SIGNATURE, 0, &pRSBlob);
		if (FAILED(hr))
		{
			ELOG("Error : D3DGetBlobPart Failed. path = %ls", psPath.c_str());
			return false;
		}

		if (!m_TonemapRootSig.Init(m_pDevice.Get(), pRSBlob))
		{
			ELOG("Error : RootSignature::Init() Failed.");
			return false;
		}

		D3D12_GRAPHICS_PIPELINE_STATE_DESC desc = SSPassPSODescCommon;
		desc.pRootSignature = m_TonemapRootSig.GetPtr();
		desc.VS.pShaderBytecode = pVSBlob->GetBufferPointer();
		desc.VS.BytecodeLength = pVSBlob->GetBufferSize();
		desc.PS.pShaderBytecode = pPSBlob->GetBufferPointer();
		desc.PS.BytecodeLength = pPSBlob->GetBufferSize();
		desc.RTVFormats[0] = m_TonemapTarget.GetRTVDesc().Format;

		hr = m_pDevice->CreateGraphicsPipelineState(
			&desc,
			IID_PPV_ARGS(m_pTonemapPSO.GetAddressOf())
		);
		if (FAILED(hr))
		{
			ELOG("Error : ID3D12Device::CreateGraphicsPipelineState Failed. retcode = 0x%x", hr);
			return false;
		}
	}

    // FXAA用ルートシグニチャとパイプラインステートの生成
	{
		std::wstring vsPath;
		std::wstring psPath;

		if (!SearchFilePath(L"QuadVS.cso", vsPath))
		{
			ELOG("Error : Vertex Shader Not Found");
			return false;
		}

		if (!SearchFilePath(L"FXAA_PS.cso", psPath))
		{
			ELOG("Error : Pixel Shader Not Found");
			return false;
		}

		ComPtr<ID3DBlob> pVSBlob;
		ComPtr<ID3DBlob> pPSBlob;

		HRESULT hr = D3DReadFileToBlob(vsPath.c_str(), pVSBlob.GetAddressOf());
		if (FAILED(hr))
		{
			ELOG("Error : D3DReadFileToBlob Failed. path = %ls", vsPath.c_str());
			return false;
		}

		hr = D3DReadFileToBlob(psPath.c_str(), pPSBlob.GetAddressOf());
		if (FAILED(hr))
		{
			ELOG("Error : D3DReadFileToBlob Failed. path = %ls", psPath.c_str());
			return false;
		}

		ComPtr<ID3DBlob> pRSBlob;
		hr = D3DGetBlobPart(pPSBlob->GetBufferPointer(), pPSBlob->GetBufferSize(), D3D_BLOB_ROOT_SIGNATURE, 0, &pRSBlob);
		if (FAILED(hr))
		{
			ELOG("Error : D3DGetBlobPart Failed. path = %ls", psPath.c_str());
			return false;
		}

		if (!m_FXAA_RootSig.Init(m_pDevice.Get(), pRSBlob))
		{
			ELOG("Error : RootSignature::Init() Failed.");
			return false;
		}

		D3D12_GRAPHICS_PIPELINE_STATE_DESC desc = SSPassPSODescCommon;
		desc.pRootSignature = m_FXAA_RootSig.GetPtr();
		desc.VS.pShaderBytecode = pVSBlob->GetBufferPointer();
		desc.VS.BytecodeLength = pVSBlob->GetBufferSize();
		desc.PS.pShaderBytecode = pPSBlob->GetBufferPointer();
		desc.PS.BytecodeLength = pPSBlob->GetBufferSize();
		desc.RTVFormats[0] = m_FXAA_Target.GetRTVDesc().Format;

		hr = m_pDevice->CreateGraphicsPipelineState(
			&desc,
			IID_PPV_ARGS(m_pFXAA_PSO.GetAddressOf())
		);
		if (FAILED(hr))
		{
			ELOG("Error : ID3D12Device::CreateGraphicsPipelineState Failed. retcode = 0x%x", hr);
			return false;
		}
	}

    // 汎用ダウンサンプルパス用ルートシグニチャとパイプラインステートの生成
	{
		std::wstring vsPath;
		std::wstring psPath;

		if (!SearchFilePath(L"QuadVS.cso", vsPath))
		{
			ELOG("Error : Vertex Shader Not Found");
			return false;
		}

		if (!SearchFilePath(L"DownsamplePS.cso", psPath))
		{
			ELOG("Error : Pixel Shader Not Found");
			return false;
		}

		ComPtr<ID3DBlob> pVSBlob;
		ComPtr<ID3DBlob> pPSBlob;

		HRESULT hr = D3DReadFileToBlob(vsPath.c_str(), pVSBlob.GetAddressOf());
		if (FAILED(hr))
		{
			ELOG("Error : D3DReadFileToBlob Failed. path = %ls", vsPath.c_str());
			return false;
		}

		hr = D3DReadFileToBlob(psPath.c_str(), pPSBlob.GetAddressOf());
		if (FAILED(hr))
		{
			ELOG("Error : D3DReadFileToBlob Failed. path = %ls", psPath.c_str());
			return false;
		}

		ComPtr<ID3DBlob> pRSBlob;
		hr = D3DGetBlobPart(pPSBlob->GetBufferPointer(), pPSBlob->GetBufferSize(), D3D_BLOB_ROOT_SIGNATURE, 0, &pRSBlob);
		if (FAILED(hr))
		{
			ELOG("Error : D3DGetBlobPart Failed. path = %ls", psPath.c_str());
			return false;
		}

		if (!m_DownsampleRootSig.Init(m_pDevice.Get(), pRSBlob))
		{
			ELOG("Error : RootSignature::Init() Failed.");
			return false;
		}

		D3D12_GRAPHICS_PIPELINE_STATE_DESC desc = SSPassPSODescCommon;
		desc.pRootSignature = m_DownsampleRootSig.GetPtr();
		desc.VS.pShaderBytecode = pVSBlob->GetBufferPointer();
		desc.VS.BytecodeLength = pVSBlob->GetBufferSize();
		desc.PS.pShaderBytecode = pPSBlob->GetBufferPointer();
		desc.PS.BytecodeLength = pPSBlob->GetBufferSize();
		desc.RTVFormats[0] = m_BloomSetupTarget[0].GetRTVDesc().Format; // TODO:フォーマットの指定は必要なのでとりあえず

		hr = m_pDevice->CreateGraphicsPipelineState(
			&desc,
			IID_PPV_ARGS(m_pDownsamplePSO.GetAddressOf())
		);
		if (FAILED(hr))
		{
			ELOG("Error : ID3D12Device::CreateGraphicsPipelineState Failed. retcode = 0x%x", hr);
			return false;
		}
	}

    // 汎用フィルタ用ルートシグニチャとパイプラインステートの生成
	{
		std::wstring vsPath;
		std::wstring psPath;

		if (!SearchFilePath(L"QuadVS.cso", vsPath))
		{
			ELOG("Error : Vertex Shader Not Found");
			return false;
		}

		if (!SearchFilePath(L"FilterPS.cso", psPath))
		{
			ELOG("Error : Pixel Shader Not Found");
			return false;
		}

		ComPtr<ID3DBlob> pVSBlob;
		ComPtr<ID3DBlob> pPSBlob;

		HRESULT hr = D3DReadFileToBlob(vsPath.c_str(), pVSBlob.GetAddressOf());
		if (FAILED(hr))
		{
			ELOG("Error : D3DReadFileToBlob Failed. path = %ls", vsPath.c_str());
			return false;
		}

		hr = D3DReadFileToBlob(psPath.c_str(), pPSBlob.GetAddressOf());
		if (FAILED(hr))
		{
			ELOG("Error : D3DReadFileToBlob Failed. path = %ls", psPath.c_str());
			return false;
		}

		ComPtr<ID3DBlob> pRSBlob;
		hr = D3DGetBlobPart(pPSBlob->GetBufferPointer(), pPSBlob->GetBufferSize(), D3D_BLOB_ROOT_SIGNATURE, 0, &pRSBlob);
		if (FAILED(hr))
		{
			ELOG("Error : D3DGetBlobPart Failed. path = %ls", psPath.c_str());
			return false;
		}

		if (!m_FilterRootSig.Init(m_pDevice.Get(), pRSBlob))
		{
			ELOG("Error : RootSignature::Init() Failed.");
			return false;
		}

		D3D12_GRAPHICS_PIPELINE_STATE_DESC desc = SSPassPSODescCommon;
		desc.pRootSignature = m_FilterRootSig.GetPtr();
		desc.VS.pShaderBytecode = pVSBlob->GetBufferPointer();
		desc.VS.BytecodeLength = pVSBlob->GetBufferSize();
		desc.PS.pShaderBytecode = pPSBlob->GetBufferPointer();
		desc.PS.BytecodeLength = pPSBlob->GetBufferSize();
		desc.RTVFormats[0] = m_BloomHorizontalTarget[0].GetRTVDesc().Format; // TODO:フォーマットの指定は必要なのでとりあえず

		hr = m_pDevice->CreateGraphicsPipelineState(
			&desc,
			IID_PPV_ARGS(m_pFilterPSO.GetAddressOf())
		);
		if (FAILED(hr))
		{
			ELOG("Error : ID3D12Device::CreateGraphicsPipelineState Failed. retcode = 0x%x", hr);
			return false;
		}
	}

    // バックバッファ描画用ルートシグニチャとパイプラインステートの生成
	// DXGIフォーマットを指定する必要があるので一般のテクスチャコピー用にはできなかった
	{
		std::wstring vsPath;
		std::wstring psPath;

		if (!SearchFilePath(L"QuadVS.cso", vsPath))
		{
			ELOG("Error : Vertex Shader Not Found");
			return false;
		}

		if (!SearchFilePath(L"SampleTexturePS.cso", psPath))
		{
			ELOG("Error : Pixel Shader Not Found");
			return false;
		}

		ComPtr<ID3DBlob> pVSBlob;
		ComPtr<ID3DBlob> pPSBlob;

		HRESULT hr = D3DReadFileToBlob(vsPath.c_str(), pVSBlob.GetAddressOf());
		if (FAILED(hr))
		{
			ELOG("Error : D3DReadFileToBlob Failed. path = %ls", vsPath.c_str());
			return false;
		}

		hr = D3DReadFileToBlob(psPath.c_str(), pPSBlob.GetAddressOf());
		if (FAILED(hr))
		{
			ELOG("Error : D3DReadFileToBlob Failed. path = %ls", psPath.c_str());
			return false;
		}

		ComPtr<ID3DBlob> pRSBlob;
		hr = D3DGetBlobPart(pPSBlob->GetBufferPointer(), pPSBlob->GetBufferSize(), D3D_BLOB_ROOT_SIGNATURE, 0, &pRSBlob);
		if (FAILED(hr))
		{
			ELOG("Error : D3DGetBlobPart Failed. path = %ls", psPath.c_str());
			return false;
		}

		if (!m_BackBufferRootSig.Init(m_pDevice.Get(), pRSBlob))
		{
			ELOG("Error : RootSignature::Init() Failed.");
			return false;
		}

		D3D12_GRAPHICS_PIPELINE_STATE_DESC desc = SSPassPSODescCommon;
		desc.pRootSignature = m_BackBufferRootSig.GetPtr();
		desc.VS.pShaderBytecode = pVSBlob->GetBufferPointer();
		desc.VS.BytecodeLength = pVSBlob->GetBufferSize();
		desc.PS.pShaderBytecode = pPSBlob->GetBufferPointer();
		desc.PS.BytecodeLength = pPSBlob->GetBufferSize();
		desc.RTVFormats[0] = m_ColorTarget[0].GetRTVDesc().Format;

		hr = m_pDevice->CreateGraphicsPipelineState(
			&desc,
			IID_PPV_ARGS(m_pBackBufferPSO.GetAddressOf())
		);
		if (FAILED(hr))
		{
			ELOG("Error : ID3D12Device::CreateGraphicsPipelineState Failed. retcode = 0x%x", hr);
			return false;
		}
	}

	// スクリーンスペースパス用頂点バッファの生成
	{
		struct Vertex
		{
			float px;
			float py;
			float tx;
			float ty;
		};

		if (!m_QuadVB.Init<Vertex>(m_pDevice.Get(), 3 * sizeof(Vertex)))
		{
			ELOG("Error : VertexBuffer::Init Failed.");
			return false;
		}

		Vertex* ptr = m_QuadVB.Map<Vertex>();
		assert(ptr != nullptr);
		ptr[0].px = -1.0f; ptr[0].py = 1.0f; ptr[0].tx = 0.0f; ptr[0].ty = 0.0f;
		ptr[1].px = 3.0f; ptr[1].py = 1.0f; ptr[1].tx = 2.0f; ptr[1].ty = 0.0f;
		ptr[2].px = -1.0f; ptr[2].py = -3.0f; ptr[2].tx = 0.0f; ptr[2].ty = 2.0f;
		m_QuadVB.Unmap();
	}

	// HCB作成パス用定数バッファの作成
	{
		if (!m_HCB_CB.Init(m_pDevice.Get(), m_pPool[POOL_TYPE_RES], sizeof(CbHZB))) // HZBと同じ構造体
		{
			ELOG("Error : ConstantBuffer::Init() Failed.");
			return false;
		}

		uint32_t mip0SizeX = (uint32_t)m_HCB_Target.GetDesc().Width;
		uint32_t mip0SizeY = (uint32_t)m_HCB_Target.GetDesc().Height;

		CbHZB* ptr = m_HCB_CB.GetPtr<CbHZB>();
		ptr->DstMip0Width = mip0SizeX;
		ptr->DstMip0Height = mip0SizeY;
		ptr->HeightScale = (float)m_Width / m_Height;
		ptr->NumOutputMip = HCB_MAX_NUM_OUTPUT_MIP; // 使わない
	}

	// HZB作成パス用定数バッファの作成
	{
		uint32_t mip0SizeX = (uint32_t)m_HZB_Target.GetDesc().Width;
		uint32_t mip0SizeY = (uint32_t)m_HZB_Target.GetDesc().Height;
		uint32_t numMips = (uint32_t)log2f((float)DirectX::XMMax(mip0SizeX, mip0SizeY));
		uint32_t numDrawCall = DivideAndRoundUp(numMips, HZB_MAX_NUM_OUTPUT_MIP);
		m_pHZB_CBs.reserve(numDrawCall);

		for (uint32_t i = 0; i < numDrawCall; i++)
		{
			ConstantBuffer* cb = new (std::nothrow) ConstantBuffer();
			if (cb == nullptr)
			{
				ELOG("Error : Out of memory.");
				return false;
			}

			if (!cb->Init(m_pDevice.Get(), m_pPool[POOL_TYPE_RES], sizeof(CbHZB)))
			{
				ELOG("Error : ConstantBuffer::Init() Failed.");
				return false;
			}

			CbHZB* ptr = cb->GetPtr<CbHZB>();
			if (i == 0)
			{
				ptr->DstMip0Width = mip0SizeX;
				ptr->DstMip0Height = mip0SizeY;
				// 幅方向にフィットさせるルールとする
				ptr->HeightScale = (float)m_Width / m_Height;
				ptr->NumOutputMip = DirectX::XMMin(numMips, HZB_MAX_NUM_OUTPUT_MIP);
			}
			else
			{
				ptr->DstMip0Width = mip0SizeX >> (HZB_MAX_NUM_OUTPUT_MIP * i);
				ptr->DstMip0Height = mip0SizeY >> (HZB_MAX_NUM_OUTPUT_MIP * i);
				// 2回目のドローコールからはSceneDepthでなく1回目の出力MipのMipレベル最大のものをサンプルする
				ptr->HeightScale = 1;
				ptr->NumOutputMip = DirectX::XMMin(numMips - HZB_MAX_NUM_OUTPUT_MIP * i, HZB_MAX_NUM_OUTPUT_MIP);
			}

			m_pHZB_CBs.push_back(cb);
		}
	}

	// ObjectVelocity用定数バッファの作成
	for (uint32_t i = 0; i < FRAME_COUNT; i++)
	{
		if (!m_ObjectVelocityCB[i].Init(m_pDevice.Get(), m_pPool[POOL_TYPE_RES], sizeof(CbObjectVelocity)))
		{
			ELOG("Error : ConstantBuffer::Init() Failed.");
			return false;
		}

		CbObjectVelocity* ptr = m_ObjectVelocityCB[i].GetPtr<CbObjectVelocity>();
		ptr->CurWVPWithJitter = Matrix::Identity;
		ptr->CurWVPNoJitter = Matrix::Identity;
		ptr->PrevWVPNoJitter = Matrix::Identity;
	}

	// CameraVelocity用定数バッファの作成
	for (uint32_t i = 0; i < FRAME_COUNT; i++)
	{
		if (!m_CameraVelocityCB[i].Init(m_pDevice.Get(), m_pPool[POOL_TYPE_RES], sizeof(CbCameraVelocity)))
		{
			ELOG("Error : ConstantBuffer::Init() Failed.");
			return false;
		}

		CbCameraVelocity* ptr = m_CameraVelocityCB[i].GetPtr<CbCameraVelocity>();
		ptr->ClipToPrevClip = Matrix::Identity;
	}

	// SSAO準備パス用定数バッファの作成
	{
		if (!m_SSAOSetupCB.Init(m_pDevice.Get(), m_pPool[POOL_TYPE_RES], sizeof(CbSSAOSetup)))
		{
			ELOG("Error : ConstantBuffer::Init() Failed.");
			return false;
		}

		CbSSAOSetup* ptr = m_SSAOSetupCB.GetPtr<CbSSAOSetup>();
		ptr->Width = m_Width;
		ptr->Height = m_Height;
		ptr->Near = CAMERA_NEAR;
		ptr->Far = CAMERA_FAR;
	}

	// SSAO半解像度用定数バッファの作成
	for (uint32_t i = 0; i < FRAME_COUNT; i++)
	{
		if (!m_SSAO_HalfResCB[i].Init(m_pDevice.Get(), m_pPool[POOL_TYPE_RES], sizeof(CbSSAO)))
		{
			ELOG("Error : ConstantBuffer::Init() Failed.");
			return false;
		}

		CbSSAO* ptr = m_SSAO_HalfResCB[i].GetPtr<CbSSAO>();
		ptr->ViewMatrix = Matrix::Identity;
		ptr->InvProjMatrix = Matrix::Identity;
		ptr->Width = (int)m_SSAO_HalfResTarget.GetDesc().Width;
		ptr->Height = (int)m_SSAO_HalfResTarget.GetDesc().Height;
		ptr->RandomationSize = Vector2((float)m_SSAO_RandomizationTex.GetDesc().Width, (float)m_SSAO_RandomizationTex.GetDesc().Height);
		// UE5は%8しているが0-10までループするのでそのままで扱っている。またUE5はRandomationSize.Widthだけで割ってるがy側はHeightで割るのが自然なのでそうしている
		ptr->TemporalOffset = (float)m_TemporalAASampleIndex * Vector2(2.48f, 7.52f) / ptr->RandomationSize;
		ptr->Near = CAMERA_NEAR;
		ptr->Far = CAMERA_FAR;
		ptr->InvTanHalfFov = 1.0f / tanf(DirectX::XMConvertToRadians(CAMERA_FOV_Y_DEGREE));
		ptr->Contrast = m_SSAO_Contrast;
		ptr->Intensity = m_SSAO_Intensity;
	}

	// SSAOフル解像度用定数バッファの作成 // TODO: 上の半解像度と設定処理が冗長
	for (uint32_t i = 0; i < FRAME_COUNT; i++)
	{
		if (!m_SSAO_FullResCB[i].Init(m_pDevice.Get(), m_pPool[POOL_TYPE_RES], sizeof(CbSSAO)))
		{
			ELOG("Error : ConstantBuffer::Init() Failed.");
			return false;
		}

		CbSSAO* ptr = m_SSAO_FullResCB[i].GetPtr<CbSSAO>();
		ptr->ViewMatrix = Matrix::Identity;
		ptr->InvProjMatrix = Matrix::Identity;
		ptr->Width = m_Width;
		ptr->Height = m_Height;
		ptr->RandomationSize = Vector2((float)m_SSAO_RandomizationTex.GetDesc().Width, (float)m_SSAO_RandomizationTex.GetDesc().Height);
		// UE5は%8しているが0-10までループするのでそのままで扱っている。またUE5はRandomationSize.Widthだけで割ってるがy側はHeightで割るのが自然なのでそうしている
		ptr->TemporalOffset = (float)m_TemporalAASampleIndex * Vector2(2.48f, 7.52f) / ptr->RandomationSize;
		ptr->Near = CAMERA_NEAR;
		ptr->Far = CAMERA_FAR;
		ptr->InvTanHalfFov = 1.0f / tanf(DirectX::XMConvertToRadians(CAMERA_FOV_Y_DEGREE));
		ptr->Contrast = m_SSAO_Contrast;
		ptr->Intensity = m_SSAO_Intensity;
	}

	// SSGIパス用定数バッファの作成
	{
		if (!m_SSGI_CB.Init(m_pDevice.Get(), m_pPool[POOL_TYPE_RES], sizeof(CbSSGI)))
		{
			ELOG("Error : ConstantBuffer::Init() Failed.");
			return false;
		}

		CbSSGI* ptr = m_SSGI_CB.GetPtr<CbSSGI>();
		ptr->ProjMatrix = Matrix::Identity;
		ptr->VRotPMatrix = Matrix::Identity;
		ptr->InvVRotPMatrix = Matrix::Identity;
		ptr->Near = CAMERA_NEAR;
		ptr->Far = CAMERA_FAR;
		ptr->Width = (int)m_SSGI_Target.GetDesc().Width;
		ptr->Height = m_SSGI_Target.GetDesc().Height;
		ptr->FrameSampleIndex = m_TemporalAASampleIndex;
		ptr->Intensity = m_SSGI_Intensity;
	}

	// SSGI_Denoiseパス用定数バッファの作成
	{
		if (!m_SSGI_DenoiseCB.Init(m_pDevice.Get(), m_pPool[POOL_TYPE_RES], sizeof(CbSSGI)))
		{
			ELOG("Error : ConstantBuffer::Init() Failed.");
			return false;
		}

		CbSSGI_Denoise* ptr = m_SSGI_DenoiseCB.GetPtr<CbSSGI_Denoise>();
		ptr->Width = (int)m_SSGI_DenoiseTarget.GetDesc().Width;
		ptr->Height = m_SSGI_DenoiseTarget.GetDesc().Height;
	}

	// SSR用定数バッファの作成
	{
		if (!m_SSR_CB.Init(m_pDevice.Get(), m_pPool[POOL_TYPE_RES], sizeof(CbSSR)))
		{
			ELOG("Error : ConstantBuffer::Init() Failed.");
			return false;
		}

		CbSSR* ptr = m_SSR_CB.GetPtr<CbSSR>();
		ptr->ProjMatrix = Matrix::Identity;
		ptr->VRotPMatrix = Matrix::Identity;
		ptr->InvVRotPMatrix = Matrix::Identity;
		ptr->Near = CAMERA_NEAR;
		ptr->Far = CAMERA_FAR;
		ptr->Width = m_Width;
		ptr->Height = m_Height;
		ptr->FrameSampleIndex = m_TemporalAASampleIndex;
		ptr->Intensity = m_SSR_Intensity;
		ptr->bDebugViewSSR = m_debugViewSSR ? 1 : 0;
	}

	// VolumetricFog用定数バッファの作成
	{
		if (!m_VolumetricFogCB.Init(m_pDevice.Get(), m_pPool[POOL_TYPE_RES], sizeof(CbVolumetricFog)))
		{
			ELOG("Error : ConstantBuffer::Init() Failed.");
			return false;
		}

		CbVolumetricFog* ptr = m_VolumetricFogCB.GetPtr<CbVolumetricFog>();
		ptr->InvVRotPMatrix = Matrix::Identity;
		ptr->ClipToPrevClip = Matrix::Identity;
		ptr->GridSizeX = (int)m_VolumetricFogScatteringTarget[m_FrameIndex].GetDesc().Width;
		ptr->GridSizeY = m_VolumetricFogScatteringTarget[m_FrameIndex].GetDesc().Height;
		ptr->GridSizeZ = m_VolumetricFogScatteringTarget[m_FrameIndex].GetDesc().DepthOrArraySize;
		ptr->Near = CAMERA_NEAR;
		ptr->Far = CAMERA_FAR;
		ptr->FrameJitterOffsetValue = VolumetricFogTemporalRandom(m_FrameNumber);
		ptr->DirectionalLightScatteringIntensity = m_directionalLightVolumetricFogScatteringIntensity;
		ptr->SpotLightScatteringIntensity = m_spotLightVolumetricFogScatteringIntensity;
	}

	// TemporalAA用定数バッファの作成
	for (uint32_t i = 0; i < FRAME_COUNT; i++)
	{
		if (!m_TemporalAA_CB[i].Init(m_pDevice.Get(), m_pPool[POOL_TYPE_RES], sizeof(CbTemporalAA)))
		{
			ELOG("Error : ConstantBuffer::Init() Failed.");
			return false;
		}

		CbTemporalAA* ptr = m_TemporalAA_CB[i].GetPtr<CbTemporalAA>();
		ptr->Width = m_Width;
		ptr->Height = m_Height;
		ptr->bEnableTemporalAA = (m_enableTemporalAA ? 1 : 0);

		// Referenced UE.
		float temporalJitetrPixelsX;
		float temporalJitetrPixelsY;
		CalculateTemporalJitterPixels(m_TemporalAASampleIndex, temporalJitetrPixelsX, temporalJitetrPixelsY);

		float totalWeight = 0;
		for (uint32_t sample = 0; sample < TEMPORAL_AA_NUM_PLUS_SAMPLE; sample++)
		{
			float pixelOffsetX = TEMPORAL_AA_NEIGHBORHOOD_SAMPLE_OFFSETS[sample][0] - temporalJitetrPixelsX;
			float pixelOffsetY = TEMPORAL_AA_NEIGHBORHOOD_SAMPLE_OFFSETS[sample][1] - temporalJitetrPixelsY;
			// Gaussian fit to Blackman-Haris filter. Sigma = 0.47
			float currWeight = expf(-2.29f * (pixelOffsetX * pixelOffsetX + pixelOffsetY * pixelOffsetY));

			//TODO: Vector4に[]演算子が無いので分岐で書くしかない
			if (sample % 4 == 0)
			{
				ptr->PlusWeights[sample / 4].x = currWeight;
			}
			else if (sample % 4 == 1)
			{
				ptr->PlusWeights[sample / 4].y = currWeight;
			}
			else if (sample % 4 == 2)
			{
				ptr->PlusWeights[sample / 4].z = currWeight;
			}
			else // sample % 4 == 3
			{
				ptr->PlusWeights[sample / 4].w = currWeight;
			}

			totalWeight += currWeight;
		}

		for (uint32_t j = 0; j < TEMPORAL_AA_NUM_PLUS_VECTOR4; j++)
		{
			ptr->PlusWeights[j] /= totalWeight;
		}
	}

	// MotionBlur用定数バッファの作成
	{
		if (!m_MotionBlurCB.Init(m_pDevice.Get(), m_pPool[POOL_TYPE_RES], sizeof(CbMotionBlur)))
		{
			ELOG("Error : ConstantBuffer::Init() Failed.");
			return false;
		}

		CbMotionBlur* ptr = m_MotionBlurCB.GetPtr<CbMotionBlur>();
		ptr->Width = m_Width;
		ptr->Height = m_Height;
		ptr->Scale = m_motionBlurScale;
	}

	// Bloom後工程用定数バッファの作成

	// UEのデフォルト値を参考にした
	static constexpr float BLOOM_INTENSITY = 0.675f;
	static constexpr uint32_t BLOOM_GAUSSIAN_KERNEL_RADIUS[BLOOM_NUM_DOWN_SAMPLE] = {256, 120, 40, 8, 4, 2};
	static constexpr float BLOOM_TINTS[BLOOM_NUM_DOWN_SAMPLE] = {0.3465f, 0.138f, 0.1176f, 0.066f, 0.066f, 0.061f};

	for (uint32_t i = 0; i < BLOOM_NUM_DOWN_SAMPLE; i++) // ドローコールの数だけ用意する
	{
		float uvOffsets[GAUSSIAN_FILTER_SAMPLES] = {0};
		float weights[GAUSSIAN_FILTER_SAMPLES] = {0};
		uint32_t numSample = Compute1DGaussianFilterKernel(BLOOM_GAUSSIAN_KERNEL_RADIUS[i], uvOffsets, weights);

		// 定数バッファは低解像度からやっていくドローコール順でなく、m_BloomSetupTarget[]の順に
		// 高解像度から定義している

		{
			if (!m_BloomHorizontalCB[i].Init(m_pDevice.Get(), m_pPool[POOL_TYPE_RES], sizeof(CbFilter)))
			{
				ELOG("Error : ConstantBuffer::Init() Failed.");
				return false;
			}

			CbFilter* ptr = m_BloomHorizontalCB[i].GetPtr<CbFilter>();
			for (uint32_t j = 0; j < GAUSSIAN_FILTER_SAMPLES / 2; j++)
			{
				ptr->SampleOffsets[j] = Vector4(uvOffsets[2 * j] / m_BloomSetupTarget[i].GetDesc().Width, 0.0f, uvOffsets[2 * j + 1] / m_BloomSetupTarget[i].GetDesc().Width, 0.0f);
			}

			for (uint32_t j = 0; j < GAUSSIAN_FILTER_SAMPLES; j++)
			{
				ptr->SampleWeights[j] = Vector4(weights[j]); // RGBAすべてウェイトは同じ
			}

			ptr->NumSample = numSample;
			ptr->bEnableAdditveTexture = false;
		}

		{
			if (!m_BloomVerticalCB[i].Init(m_pDevice.Get(), m_pPool[POOL_TYPE_RES], sizeof(CbFilter)))
			{
				ELOG("Error : ConstantBuffer::Init() Failed.");
				return false;
			}

			CbFilter* ptr = m_BloomVerticalCB[i].GetPtr<CbFilter>();
			for (uint32_t j = 0; j < GAUSSIAN_FILTER_SAMPLES / 2; j++)
			{
				ptr->SampleOffsets[j] = Vector4(0.0f, uvOffsets[2 * j] / m_BloomSetupTarget[i].GetDesc().Height, 0.0f, uvOffsets[2 * j + 1] / m_BloomSetupTarget[i].GetDesc().Height);
			}

			for (uint32_t j = 0; j < GAUSSIAN_FILTER_SAMPLES; j++)
			{
				ptr->SampleWeights[j] = Vector4(weights[j]) * BLOOM_INTENSITY * BLOOM_TINTS[i] / BLOOM_NUM_DOWN_SAMPLE; // RGBAすべてウェイトは同じ
			}

			ptr->NumSample = numSample;
			ptr->bEnableAdditveTexture = (i < BLOOM_NUM_DOWN_SAMPLE - 1);
		}
	}

	// トーンマップ用定数バッファの作成
	for (uint32_t i = 0; i < FRAME_COUNT; i++)
	{
		if (!m_TonemapCB[i].Init(m_pDevice.Get(), m_pPool[POOL_TYPE_RES], sizeof(CbTonemap)))
		{
			ELOG("Error : ConstantBuffer::Init() Failed.");
			return false;
		}

		CbTonemap* ptr = m_TonemapCB[i].GetPtr<CbTonemap>();
		ptr->BloomIntensity = m_BloomIntensity;
	}

	// FXAA用定数バッファの作成
	{
		if (!m_FXAA_CB.Init(m_pDevice.Get(), m_pPool[POOL_TYPE_RES], sizeof(CbFXAA)))
		{
			ELOG("Error : ConstantBuffer::Init() Failed.");
			return false;
		}

		CbFXAA* ptr = m_FXAA_CB.GetPtr<CbFXAA>();
		ptr->Width = m_Width;
		ptr->Height = m_Height;
		ptr->bEnableFXAA = (m_enableFXAA ? 1 : 0);
		ptr->bEnableFXAAHighQuality = (m_enableFXAA_HighQuality ? 1 : 0);
	}

	// 汎用ダウンサンプルパス用定数バッファの作成
	for (uint32_t i = 0; i < BLOOM_NUM_DOWN_SAMPLE - 1; i++) // ドローコールの数だけ用意する
	{
		if (!m_DownsampleCB[i].Init(m_pDevice.Get(), m_pPool[POOL_TYPE_RES], sizeof(CbDownsample)))
		{
			ELOG("Error : ConstantBuffer::Init() Failed.");
			return false;
		}

		CbDownsample* ptr = m_DownsampleCB[i].GetPtr<CbDownsample>();
		ptr->SrcWidth = (int)m_BloomSetupTarget[i].GetDesc().Width;
		ptr->SrcHeight = (int)m_BloomSetupTarget[i].GetDesc().Height;
	}

	// バックバッファ描画用の定数バッファの作成
	{
		if (!m_BackBufferCB.Init(m_pDevice.Get(), m_pPool[POOL_TYPE_RES], sizeof(CbSampleTexture)))
		{
			ELOG("Error : ConstantBuffer::Init() Failed.");
			return false;
		}

		CbSampleTexture* ptr = m_BackBufferCB.GetPtr<CbSampleTexture>();
		ptr->bOnlyRedChannel = 1;
		ptr->Contrast = 1.0f;
		ptr->Scale = 1.0f;
		ptr->Bias = 0.0f;
	}

	// シャドウマップとシーンの変換行列用の定数バッファの作成
	{
		// モデルのサイズから目分量で決めている
		float zNear = 0.0f;
		float zFar = 40.0f;
		float widthHeight = 40.0f;

		//const Matrix& matrix = Matrix::CreateRotationY(m_RotateAngle);
		const Matrix& matrix = Matrix::Identity;
		Vector3 dirLightForward = Vector3::TransformNormal(Vector3(-1.0f, -10.0f, -1.0f), matrix);
		dirLightForward.Normalize();

		const Matrix& dirLightShadowView = Matrix::CreateLookAt(Vector3::Zero - dirLightForward * (zFar - zNear) * 0.5f, Vector3::Zero, Vector3::UnitY);
		const Matrix& dirLightShadowProj = Matrix::CreateOrthographic(widthHeight, widthHeight, zNear, zFar);
		const Matrix& dirLightShadowViewProj = dirLightShadowView * dirLightShadowProj; // 行ベクトル形式の順序で乗算するのがXMMatrixMultiply()

		for (uint32_t i = 0u; i < FRAME_COUNT; i++)
		{
			if (!m_DirLightShadowMapTransformCB[i].Init(m_pDevice.Get(), m_pPool[POOL_TYPE_RES], sizeof(CbTransform)))
			{
				ELOG("Error : ConstantBuffer::Init() Failed.");
				return false;
			}

			CbTransform* ptr = m_DirLightShadowMapTransformCB[m_FrameIndex].GetPtr<CbTransform>();
			ptr->ViewProj = dirLightShadowViewProj;
		}

		for (uint32_t i = 0u; i < FRAME_COUNT; i++)
		{
			if (!m_TransformCB[i].Init(m_pDevice.Get(), m_pPool[POOL_TYPE_RES], sizeof(CbTransform)))
			{
				ELOG("Error : ConstantBuffer::Init() Failed.");
				return false;
			}

			constexpr float fovY = DirectX::XMConvertToRadians(CAMERA_FOV_Y_DEGREE);
			float aspect = static_cast<float>(m_Width) / static_cast<float>(m_Height);

			const Matrix& view = m_Camera.GetView();
			const Matrix& proj = Matrix::CreatePerspectiveFieldOfView(fovY, aspect, CAMERA_NEAR, CAMERA_FAR);
			CbTransform* ptr = m_TransformCB[m_FrameIndex].GetPtr<CbTransform>();
			ptr->ViewProj = view * proj; // 行ベクトル形式の順序で乗算するのがXMMatrixMultiply()

			// プロジェクション座標の[-1,-1]*[-1,1]*[0,1]をシャドウマップ用座標[0,1]*[1,0]*[0,1]に変換する
			const Matrix& toShadowMap = Matrix::CreateScale(0.5f, -0.5f, 1.0f) * Matrix::CreateTranslation(0.5f, 0.5f, 0.0f);
			// World行列はMatrix::Identityとする
			ptr->ModelToDirLightShadowMap = dirLightShadowViewProj * toShadowMap; // 行ベクトル形式の順序で乗算するのがXMMatrixMultiply()

			if (RENDER_SPONZA)
			{
				ptr->WorldToSpotLight1ShadowMap = m_SpotLightShadowMapTransformCB[0].GetPtr<CbTransform>()->ViewProj * toShadowMap; // 行ベクトル形式の順序で乗算するのがXMMatrixMultiply()
				ptr->WorldToSpotLight2ShadowMap = m_SpotLightShadowMapTransformCB[1].GetPtr<CbTransform>()->ViewProj * toShadowMap; // 行ベクトル形式の順序で乗算するのがXMMatrixMultiply()
				ptr->WorldToSpotLight3ShadowMap = m_SpotLightShadowMapTransformCB[2].GetPtr<CbTransform>()->ViewProj * toShadowMap; // 行ベクトル形式の順序で乗算するのがXMMatrixMultiply()
			}
		}
	}

	if (RENDER_SPONZA)
	{
		// スポットライトのシャドウマップの作成
		{
			ID3D12GraphicsCommandList* pCmd = m_CommandList.Reset();

			ID3D12DescriptorHeap* const pHeaps[] = {
				m_pPool[POOL_TYPE_RES]->GetHeap()
			};

			pCmd->SetDescriptorHeaps(1, pHeaps);
			
			pCmd->RSSetViewports(1, &m_SpotLightShadowMapViewport);
			pCmd->RSSetScissorRects(1, &m_SpotLightShadowMapScissor);

			for (uint32_t i = 0u; i < NUM_SPOT_LIGHTS; i++)
			{
				DirectX::TransitionResource(pCmd, m_SpotLightShadowMapTarget[i].GetResource(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_DEPTH_WRITE);

				const DescriptorHandle* handleDSV = m_SpotLightShadowMapTarget[i].GetHandleDSV();

				pCmd->OMSetRenderTargets(0, nullptr, FALSE, &handleDSV->HandleCPU);

				m_SpotLightShadowMapTarget[i].ClearView(pCmd);

				// TODO:PSOがOpaqueとMaskで切り替わっているのでライトごとでなくまとめるべきかも
				DrawSpotLightShadowMap(pCmd, i);

				DirectX::TransitionResource(pCmd, m_SpotLightShadowMapTarget[i].GetResource(), D3D12_RESOURCE_STATE_DEPTH_WRITE, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
			}

			pCmd->Close();

			ID3D12CommandList* pLists[] = {pCmd};
			m_pQueue->ExecuteCommandLists(1, pLists);

			// Wait command queue finishing.
			m_Fence.Wait(m_pQueue.Get(), INFINITE);
		}
	}
	else
	{
		// スフィアマップロード
		{
			DirectX::ResourceUploadBatch batch(m_pDevice.Get());

			batch.Begin();

			{
				std::wstring sphereMapPath;
				if (!SearchFilePathW(L"../res/Environments/Cannon_Exterior.dds", sphereMapPath))
				{
					ELOG("Error : File Not Found.");
					return false;
				}

				if (!m_SphereMap.Init(m_pDevice.Get(), m_pPool[POOL_TYPE_RES], sphereMapPath.c_str(), false, batch))
				{
					ELOG("Error : Texture::Init() Failed.");
					return false;
				}
			}

			std::future<void> future = batch.End(m_pQueue.Get());
			future.wait();
		}

		// スフィアマップコンバータ初期化
		{
			if (!m_SphereMapConverter.Init
			(
				m_pDevice.Get(),
				m_pPool[POOL_TYPE_RTV],
				m_pPool[POOL_TYPE_RES],
				m_SphereMap.GetResource()->GetDesc()
			))
			{
				ELOG("Error : SphereMapConverter::Init() Failed.");
				return false;
			}
		}

		// IBLベイカーの生成
		{
			if (!m_IBLBaker.Init(m_pDevice.Get(), m_pPool[POOL_TYPE_RES], m_pPool[POOL_TYPE_RTV]))
			{
				ELOG("Error : IBLBaker::Init() Failed.");
				return false;
			}
		}

		// IBLキューブマップベイク
		{
			ID3D12GraphicsCommandList* pCmd = m_CommandList.Reset();

			ID3D12DescriptorHeap* const pHeaps[] = {
				m_pPool[POOL_TYPE_RES]->GetHeap()
			};

			pCmd->SetDescriptorHeaps(1, pHeaps);

			m_SphereMapConverter.DrawToCube(pCmd, m_SphereMap.GetHandleGPU());

			const D3D12_RESOURCE_DESC& desc = m_SphereMapConverter.GetCubeMapDesc();
			const D3D12_GPU_DESCRIPTOR_HANDLE& handle = m_SphereMapConverter.GetHandleGPU();

			m_IBLBaker.IntegrateDFG(pCmd);

			m_IBLBaker.IntegrateLD(pCmd, uint32_t(desc.Width), desc.MipLevels, handle);

			pCmd->Close();

			ID3D12CommandList* pLists[] = {pCmd};
			m_pQueue->ExecuteCommandLists(1, pLists);

			// Wait command queue finishing.
			m_Fence.Wait(m_pQueue.Get(), INFINITE);
		}

		// IBLバッファの設定
		{
			if (!m_IBL_CB.Init(m_pDevice.Get(), m_pPool[POOL_TYPE_RES], sizeof(CbIBL)))
			{
				ELOG("Error : ConstantBuffer::Init() Failed.");
				return false;
			}

			CbIBL* ptr = m_IBL_CB.GetPtr<CbIBL>();
			ptr->TextureSize = m_IBLBaker.LDTextureSize; // TODO:DFGTextureSizeはLDTextureSizeの2倍あるのにいいのか？
			ptr->MipCount = m_IBLBaker.MipCount;
			ptr->LightIntensity = 1.0f;
		}

		// スカイボックス初期化
		{
			if (!m_SkyBox.Init
			(
				m_pDevice.Get(),
				m_pPool[POOL_TYPE_RES],
				DXGI_FORMAT_R10G10B10A2_UNORM,
				DXGI_FORMAT_D32_FLOAT
			))
			{
				ELOG("Error : SkyBox::Init() Failed.");
				return false;
			}
		}
	}

	return true;
}

void SampleApp::OnTerm()
{
	// imgui終了処理
	{
		// https://github.com/ocornut/imgui/wiki/Getting-Started#example-if-you-are-using-raw-win32-api--directx12を参考にしている
		ImGui_ImplDX12_Shutdown();
		ImGui_ImplWin32_Shutdown();
		ImGui::DestroyContext();
	}

	m_DummyTexture.Term();

	m_QuadVB.Term();

	for (uint32_t i = 0; i < FRAME_COUNT; i++)
	{
		m_DirectionalLightCB[i].Term();
		m_CameraCB[i].Term();
		m_DirLightShadowMapTransformCB[i].Term();
		m_TransformCB[i].Term();
		m_ObjectVelocityCB[i].Term();
		m_CameraVelocityCB[i].Term();
		m_SSAO_HalfResCB[i].Term();
		m_SSAO_FullResCB[i].Term();
		m_TemporalAA_CB[i].Term();
		m_TonemapCB[i].Term();
	}

	for (uint32_t i = 0u; i < NUM_POINT_LIGHTS; i++)
	{
		m_PointLightCB[i].Term();
	}

	for (uint32_t i = 0u; i < NUM_SPOT_LIGHTS; i++)
	{
		m_SpotLightCB[i].Term();
		m_SpotLightShadowMapTransformCB[i].Term();
	}

	for (ConstantBuffer* cb : m_pHZB_CBs)
	{
		if (cb != nullptr)
		{
			cb->Term();
		}
	}
	m_pHZB_CBs.clear();

	m_SSAOSetupCB.Term();

	m_SSGI_CB.Term();

	m_SSGI_DenoiseCB.Term();

	m_SSR_CB.Term();

	m_VolumetricFogCB.Term();

	for (uint32_t i = 0; i < BLOOM_NUM_DOWN_SAMPLE - 1; i++)
	{
		m_DownsampleCB[i].Term();
	}

	for (uint32_t i = 0; i < BLOOM_NUM_DOWN_SAMPLE; i++)
	{
		m_BloomHorizontalCB[i].Term();
		m_BloomVerticalCB[i].Term();
	}

	m_MotionBlurCB.Term();

	m_BackBufferCB.Term();

	m_IBL_CB.Term();

	for (Model* model : m_pModels)
	{
		if (model != nullptr)
		{
			model->Term();
		}
	}
	m_pModels.clear();

	for (DescriptorHandle* handle : m_pHZB_ParentMipSRVs)
	{
		if (handle != nullptr && m_pPool[POOL_TYPE_RES] != nullptr)
		{
			m_pPool[POOL_TYPE_RES]->FreeHandle(handle);
		}
	}
	m_pHZB_ParentMipSRVs.clear();

	m_DirLightShadowMapTarget.Term();

	for (uint32_t i = 0u; i < NUM_SPOT_LIGHTS; i++)
	{
		m_SpotLightShadowMapTarget[i].Term();
	}

	m_SkyTransmittanceLUT_Target.Term();

	m_SceneColorTarget.Term();
	m_SceneNormalTarget.Term();
	m_SceneMetallicRoughnessTarget.Term();
	m_SceneDepthTarget.Term();

	m_HCB_Target.Term();
	m_HZB_Target.Term();

	m_ObjectVelocityTarget.Term();

	m_VelocityTarget.Term();

	m_SSAOSetupTarget.Term();

	m_SSAO_HalfResTarget.Term();
	m_SSAO_FullResTarget.Term();
	m_SSAO_RandomizationTex.Term();

	m_SSGI_Target.Term();
	m_SSGI_DenoiseTarget.Term();
	for (uint32_t i = 0; i < FRAME_COUNT; i++)
	{
		m_SSGI_TemporalAccumulationTarget[i].Term();
	}

	m_AmbientLightTarget.Term();

	m_SSR_Target.Term();

	for (uint32_t i = 0; i < FRAME_COUNT; i++)
	{
		m_VolumetricFogScatteringTarget[i].Term();
	}
	m_VolumetricFogIntegrationTarget.Term();
	m_VolumetricCompositionTarget.Term();

	for (uint32_t i = 0; i < FRAME_COUNT; i++)
	{
		m_TemporalAA_Target[i].Term();
	}

	m_MotionBlurTarget.Term();

	for (uint32_t i = 0; i < BLOOM_NUM_DOWN_SAMPLE; i++)
	{
		m_BloomSetupTarget[i].Term();
	}

	for (uint32_t i = 0; i < BLOOM_NUM_DOWN_SAMPLE; i++)
	{
		m_BloomHorizontalTarget[i].Term();
		m_BloomVerticalTarget[i].Term();
	}

	m_TonemapTarget.Term();

	m_FXAA_Target.Term();

	m_pSponzaOpaquePSO.Reset();
	m_pSponzaMaskPSO.Reset();
	m_pSponzaDepthOpaquePSO.Reset();
	m_pSponzaDepthMaskPSO.Reset();

	m_SponzaRootSig.Term();

	m_pSceneOpaquePSO.Reset();
	m_pSceneMaskPSO.Reset();
	m_pSceneDepthOpaquePSO.Reset();
	m_pSceneDepthMaskPSO.Reset();

	m_SceneRootSig.Term();

	m_pHCB_PSO.Reset();
	m_HCB_RootSig.Term();

	m_pHZB_PSO.Reset();
	m_HZB_RootSig.Term();

	m_pObjectVelocityPSO.Reset();
	m_ObjectVelocityRootSig.Term();

	m_pCameraVelocityPSO.Reset();
	m_CameraVelocityRootSig.Term();

	m_pSSAOSetupPSO.Reset();
	m_SSAOSetupRootSig.Term();

	m_pSSAO_PSO.Reset();
	m_SSAO_RootSig.Term();

	m_pSSGI_PSO.Reset();
	m_SSGI_RootSig.Term();

	m_pSSGI_DenoisePSO.Reset();
	m_SSGI_DenoiseRootSig.Term();

	m_pSSGI_TemporalAccumulationPSO.Reset();
	m_SSGI_TemporalAccumulationRootSig.Term();

	m_pAmbientLightPSO.Reset();
	m_AmbientLightRootSig.Term();

	m_pSSR_PSO.Reset();
	m_SSR_RootSig.Term();

	m_pVolumetricFogScatteringPSO.Reset();
	m_VolumetricFogScatteringRootSig.Term();

	m_pVolumetricFogIntegrationPSO.Reset();
	m_VolumetricFogIntegrationRootSig.Term();

	m_pVolumetricFogCompositionPSO.Reset();
	m_VolumetricFogCompositionRootSig.Term();

	m_pTemporalAA_PSO.Reset();
	m_TemporalAA_RootSig.Term();

	m_pMotionBlurPSO.Reset();
	m_MotionBlurRootSig.Term();

	m_pBloomSetupPSO.Reset();
	m_BloomSetupRootSig.Term();

	m_pTonemapPSO.Reset();
	m_TonemapRootSig.Term();

	m_pFXAA_PSO.Reset();
	m_FXAA_RootSig.Term();

	m_pDownsamplePSO.Reset();
	m_DownsampleRootSig.Term();

	m_pFilterPSO.Reset();
	m_FilterRootSig.Term();

	m_pBackBufferPSO.Reset();
	m_BackBufferRootSig.Term();

	m_IBLBaker.Term();
	m_SphereMapConverter.Term();
	m_SphereMap.Term();
	m_SkyBox.Term();
}

void SampleApp::OnRender()
{
	// 共通変数の更新
	float temporalJitetrPixelsX;
	float temporalJitetrPixelsY;
	CalculateTemporalJitterPixels(m_TemporalAASampleIndex, temporalJitetrPixelsX, temporalJitetrPixelsY);

	const Matrix& view = m_Camera.GetView();
	Matrix viewProjNoJitter;
	Matrix viewProjWithJitter;
	Matrix viewRotProjNoJitter;
	Matrix viewRotProjWithJitter;
	Matrix projNoJitter;
	Matrix projWithJitter;

	{
		if (m_moveFlowerVase)
		{
			m_RotateAngle += 0.2f;
		}

		m_FrameNumber++;

		m_TemporalAASampleIndex++;
		if (m_TemporalAASampleIndex >= TEMPORAL_AA_SAMPLES)
		{
			m_TemporalAASampleIndex = 0;
		}

		constexpr float fovY = DirectX::XMConvertToRadians(CAMERA_FOV_Y_DEGREE);
		float aspect = static_cast<float>(m_Width) / static_cast<float>(m_Height);

		Matrix viewRot = view;
		viewRot.m[3][0] = viewRot.m[3][1] = viewRot.m[3][2] = 0;

		projNoJitter = Matrix::CreatePerspectiveFieldOfView(fovY, aspect, CAMERA_NEAR, CAMERA_FAR);
		viewProjNoJitter = view * projNoJitter; // 行ベクトル形式の順序で乗算するのがXMMatrixMultiply()
		viewRotProjNoJitter = viewRot * projNoJitter;

		// UEのTAAのジッタを参考にしている
		projWithJitter = projNoJitter;
		projWithJitter.m[2][0] += temporalJitetrPixelsX * 2.0f / m_Width;
		projWithJitter.m[2][1] += temporalJitetrPixelsY * -2.0f / m_Height;

		viewProjWithJitter = view * projWithJitter; // 行ベクトル形式の順序で乗算するのがXMMatrixMultiply()
		viewRotProjWithJitter = viewRot * projWithJitter;
	}

	// ディレクショナルライト方向（の逆方向ベクトル）の更新
	Vector3 lightForward;
	{
		//const Matrix& matrix = Matrix::CreateRotationY(m_RotateAngle);
		const Matrix& matrix = Matrix::Identity;
		lightForward = Vector3::TransformNormal(Vector3(-1.0f, -10.0f, -1.0f), matrix);
		lightForward.Normalize();
	}

	//TODO: MovableなメッシュをVelocityのテストのためサインカーブで動かす
	const Matrix& worldForMovable = Matrix::CreateTranslation(0, 0, sinf(m_RotateAngle));

	{
		for (const Model* model : m_pModels)
		{
			for (size_t meshIdx = 0; meshIdx < model->GetMeshCount(); meshIdx++)
			{
				const Mesh* pMesh = model->GetMesh(meshIdx);
				if (pMesh->GetMobility() == Mobility::Movable)
				{
					CbMesh* ptr = pMesh->GetBufferPtr<CbMesh>(m_FrameIndex);
					ptr->World = worldForMovable;
				}
			}
		}
	}

	ID3D12GraphicsCommandList* pCmd = m_CommandList.Reset();

	ID3D12DescriptorHeap* const pHeaps[] = {
		m_pPool[POOL_TYPE_RES]->GetHeap()
	};

	pCmd->SetDescriptorHeaps(1, pHeaps);
	
	if (RENDER_SPONZA)
	{
		DrawDirectionalLightShadowMap(pCmd, lightForward);
	}

	if (m_enableTemporalAA)
	{
		DrawScene(pCmd, lightForward, viewProjWithJitter, view, projWithJitter);
	}
	else
	{
		DrawScene(pCmd, lightForward, viewProjNoJitter, view, projNoJitter);
	}

	DrawHCB(pCmd);

	DrawHZB(pCmd);

	if (m_enableVelocity)
	{
		if (m_enableTemporalAA)
		{
			DrawObjectVelocity(pCmd, worldForMovable, m_PrevWorldForMovable, viewProjWithJitter, viewProjNoJitter, m_PrevViewProjNoJitter);
		}
		else
		{
			DrawObjectVelocity(pCmd, worldForMovable, m_PrevWorldForMovable, viewProjNoJitter, viewProjNoJitter, m_PrevViewProjNoJitter);
		}

		DrawCameraVelocity(pCmd, viewProjNoJitter);
	}

	DrawSSAOSetup(pCmd);

	if (m_enableTemporalAA)
	{
		DrawSSAO(pCmd, projWithJitter);
	}
	else
	{
		DrawSSAO(pCmd, projNoJitter);
	}

	if (m_enableTemporalAA)
	{
		DrawSSGI(pCmd, projWithJitter, viewProjWithJitter);
	}
	else
	{
		DrawSSGI(pCmd, projNoJitter, viewProjNoJitter);
	}

	DrawSSGI_Denoise(pCmd);

	const ColorTarget& SSGI_PrevTarget = m_SSGI_TemporalAccumulationTarget[m_FrameIndex];
	const ColorTarget& SSGI_CurTarget = m_SSGI_TemporalAccumulationTarget[(m_FrameIndex + 1) % FRAME_COUNT]; // FRAME_COUNT=2前提だとm_FrameIndex ^ 1でも可能

	DrawSSGI_TemporalAccumulation(pCmd, SSGI_PrevTarget, SSGI_CurTarget);

	DrawAmbientLight(pCmd, SSGI_CurTarget);

	if (m_enableTemporalAA)
	{
		DrawSSR(pCmd, projWithJitter, viewRotProjWithJitter);
	}
	else
	{
		DrawSSR(pCmd, projNoJitter, viewRotProjNoJitter);
	}

	const ColorTarget& volumetricFogScatteringPrevTarget = m_VolumetricFogScatteringTarget[m_FrameIndex];
	const ColorTarget& volumetricFogScatteringCurTarget = m_VolumetricFogScatteringTarget[(m_FrameIndex + 1) % FRAME_COUNT]; // FRAME_COUNT=2前提だとm_FrameIndex ^ 1でも可能

	if (RENDER_SPONZA)
	{
		DrawVolumetricFogScattering(pCmd, viewRotProjNoJitter, viewProjNoJitter, m_PrevViewProjNoJitter, volumetricFogScatteringPrevTarget, volumetricFogScatteringCurTarget);
		DrawVolumetricFogIntegration(pCmd, volumetricFogScatteringCurTarget);
		DrawVolumetricFogComposition(pCmd);
	}

	const ColorTarget& temporalAA_PrevTarget = m_TemporalAA_Target[m_FrameIndex];
	const ColorTarget& temporalAA_CurTarget = m_TemporalAA_Target[(m_FrameIndex + 1) % FRAME_COUNT]; // FRAME_COUNT=2前提だとm_FrameIndex ^ 1でも可能

	DrawTemporalAA(pCmd, temporalJitetrPixelsX, temporalJitetrPixelsY, temporalAA_PrevTarget, temporalAA_CurTarget);

	DrawMotionBlur(pCmd, temporalAA_CurTarget);

	DrawBloomSetup(pCmd);

	{
		ScopedTimer scopedTimer(pCmd, L"Downsample");

		for (uint32_t i = 0; i < BLOOM_NUM_DOWN_SAMPLE - 1; i++)
		{
			DrawDownsample(pCmd, m_BloomSetupTarget[i], m_BloomSetupTarget[i + 1], i);
		}
	}

	{
		ScopedTimer scopedTimer(pCmd, L"BloomGaussianFilter");

		for (int32_t i = BLOOM_NUM_DOWN_SAMPLE - 1; i >= 0; i--) // 解像度の小さい方から重ねていくので降順
		{
			if (i == (BLOOM_NUM_DOWN_SAMPLE - 1))
			{
				// m_SceneColorTargetをDownerResultColorとして使っているのはダミー
				DrawFilter(pCmd, m_BloomSetupTarget[i], m_BloomHorizontalTarget[i], m_BloomVerticalTarget[i], m_SceneColorTarget, m_BloomHorizontalCB[i], m_BloomVerticalCB[i]);
			}
			else
			{
				DrawFilter(pCmd, m_BloomSetupTarget[i], m_BloomHorizontalTarget[i], m_BloomVerticalTarget[i], m_BloomVerticalTarget[i + 1], m_BloomHorizontalCB[i], m_BloomVerticalCB[i]);
			}
		}
	}

	DrawTonemap(pCmd);

	DrawFXAA(pCmd);

	DrawBackBuffer(pCmd);

	DrawImGui(pCmd);

	pCmd->Close();

	ID3D12CommandList* pLists[] = {pCmd};
	m_pQueue->ExecuteCommandLists(1, pLists);

	Present(1);

	m_PrevWorldForMovable = worldForMovable;
	m_PrevViewProjNoJitter = viewProjNoJitter;
}

void SampleApp::DrawDirectionalLightShadowMap(ID3D12GraphicsCommandList* pCmdList, const Vector3& lightForward)
{
	assert(RENDER_SPONZA);

	ScopedTimer scopedTimer(pCmdList, L"DirectionalLightShadowMap");

	// 変換行列用の定数バッファの更新
	{
		float width = static_cast<float>(m_DirLightShadowMapTarget.GetDesc().Width);
		float height = static_cast<float>(m_DirLightShadowMapTarget.GetDesc().Height);
		// モデルのサイズから目分量で決めている
		float zNear = 0.0f;
		float zFar = 40.0f;
		float widthHeight = 40.0f;

		CbTransform* ptr = m_DirLightShadowMapTransformCB[m_FrameIndex].GetPtr<CbTransform>();

		const Matrix& view = Matrix::CreateLookAt(Vector3::Zero - lightForward * (zFar - zNear) * 0.5f, Vector3::Zero, Vector3::UnitY);
		const Matrix& proj = Matrix::CreateOrthographic(widthHeight, widthHeight, zNear, zFar);
		ptr->ViewProj = view * proj; // 行ベクトル形式の順序で乗算するのがXMMatrixMultiply()
	}

	DirectX::TransitionResource(pCmdList, m_DirLightShadowMapTarget.GetResource(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_DEPTH_WRITE);

	const DescriptorHandle* handleDSV = m_DirLightShadowMapTarget.GetHandleDSV();

	pCmdList->OMSetRenderTargets(0, nullptr, FALSE, &handleDSV->HandleCPU);

	m_DirLightShadowMapTarget.ClearView(pCmdList);

	pCmdList->RSSetViewports(1, &m_DirLightShadowMapViewport);
	pCmdList->RSSetScissorRects(1, &m_DirLightShadowMapScissor);
	pCmdList->SetGraphicsRootSignature(m_SponzaRootSig.GetPtr());
	pCmdList->SetGraphicsRootDescriptorTable(0, m_DirLightShadowMapTransformCB[m_FrameIndex].GetHandleGPU());

	pCmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	// Opaqueマテリアルのメッシュの描画
	pCmdList->SetPipelineState(m_pSponzaDepthOpaquePSO.Get());
	DrawMesh(pCmdList, ALPHA_MODE::ALPHA_MODE_OPAQUE);

	// Mask, DoubleSidedマテリアルのメッシュの描画
	pCmdList->SetPipelineState(m_pSponzaDepthMaskPSO.Get());
	DrawMesh(pCmdList, ALPHA_MODE::ALPHA_MODE_MASK);

	DirectX::TransitionResource(pCmdList, m_DirLightShadowMapTarget.GetResource(), D3D12_RESOURCE_STATE_DEPTH_WRITE, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
}

void SampleApp::DrawSpotLightShadowMap(ID3D12GraphicsCommandList* pCmdList, uint32_t spotLightIdx)
{
	assert(RENDER_SPONZA);

	pCmdList->SetGraphicsRootSignature(m_SponzaRootSig.GetPtr());
	pCmdList->SetGraphicsRootDescriptorTable(0, m_SpotLightShadowMapTransformCB[spotLightIdx].GetHandleGPU());

	pCmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	// Opaqueマテリアルのメッシュの描画
	pCmdList->SetPipelineState(m_pSponzaDepthOpaquePSO.Get());
	DrawMesh(pCmdList, ALPHA_MODE::ALPHA_MODE_OPAQUE);

	// Mask, DoubleSidedマテリアルのメッシュの描画
	pCmdList->SetPipelineState(m_pSponzaDepthMaskPSO.Get());
	DrawMesh(pCmdList, ALPHA_MODE::ALPHA_MODE_MASK);
}

void SampleApp::DrawScene(ID3D12GraphicsCommandList* pCmdList, const DirectX::SimpleMath::Vector3& lightForward, const Matrix& viewProj, const Matrix& view, const Matrix& proj)
{
	ScopedTimer scopedTimer(pCmdList, L"BasePass");

	// 変換行列用の定数バッファの更新
	{
		CbTransform* ptr = m_TransformCB[m_FrameIndex].GetPtr<CbTransform>();
		ptr->ViewProj = viewProj;

		if (RENDER_SPONZA)
		{
			float zNear = 0.0f;
			float zFar = 40.0f;
			float widthHeight = 40.0f;

			const Matrix& shadowView = Matrix::CreateLookAt(Vector3::Zero - lightForward * (zFar - zNear) * 0.5f, Vector3::Zero, Vector3::UnitY);
			const Matrix& shadowProj = Matrix::CreateOrthographic(widthHeight, widthHeight, zNear, zFar);
			const Matrix& shadowViewProj = shadowView * shadowProj; // 行ベクトル形式の順序で乗算するのがXMMatrixMultiply()

			// プロジェクション座標の[-0.5,0.5]*[-0.5,0.5]*[0,1]をシャドウマップ用座標[-1,1]*[-1,1]*[0,1]に変換する
			const Matrix& toShadowMap = Matrix::CreateScale(0.5f, -0.5f, 1.0f) * Matrix::CreateTranslation(0.5f, 0.5f, 0.0f);
			// World行列はMatrix::Identityとする
			ptr->ModelToDirLightShadowMap = shadowViewProj * toShadowMap; // 行ベクトル形式の順序で乗算するのがXMMatrixMultiply()

			ptr->WorldToSpotLight1ShadowMap = m_SpotLightShadowMapTransformCB[0].GetPtr<CbTransform>()->ViewProj * toShadowMap; // 行ベクトル形式の順序で乗算するのがXMMatrixMultiply()
			ptr->WorldToSpotLight2ShadowMap = m_SpotLightShadowMapTransformCB[1].GetPtr<CbTransform>()->ViewProj * toShadowMap; // 行ベクトル形式の順序で乗算するのがXMMatrixMultiply()
			ptr->WorldToSpotLight3ShadowMap = m_SpotLightShadowMapTransformCB[2].GetPtr<CbTransform>()->ViewProj * toShadowMap; // 行ベクトル形式の順序で乗算するのがXMMatrixMultiply()
		}
	}

	// カメラバッファの更新
	{
		CbCamera* ptr = m_CameraCB[m_FrameIndex].GetPtr<CbCamera>();
		ptr->CameraPosition = m_Camera.GetPosition();
	}

	// ライトバッファの更新
	if (RENDER_SPONZA)
	{
		{
			CbDirectionalLight* ptr = m_DirectionalLightCB[m_FrameIndex].GetPtr<CbDirectionalLight>();
			ptr->LightColor = Vector3(1.0f, 1.0f, 1.0f); // 白色光
			ptr->LightForward = lightForward;
			ptr->LightIntensity = m_directionalLightIntensity;
			ptr->ShadowMapSize = Vector2((float)DIRECTIONAL_LIGHT_SHADOW_MAP_SIZE, 1.0f / DIRECTIONAL_LIGHT_SHADOW_MAP_SIZE);
		}

		for (uint32_t i = 0u; i < NUM_POINT_LIGHTS; i++)
		{
			CbPointLight* ptr = m_PointLightCB[i].GetPtr<CbPointLight>();
			ptr->LightIntensity = m_pointLightIntensity;
		}

		for (uint32_t i = 0u; i < NUM_SPOT_LIGHTS; i++)
		{
			CbSpotLight* ptr = m_SpotLightCB[i].GetPtr<CbSpotLight>();
			ptr->LightIntensity = m_spotLightIntensity;
		}
	}

	DirectX::TransitionResource(pCmdList, m_SceneColorTarget.GetResource(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
	DirectX::TransitionResource(pCmdList, m_SceneNormalTarget.GetResource(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
	DirectX::TransitionResource(pCmdList, m_SceneMetallicRoughnessTarget.GetResource(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
	DirectX::TransitionResource(pCmdList, m_SceneDepthTarget.GetResource(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_DEPTH_WRITE);

	D3D12_CPU_DESCRIPTOR_HANDLE rtvs[3] = {
		m_SceneColorTarget.GetHandleRTV()->HandleCPU,
		m_SceneNormalTarget.GetHandleRTV()->HandleCPU, 
		m_SceneMetallicRoughnessTarget.GetHandleRTV()->HandleCPU 
	};
	const DescriptorHandle* handleDSV = m_SceneDepthTarget.GetHandleDSV();

	pCmdList->OMSetRenderTargets(3, rtvs, FALSE, &handleDSV->HandleCPU);

	m_SceneColorTarget.ClearView(pCmdList);
	m_SceneNormalTarget.ClearView(pCmdList);
	m_SceneMetallicRoughnessTarget.ClearView(pCmdList);
	m_SceneDepthTarget.ClearView(pCmdList);

	pCmdList->RSSetViewports(1, &m_Viewport);
	pCmdList->RSSetScissorRects(1, &m_Scissor);

	//TODO:DrawDirectionalLightShadowMapと重複してるがとりあえず
	if (RENDER_SPONZA)
	{
		pCmdList->SetGraphicsRootSignature(m_SponzaRootSig.GetPtr());
	}
	else
	{
		pCmdList->SetGraphicsRootSignature(m_SceneRootSig.GetPtr());
	}
	pCmdList->SetGraphicsRootDescriptorTable(0, m_TransformCB[m_FrameIndex].GetHandleGPU());
	pCmdList->SetGraphicsRootDescriptorTable(2, m_CameraCB[m_FrameIndex].GetHandleGPU());

	if (RENDER_SPONZA)
	{
		pCmdList->SetGraphicsRootDescriptorTable(4, m_DirectionalLightCB[m_FrameIndex].GetHandleGPU());

		for (uint32_t i = 0u; i < NUM_POINT_LIGHTS; i++)
		{
			pCmdList->SetGraphicsRootDescriptorTable(5 + i, m_PointLightCB[i].GetHandleGPU());
		}

		for (uint32_t i = 0u; i < NUM_SPOT_LIGHTS; i++)
		{
			pCmdList->SetGraphicsRootDescriptorTable(9 + i, m_SpotLightCB[i].GetHandleGPU());
		}
	}
	else
	{
		pCmdList->SetGraphicsRootDescriptorTable(4, m_IBL_CB.GetHandleGPU());
	}

	if (RENDER_SPONZA)
	{
		pCmdList->SetGraphicsRootDescriptorTable(17, m_DirLightShadowMapTarget.GetHandleSRV()->HandleGPU);

		for (uint32_t i = 0u; i < NUM_SPOT_LIGHTS; i++)
		{
			pCmdList->SetGraphicsRootDescriptorTable(18 + i, m_SpotLightShadowMapTarget[i].GetHandleSRV()->HandleGPU);
		}
	}
	else
	{
		pCmdList->SetGraphicsRootDescriptorTable(10, m_IBLBaker.GetHandleGPU_DFG());
		pCmdList->SetGraphicsRootDescriptorTable(11, m_IBLBaker.GetHandleGPU_DiffuseLD());
		pCmdList->SetGraphicsRootDescriptorTable(12, m_IBLBaker.GetHandleGPU_SpecularLD());
	}

	pCmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	// Opaqueマテリアルのメッシュの描画
	if (RENDER_SPONZA)
	{
		pCmdList->SetPipelineState(m_pSponzaOpaquePSO.Get());
	}
	else
	{
		pCmdList->SetPipelineState(m_pSceneOpaquePSO.Get());
	}
	DrawMesh(pCmdList, ALPHA_MODE::ALPHA_MODE_OPAQUE);

	// Mask, DoubleSidedマテリアルのメッシュの描画
	if (RENDER_SPONZA)
	{
		pCmdList->SetPipelineState(m_pSponzaMaskPSO.Get());
	}
	else
	{
		pCmdList->SetPipelineState(m_pSceneMaskPSO.Get());
	}
	DrawMesh(pCmdList, ALPHA_MODE::ALPHA_MODE_MASK);

	if (!RENDER_SPONZA)
	{
		m_SkyBox.Draw(pCmdList, m_SphereMapConverter.GetHandleGPU(), view, proj, SKY_BOX_HALF_EXTENT);
	}

	DirectX::TransitionResource(pCmdList, m_SceneColorTarget.GetResource(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
	DirectX::TransitionResource(pCmdList, m_SceneNormalTarget.GetResource(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
	DirectX::TransitionResource(pCmdList, m_SceneMetallicRoughnessTarget.GetResource(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
	DirectX::TransitionResource(pCmdList, m_SceneDepthTarget.GetResource(), D3D12_RESOURCE_STATE_DEPTH_WRITE, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
}

void SampleApp::DrawMesh(ID3D12GraphicsCommandList* pCmdList, ALPHA_MODE AlphaMode)
{
	for (const Model* model : m_pModels)
	{
		for (size_t meshIdx = 0; meshIdx < model->GetMeshCount(); meshIdx++)
		{
			const Mesh* pMesh = model->GetMesh(meshIdx);

			// TODO:Materialはとりあえず最初は一種類しか作らない。テクスチャの差し替えで使いまわす
			const Material* pMaterial = model->GetMaterial(pMesh->GetMaterialId());

			if (AlphaMode == ALPHA_MODE::ALPHA_MODE_OPAQUE && pMaterial->GetDoubleSided())
			{
				continue;
			}
			else if (AlphaMode == ALPHA_MODE::ALPHA_MODE_MASK && !pMaterial->GetDoubleSided())
			{
				continue;
			}

			pCmdList->SetGraphicsRootDescriptorTable(1, pMesh->GetConstantBufferHandle(m_FrameIndex));
			pCmdList->SetGraphicsRootDescriptorTable(3, pMaterial->GetBufferHandle());
			if (RENDER_SPONZA)
			{
				pCmdList->SetGraphicsRootDescriptorTable(12, pMaterial->GetTextureHandle(Material::TEXTURE_USAGE_BASE_COLOR));
				pCmdList->SetGraphicsRootDescriptorTable(13, pMaterial->GetTextureHandle(Material::TEXTURE_USAGE_METALLIC_ROUGHNESS));
				pCmdList->SetGraphicsRootDescriptorTable(14, pMaterial->GetTextureHandle(Material::TEXTURE_USAGE_NORMAL));
				pCmdList->SetGraphicsRootDescriptorTable(15, pMaterial->GetTextureHandle(Material::TEXTURE_USAGE_EMISSIVE));
				pCmdList->SetGraphicsRootDescriptorTable(16, pMaterial->GetTextureHandle(Material::TEXTURE_USAGE_AMBIENT_OCCLUSION));
			}
			else
			{
				pCmdList->SetGraphicsRootDescriptorTable(5, pMaterial->GetTextureHandle(Material::TEXTURE_USAGE_BASE_COLOR));
				pCmdList->SetGraphicsRootDescriptorTable(6, pMaterial->GetTextureHandle(Material::TEXTURE_USAGE_METALLIC_ROUGHNESS));
				pCmdList->SetGraphicsRootDescriptorTable(7, pMaterial->GetTextureHandle(Material::TEXTURE_USAGE_NORMAL));
				pCmdList->SetGraphicsRootDescriptorTable(8, pMaterial->GetTextureHandle(Material::TEXTURE_USAGE_EMISSIVE));
				pCmdList->SetGraphicsRootDescriptorTable(9, pMaterial->GetTextureHandle(Material::TEXTURE_USAGE_AMBIENT_OCCLUSION));
			}

			pMesh->Draw(pCmdList);
		}
	}
}

void SampleApp::DrawHCB(ID3D12GraphicsCommandList* pCmdList)
{
	ScopedTimer scopedTimer(pCmdList, L"BuildHCB");

	// シェーダ側と合わせている
	const size_t GROUP_SIZE_X = 1 << (HCB_MAX_NUM_OUTPUT_MIP - 1);
	const size_t GROUP_SIZE_Y = 1 << (HCB_MAX_NUM_OUTPUT_MIP - 1);

	uint32_t mip0SizeX = (uint32_t)m_HCB_Target.GetDesc().Width;
	uint32_t mip0SizeY = (uint32_t)m_HCB_Target.GetDesc().Height;

	D3D12_RESOURCE_BARRIER templateBarrier = {};
	templateBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	templateBarrier.Transition.pResource = m_HCB_Target.GetResource();

	std::vector<D3D12_RESOURCE_BARRIER> mipBarriers;

	// バリアの設定。HZBの場合はサブリソースごとにSRVかUAVかで指定を変える
	DirectX::TransitionResource(pCmdList, m_SceneColorTarget.GetResource(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

	for (uint32_t mip = 0; mip < HCB_MAX_NUM_OUTPUT_MIP; mip++)
	{
		D3D12_RESOURCE_BARRIER mipBarrier = templateBarrier;
		mipBarrier.Transition.Subresource = mip;
		mipBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
		mipBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;

		mipBarriers.push_back(mipBarrier);
	}

	// DirectX::TransitionResource()ではサブリソースごとの指定はできないのでScopedBarrierを使う
	DirectX::ScopedBarrier mipTransitions(pCmdList, mipBarriers.data(), mipBarriers.size());

	pCmdList->SetComputeRootSignature(m_HCB_RootSig.GetPtr());
	pCmdList->SetPipelineState(m_pHCB_PSO.Get());
	pCmdList->SetComputeRootDescriptorTable(0, m_HCB_CB.GetHandleGPU());
	pCmdList->SetComputeRootDescriptorTable(1, m_SceneColorTarget.GetHandleSRV()->HandleGPU);

	for (uint32_t mip = 0; mip < HCB_MAX_NUM_OUTPUT_MIP; mip++)
	{
		pCmdList->SetComputeRootDescriptorTable(2 + mip, m_HCB_Target.GetHandleUAVs()[mip]->HandleGPU);
	}

	// グループ数は切り上げ
	UINT NumGroupX = DivideAndRoundUp(mip0SizeX, GROUP_SIZE_X);
	UINT NumGroupY = DivideAndRoundUp(mip0SizeY, GROUP_SIZE_Y);
	UINT NumGroupZ = 1;
	pCmdList->Dispatch(NumGroupX, NumGroupY, NumGroupZ);

	DirectX::TransitionResource(pCmdList, m_SceneColorTarget.GetResource(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
}

void SampleApp::DrawHZB(ID3D12GraphicsCommandList* pCmdList)
{
	ScopedTimer scopedTimer(pCmdList, L"BuildHZB");

	// シェーダ側と合わせている
	const size_t GROUP_SIZE_X = 1 << (HZB_MAX_NUM_OUTPUT_MIP - 1);
	const size_t GROUP_SIZE_Y = 1 << (HZB_MAX_NUM_OUTPUT_MIP - 1);

	uint32_t mip0SizeX = (uint32_t)m_HZB_Target.GetDesc().Width;
	uint32_t mip0SizeY = (uint32_t)m_HZB_Target.GetDesc().Height;
	uint32_t numMips = m_HZB_Target.GetDesc().MipLevels;
	uint32_t numDrawCall = (uint32_t)m_pHZB_CBs.size();
	assert(numDrawCall > 0);

	D3D12_RESOURCE_BARRIER templateBarrier = {};
	templateBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	templateBarrier.Transition.pResource = m_HZB_Target.GetResource();

	for (uint32_t i = 0; i < numDrawCall; i++)
	{
		uint32_t numOutputMip = 0;
		if (i == 0)
		{
			numOutputMip = DirectX::XMMin(numMips, HZB_MAX_NUM_OUTPUT_MIP);
		}
		else
		{
			numOutputMip = DirectX::XMMin(numMips - HZB_MAX_NUM_OUTPUT_MIP * i, HZB_MAX_NUM_OUTPUT_MIP);
		}
		assert(numOutputMip > 0);

		std::vector<D3D12_RESOURCE_BARRIER> mipBarriers;

		// バリアの設定。HZBの場合はサブリソースごとにSRVかUAVかで指定を変える
		if (i == 0)
		{
			DirectX::TransitionResource(pCmdList, m_SceneDepthTarget.GetResource(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
		}
		else
		{
			D3D12_RESOURCE_BARRIER parentMipBarrier = templateBarrier;
			parentMipBarrier.Transition.Subresource = HZB_MAX_NUM_OUTPUT_MIP * i - 1;
			parentMipBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
			parentMipBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;

			mipBarriers.push_back(parentMipBarrier);
		}

		for (uint32_t mip = 0; mip < numOutputMip; mip++)
		{
			D3D12_RESOURCE_BARRIER mipBarrier = templateBarrier;
			mipBarrier.Transition.Subresource = HZB_MAX_NUM_OUTPUT_MIP * i + mip;
			mipBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
			mipBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;

			mipBarriers.push_back(mipBarrier);
		}

		// DirectX::TransitionResource()ではサブリソースごとの指定はできないのでScopedBarrierを使う
		DirectX::ScopedBarrier mipTransitions(pCmdList, mipBarriers.data(), mipBarriers.size());

		pCmdList->SetComputeRootSignature(m_HZB_RootSig.GetPtr());
		pCmdList->SetPipelineState(m_pHZB_PSO.Get());
		pCmdList->SetComputeRootDescriptorTable(0, m_pHZB_CBs[i]->GetHandleGPU());
		if (i == 0)
		{
			pCmdList->SetComputeRootDescriptorTable(1, m_SceneDepthTarget.GetHandleSRV()->HandleGPU);
		}
		else
		{
			pCmdList->SetComputeRootDescriptorTable(1, m_pHZB_ParentMipSRVs[i - 1]->HandleGPU);
		}

		for (uint32_t mip = 0; mip < numOutputMip; mip++)
		{
			pCmdList->SetComputeRootDescriptorTable(2 + mip, m_HZB_Target.GetHandleUAVs()[HZB_MAX_NUM_OUTPUT_MIP * i + mip]->HandleGPU);
		}

		// グループ数は切り上げ
		uint32_t minMipSizeX = (mip0SizeX >> HZB_MAX_NUM_OUTPUT_MIP * i);
		uint32_t minMipSizeY = (mip0SizeY >> HZB_MAX_NUM_OUTPUT_MIP * i);
		UINT NumGroupX = DivideAndRoundUp(minMipSizeX, GROUP_SIZE_X);
		UINT NumGroupY = DivideAndRoundUp(minMipSizeY, GROUP_SIZE_Y);
		UINT NumGroupZ = 1;
		pCmdList->Dispatch(NumGroupX, NumGroupY, NumGroupZ);

		if (i == 0)
		{
			DirectX::TransitionResource(pCmdList, m_SceneDepthTarget.GetResource(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
		}
	}
}

void SampleApp::DrawObjectVelocity(ID3D12GraphicsCommandList* pCmdList, const DirectX::SimpleMath::Matrix& world, const DirectX::SimpleMath::Matrix& prevWorld, const DirectX::SimpleMath::Matrix& viewProjWithJitter, const DirectX::SimpleMath::Matrix& viewProjNoJitter, const DirectX::SimpleMath::Matrix& prevViewProjNoJitter)
{
	ScopedTimer scopedTimer(pCmdList, L"ObjectVelocity");

	// 変換行列用の定数バッファの更新
	{
		CbObjectVelocity* ptr = m_ObjectVelocityCB[m_FrameIndex].GetPtr<CbObjectVelocity>();
		// 行ベクトル形式の順序で乗算するのがXMMatrixMultiply()
		ptr->CurWVPWithJitter = world * viewProjWithJitter;
		ptr->CurWVPNoJitter = world * viewProjNoJitter;
		ptr->PrevWVPNoJitter = prevWorld * prevViewProjNoJitter;
	}

	DirectX::TransitionResource(pCmdList, m_ObjectVelocityTarget.GetResource(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
	DirectX::TransitionResource(pCmdList, m_SceneDepthTarget.GetResource(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_DEPTH_WRITE);

	const DescriptorHandle* handleRTV = m_ObjectVelocityTarget.GetHandleRTV();
	const DescriptorHandle* handleDSV = m_SceneDepthTarget.GetHandleDSV();
	pCmdList->OMSetRenderTargets(1, &handleRTV->HandleCPU, FALSE, &handleDSV->HandleCPU);

	m_ObjectVelocityTarget.ClearView(pCmdList);

	pCmdList->RSSetViewports(1, &m_Viewport);
	pCmdList->RSSetScissorRects(1, &m_Scissor);

	//TODO:DrawDirectionalLightShadowMapと重複してるがとりあえず
	pCmdList->SetGraphicsRootSignature(m_ObjectVelocityRootSig.GetPtr());
	pCmdList->SetGraphicsRootDescriptorTable(0, m_ObjectVelocityCB[m_FrameIndex].GetHandleGPU());
	pCmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	pCmdList->SetPipelineState(m_pObjectVelocityPSO.Get());

	// Movableなものだけ描画
	for (const Model* model : m_pModels)
	{
		for (size_t meshIdx = 0; meshIdx < model->GetMeshCount(); meshIdx++)
		{
			const Mesh* pMesh = model->GetMesh(meshIdx);
			if (pMesh->GetMobility() != Mobility::Movable)
			{
				continue;
			}

			pMesh->Draw(pCmdList);
		}
	}

	DirectX::TransitionResource(pCmdList, m_ObjectVelocityTarget.GetResource(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
	DirectX::TransitionResource(pCmdList, m_SceneDepthTarget.GetResource(), D3D12_RESOURCE_STATE_DEPTH_WRITE, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
}

void SampleApp::DrawCameraVelocity(ID3D12GraphicsCommandList* pCmdList, const DirectX::SimpleMath::Matrix& viewProjNoJitter)
{
	ScopedTimer scopedTimer(pCmdList, L"CameraVelocity");

	{
		CbCameraVelocity* ptr = m_CameraVelocityCB[m_FrameIndex].GetPtr<CbCameraVelocity>();
		// これはfloat精度の誤差が入る。前フレームとVPが変わらなくても誤差で単位行列にはならない
		ptr->ClipToPrevClip = viewProjNoJitter.Invert() * m_PrevViewProjNoJitter;
	}

	DirectX::TransitionResource(pCmdList, m_VelocityTarget.GetResource(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);

	const DescriptorHandle* handleRTV = m_VelocityTarget.GetHandleRTV();
	pCmdList->OMSetRenderTargets(1, &handleRTV->HandleCPU, FALSE, nullptr);

	m_VelocityTarget.ClearView(pCmdList);

	pCmdList->SetGraphicsRootSignature(m_CameraVelocityRootSig.GetPtr());
	pCmdList->SetGraphicsRootDescriptorTable(0, m_CameraVelocityCB[m_FrameIndex].GetHandleGPU());
	pCmdList->SetGraphicsRootDescriptorTable(1, m_SceneDepthTarget.GetHandleSRV()->HandleGPU);
	pCmdList->SetGraphicsRootDescriptorTable(2, m_ObjectVelocityTarget.GetHandleSRV()->HandleGPU);
	pCmdList->SetPipelineState(m_pCameraVelocityPSO.Get());

	pCmdList->RSSetViewports(1, &m_Viewport);
	pCmdList->RSSetScissorRects(1, &m_Scissor);
	
	pCmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	const D3D12_VERTEX_BUFFER_VIEW& VBV = m_QuadVB.GetView();
	pCmdList->IASetVertexBuffers(0, 1, &VBV);

	pCmdList->DrawInstanced(3, 1, 0, 0);

	DirectX::TransitionResource(pCmdList, m_VelocityTarget.GetResource(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
}

void SampleApp::DrawSSAOSetup(ID3D12GraphicsCommandList* pCmdList)
{
	ScopedTimer scopedTimer(pCmdList, L"SSAOSetup");

	DirectX::TransitionResource(pCmdList, m_SSAOSetupTarget.GetResource(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);

	const DescriptorHandle* handleRTV = m_SSAOSetupTarget.GetHandleRTV();
	pCmdList->OMSetRenderTargets(1, &handleRTV->HandleCPU, FALSE, nullptr);

	m_SSAOSetupTarget.ClearView(pCmdList);

	pCmdList->SetGraphicsRootSignature(m_SSAOSetupRootSig.GetPtr());
	pCmdList->SetGraphicsRootDescriptorTable(0, m_SSAOSetupCB.GetHandleGPU());
	pCmdList->SetGraphicsRootDescriptorTable(1, m_SceneDepthTarget.GetHandleSRV()->HandleGPU);
	pCmdList->SetGraphicsRootDescriptorTable(2, m_SceneNormalTarget.GetHandleSRV()->HandleGPU);
	pCmdList->SetPipelineState(m_pSSAOSetupPSO.Get());

	D3D12_VIEWPORT halfResViewport = m_Viewport;
	halfResViewport.Width = (FLOAT)m_SSAOSetupTarget.GetDesc().Width;
	halfResViewport.Height = (FLOAT)m_SSAOSetupTarget.GetDesc().Height;
	pCmdList->RSSetViewports(1, &halfResViewport);

	D3D12_RECT halfResScissor = m_Scissor;
	halfResScissor.right = (LONG)m_SSAOSetupTarget.GetDesc().Width;
	halfResScissor.bottom = (LONG)m_SSAOSetupTarget.GetDesc().Height;
	pCmdList->RSSetScissorRects(1, &halfResScissor);
	
	pCmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	const D3D12_VERTEX_BUFFER_VIEW& VBV = m_QuadVB.GetView();
	pCmdList->IASetVertexBuffers(0, 1, &VBV);

	pCmdList->DrawInstanced(3, 1, 0, 0);

	DirectX::TransitionResource(pCmdList, m_SSAOSetupTarget.GetResource(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
}

//TODO:SSパスは処理を共通化したい
void SampleApp::DrawSSAO(ID3D12GraphicsCommandList* pCmdList, const DirectX::SimpleMath::Matrix& proj)
{
	// 半解像度パス // TODO:フル解像度パスと処理が冗長
	{
		ScopedTimer scopedTimer(pCmdList, L"SSAOHalfRes");

		{
			CbSSAO* ptr = m_SSAO_HalfResCB[m_FrameIndex].GetPtr<CbSSAO>();
			// UE5はRandomationSize.Widthだけで割ってるがy側はHeightで割るのが自然なのでそうしている
			ptr->TemporalOffset = (float)m_TemporalAASampleIndex * Vector2(2.48f, 7.52f) / ptr->RandomationSize;
			ptr->ViewMatrix = m_Camera.GetView();
			ptr->InvProjMatrix = proj.Invert();
			ptr->bHalfRes = 1;
			ptr->Contrast = m_SSAO_Contrast;
			ptr->Intensity = m_SSAO_Intensity;
		}

		DirectX::TransitionResource(pCmdList, m_SSAO_HalfResTarget.GetResource(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);

		const DescriptorHandle* handleRTV = m_SSAO_HalfResTarget.GetHandleRTV();
		pCmdList->OMSetRenderTargets(1, &handleRTV->HandleCPU, FALSE, nullptr);

		m_SSAO_HalfResTarget.ClearView(pCmdList);

		pCmdList->SetGraphicsRootSignature(m_SSAO_RootSig.GetPtr());
		pCmdList->SetGraphicsRootDescriptorTable(0, m_SSAO_HalfResCB[m_FrameIndex].GetHandleGPU());
		pCmdList->SetGraphicsRootDescriptorTable(1, m_SceneDepthTarget.GetHandleSRV()->HandleGPU);
		pCmdList->SetGraphicsRootDescriptorTable(2, m_SSAOSetupTarget.GetHandleSRV()->HandleGPU);
		pCmdList->SetGraphicsRootDescriptorTable(3, m_SSAO_RandomizationTex.GetHandleGPU());
		pCmdList->SetPipelineState(m_pSSAO_PSO.Get());

		D3D12_VIEWPORT halfResViewport = m_Viewport;
		halfResViewport.Width = (FLOAT)m_SSAO_HalfResTarget.GetDesc().Width;
		halfResViewport.Height = (FLOAT)m_SSAO_HalfResTarget.GetDesc().Height;
		pCmdList->RSSetViewports(1, &halfResViewport);

		D3D12_RECT halfResScissor = m_Scissor;
		halfResScissor.right = (LONG)m_SSAO_HalfResTarget.GetDesc().Width;
		halfResScissor.bottom = (LONG)m_SSAO_HalfResTarget.GetDesc().Height;
		pCmdList->RSSetScissorRects(1, &halfResScissor);
		
		pCmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		const D3D12_VERTEX_BUFFER_VIEW& VBV = m_QuadVB.GetView();
		pCmdList->IASetVertexBuffers(0, 1, &VBV);

		pCmdList->DrawInstanced(3, 1, 0, 0);

		DirectX::TransitionResource(pCmdList, m_SSAO_HalfResTarget.GetResource(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
	}

	// フル解像度パス
	{
		ScopedTimer scopedTimer(pCmdList, L"SSAOFullRes");

		{
			CbSSAO* ptr = m_SSAO_FullResCB[m_FrameIndex].GetPtr<CbSSAO>();
			// UE5は%8しているが0-10までループするのでそのままで扱っている。またUE5はRandomationSize.Widthだけで割ってるがy側はHeightで割るのが自然なのでそうしている
			ptr->TemporalOffset = (float)m_TemporalAASampleIndex * Vector2(2.48f, 7.52f) / ptr->RandomationSize;
			ptr->ViewMatrix = m_Camera.GetView();
			ptr->InvProjMatrix = proj.Invert();
			ptr->bHalfRes = 0;
			ptr->Contrast = m_SSAO_Contrast;
			ptr->Intensity = m_SSAO_Intensity;
		}

		DirectX::TransitionResource(pCmdList, m_SSAO_FullResTarget.GetResource(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);

		const DescriptorHandle* handleRTV = m_SSAO_FullResTarget.GetHandleRTV();
		pCmdList->OMSetRenderTargets(1, &handleRTV->HandleCPU, FALSE, nullptr);

		m_SSAO_FullResTarget.ClearView(pCmdList);

		pCmdList->SetGraphicsRootSignature(m_SSAO_RootSig.GetPtr());
		pCmdList->SetGraphicsRootDescriptorTable(0, m_SSAO_FullResCB[m_FrameIndex].GetHandleGPU());
		pCmdList->SetGraphicsRootDescriptorTable(1, m_SceneDepthTarget.GetHandleSRV()->HandleGPU);
		pCmdList->SetGraphicsRootDescriptorTable(2, m_SSAOSetupTarget.GetHandleSRV()->HandleGPU);
		pCmdList->SetGraphicsRootDescriptorTable(3, m_SSAO_RandomizationTex.GetHandleGPU());
		pCmdList->SetGraphicsRootDescriptorTable(4, m_SSAO_HalfResTarget.GetHandleSRV()->HandleGPU);
		pCmdList->SetGraphicsRootDescriptorTable(5, m_SceneNormalTarget.GetHandleSRV()->HandleGPU);
		pCmdList->SetPipelineState(m_pSSAO_PSO.Get());

		pCmdList->RSSetViewports(1, &m_Viewport);
		pCmdList->RSSetScissorRects(1, &m_Scissor);
		
		pCmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		const D3D12_VERTEX_BUFFER_VIEW& VBV = m_QuadVB.GetView();
		pCmdList->IASetVertexBuffers(0, 1, &VBV);

		pCmdList->DrawInstanced(3, 1, 0, 0);

		DirectX::TransitionResource(pCmdList, m_SSAO_FullResTarget.GetResource(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
	}
}

void SampleApp::DrawSSGI(ID3D12GraphicsCommandList* pCmdList, const DirectX::SimpleMath::Matrix& proj, const DirectX::SimpleMath::Matrix& viewRotProj)
{
	ScopedTimer scopedTimer(pCmdList, L"SSGI");

	{
		CbSSGI* ptr = m_SSGI_CB.GetPtr<CbSSGI>();
		ptr->ProjMatrix = proj;
		ptr->VRotPMatrix = viewRotProj;
		ptr->InvVRotPMatrix = viewRotProj.Invert();
		ptr->FrameSampleIndex = m_TemporalAASampleIndex;
		ptr->Intensity = m_SSGI_Intensity;
	}

	DirectX::TransitionResource(pCmdList, m_HCB_Target.GetResource(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	DirectX::TransitionResource(pCmdList, m_HZB_Target.GetResource(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	DirectX::TransitionResource(pCmdList, m_SceneNormalTarget.GetResource(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	DirectX::TransitionResource(pCmdList, m_SSGI_Target.GetResource(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

	pCmdList->SetComputeRootSignature(m_SSGI_RootSig.GetPtr());
	pCmdList->SetPipelineState(m_pSSGI_PSO.Get());
	pCmdList->SetComputeRootDescriptorTable(0, m_SSGI_CB.GetHandleGPU());
	pCmdList->SetComputeRootDescriptorTable(1, m_HCB_Target.GetHandleSRV()->HandleGPU);
	pCmdList->SetComputeRootDescriptorTable(2, m_HZB_Target.GetHandleSRV()->HandleGPU);
	pCmdList->SetComputeRootDescriptorTable(3, m_SceneNormalTarget.GetHandleSRV()->HandleGPU);
	pCmdList->SetComputeRootDescriptorTable(4, m_SSGI_Target.GetHandleUAVs()[0]->HandleGPU);

	// シェーダ側と合わせている
	const size_t GROUP_SIZE_X = 4;
	const size_t GROUP_SIZE_Y = 4;

	// グループ数は切り上げ
	UINT NumGroupX = DivideAndRoundUp((uint32_t)m_SSGI_Target.GetDesc().Width, GROUP_SIZE_X);
	UINT NumGroupY = DivideAndRoundUp(m_SSGI_Target.GetDesc().Height, GROUP_SIZE_Y);
	UINT NumGroupZ = 1;
	pCmdList->Dispatch(NumGroupX, NumGroupY, NumGroupZ);

	DirectX::TransitionResource(pCmdList, m_HCB_Target.GetResource(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
	DirectX::TransitionResource(pCmdList, m_HZB_Target.GetResource(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
	DirectX::TransitionResource(pCmdList, m_SceneNormalTarget.GetResource(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
	DirectX::TransitionResource(pCmdList, m_SSGI_Target.GetResource(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
}

void SampleApp::DrawSSGI_Denoise(ID3D12GraphicsCommandList* pCmdList)
{
	ScopedTimer scopedTimer(pCmdList, L"SSGI Denoise");

	DirectX::TransitionResource(pCmdList, m_SSGI_Target.GetResource(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	DirectX::TransitionResource(pCmdList, m_SSGI_DenoiseTarget.GetResource(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

	pCmdList->SetComputeRootSignature(m_SSGI_DenoiseRootSig.GetPtr());
	pCmdList->SetPipelineState(m_pSSGI_DenoisePSO.Get());
	pCmdList->SetComputeRootDescriptorTable(0, m_SSGI_DenoiseCB.GetHandleGPU());
	pCmdList->SetComputeRootDescriptorTable(1, m_SSGI_Target.GetHandleSRV()->HandleGPU);
	pCmdList->SetComputeRootDescriptorTable(2, m_SSGI_DenoiseTarget.GetHandleUAVs()[0]->HandleGPU);

	// シェーダ側と合わせている
	const size_t GROUP_SIZE_X = 8;
	const size_t GROUP_SIZE_Y = 8;

	// グループ数は切り上げ
	UINT NumGroupX = DivideAndRoundUp((uint32_t)m_SSGI_DenoiseTarget.GetDesc().Width, GROUP_SIZE_X);
	UINT NumGroupY = DivideAndRoundUp(m_SSGI_DenoiseTarget.GetDesc().Height, GROUP_SIZE_Y);
	UINT NumGroupZ = 1;
	pCmdList->Dispatch(NumGroupX, NumGroupY, NumGroupZ);

	DirectX::TransitionResource(pCmdList, m_SSGI_Target.GetResource(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
	DirectX::TransitionResource(pCmdList, m_SSGI_DenoiseTarget.GetResource(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
}

// TODO: SSGIのTemporalAccumulationはUEの実装が汎用的すぎて参考にするのが難しいので一旦開発を止めている
void SampleApp::DrawSSGI_TemporalAccumulation(ID3D12GraphicsCommandList* pCmdList, const ColorTarget& prevTarget, const ColorTarget& curTarget)
{
	ScopedTimer scopedTimer(pCmdList, L"SSGI TemporalAccumulation");

	DirectX::TransitionResource(pCmdList, m_SSGI_DenoiseTarget.GetResource(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	DirectX::TransitionResource(pCmdList, prevTarget.GetResource(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	DirectX::TransitionResource(pCmdList, m_VelocityTarget.GetResource(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	DirectX::TransitionResource(pCmdList, curTarget.GetResource(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

	pCmdList->SetComputeRootSignature(m_SSGI_TemporalAccumulationRootSig.GetPtr());
	pCmdList->SetPipelineState(m_pSSGI_TemporalAccumulationPSO.Get());
	pCmdList->SetComputeRootDescriptorTable(0, m_SSGI_DenoiseCB.GetHandleGPU());
	pCmdList->SetComputeRootDescriptorTable(1, m_SSGI_DenoiseTarget.GetHandleSRV()->HandleGPU);
	pCmdList->SetComputeRootDescriptorTable(2, prevTarget.GetHandleSRV()->HandleGPU);
	pCmdList->SetComputeRootDescriptorTable(3, m_VelocityTarget.GetHandleSRV()->HandleGPU);
	pCmdList->SetComputeRootDescriptorTable(4, curTarget.GetHandleUAVs()[0]->HandleGPU);

	// シェーダ側と合わせている
	const size_t GROUP_SIZE_X = 8;
	const size_t GROUP_SIZE_Y = 8;

	// グループ数は切り上げ
	UINT NumGroupX = DivideAndRoundUp((uint32_t)curTarget.GetDesc().Width, GROUP_SIZE_X);
	UINT NumGroupY = DivideAndRoundUp(curTarget.GetDesc().Height, GROUP_SIZE_Y);
	UINT NumGroupZ = 1;
	pCmdList->Dispatch(NumGroupX, NumGroupY, NumGroupZ);

	DirectX::TransitionResource(pCmdList, m_SSGI_DenoiseTarget.GetResource(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
	DirectX::TransitionResource(pCmdList, prevTarget.GetResource(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
	DirectX::TransitionResource(pCmdList, m_VelocityTarget.GetResource(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
	DirectX::TransitionResource(pCmdList, curTarget.GetResource(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
}

void SampleApp::DrawAmbientLight(ID3D12GraphicsCommandList* pCmdList, const ColorTarget& SSGI_CurTarget)
{
	ScopedTimer scopedTimer(pCmdList, L"AmbientLight");

	DirectX::TransitionResource(pCmdList, m_AmbientLightTarget.GetResource(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);

	const DescriptorHandle* handleRTV = m_AmbientLightTarget.GetHandleRTV();
	pCmdList->OMSetRenderTargets(1, &handleRTV->HandleCPU, FALSE, nullptr);

	m_AmbientLightTarget.ClearView(pCmdList);

	pCmdList->SetGraphicsRootSignature(m_AmbientLightRootSig.GetPtr());
	pCmdList->SetGraphicsRootDescriptorTable(0, m_SceneColorTarget.GetHandleSRV()->HandleGPU);
	pCmdList->SetGraphicsRootDescriptorTable(1, m_SSAO_FullResTarget.GetHandleSRV()->HandleGPU);
	pCmdList->SetGraphicsRootDescriptorTable(2, SSGI_CurTarget.GetHandleSRV()->HandleGPU);
	pCmdList->SetPipelineState(m_pAmbientLightPSO.Get());

	pCmdList->RSSetViewports(1, &m_Viewport);
	pCmdList->RSSetScissorRects(1, &m_Scissor);
	
	pCmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	const D3D12_VERTEX_BUFFER_VIEW& VBV = m_QuadVB.GetView();
	pCmdList->IASetVertexBuffers(0, 1, &VBV);

	pCmdList->DrawInstanced(3, 1, 0, 0);

	DirectX::TransitionResource(pCmdList, m_AmbientLightTarget.GetResource(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
}

void SampleApp::DrawSSR(ID3D12GraphicsCommandList* pCmdList, const DirectX::SimpleMath::Matrix& proj, const DirectX::SimpleMath::Matrix& viewRotProj)
{
	ScopedTimer scopedTimer(pCmdList, L"SSR");

	{
		CbSSR* ptr = m_SSR_CB.GetPtr<CbSSR>();
		ptr->ProjMatrix = proj;
		ptr->VRotPMatrix = viewRotProj;
		ptr->InvVRotPMatrix = viewRotProj.Invert();
		ptr->FrameSampleIndex = m_TemporalAASampleIndex;
		ptr->Intensity = m_SSR_Intensity;
		ptr->bDebugViewSSR = m_debugViewSSR ? 1 : 0;
	}

	DirectX::TransitionResource(pCmdList, m_SSR_Target.GetResource(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);

	const DescriptorHandle* handleRTV = m_SSR_Target.GetHandleRTV();
	pCmdList->OMSetRenderTargets(1, &handleRTV->HandleCPU, FALSE, nullptr);

	m_SSR_Target.ClearView(pCmdList);

	pCmdList->SetGraphicsRootSignature(m_SSR_RootSig.GetPtr());
	pCmdList->SetGraphicsRootDescriptorTable(0, m_SSR_CB.GetHandleGPU());
	pCmdList->SetGraphicsRootDescriptorTable(1, m_AmbientLightTarget.GetHandleSRV()->HandleGPU);
	pCmdList->SetGraphicsRootDescriptorTable(2, m_SceneDepthTarget.GetHandleSRV()->HandleGPU);
	pCmdList->SetGraphicsRootDescriptorTable(3, m_SceneNormalTarget.GetHandleSRV()->HandleGPU);
	pCmdList->SetGraphicsRootDescriptorTable(4, m_SceneMetallicRoughnessTarget.GetHandleSRV()->HandleGPU);
	pCmdList->SetGraphicsRootDescriptorTable(5, m_HZB_Target.GetHandleSRV()->HandleGPU);
	pCmdList->SetPipelineState(m_pSSR_PSO.Get());

	pCmdList->RSSetViewports(1, &m_Viewport);
	pCmdList->RSSetScissorRects(1, &m_Scissor);
	
	pCmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	const D3D12_VERTEX_BUFFER_VIEW& VBV = m_QuadVB.GetView();
	pCmdList->IASetVertexBuffers(0, 1, &VBV);

	pCmdList->DrawInstanced(3, 1, 0, 0);

	DirectX::TransitionResource(pCmdList, m_SSR_Target.GetResource(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
}

void SampleApp::DrawVolumetricFogScattering(ID3D12GraphicsCommandList* pCmdList, const DirectX::SimpleMath::Matrix& viewRotProjNoJitter, const DirectX::SimpleMath::Matrix& viewProjNoJitter, const DirectX::SimpleMath::Matrix& prevViewProjNoJitter, const ColorTarget& prevTarget, const ColorTarget& curTarget)
{
	assert(RENDER_SPONZA);

	ScopedTimer scopedTimer(pCmdList, L"VolumetricFogScattering");

	{
		CbVolumetricFog* ptr = m_VolumetricFogCB.GetPtr<CbVolumetricFog>();
		ptr->InvVRotPMatrix = viewRotProjNoJitter.Invert();
		// これはfloat精度の誤差が入る。前フレームとVPが変わらなくても誤差で単位行列にはならない
		ptr->ClipToPrevClip = viewProjNoJitter.Invert() * prevViewProjNoJitter;
		ptr->FrameJitterOffsetValue = VolumetricFogTemporalRandom(m_FrameNumber);
		ptr->DirectionalLightScatteringIntensity = m_directionalLightVolumetricFogScatteringIntensity;
		ptr->SpotLightScatteringIntensity = m_spotLightVolumetricFogScatteringIntensity;
	}

	DirectX::TransitionResource(pCmdList, prevTarget.GetResource(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	DirectX::TransitionResource(pCmdList, m_DirLightShadowMapTarget.GetResource(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	for (uint32_t i = 0u; i < NUM_SPOT_LIGHTS; i++)
	{
		DirectX::TransitionResource(pCmdList, m_SpotLightShadowMapTarget[i].GetResource(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	}
	DirectX::TransitionResource(pCmdList, curTarget.GetResource(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

	pCmdList->SetComputeRootSignature(m_VolumetricFogScatteringRootSig.GetPtr());
	pCmdList->SetPipelineState(m_pVolumetricFogScatteringPSO.Get());
	pCmdList->SetComputeRootDescriptorTable(0, m_VolumetricFogCB.GetHandleGPU());
	pCmdList->SetComputeRootDescriptorTable(1, m_DirectionalLightCB[m_FrameIndex].GetHandleGPU());
	for (uint32_t i = 0u; i < NUM_SPOT_LIGHTS; i++)
	{
		pCmdList->SetComputeRootDescriptorTable(2 + i, m_SpotLightCB[i].GetHandleGPU());
	}
	pCmdList->SetComputeRootDescriptorTable(2 + NUM_SPOT_LIGHTS, m_CameraCB[m_FrameIndex].GetHandleGPU());
	pCmdList->SetComputeRootDescriptorTable(3 + NUM_SPOT_LIGHTS, m_TransformCB[m_FrameIndex].GetHandleGPU());

	pCmdList->SetComputeRootDescriptorTable(4 + NUM_SPOT_LIGHTS, prevTarget.GetHandleSRV()->HandleGPU);
	pCmdList->SetComputeRootDescriptorTable(5 + NUM_SPOT_LIGHTS, m_DirLightShadowMapTarget.GetHandleSRV()->HandleGPU);
	for (uint32_t i = 0u; i < NUM_SPOT_LIGHTS; i++)
	{
		pCmdList->SetComputeRootDescriptorTable(6 + NUM_SPOT_LIGHTS + i, m_SpotLightShadowMapTarget[i].GetHandleSRV()->HandleGPU);
	}

	pCmdList->SetComputeRootDescriptorTable(6 + NUM_SPOT_LIGHTS * 2, curTarget.GetHandleUAVs()[0]->HandleGPU);

	// シェーダ側と合わせている
	const size_t GROUP_SIZE_XYZ = 4;

	UINT NumGroupX = DivideAndRoundUp((uint32_t)curTarget.GetDesc().Width, GROUP_SIZE_XYZ);
	UINT NumGroupY = DivideAndRoundUp(curTarget.GetDesc().Height, GROUP_SIZE_XYZ);
	UINT NumGroupZ = DivideAndRoundUp(curTarget.GetDesc().DepthOrArraySize, GROUP_SIZE_XYZ);
	pCmdList->Dispatch(NumGroupX, NumGroupY, NumGroupZ);

	DirectX::TransitionResource(pCmdList, prevTarget.GetResource(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
	DirectX::TransitionResource(pCmdList, m_DirLightShadowMapTarget.GetResource(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
	for (uint32_t i = 0u; i < NUM_SPOT_LIGHTS; i++)
	{
		DirectX::TransitionResource(pCmdList, m_SpotLightShadowMapTarget[i].GetResource(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
	}
	DirectX::TransitionResource(pCmdList, curTarget.GetResource(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
}

void SampleApp::DrawVolumetricFogIntegration(ID3D12GraphicsCommandList* pCmdList, const ColorTarget& curTarget)
{
	assert(RENDER_SPONZA);

	ScopedTimer scopedTimer(pCmdList, L"VolumetricFogIntegration");

	DirectX::TransitionResource(pCmdList, curTarget.GetResource(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	DirectX::TransitionResource(pCmdList, m_VolumetricFogIntegrationTarget.GetResource(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

	pCmdList->SetComputeRootSignature(m_VolumetricFogIntegrationRootSig.GetPtr());
	pCmdList->SetPipelineState(m_pVolumetricFogIntegrationPSO.Get());
	pCmdList->SetComputeRootDescriptorTable(0, m_VolumetricFogCB.GetHandleGPU());
	pCmdList->SetComputeRootDescriptorTable(1, curTarget.GetHandleSRV()->HandleGPU);
	pCmdList->SetComputeRootDescriptorTable(2, m_VolumetricFogIntegrationTarget.GetHandleUAVs()[0]->HandleGPU);

	// シェーダ側と合わせている
	const size_t GROUP_SIZE_XY = 8;

	UINT NumGroupX = DivideAndRoundUp((uint32_t)m_VolumetricFogIntegrationTarget.GetDesc().Width, GROUP_SIZE_XY);
	UINT NumGroupY = DivideAndRoundUp(m_VolumetricFogIntegrationTarget.GetDesc().Height, GROUP_SIZE_XY);
	UINT NumGroupZ = 1;
	pCmdList->Dispatch(NumGroupX, NumGroupY, NumGroupZ);

	DirectX::TransitionResource(pCmdList, curTarget.GetResource(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
	DirectX::TransitionResource(pCmdList, m_VolumetricFogIntegrationTarget.GetResource(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
}

void SampleApp::DrawVolumetricFogComposition(ID3D12GraphicsCommandList* pCmdList)
{
	assert(RENDER_SPONZA);

	ScopedTimer scopedTimer(pCmdList, L"VolumetricFogComposition");

	DirectX::TransitionResource(pCmdList, m_VolumetricCompositionTarget.GetResource(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);

	const DescriptorHandle* handleRTV = m_VolumetricCompositionTarget.GetHandleRTV();
	pCmdList->OMSetRenderTargets(1, &handleRTV->HandleCPU, FALSE, nullptr);

	m_VolumetricCompositionTarget.ClearView(pCmdList);

	pCmdList->SetGraphicsRootSignature(m_VolumetricFogCompositionRootSig.GetPtr());
	pCmdList->SetGraphicsRootDescriptorTable(0, m_VolumetricFogCB.GetHandleGPU());
	pCmdList->SetGraphicsRootDescriptorTable(1, m_SSR_Target.GetHandleSRV()->HandleGPU);
	pCmdList->SetGraphicsRootDescriptorTable(2, m_SceneDepthTarget.GetHandleSRV()->HandleGPU);
	pCmdList->SetGraphicsRootDescriptorTable(3, m_VolumetricFogIntegrationTarget.GetHandleSRV()->HandleGPU);
	pCmdList->SetPipelineState(m_pVolumetricFogCompositionPSO.Get());

	pCmdList->RSSetViewports(1, &m_Viewport);
	pCmdList->RSSetScissorRects(1, &m_Scissor);
	
	pCmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	const D3D12_VERTEX_BUFFER_VIEW& VBV = m_QuadVB.GetView();
	pCmdList->IASetVertexBuffers(0, 1, &VBV);

	pCmdList->DrawInstanced(3, 1, 0, 0);

	DirectX::TransitionResource(pCmdList, m_VolumetricCompositionTarget.GetResource(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
}

void SampleApp::DrawTemporalAA(ID3D12GraphicsCommandList* pCmdList, float temporalJitetrPixelsX, float temporalJitetrPixelsY, const ColorTarget& prevTarget, const ColorTarget& curTarget)
{
	ScopedTimer scopedTimer(pCmdList, L"TemporalAA");

	{
		CbTemporalAA* ptr = m_TemporalAA_CB[m_FrameIndex].GetPtr<CbTemporalAA>();
		ptr->bEnableTemporalAA = (m_enableTemporalAA ? 1 : 0);

		// Referenced UE.
		float totalWeight = 0;
		for (int32_t sample = 0; sample < TEMPORAL_AA_NUM_PLUS_SAMPLE; sample++)
		{
			float pixelOffsetX = TEMPORAL_AA_NEIGHBORHOOD_SAMPLE_OFFSETS[sample][0] - temporalJitetrPixelsX;
			float pixelOffsetY = TEMPORAL_AA_NEIGHBORHOOD_SAMPLE_OFFSETS[sample][1] - temporalJitetrPixelsY;
			// Gaussian fit to Blackman-Haris filter. Sigma = 0.47
			float currWeight = expf(-2.29f * (pixelOffsetX * pixelOffsetX + pixelOffsetY * pixelOffsetY));

			//TODO: Vector4に[]演算子が無いので分岐で書くしかない
			if (sample % 4 == 0)
			{
				ptr->PlusWeights[sample / 4].x = currWeight;
			}
			else if (sample % 4 == 1)
			{
				ptr->PlusWeights[sample / 4].y = currWeight;
			}
			else if (sample % 4 == 2)
			{
				ptr->PlusWeights[sample / 4].z = currWeight;
			}
			else // sample % 4 == 3
			{
				ptr->PlusWeights[sample / 4].w = currWeight;
			}

			totalWeight += currWeight;
		}

		for (uint32_t j = 0; j < TEMPORAL_AA_NUM_PLUS_VECTOR4; j++)
		{
			ptr->PlusWeights[j] /= totalWeight;
		}
	}

	if (RENDER_SPONZA)
	{
		DirectX::TransitionResource(pCmdList, m_VolumetricCompositionTarget.GetResource(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	}
	else
	{
		DirectX::TransitionResource(pCmdList, m_SSR_Target.GetResource(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	}
	DirectX::TransitionResource(pCmdList, prevTarget.GetResource(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	DirectX::TransitionResource(pCmdList, m_VelocityTarget.GetResource(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	DirectX::TransitionResource(pCmdList, curTarget.GetResource(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

	pCmdList->SetComputeRootSignature(m_TemporalAA_RootSig.GetPtr());
	pCmdList->SetPipelineState(m_pTemporalAA_PSO.Get());
	pCmdList->SetComputeRootDescriptorTable(0, m_TemporalAA_CB[m_FrameIndex].GetHandleGPU());
	if (RENDER_SPONZA)
	{
		pCmdList->SetComputeRootDescriptorTable(1, m_VolumetricCompositionTarget.GetHandleSRV()->HandleGPU);
	}
	else
	{
		pCmdList->SetComputeRootDescriptorTable(1, m_SSR_Target.GetHandleSRV()->HandleGPU);
	}
	pCmdList->SetComputeRootDescriptorTable(2, prevTarget.GetHandleSRV()->HandleGPU);
	pCmdList->SetComputeRootDescriptorTable(3, m_VelocityTarget.GetHandleSRV()->HandleGPU);
	pCmdList->SetComputeRootDescriptorTable(4, curTarget.GetHandleUAVs()[0]->HandleGPU);

	// シェーダ側と合わせている
	const size_t GROUP_SIZE_X = 8;
	const size_t GROUP_SIZE_Y = 8;

	// グループ数は切り上げ
	UINT NumGroupX = DivideAndRoundUp(m_Width, GROUP_SIZE_X);
	UINT NumGroupY = DivideAndRoundUp(m_Height, GROUP_SIZE_Y);
	UINT NumGroupZ = 1;
	pCmdList->Dispatch(NumGroupX, NumGroupY, NumGroupZ);

	if (RENDER_SPONZA)
	{
		DirectX::TransitionResource(pCmdList, m_VolumetricCompositionTarget.GetResource(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
	}
	else
	{
		DirectX::TransitionResource(pCmdList, m_SSR_Target.GetResource(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
	}
	DirectX::TransitionResource(pCmdList, prevTarget.GetResource(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
	DirectX::TransitionResource(pCmdList, m_VelocityTarget.GetResource(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
	DirectX::TransitionResource(pCmdList, curTarget.GetResource(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
}

void SampleApp::DrawMotionBlur(ID3D12GraphicsCommandList* pCmdList, const ColorTarget& InputColor)
{
	ScopedTimer scopedTimer(pCmdList, L"MotionBlur");

	{
		CbMotionBlur* ptr = m_MotionBlurCB.GetPtr<CbMotionBlur>();
		ptr->Scale = m_motionBlurScale;
	}

	DirectX::TransitionResource(pCmdList, m_MotionBlurTarget.GetResource(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);

	const DescriptorHandle* handleRTV = m_MotionBlurTarget.GetHandleRTV();
	pCmdList->OMSetRenderTargets(1, &handleRTV->HandleCPU, FALSE, nullptr);

	m_MotionBlurTarget.ClearView(pCmdList);

	pCmdList->SetGraphicsRootSignature(m_MotionBlurRootSig.GetPtr());
	pCmdList->SetGraphicsRootDescriptorTable(0, m_MotionBlurCB.GetHandleGPU());
	pCmdList->SetGraphicsRootDescriptorTable(1, InputColor.GetHandleSRV()->HandleGPU);
	pCmdList->SetGraphicsRootDescriptorTable(2, m_VelocityTarget.GetHandleSRV()->HandleGPU);
	pCmdList->SetPipelineState(m_pMotionBlurPSO.Get());

	pCmdList->RSSetViewports(1, &m_Viewport);
	pCmdList->RSSetScissorRects(1, &m_Scissor);
	
	pCmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	const D3D12_VERTEX_BUFFER_VIEW& VBV = m_QuadVB.GetView();
	pCmdList->IASetVertexBuffers(0, 1, &VBV);

	pCmdList->DrawInstanced(3, 1, 0, 0);

	DirectX::TransitionResource(pCmdList, m_MotionBlurTarget.GetResource(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
}

void SampleApp::DrawBloomSetup(ID3D12GraphicsCommandList* pCmdList)
{
	ScopedTimer scopedTimer(pCmdList, L"BloomSetup");

	DirectX::TransitionResource(pCmdList, m_BloomSetupTarget[0].GetResource(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);

	const DescriptorHandle* handleRTV = m_BloomSetupTarget[0].GetHandleRTV();
	pCmdList->OMSetRenderTargets(1, &handleRTV->HandleCPU, FALSE, nullptr);

	pCmdList->SetGraphicsRootSignature(m_BloomSetupRootSig.GetPtr());
	pCmdList->SetGraphicsRootDescriptorTable(0, m_MotionBlurTarget.GetHandleSRV()->HandleGPU);
	pCmdList->SetPipelineState(m_pBloomSetupPSO.Get());

	D3D12_VIEWPORT halfResViewport = m_Viewport;
	halfResViewport.Width = (FLOAT)m_BloomSetupTarget[0].GetDesc().Width;
	halfResViewport.Height = (FLOAT)m_BloomSetupTarget[0].GetDesc().Height;
	pCmdList->RSSetViewports(1, &halfResViewport);

	D3D12_RECT halfResScissor = m_Scissor;
	halfResScissor.right = (LONG)m_BloomSetupTarget[0].GetDesc().Width;
	halfResScissor.bottom = (LONG)m_BloomSetupTarget[0].GetDesc().Height;
	pCmdList->RSSetScissorRects(1, &halfResScissor);
	
	pCmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	const D3D12_VERTEX_BUFFER_VIEW& VBV = m_QuadVB.GetView();
	pCmdList->IASetVertexBuffers(0, 1, &VBV);

	pCmdList->DrawInstanced(3, 1, 0, 0);

	DirectX::TransitionResource(pCmdList, m_BloomSetupTarget[0].GetResource(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
}

void SampleApp::DrawTonemap(ID3D12GraphicsCommandList* pCmdList)
{
	ScopedTimer scopedTimer(pCmdList, L"Tonemap");

	{
		CbTonemap* ptr = m_TonemapCB[m_FrameIndex].GetPtr<CbTonemap>();
		ptr->Type = m_TonemapType;
		ptr->ColorSpace = m_ColorSpace;
		ptr->BaseLuminance = m_BaseLuminance;
		ptr->MaxLuminance = m_MaxLuminance;
		ptr->BloomIntensity = m_BloomIntensity;
	}

	DirectX::TransitionResource(pCmdList, m_TonemapTarget.GetResource(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);

	const DescriptorHandle* handleRTV = m_TonemapTarget.GetHandleRTV();
	pCmdList->OMSetRenderTargets(1, &handleRTV->HandleCPU, FALSE, nullptr);

	m_TonemapTarget.ClearView(pCmdList);

	pCmdList->SetGraphicsRootSignature(m_TonemapRootSig.GetPtr());
	pCmdList->SetGraphicsRootDescriptorTable(0, m_TonemapCB[m_FrameIndex].GetHandleGPU());
	pCmdList->SetGraphicsRootDescriptorTable(1, m_MotionBlurTarget.GetHandleSRV()->HandleGPU);
	pCmdList->SetGraphicsRootDescriptorTable(2, m_BloomVerticalTarget[0].GetHandleSRV()->HandleGPU);
	pCmdList->SetPipelineState(m_pTonemapPSO.Get());

	pCmdList->RSSetViewports(1, &m_Viewport);
	pCmdList->RSSetScissorRects(1, &m_Scissor);
	
	pCmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	const D3D12_VERTEX_BUFFER_VIEW& VBV = m_QuadVB.GetView();
	pCmdList->IASetVertexBuffers(0, 1, &VBV);

	pCmdList->DrawInstanced(3, 1, 0, 0);

	DirectX::TransitionResource(pCmdList, m_TonemapTarget.GetResource(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
}

void SampleApp::DrawFXAA(ID3D12GraphicsCommandList* pCmdList)
{
	ScopedTimer scopedTimer(pCmdList, L"FXAA");

	{
		CbFXAA* ptr = m_FXAA_CB.GetPtr<CbFXAA>();
		ptr->bEnableFXAA = (m_enableFXAA ? 1 : 0);
		ptr->bEnableFXAAHighQuality = (m_enableFXAA_HighQuality ? 1 : 0);
	}

	DirectX::TransitionResource(pCmdList, m_FXAA_Target.GetResource(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);

	const DescriptorHandle* handleRTV = m_FXAA_Target.GetHandleRTV();
	pCmdList->OMSetRenderTargets(1, &handleRTV->HandleCPU, FALSE, nullptr);

	m_FXAA_Target.ClearView(pCmdList);

	pCmdList->SetGraphicsRootSignature(m_FXAA_RootSig.GetPtr());
	pCmdList->SetGraphicsRootDescriptorTable(0, m_FXAA_CB.GetHandleGPU());
	pCmdList->SetGraphicsRootDescriptorTable(1, m_TonemapTarget.GetHandleSRV()->HandleGPU);
	pCmdList->SetPipelineState(m_pFXAA_PSO.Get());

	pCmdList->RSSetViewports(1, &m_Viewport);
	pCmdList->RSSetScissorRects(1, &m_Scissor);
	
	pCmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	const D3D12_VERTEX_BUFFER_VIEW& VBV = m_QuadVB.GetView();
	pCmdList->IASetVertexBuffers(0, 1, &VBV);

	pCmdList->DrawInstanced(3, 1, 0, 0);

	DirectX::TransitionResource(pCmdList, m_FXAA_Target.GetResource(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
}

void SampleApp::DrawDownsample(ID3D12GraphicsCommandList* pCmdList, const ColorTarget& SrcColor, const ColorTarget& DstColor, uint32_t CBIdx)
{
	std::wstringstream markerName;
	markerName << L"Downsample ";
	markerName << DstColor.GetDesc().Width;
	markerName << L"x";
	markerName << DstColor.GetDesc().Height;
	ScopedTimer scopedTimer(pCmdList, markerName.str());

	DirectX::TransitionResource(pCmdList, DstColor.GetResource(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);

	const DescriptorHandle* handleRTV = DstColor.GetHandleRTV();
	pCmdList->OMSetRenderTargets(1, &handleRTV->HandleCPU, FALSE, nullptr);

	pCmdList->SetGraphicsRootSignature(m_DownsampleRootSig.GetPtr());
	pCmdList->SetGraphicsRootDescriptorTable(0, m_DownsampleCB[CBIdx].GetHandleGPU());
	pCmdList->SetGraphicsRootDescriptorTable(1, SrcColor.GetHandleSRV()->HandleGPU);
	pCmdList->SetPipelineState(m_pDownsamplePSO.Get());

	
	D3D12_VIEWPORT viewport = m_Viewport;
	viewport.Width = (FLOAT)DstColor.GetDesc().Width;
	viewport.Height = (FLOAT)DstColor.GetDesc().Height;
	pCmdList->RSSetViewports(1, &viewport);

	D3D12_RECT scissor = m_Scissor;
	scissor.right = (LONG)DstColor.GetDesc().Width;
	scissor.bottom = (LONG)DstColor.GetDesc().Height;
	pCmdList->RSSetScissorRects(1, &scissor);
	
	pCmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	const D3D12_VERTEX_BUFFER_VIEW& VBV = m_QuadVB.GetView();
	pCmdList->IASetVertexBuffers(0, 1, &VBV);

	pCmdList->DrawInstanced(3, 1, 0, 0);

	DirectX::TransitionResource(pCmdList, DstColor.GetResource(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
}

void SampleApp::DrawFilter(ID3D12GraphicsCommandList* pCmdList, const ColorTarget& SrcColor, const ColorTarget& IntermediateColor, const ColorTarget& DstColor, const ColorTarget& DownerResultColor, const ConstantBuffer& HorizontalConstantBuffer, const ConstantBuffer& VerticalConstantBuffer)
{
	// Horizontal Gaussian Filter
	{
		std::wstringstream markerName;
		markerName << L"FilterHorizontal ";
		markerName << IntermediateColor.GetDesc().Width;
		markerName << L"x";
		markerName << IntermediateColor.GetDesc().Height;
		ScopedTimer scopedTimer(pCmdList, markerName.str());

		DirectX::TransitionResource(pCmdList, IntermediateColor.GetResource(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);

		const DescriptorHandle* handleRTV = IntermediateColor.GetHandleRTV();
		pCmdList->OMSetRenderTargets(1, &handleRTV->HandleCPU, FALSE, nullptr);

		pCmdList->SetGraphicsRootSignature(m_FilterRootSig.GetPtr());
		pCmdList->SetGraphicsRootDescriptorTable(0, HorizontalConstantBuffer.GetHandleGPU());
		pCmdList->SetGraphicsRootDescriptorTable(1, SrcColor.GetHandleSRV()->HandleGPU);
		pCmdList->SetGraphicsRootDescriptorTable(2, DownerResultColor.GetHandleSRV()->HandleGPU);
		pCmdList->SetPipelineState(m_pFilterPSO.Get());

		D3D12_VIEWPORT viewport = m_Viewport;
		viewport.Width = (FLOAT)IntermediateColor.GetDesc().Width;
		viewport.Height = (FLOAT)IntermediateColor.GetDesc().Height;
		pCmdList->RSSetViewports(1, &viewport);

		D3D12_RECT scissor = m_Scissor;
		scissor.right = (LONG)IntermediateColor.GetDesc().Width;
		scissor.bottom = (LONG)IntermediateColor.GetDesc().Height;
		pCmdList->RSSetScissorRects(1, &scissor);
		
		pCmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		const D3D12_VERTEX_BUFFER_VIEW& VBV = m_QuadVB.GetView();
		pCmdList->IASetVertexBuffers(0, 1, &VBV);

		pCmdList->DrawInstanced(3, 1, 0, 0);

		DirectX::TransitionResource(pCmdList, IntermediateColor.GetResource(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
	}

	// Vertical Gaussian Filter
	{
		std::wstringstream markerName;
		markerName << L"FilterVertical ";
		markerName << DstColor.GetDesc().Width;
		markerName << L"x";
		markerName << DstColor.GetDesc().Height;
		ScopedTimer scopedTimer(pCmdList, markerName.str());

		DirectX::TransitionResource(pCmdList, DstColor.GetResource(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);

		const DescriptorHandle* handleRTV = DstColor.GetHandleRTV();
		pCmdList->OMSetRenderTargets(1, &handleRTV->HandleCPU, FALSE, nullptr);

		pCmdList->SetGraphicsRootSignature(m_FilterRootSig.GetPtr());
		pCmdList->SetGraphicsRootDescriptorTable(0, VerticalConstantBuffer.GetHandleGPU());
		pCmdList->SetGraphicsRootDescriptorTable(1, IntermediateColor.GetHandleSRV()->HandleGPU);
		pCmdList->SetGraphicsRootDescriptorTable(2, DownerResultColor.GetHandleSRV()->HandleGPU);
		pCmdList->SetPipelineState(m_pFilterPSO.Get());
		
		D3D12_VIEWPORT viewport = m_Viewport;
		viewport.Width = (FLOAT)DstColor.GetDesc().Width;
		viewport.Height = (FLOAT)DstColor.GetDesc().Height;
		pCmdList->RSSetViewports(1, &viewport);

		D3D12_RECT scissor = m_Scissor;
		scissor.right = (LONG)DstColor.GetDesc().Width;
		scissor.bottom = (LONG)DstColor.GetDesc().Height;
		pCmdList->RSSetScissorRects(1, &scissor);
		
		pCmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		const D3D12_VERTEX_BUFFER_VIEW& VBV = m_QuadVB.GetView();
		pCmdList->IASetVertexBuffers(0, 1, &VBV);

		pCmdList->DrawInstanced(3, 1, 0, 0);

		DirectX::TransitionResource(pCmdList, DstColor.GetResource(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
	}
}

void SampleApp::DrawBackBuffer(ID3D12GraphicsCommandList* pCmdList)
{
	std::wstring renderTargetName;
	switch (m_debugViewRenderTarget)
	{
		case DEBUG_VIEW_NONE:
			renderTargetName = L"Final Result";
			break;
		case DEBUG_VIEW_DEPTH:
			renderTargetName = L"Depth";
			break;
		case DEBUG_VIEW_NORMAL:
			renderTargetName = L"Normal";
			break;
		case DEBUG_VIEW_SSAO_FULL_RES:
			renderTargetName = L"SSAO Full Res";
			break;
		case DEBUG_VIEW_SSAO_HALF_RES:
			renderTargetName = L"SSAO Half Res";
			break;
		case DEBUG_VIEW_SSGI:
			renderTargetName = L"SSGI";
			break;
		case DEBUG_VIEW_VELOCITY:
			renderTargetName = L"Velocity";
			break;
		default:
			assert(false);
			break;
	}

	ScopedTimer scopedTimer(pCmdList, L"Draw " + renderTargetName + L"to BackBuffer");

	{
		CbSampleTexture* ptr = m_BackBufferCB.GetPtr<CbSampleTexture>();
		ptr->Contrast = m_debugViewContrast;

		switch (m_debugViewRenderTarget)
		{
			case DEBUG_VIEW_NONE:
			case DEBUG_VIEW_SSGI:
				ptr->bOnlyRedChannel = 0;
				ptr->Scale = 1.0f;
				ptr->Bias = 0.0f;
				break;
			case DEBUG_VIEW_DEPTH:
			case DEBUG_VIEW_SSAO_FULL_RES:
			case DEBUG_VIEW_SSAO_HALF_RES:
				ptr->bOnlyRedChannel = 1;
				ptr->Scale = 1.0f;
				ptr->Bias = 0.0f;
				break;
			case DEBUG_VIEW_NORMAL:
			case DEBUG_VIEW_VELOCITY:
				ptr->bOnlyRedChannel = 0;
				ptr->Scale = 0.5f;
				ptr->Bias = 0.5f;
				break;
			default:
				assert(false);
				break;
		}
	}

	// R8_UNORMとR10G10B10A2_UNORMではCopyResourceでは非対応でエラーが出るのでシェーダでコピーする

	//DirectX::TransitionResource(pCmd, m_SSAO_FullResTarget.GetResource(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_SOURCE);
	//DirectX::TransitionResource(pCmd, m_ColorTarget[m_FrameIndex].GetResource(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_COPY_DEST);
	//pCmd->CopyResource(m_ColorTarget[m_FrameIndex].GetResource(), m_SSAO_FullResTarget.GetResource());
	//DirectX::TransitionResource(pCmd, m_SSAO_FullResTarget.GetResource(), D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
	//DirectX::TransitionResource(pCmd, m_ColorTarget[m_FrameIndex].GetResource(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PRESENT);

	DirectX::TransitionResource(pCmdList, m_ColorTarget[m_FrameIndex].GetResource(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);

	const DescriptorHandle* handleRTV = m_ColorTarget[m_FrameIndex].GetHandleRTV();
	pCmdList->OMSetRenderTargets(1, &handleRTV->HandleCPU, FALSE, nullptr);

	m_ColorTarget[m_FrameIndex].ClearView(pCmdList);

	pCmdList->SetGraphicsRootSignature(m_BackBufferRootSig.GetPtr());
	pCmdList->SetPipelineState(m_pBackBufferPSO.Get());
	pCmdList->SetGraphicsRootDescriptorTable(0, m_BackBufferCB.GetHandleGPU());
	switch (m_debugViewRenderTarget)
	{
		case DEBUG_VIEW_NONE:
			pCmdList->SetGraphicsRootDescriptorTable(1, m_FXAA_Target.GetHandleSRV()->HandleGPU);
			break;
		case DEBUG_VIEW_DEPTH:
			pCmdList->SetGraphicsRootDescriptorTable(1, m_SceneDepthTarget.GetHandleSRV()->HandleGPU);
			break;
		case DEBUG_VIEW_NORMAL:
			pCmdList->SetGraphicsRootDescriptorTable(1, m_SceneNormalTarget.GetHandleSRV()->HandleGPU);
			break;
		case DEBUG_VIEW_SSAO_FULL_RES:
			pCmdList->SetGraphicsRootDescriptorTable(1, m_SSAO_FullResTarget.GetHandleSRV()->HandleGPU);
			break;
		case DEBUG_VIEW_SSAO_HALF_RES:
			pCmdList->SetGraphicsRootDescriptorTable(1, m_SSAO_HalfResTarget.GetHandleSRV()->HandleGPU);
			break;
		case DEBUG_VIEW_SSGI:
			pCmdList->SetGraphicsRootDescriptorTable(1, m_SSGI_DenoiseTarget.GetHandleSRV()->HandleGPU);
			break;
		case DEBUG_VIEW_VELOCITY:
			pCmdList->SetGraphicsRootDescriptorTable(1, m_VelocityTarget.GetHandleSRV()->HandleGPU);
			break;
		default:
			assert(false);
			break;
	}

	// BackBufferのサイズはウィンドウサイズになっているのでアスペクト比を維持する
	DXGI_SWAP_CHAIN_DESC desc;
	m_pSwapChain->GetDesc(&desc);

	D3D12_VIEWPORT viewport = m_Viewport;
	if ((float)desc.BufferDesc.Width / desc.BufferDesc.Height < (float)m_Width / m_Height)
	{
		viewport.Width = (float)desc.BufferDesc.Width;
		viewport.Height = desc.BufferDesc.Width * ((float)m_Height / m_Width);
		viewport.TopLeftX = 0.0f;
		viewport.TopLeftY = desc.BufferDesc.Height * 0.5f - viewport.Height * 0.5f;
	}
	else
	{
		viewport.Height = (float)desc.BufferDesc.Height;
		viewport.Width = desc.BufferDesc.Height * ((float)m_Width / m_Height);
		viewport.TopLeftX = desc.BufferDesc.Width * 0.5f - viewport.Width * 0.5f;
		viewport.TopLeftY = 0.0f;
	}

	pCmdList->RSSetViewports(1, &viewport);
	pCmdList->RSSetScissorRects(1, &m_Scissor);
	
	pCmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	const D3D12_VERTEX_BUFFER_VIEW& VBV = m_QuadVB.GetView();
	pCmdList->IASetVertexBuffers(0, 1, &VBV);

	pCmdList->DrawInstanced(3, 1, 0, 0);

	DirectX::TransitionResource(pCmdList, m_ColorTarget[m_FrameIndex].GetResource(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
}

void SampleApp::DrawImGui(ID3D12GraphicsCommandList* pCmdList)
{
	ScopedTimer scopedTimer(pCmdList, L"ImGui");

	// TODO: Transitionが直前のパスと重複している
	DirectX::TransitionResource(pCmdList, m_ColorTarget[m_FrameIndex].GetResource(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);

	const DescriptorHandle* handleRTV = m_ColorTarget[m_FrameIndex].GetHandleRTV();
	pCmdList->OMSetRenderTargets(1, &handleRTV->HandleCPU, FALSE, nullptr);

	// https://github.com/ocornut/imgui/wiki/Getting-Started#example-if-you-are-using-raw-win32-api--directx12を参考にしている
	ImGui_ImplDX12_NewFrame();
	ImGui_ImplWin32_NewFrame();
	ImGui::NewFrame();

	ImGui::Begin("Render Settings");

	// imgui_demo.cppを参考にしている。右列のラベル部分のサイズを固定する
    ImGui::PushItemWidth(ImGui::GetFontSize() * -12);

	ImGui::SeparatorText("Debug View");
	ImGui::RadioButton("No Debug View", &m_debugViewRenderTarget, DEBUG_VIEW_NONE);
	ImGui::RadioButton("Depth", &m_debugViewRenderTarget, DEBUG_VIEW_DEPTH);
	ImGui::RadioButton("Normal", &m_debugViewRenderTarget, DEBUG_VIEW_NORMAL);
	ImGui::RadioButton("Velocity", &m_debugViewRenderTarget, DEBUG_VIEW_VELOCITY);
	ImGui::RadioButton("SSAO FullRes", &m_debugViewRenderTarget, DEBUG_VIEW_SSAO_FULL_RES);
	ImGui::RadioButton("SSAO HalfRes", &m_debugViewRenderTarget, DEBUG_VIEW_SSAO_HALF_RES);
	ImGui::RadioButton("SSGI", &m_debugViewRenderTarget, DEBUG_VIEW_SSGI);
	ImGui::SliderFloat("Debug View Contrast", &m_debugViewContrast, 0.01f, 100.0f, "%f", ImGuiSliderFlags_Logarithmic);

	ImGui::SeparatorText("Light Intensity");
	ImGui::SliderFloat("Dir Light Intensity", &m_directionalLightIntensity, 0.0f, 100.0f);
	ImGui::SliderFloat("Point Light Intensity", &m_pointLightIntensity, 0.0f, 1000.0f);
	ImGui::SliderFloat("Spot Light Intensity", &m_spotLightIntensity, 0.0f, 10000.0f);

	ImGui::SeparatorText("Velocity and Motion Blur");
	ImGui::Checkbox("Move Flower Base", &m_moveFlowerVase);
	ImGui::Checkbox("Generate Velocity", &m_enableVelocity);
	ImGui::SliderFloat("Motion Blur Scale", &m_motionBlurScale, 0.0f, 10.0f);

	ImGui::SeparatorText("SSAO");
	ImGui::SliderFloat("SSAO Contrast", &m_SSAO_Contrast, 0.01f, 10.0f, "%f", ImGuiSliderFlags_Logarithmic);
	ImGui::SliderFloat("SSAO Intensity", &m_SSAO_Intensity, 0.0f, 1.0f);

	ImGui::SeparatorText("SSGI(WIP)");
	ImGui::SliderFloat("SSGI Intensity", &m_SSGI_Intensity, 0.0f, 1.0f);

	ImGui::SeparatorText("Volumetric Fog Intensity");
	ImGui::SliderFloat("Dir Light Scattering", &m_directionalLightVolumetricFogScatteringIntensity, 0.0f, 10000.0f);
	ImGui::SliderFloat("Spot Light Scattering", &m_spotLightVolumetricFogScatteringIntensity, 0.0f, 100000.0f);

	ImGui::SeparatorText("AA");
	ImGui::Checkbox("Temporal AA", &m_enableTemporalAA);
	ImGui::Checkbox("FXAA", &m_enableFXAA);
	ImGui::Checkbox("FXAA High Quality", &m_enableFXAA_HighQuality);

	ImGui::SeparatorText("Tonemap");
	ImGui::RadioButton("No Tonemap", &m_TonemapType, TONEMAP_NONE);
	ImGui::RadioButton("Reinhard", &m_TonemapType, TONEMAP_REINHARD);
	ImGui::RadioButton("Gran Turismo", &m_TonemapType, TONEMAP_GT);
	ImGui::RadioButton("Khronos PBR Neutral", &m_TonemapType, TONEMAP_KHRONOS_PBR_NEUTRAL);

	ImGui::SeparatorText("Other Postprocess");
	ImGui::SliderFloat("SSR Intensity", &m_SSR_Intensity, 0.0f, 10.0f);
#if 0 // TODO: SSRだけでなくVolumetricFogも表示されるので、一旦機能をカット。将来的に他のパスについてもDebugViewを一通り揃えるなら改めてSSRについても正しいものを追加する
	ImGui::Checkbox("Debug View SSR", &m_debugViewSSR);
#endif
	ImGui::SliderFloat("Bloom Intensity", &m_BloomIntensity, 0.0f, 10.0f);

	ImGui::End();

	ImGui::Render();
	ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), pCmdList);

	DirectX::TransitionResource(pCmdList, m_ColorTarget[m_FrameIndex].GetResource(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
}

void SampleApp::ChangeDisplayMode(bool hdr)
{
	if (hdr)
	{
		if (!IsSupportHDR())
		{
			MessageBox
			(
				nullptr,
				TEXT("HDRがサポートされていないディスプレイです。"),
				TEXT("HDR非サポート"),
				MB_OK | MB_ICONINFORMATION
			);
			ELOG("Error : Display not support HDR.");
			return;
		}

		HRESULT hr = m_pSwapChain->SetColorSpace1(DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020);
		if (FAILED(hr))
		{
			MessageBox
			(
				nullptr,
				TEXT("ITU-R BT.2100 PQ Systemの色域設定に失敗しました。"),
				TEXT("色域設定失敗"),
				MB_OK | MB_ICONERROR
			);
			ELOG("Error : IDXGISwapChain::SetColorSpace1() Failed.");
			return;
		}

		DXGI_HDR_METADATA_HDR10 metaData = {};

		metaData.RedPrimary[0] = GetChromaticityCoord(0.708);
		metaData.RedPrimary[1] = GetChromaticityCoord(0.292);
		metaData.BluePrimary[0] = GetChromaticityCoord(0.170);
		metaData.BluePrimary[1] = GetChromaticityCoord(0.797);
		metaData.GreenPrimary[0] = GetChromaticityCoord(0.131);
		metaData.GreenPrimary[1] = GetChromaticityCoord(0.046);
		metaData.WhitePoint[0] = GetChromaticityCoord(0.3127);
		metaData.WhitePoint[1] = GetChromaticityCoord(0.3290);

		metaData.MaxMasteringLuminance = UINT(GetMaxLuminance() * 10000);
		metaData.MinMasteringLuminance = UINT(GetMinLuminance() * 0.001);

		metaData.MaxContentLightLevel = 2000;

		hr = m_pSwapChain->SetHDRMetaData(DXGI_HDR_METADATA_TYPE_HDR10, sizeof(DXGI_HDR_METADATA_HDR10), &metaData);
		if (FAILED(hr))
		{
			ELOG("Error : IDXGISwapChain::SetHDRMetaData() Failed.");
		}

		m_BaseLuminance = 100.0f;
		m_MaxLuminance = GetMaxLuminance();

		std::string message;
		message += "HDRディスプレイ用に設定を変更しました\n\n";
		message += "色空間：ITU-R BT.2100 PQ\n";
		message += "最大輝度値：";
		message += std::to_string(GetMaxLuminance());
		message += " [nit]\n";
		message += "最小輝度値：";
		message += std::to_string(GetMinLuminance());
		message += " [nit]\n";

		MessageBoxA
		(
			nullptr,
			message.c_str(),
			"HDR設定成功",
			MB_OK | MB_ICONINFORMATION
		);
	}
	else
	{
		HRESULT hr = m_pSwapChain->SetColorSpace1(DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709);
		if (FAILED(hr))
		{
			MessageBox
			(
				nullptr,
				TEXT("ITU-R BT.709の色域設定に失敗しました。"),
				TEXT("色域設定失敗"),
				MB_OK | MB_ICONERROR
			);
			ELOG("Error : IDXGISwapChain::SetColorSpace1() Failed.");
			return;
		}

		DXGI_HDR_METADATA_HDR10 metaData = {};

		metaData.RedPrimary[0] = GetChromaticityCoord(0.640);
		metaData.RedPrimary[1] = GetChromaticityCoord(0.330);
		metaData.BluePrimary[0] = GetChromaticityCoord(0.300);
		metaData.BluePrimary[1] = GetChromaticityCoord(0.600);
		metaData.GreenPrimary[0] = GetChromaticityCoord(0.150);
		metaData.GreenPrimary[1] = GetChromaticityCoord(0.060);
		metaData.WhitePoint[0] = GetChromaticityCoord(0.3127);
		metaData.WhitePoint[1] = GetChromaticityCoord(0.3290);

		metaData.MaxMasteringLuminance = UINT(GetMaxLuminance() * 10000);
		metaData.MinMasteringLuminance = UINT(GetMinLuminance() * 0.001);

		metaData.MaxContentLightLevel = 100;

		hr = m_pSwapChain->SetHDRMetaData(DXGI_HDR_METADATA_TYPE_HDR10, sizeof(DXGI_HDR_METADATA_HDR10), &metaData);
		if (FAILED(hr))
		{
			ELOG("Error : IDXGISwapChain::SetHDRMetaData() Failed.");
		}

		m_BaseLuminance = 100.0f;
		m_MaxLuminance = 100.0f;

		std::string message;
		message += "SDRディスプレイ用に設定を変更しました\n\n";
		message += "色空間：ITU-R BT.709\n";
		message += "最大輝度値：";
		message += std::to_string(GetMaxLuminance());
		message += " [nit]\n";
		message += "最小輝度値：";
		message += std::to_string(GetMinLuminance());
		message += " [nit]\n";

		MessageBoxA
		(
			nullptr,
			message.c_str(),
			"SDR設定成功",
			MB_OK | MB_ICONINFORMATION
		);
	}
}

bool SampleApp::OnMsgProc(HWND hWnd, UINT msg, WPARAM wp, LPARAM lp)
{
	// https://github.com/ocornut/imgui/wiki/Getting-Started#example-if-you-are-using-raw-win32-api--directx12を参考にしている
	extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
	if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wp, lp))
	{
		return false;
	}

	// imguiウィンドウ内でマウスイベントを扱っているときは他のウィンドウでマウスイベントは扱わない
	if (ImGui::GetIO().WantCaptureMouse)
	{
		return false;
	}

	if (
		(msg == WM_KEYDOWN)
		|| (msg == WM_SYSKEYDOWN)
		|| (msg == WM_KEYUP)
		|| (msg == WM_SYSKEYUP)
	)
	{
		DWORD mask = (1 << 29);

		bool isKeyDown = ((msg == WM_KEYDOWN) || (msg == WM_SYSKEYDOWN));
		bool isAltDown = ((lp & mask) != 0);
		uint32_t keyCode = uint32_t(wp);

		if (isKeyDown)
		{
			switch (keyCode)
			{
				case VK_ESCAPE:
					PostQuitMessage(0);
					break;
				case 'H':
					ChangeDisplayMode(true);
					break;
				case 'S':
					ChangeDisplayMode(false);
					break;
				case 'C':
					m_Camera.Reset();
					break;
				default:
					break;
			}
		}
	}

	const UINT OLD_WM_MOUSEWHEEL = 0x020A;

	if (
		(msg == WM_LBUTTONDOWN)
		|| (msg == WM_LBUTTONUP)
		|| (msg == WM_LBUTTONDBLCLK)
		|| (msg == WM_MBUTTONDOWN)
		|| (msg == WM_MBUTTONUP)
		|| (msg == WM_MBUTTONDBLCLK)
		|| (msg == WM_RBUTTONDOWN)
		|| (msg == WM_RBUTTONUP)
		|| (msg == WM_RBUTTONDBLCLK)
		|| (msg == WM_XBUTTONDOWN)
		|| (msg == WM_XBUTTONUP)
		|| (msg == WM_XBUTTONDBLCLK)
		|| (msg == WM_MOUSEHWHEEL)
		|| (msg == WM_MOUSEMOVE)
		|| (msg == OLD_WM_MOUSEWHEEL)
	)
	{
		int x = int(LOWORD(lp));
		int y = int(HIWORD(lp));

		int delta = 0;
		if (msg == WM_MOUSEHWHEEL || msg == OLD_WM_MOUSEWHEEL)
		{
			POINT pt;
			pt.x = x;
			pt.y = y;

			ScreenToClient(hWnd, &pt);
			x = pt.x;
			y = pt.y;
		}

		int state = int(LOWORD(wp));
		bool left = ((state & MK_LBUTTON) != 0);
		bool right = ((state & MK_RBUTTON) != 0);
		bool middle = ((state & MK_MBUTTON) != 0);

		Camera::Event args = {};

		if (left)
		{
			args.Type = Camera::EventRotate;
			args.RotateH = DirectX::XMConvertToRadians(-0.5f * (x - m_PrevCursorX));
			args.RotateV = DirectX::XMConvertToRadians(0.5f * (y - m_PrevCursorY));
			m_Camera.UpdateByEvent(args);
		}
		else if (right)
		{
			args.Type = Camera::EventDolly;
			args.Dolly = DirectX::XMConvertToRadians(0.5f * (y - m_PrevCursorY));
			m_Camera.UpdateByEvent(args);
		}
		else if (middle)
		{
			args.Type = Camera::EventMove;
			if (GetAsyncKeyState(VK_MENU) != 0)
			{
				args.MoveX = DirectX::XMConvertToRadians(0.5f * (x - m_PrevCursorX));
				args.MoveZ = DirectX::XMConvertToRadians(0.5f * (y - m_PrevCursorY));
			}
			else
			{
				args.MoveX = DirectX::XMConvertToRadians(0.5f * (x - m_PrevCursorX));
				args.MoveY = DirectX::XMConvertToRadians(0.5f * (y - m_PrevCursorY));
			}
			m_Camera.UpdateByEvent(args);
		}

		m_PrevCursorX = x;
		m_PrevCursorY = y;
	}

	return true;
}
