#include "SampleApp.h"
#include <sstream>
#include <DirectXMath.h>
#include <CommonStates.h>
#include <DirectXHelpers.h>
#include "FileUtil.h"
#include "Logger.h"
#include "ResMesh.h"
#include "Mesh.h"
#include "InlineUtil.h"
#include "RootSignature.h"
#include "ScopedTimer.h"

// シェーダ側にも同じ定数があるので変えるときは同時に変えること
#define USE_COMPARISON_SAMPLER_FOR_SHADOW_MAP

#define ENABLE_SSAO true
#define ENABLE_TEMPORAL_AA false
#define ENABLE_BLOOM false
#define ENABLE_MOTION_BLUR false

#define DEBUG_VIEW_SSAO false

using namespace DirectX::SimpleMath;

namespace
{
	static constexpr float CAMERA_FOV_Y_DEGREE = 37.5f;
	static constexpr float CAMERA_NEAR = 0.1f;
	static constexpr float CAMERA_FAR = 100.0f;

	static constexpr uint32_t SPOT_LIGHT_SHADOW_MAP_SIZE = 512; // TODO:ModelViewerを参考にした

	static constexpr uint32_t TEMPORAL_AA_SAMPLES = 11;

	// シェーダ側のマクロ定数と同じ値である必要がある
	// 定数バッファ内配列のfloat4へのパッキングルールがあるので4の倍数である必要がある
	static constexpr uint32_t GAUSSIAN_FILTER_SAMPLES = 32;

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
	};

	struct alignas(256) CbMesh
	{
		Matrix World;
	};

	struct alignas(256) CbTransform
	{
		Matrix ViewProj;
		Matrix ModelToDirLightShadowMap;
		Matrix ModelToSpotLight1ShadowMap;
		Matrix ModelToSpotLight2ShadowMap;
		Matrix ModelToSpotLight3ShadowMap;
	};

	struct alignas(256) CbDirectionalLight
	{
		Vector3 LightColor;
		float LightIntensity;
		Vector3 LightForward;
		float ShadowMapTexelSize;
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
		float ShadowMapTexelSize;
		float Padding[1];
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
		float AlphaCutoff;
		float Padding[2];
	};

	// TODO: Width/Heightは多くのSSシェーダで定数バッファにしているので共通化したい
	struct alignas(256) CbSSAOSetup
	{
		int Width;
		int Height;
		float Padding[2];
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
		int bEnableSSAO;
		float Padding[2];
	};

	struct alignas(256) CbCameraVelocity
	{
		Matrix ClipToPrevClip;
	};

	struct alignas(256) CbTemporalAA
	{
		Matrix ClipToPrevClip;
		int Width;
		int Height;
		int bEnableTemporalAA;
		float Padding[1];
	};

	// TODO: Width/Heightは多くのSSシェーダで定数バッファにしているので共通化したい
	struct alignas(256) CbMotionBlur
	{
		int Width;
		int Height;
		int bEnableMotionBlur;
		float Padding[1];
	};

	struct alignas(256) CbTonemap
	{
		int Type;
		int ColorSpace;
		float BaseLuminance;
		float MaxLuminance;
		int bEnableBloom;
		float Padding[3];
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
		result.ShadowMapTexelSize = 1.0f / shadowMapSize;
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
, m_TonemapType(TONEMAP_NONE)
, m_ColorSpace(COLOR_SPACE_BT709)
, m_BaseLuminance(100.0f)
, m_MaxLuminance(100.0f)
, m_PrevCursorX(0)
, m_PrevCursorY(0)
, m_RotateAngle(0.0f)
, m_DirLightShadowMapViewport()
, m_DirLightShadowMapScissor()
, m_TemporalAASampleIndex(0)
, m_PrevViewProjNoJitter(Matrix::Identity)
{
}

SampleApp::~SampleApp()
{
}

bool SampleApp::OnInit()
{
	// メッシュをロード
	{
		std::wstring path;
		//if (!SearchFilePath(L"res/matball/matball.obj", path))
		if (!SearchFilePath(L"res/SponzaKhronos/glTF/Sponza.gltf", path))
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

		m_pMesh.reserve(resMesh.size());

		for (size_t i = 0; i < resMesh.size(); i++)
		{
			Mesh* mesh = new (std::nothrow) Mesh();
			if (mesh == nullptr)
			{
				ELOG("Error : Out of memory.");
				return false;
			}

			if (!mesh->Init(m_pDevice.Get(), resMesh[i]))
			{
				ELOG("Error : Mesh Initialize Failed.");
				delete mesh;
				return false;
			}

			m_pMesh.push_back(mesh);
		}

		m_pMesh.shrink_to_fit();

		// TODO:Materialはとりあえず最初は一種類しか作らない。テクスチャの差し替えで使いまわす
		if (!m_Material.Init(m_pDevice.Get(), m_pPool[POOL_TYPE_RES], sizeof(CbMaterial), resMaterial.size())) // ContantBufferはこの時点では作らない。テクスチャはダミー。
		{
			ELOG("Error : Material Initialize Failed.");
			return false;
		}

		DirectX::ResourceUploadBatch batch(m_pDevice.Get());

		batch.Begin();

		std::wstring dir = GetDirectoryPath(path.c_str());
		for (size_t i = 0; i < resMaterial.size(); i++)
		{
			m_Material.SetTexture(i, Material::TEXTURE_USAGE_BASE_COLOR, dir + resMaterial[i].BaseColorMap, batch);
			m_Material.SetTexture(i, Material::TEXTURE_USAGE_METALLIC_ROUGHNESS, dir + resMaterial[i].MetallicRoughnessMap, batch);
			m_Material.SetTexture(i, Material::TEXTURE_USAGE_NORMAL, dir + resMaterial[i].NormalMap, batch);

			m_Material.SetDoubleSided(i, resMaterial[i].DoubleSided);

			CbMaterial* ptr = m_Material.GetBufferPtr<CbMaterial>(i);
			ptr->BaseColorFactor = resMaterial[i].BaseColor;
			ptr->MetallicFactor = resMaterial[i].MetallicFactor;
			ptr->RoughnessFactor = resMaterial[i].RoughnessFactor;
			ptr->AlphaCutoff = resMaterial[i].AlphaCutoff;
		}

		std::future<void> future = batch.End(m_pQueue.Get());
		future.wait();
	}

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
		*ptr = ComputePointLight(Vector3(-4.95f, 1.10f, 1.15f), 20.0f, Vector3(1.0f, 1.0f, 0.5f), 100.0f);

		ptr = m_PointLightCB[1].GetPtr<CbPointLight>();
		// 少し黄色っぽい光
		//*ptr = ComputePointLight(Vector3(-4.95f, 1.10f, -1.75f), 5.0f, Vector3(1.0f, 1.0f, 0.5f), 100.0f);
		*ptr = ComputePointLight(Vector3(-4.95f, 1.10f, -1.75f), 20.0f, Vector3(1.0f, 1.0f, 0.5f), 100.0f);

		ptr = m_PointLightCB[2].GetPtr<CbPointLight>();
		// 少し黄色っぽい光
		//*ptr = ComputePointLight(Vector3(3.90f, 1.10f, 1.15f), 5.0f, Vector3(1.0f, 1.0f, 0.5f), 100.0f);
		*ptr = ComputePointLight(Vector3(3.90f, 1.10f, 1.15f), 20.0f, Vector3(1.0f, 1.0f, 0.5f), 100.0f);

		ptr = m_PointLightCB[3].GetPtr<CbPointLight>();
		// 少し黄色っぽい光
		//*ptr = ComputePointLight(Vector3(3.90f, 1.10f, -1.75f), 5.0f, Vector3(1.0f, 1.0f, 0.5f), 100.0f);
		*ptr = ComputePointLight(Vector3(3.90f, 1.10f, -1.75f), 20.0f, Vector3(1.0f, 1.0f, 0.5f), 100.0f);
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
		*ptr = ComputeSpotLight(0, SpotLight1Dir, SpotLight1Pos, 20.0f, Vector3(1.0f, 0.5f, 0.5f), 1000.0f, DirectX::XMConvertToRadians(5.0f), DirectX::XMConvertToRadians(10.0f), SPOT_LIGHT_SHADOW_MAP_SIZE);
		CbTransform* tptr = m_SpotLightShadowMapTransformCB[0].GetPtr<CbTransform>();
		tptr->ViewProj = ComputeSpotLightViewProj(SpotLight1Dir, SpotLight1Pos, 20.0f, DirectX::XMConvertToRadians(10.0f));

		const Vector3& SpotLight2Dir = Vector3(0.0f, -10.0f, 2.0f);
		const Vector3& SpotLight2Pos = Vector3(0.0f, 10.0f, 0.0f);
		ptr = m_SpotLightCB[1].GetPtr<CbSpotLight>();
		// 少し緑っぽい光
		*ptr = ComputeSpotLight(0, SpotLight2Dir, SpotLight2Pos, 20.0f, Vector3(0.5f, 1.0f, 0.5f), 1000.0f, DirectX::XMConvertToRadians(5.0f), DirectX::XMConvertToRadians(10.0f), SPOT_LIGHT_SHADOW_MAP_SIZE);

		tptr = m_SpotLightShadowMapTransformCB[1].GetPtr<CbTransform>();
		tptr->ViewProj = ComputeSpotLightViewProj(SpotLight2Dir, SpotLight2Pos, 20.0f, DirectX::XMConvertToRadians(10.0f));

		const Vector3& SpotLight3Dir = Vector3(20.0f, -4.0f, 0.0f);
		const Vector3& SpotLight3Pos = Vector3(0.0f, 4.0f, 0.0f);
		ptr = m_SpotLightCB[2].GetPtr<CbSpotLight>();
		// 少し青っぽい光
		*ptr = ComputeSpotLight(0, SpotLight3Dir, SpotLight3Pos, 20.0f, Vector3(0.5f, 0.5f, 1.0f), 1000.0f, DirectX::XMConvertToRadians(5.0f), DirectX::XMConvertToRadians(10.0f), SPOT_LIGHT_SHADOW_MAP_SIZE);

		tptr = m_SpotLightShadowMapTransformCB[2].GetPtr<CbTransform>();
		tptr->ViewProj = ComputeSpotLightViewProj(SpotLight3Dir, SpotLight3Pos, 20.0f, DirectX::XMConvertToRadians(10.0f));
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

	// シーン用カラーターゲットの生成
	{
		float clearColor[4] = {0.2f, 0.2f, 0.2f, 1.0f};

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

	// SSAO準備パス用カラーターゲットの生成
	{
		float clearColor[4] = {0.0f, 0.0f, 0.0f, 1.0f};

		if (!m_SSAOSetup_Target.InitRenderTarget
		(
			m_pDevice.Get(),
			m_pPool[POOL_TYPE_RTV],
			m_pPool[POOL_TYPE_RES],
			(m_Width + 1) / 2, // 切り上げ
			(m_Height + 1) / 2, // 切り上げ
			DXGI_FORMAT_R16G16B16A16_FLOAT,
			clearColor
		))
		{
			ELOG("Error : ColorTarget::Init() Failed.");
			return false;
		}
	}

	// SSAO用カラーターゲットの生成
	{
		float clearColor[4] = {1.0f, 1.0f, 1.0f, 1.0f};

		if (!m_SSAO_Target.InitRenderTarget
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

		if (!m_SSAO_RandomizationTarget.InitFromData<uint16_t>
		(
			m_pDevice.Get(),
			pCmd,
			m_pPool[POOL_TYPE_RES],
			SSAO_RANDOMIZATIN_TEXTURE_SIZE,
			SSAO_RANDOMIZATIN_TEXTURE_SIZE,
			DXGI_FORMAT_R8G8_UNORM,
			texData.data() // TODO:書き込んだデータを渡す
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

	// CameraVelocity用カラーターゲットの生成
	{
		float clearColor[4] = {0.0f, 0.0f, 0.0f, 0.0f};

		if (!m_CameraVelocityTarget.InitRenderTarget
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

	// TemporalAA用ターゲットの生成
	{
		float clearColor[4] = {0.0f, 0.0f, 0.0f, 1.0f};

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
				ELOG("Error : ColorTarget::Init() Failed.");
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
			width = (width + 1) / 2; // 切り上げ
			height = (height + 1) / 2; // 切り上げ

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
			width = (width + 1) / 2; // 切り上げ
			height = (height + 1) / 2; // 切り上げ

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

    // シーン用ルートシグニチャの生成。デプスだけ描画するパスにも使用される
	{
		RootSignature::Desc desc;
		desc.Begin()
			.SetCBV(ShaderStage::VS, 0, 0)
			.SetCBV(ShaderStage::VS, 1, 1)
			.SetCBV(ShaderStage::PS, 2, 0)
			.SetCBV(ShaderStage::PS, 3, 1)
			.SetCBV(ShaderStage::PS, 4, 2)
			.SetCBV(ShaderStage::PS, 5, 3)
			.SetCBV(ShaderStage::PS, 6, 4)
			.SetCBV(ShaderStage::PS, 7, 5)
			.SetCBV(ShaderStage::PS, 8, 6)
			.SetCBV(ShaderStage::PS, 9, 7)
			.SetCBV(ShaderStage::PS, 10, 8)
			.SetCBV(ShaderStage::PS, 11, 9)

			.SetSRV(ShaderStage::PS, 12, 0)
			.SetSRV(ShaderStage::PS, 13, 1)
			.SetSRV(ShaderStage::PS, 14, 2)
			.SetSRV(ShaderStage::PS, 15, 3)
			.SetSRV(ShaderStage::PS, 16, 4)
			.SetSRV(ShaderStage::PS, 17, 5)
			.SetSRV(ShaderStage::PS, 18, 6)

			.AddStaticSmp(ShaderStage::PS, 0, SamplerState::AnisotropicWrap)
#ifdef USE_COMPARISON_SAMPLER_FOR_SHADOW_MAP
			.AddStaticCmpSmp(ShaderStage::PS, 1, SamplerState::MinMagLinearMipPointClamp)
#else
			.AddStaticSmp(ShaderStage::PS, 1, SamplerState::MinMagLinearMipPointClamp)
#endif
			.AllowIL()
			.End();

		if (!m_SceneRootSig.Init(m_pDevice.Get(), desc.GetDesc()))
		{
			ELOG("Error : RootSignature::Init() Failed.");
			return false;
		}
	}

    // シーン用パイプラインステートの生成
	{
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
		if (!SearchFilePath(L"BasicVS.cso", vsPath))
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
		std::wstring psPath;
		if (!SearchFilePath(L"DepthMaskPS.cso", psPath))
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

		desc.PS.pShaderBytecode = pPSBlob->GetBufferPointer();
		desc.PS.BytecodeLength = pPSBlob->GetBufferSize();
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
		if (!SearchFilePath(L"BasicOpaquePS.cso", psPath))
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

		desc.RasterizerState = DirectX::CommonStates::CullClockwise;
		desc.NumRenderTargets = 2;
		desc.RTVFormats[0] = m_SceneColorTarget.GetRTVDesc().Format;
		desc.RTVFormats[1] = m_SceneNormalTarget.GetRTVDesc().Format;
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
		if (!SearchFilePath(L"BasicMaskPS.cso", psPath))
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

    // SSAO準備パス用ルートシグニチャの生成
	{
		RootSignature::Desc desc;
		desc.Begin()
			.SetCBV(ShaderStage::PS, 0, 0)
			.SetSRV(ShaderStage::PS, 1, 0)
			.SetSRV(ShaderStage::PS, 2, 1)
			.AddStaticSmp(ShaderStage::PS, 0, SamplerState::PointClamp)
			.AllowIL()
			.End();

		if (!m_SSAOSetup_RootSig.Init(m_pDevice.Get(), desc.GetDesc()))
		{
			ELOG("Error : RootSignature::Init() Failed.");
			return false;
		}
	}

	// スクリーンスペース描画パス用のD3D12_GRAPHICS_PIPELINE_STATE_DESCの共通項
	D3D12_GRAPHICS_PIPELINE_STATE_DESC SSPassPSODescCommon = {};
	{
		D3D12_INPUT_ELEMENT_DESC elements[2];
		elements[0].SemanticName = "POSITION";
		elements[0].SemanticIndex = 0;
		elements[0].Format = DXGI_FORMAT_R32G32_FLOAT;
		elements[0].InputSlot = 0;
		elements[0].AlignedByteOffset = 0;
		elements[0].InputSlotClass = D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA;
		elements[0].InstanceDataStepRate = 0;

		elements[1].SemanticName = "TEXCOORD";
		elements[1].SemanticIndex = 0;
		elements[1].Format = DXGI_FORMAT_R32G32_FLOAT;
		elements[1].InputSlot = 0;
		elements[1].AlignedByteOffset = 8;
		elements[1].InputSlotClass = D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA;
		elements[1].InstanceDataStepRate = 0;

		SSPassPSODescCommon.InputLayout.pInputElementDescs = elements;
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

    // SSAO準備パス用パイプラインステートの生成
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


		D3D12_GRAPHICS_PIPELINE_STATE_DESC desc = SSPassPSODescCommon;
		desc.pRootSignature = m_SSAOSetup_RootSig.GetPtr();
		desc.VS.pShaderBytecode = pVSBlob->GetBufferPointer();
		desc.VS.BytecodeLength = pVSBlob->GetBufferSize();
		desc.PS.pShaderBytecode = pPSBlob->GetBufferPointer();
		desc.PS.BytecodeLength = pPSBlob->GetBufferSize();
		desc.RTVFormats[0] = m_SSAOSetup_Target.GetRTVDesc().Format;

		hr = m_pDevice->CreateGraphicsPipelineState(
			&desc,
			IID_PPV_ARGS(m_pSSAOSetup_PSO.GetAddressOf())
		);
		if (FAILED(hr))
		{
			ELOG("Error : ID3D12Device::CreateGraphicsPipelineState Failed. retcode = 0x%x", hr);
			return false;
		}
	}

    // SSAO用ルートシグニチャの生成
	{
		RootSignature::Desc desc;
		desc.Begin()
			.SetCBV(ShaderStage::PS, 0, 0)
			.SetSRV(ShaderStage::PS, 1, 0)
			.SetSRV(ShaderStage::PS, 2, 1)
			.SetSRV(ShaderStage::PS, 3, 2)
			.AddStaticSmp(ShaderStage::PS, 0, SamplerState::PointClamp)
			.AddStaticSmp(ShaderStage::PS, 1, SamplerState::PointWrap)
			.AllowIL()
			.End();

		if (!m_SSAO_RootSig.Init(m_pDevice.Get(), desc.GetDesc()))
		{
			ELOG("Error : RootSignature::Init() Failed.");
			return false;
		}
	}

    // SSAO用パイプラインステートの生成
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


		D3D12_GRAPHICS_PIPELINE_STATE_DESC desc = SSPassPSODescCommon;
		desc.pRootSignature = m_SSAO_RootSig.GetPtr();
		desc.VS.pShaderBytecode = pVSBlob->GetBufferPointer();
		desc.VS.BytecodeLength = pVSBlob->GetBufferSize();
		desc.PS.pShaderBytecode = pPSBlob->GetBufferPointer();
		desc.PS.BytecodeLength = pPSBlob->GetBufferSize();
		desc.RTVFormats[0] = m_SSAO_Target.GetRTVDesc().Format;

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

    // AmbientLight用ルートシグニチャの生成
	{
		RootSignature::Desc desc;
		desc.Begin()
			.SetSRV(ShaderStage::PS, 0, 0)
			.SetSRV(ShaderStage::PS, 1, 1)
			.AddStaticSmp(ShaderStage::PS, 0, SamplerState::PointClamp)
			.AllowIL()
			.End();

		if (!m_AmbientLightRootSig.Init(m_pDevice.Get(), desc.GetDesc()))
		{
			ELOG("Error : RootSignature::Init() Failed.");
			return false;
		}
	}

    // AmbientLight用パイプラインステートの生成
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

    // CameraVelocity用ルートシグニチャの生成
	{
		RootSignature::Desc desc;
		desc.Begin()
			.SetCBV(ShaderStage::PS, 0, 0)
			.SetSRV(ShaderStage::PS, 1, 0)
			.SetSRV(ShaderStage::PS, 2, 1)
			.AddStaticSmp(ShaderStage::PS, 0, SamplerState::PointClamp)
			.AllowIL()
			.End();

		if (!m_CameraVelocityRootSig.Init(m_pDevice.Get(), desc.GetDesc()))
		{
			ELOG("Error : RootSignature::Init() Failed.");
			return false;
		}
	}

    // CameraVelocity用パイプラインステートの生成
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

		D3D12_GRAPHICS_PIPELINE_STATE_DESC desc = SSPassPSODescCommon;
		desc.pRootSignature = m_CameraVelocityRootSig.GetPtr();
		desc.VS.pShaderBytecode = pVSBlob->GetBufferPointer();
		desc.VS.BytecodeLength = pVSBlob->GetBufferSize();
		desc.PS.pShaderBytecode = pPSBlob->GetBufferPointer();
		desc.PS.BytecodeLength = pPSBlob->GetBufferSize();
		desc.RTVFormats[0] = m_CameraVelocityTarget.GetRTVDesc().Format;

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

    // TemporalAA用ルートシグニチャの生成
	{
		RootSignature::Desc desc;
		desc.Begin()
			.SetCBV(ShaderStage::ALL, 0, 0)
			.SetSRV(ShaderStage::ALL, 1, 0)
			.SetSRV(ShaderStage::ALL, 2, 1)
			.SetSRV(ShaderStage::ALL, 3, 2)
			.SetUAV(ShaderStage::ALL, 4, 0)
			.AddStaticSmp(ShaderStage::ALL, 0, SamplerState::PointClamp)
			.End();

		if (!m_TemporalAA_RootSig.Init(m_pDevice.Get(), desc.GetDesc()))
		{
			ELOG("Error : RootSignature::Init() Failed.");
			return false;
		}
	}

    // TemporalAA用パイプラインステートの生成
	{
		std::wstring csPath;

		if (!SearchFilePath(L"TemporalAA_CS.cso", csPath))
		{
			ELOG("Error : Vertex Shader Not Found");
			return false;
		}

		ComPtr<ID3DBlob> pCSBlob;

		HRESULT hr = D3DReadFileToBlob(csPath.c_str(), pCSBlob.GetAddressOf());
		if (FAILED(hr))
		{
			ELOG("Error : D3DReadFileToBlob Failed. path = %ls", csPath.c_str());
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

    // MotionBlur用ルートシグニチャの生成
	{
		RootSignature::Desc desc;
		desc.Begin()
			.SetCBV(ShaderStage::PS, 0, 0)
			.SetSRV(ShaderStage::PS, 1, 0)
			.SetSRV(ShaderStage::PS, 2, 1)
			.AddStaticSmp(ShaderStage::PS, 0, SamplerState::PointClamp)
			.AllowIL()
			.End();

		if (!m_MotionBlurRootSig.Init(m_pDevice.Get(), desc.GetDesc()))
		{
			ELOG("Error : RootSignature::Init() Failed.");
			return false;
		}
	}

    // MotionBlur用パイプラインステートの生成
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

    // Bloom前工程用ルートシグニチャの生成
	{
		RootSignature::Desc desc;
		desc.Begin()
			.SetSRV(ShaderStage::PS, 0, 0)
			.AddStaticSmp(ShaderStage::PS, 0, SamplerState::PointClamp)
			.AllowIL()
			.End();

		if (!m_BloomSetupRootSig.Init(m_pDevice.Get(), desc.GetDesc()))
		{
			ELOG("Error : RootSignature::Init() Failed.");
			return false;
		}
	}

    // Bloom前工程用パイプラインステートの生成
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

    // トーンマップ用ルートシグニチャの生成
	{
		RootSignature::Desc desc;
		desc.Begin()
			.SetCBV(ShaderStage::PS, 0, 0)
			.SetSRV(ShaderStage::PS, 1, 0)
			.SetSRV(ShaderStage::PS, 2, 1)
			.AddStaticSmp(ShaderStage::PS, 0, SamplerState::PointClamp)
			.AllowIL()
			.End();

		if (!m_TonemapRootSig.Init(m_pDevice.Get(), desc.GetDesc()))
		{
			ELOG("Error : RootSignature::Init() Failed.");
			return false;
		}
	}

    // トーンマップ用パイプラインステートの生成
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

		D3D12_GRAPHICS_PIPELINE_STATE_DESC desc = SSPassPSODescCommon;
		desc.pRootSignature = m_TonemapRootSig.GetPtr();
		desc.VS.pShaderBytecode = pVSBlob->GetBufferPointer();
		desc.VS.BytecodeLength = pVSBlob->GetBufferSize();
		desc.PS.pShaderBytecode = pPSBlob->GetBufferPointer();
		desc.PS.BytecodeLength = pPSBlob->GetBufferSize();
		desc.RTVFormats[0] = m_ColorTarget[0].GetRTVDesc().Format;

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

    // 汎用ダウンサンプルパス用ルートシグニチャの生成
	{
		RootSignature::Desc desc;
		desc.Begin()
			.SetCBV(ShaderStage::PS, 0, 0)
			.SetSRV(ShaderStage::PS, 1, 0)
			.AddStaticSmp(ShaderStage::PS, 0, SamplerState::MinMagLinearMipPointClamp)
			.AllowIL()
			.End();

		if (!m_DownsampleRootSig.Init(m_pDevice.Get(), desc.GetDesc()))
		{
			ELOG("Error : RootSignature::Init() Failed.");
			return false;
		}
	}

    // 汎用ダウンサンプルパス用パイプラインステートの生成
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

    // 汎用フィルタ用ルートシグニチャの生成
	{
		RootSignature::Desc desc;
		desc.Begin()
			.SetCBV(ShaderStage::PS, 0, 0)
			.SetSRV(ShaderStage::PS, 1, 0)
			.SetSRV(ShaderStage::PS, 2, 1)
			.AddStaticSmp(ShaderStage::PS, 0, SamplerState::MinMagLinearMipPointBorder)
			.AllowIL()
			.End();

		if (!m_FilterRootSig.Init(m_pDevice.Get(), desc.GetDesc()))
		{
			ELOG("Error : RootSignature::Init() Failed.");
			return false;
		}
	}

    // 汎用フィルタ用パイプラインステートの生成
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

    // レンダーターゲットデバッグ表示用ルートシグニチャの生成
	{
		RootSignature::Desc desc;
		desc.Begin()
			.SetSRV(ShaderStage::PS, 0, 0)
			.AddStaticSmp(ShaderStage::PS, 0, SamplerState::PointClamp)
			.AllowIL()
			.End();

		if (!m_DebugRenderTargetRootSig.Init(m_pDevice.Get(), desc.GetDesc()))
		{
			ELOG("Error : RootSignature::Init() Failed.");
			return false;
		}
	}

    // レンダーターゲットデバッグ表示用パイプラインステートの生成
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

		D3D12_GRAPHICS_PIPELINE_STATE_DESC desc = SSPassPSODescCommon;
		desc.pRootSignature = m_DebugRenderTargetRootSig.GetPtr();
		desc.VS.pShaderBytecode = pVSBlob->GetBufferPointer();
		desc.VS.BytecodeLength = pVSBlob->GetBufferSize();
		desc.PS.pShaderBytecode = pPSBlob->GetBufferPointer();
		desc.PS.BytecodeLength = pPSBlob->GetBufferSize();
		desc.RTVFormats[0] = m_ColorTarget[0].GetRTVDesc().Format;

		hr = m_pDevice->CreateGraphicsPipelineState(
			&desc,
			IID_PPV_ARGS(m_pDebugRenderTargetPSO.GetAddressOf())
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
	}

	// SSAO用定数バッファの作成
	for (uint32_t i = 0; i < FRAME_COUNT; i++)
	{
		if (!m_SSAO_CB[i].Init(m_pDevice.Get(), m_pPool[POOL_TYPE_RES], sizeof(CbSSAO)))
		{
			ELOG("Error : ConstantBuffer::Init() Failed.");
			return false;
		}

		CbSSAO* ptr = m_SSAO_CB[i].GetPtr<CbSSAO>();
		ptr->ViewMatrix = Matrix::Identity;
		ptr->InvProjMatrix = Matrix::Identity;
		ptr->Width = m_Width;
		ptr->Height = m_Height;
		ptr->RandomationSize = Vector2((float)m_SSAO_RandomizationTarget.GetDesc().Width, (float)m_SSAO_RandomizationTarget.GetDesc().Height);
		// UE5は%8しているが0-10までループするのでそのままで扱っている。またUE5はRandomationSize.Widthだけで割ってるがy側はHeightで割るのが自然なのでそうしている
		ptr->TemporalOffset = (float)m_TemporalAASampleIndex * Vector2(2.48f, 7.52f) / ptr->RandomationSize;
		ptr->Near = CAMERA_NEAR;
		ptr->Far = CAMERA_FAR;
		ptr->InvTanHalfFov = 1.0f / tanf(DirectX::XMConvertToRadians(CAMERA_FOV_Y_DEGREE));
		ptr->bEnableSSAO = (ENABLE_SSAO ? 1 : 0);
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
		ptr->bEnableTemporalAA = (ENABLE_TEMPORAL_AA ? 1 : 0);
		ptr->ClipToPrevClip = Matrix::Identity;
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
		ptr->bEnableMotionBlur = (ENABLE_MOTION_BLUR ? 1 : 0);
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
		ptr->bEnableBloom = (ENABLE_BLOOM ? 1 : 0);
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

	// シャドウマップとシーンの変換行列用の定数バッファの作成
	{
		// モデルのサイズから目分量で決めている
		float zNear = 0.0f;
		float zFar = 40.0f;
		float widthHeight = 40.0f;

		const Matrix& matrix = Matrix::CreateRotationY(m_RotateAngle);
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

			// プロジェクション座標の[-0.5,0.5]*[-0.5,0.5]*[0,1]をシャドウマップ用座標[-1,1]*[-1,1]*[0,1]に変換する
			const Matrix& toShadowMap = Matrix::CreateScale(0.5f, -0.5f, 1.0f) * Matrix::CreateTranslation(0.5f, 0.5f, 0.0f);
			// World行列はMatrix::Identityとする
			ptr->ModelToDirLightShadowMap = dirLightShadowViewProj * toShadowMap; // 行ベクトル形式の順序で乗算するのがXMMatrixMultiply()
			ptr->ModelToSpotLight1ShadowMap = m_SpotLightShadowMapTransformCB[0].GetPtr<CbTransform>()->ViewProj * toShadowMap; // 行ベクトル形式の順序で乗算するのがXMMatrixMultiply()
			ptr->ModelToSpotLight2ShadowMap = m_SpotLightShadowMapTransformCB[1].GetPtr<CbTransform>()->ViewProj * toShadowMap; // 行ベクトル形式の順序で乗算するのがXMMatrixMultiply()
			ptr->ModelToSpotLight3ShadowMap = m_SpotLightShadowMapTransformCB[2].GetPtr<CbTransform>()->ViewProj * toShadowMap; // 行ベクトル形式の順序で乗算するのがXMMatrixMultiply()
		}
	}

	// メッシュ用バッファの作成
	{
		if (!m_MeshCB.Init(m_pDevice.Get(), m_pPool[POOL_TYPE_RES], sizeof(CbMesh)))
		{
			ELOG("Error : ConstantBuffer::Init() Failed.");
			return false;
		}

		CbMesh* ptr = m_MeshCB.GetPtr<CbMesh>();
		ptr->World = Matrix::Identity;
	}

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

	return true;
}

void SampleApp::OnTerm()
{
	m_QuadVB.Term();

	for (uint32_t i = 0; i < FRAME_COUNT; i++)
	{
		m_DirectionalLightCB[i].Term();
		m_CameraCB[i].Term();
		m_DirLightShadowMapTransformCB[i].Term();
		m_TransformCB[i].Term();
		m_SSAO_CB[i].Term();
		m_CameraVelocityCB[i].Term();
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

	m_MeshCB.Term();

	m_SSAOSetupCB.Term();

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

	for (size_t i = 0; i < m_pMesh.size(); i++)
	{
		SafeTerm(m_pMesh[i]);
	}
	m_pMesh.clear();
	m_pMesh.shrink_to_fit();

	m_Material.Term();

	m_DirLightShadowMapTarget.Term();

	for (uint32_t i = 0u; i < NUM_SPOT_LIGHTS; i++)
	{
		m_SpotLightShadowMapTarget[i].Term();
	}

	m_SceneColorTarget.Term();
	m_SceneNormalTarget.Term();
	m_SceneDepthTarget.Term();

	m_SSAOSetup_Target.Term();

	m_SSAO_Target.Term();
	m_SSAO_RandomizationTarget.Term();

	m_AmbientLightTarget.Term();

	m_CameraVelocityTarget.Term();

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

	m_pSceneOpaquePSO.Reset();
	m_pSceneMaskPSO.Reset();
	m_pSceneDepthOpaquePSO.Reset();
	m_pSceneDepthMaskPSO.Reset();

	m_SceneRootSig.Term();

	m_pSSAOSetup_PSO.Reset();
	m_SSAOSetup_RootSig.Term();

	m_pSSAO_PSO.Reset();
	m_SSAO_RootSig.Term();

	m_pAmbientLightPSO.Reset();
	m_AmbientLightRootSig.Term();

	m_pCameraVelocityPSO.Reset();
	m_CameraVelocityRootSig.Term();

	m_pTemporalAA_PSO.Reset();
	m_TemporalAA_RootSig.Term();

	m_pMotionBlurPSO.Reset();
	m_MotionBlurRootSig.Term();

	m_pBloomSetupPSO.Reset();
	m_BloomSetupRootSig.Term();

	m_pTonemapPSO.Reset();
	m_TonemapRootSig.Term();

	m_pDownsamplePSO.Reset();
	m_DownsampleRootSig.Term();

	m_pFilterPSO.Reset();
	m_FilterRootSig.Term();

	m_pDebugRenderTargetPSO.Reset();
	m_DebugRenderTargetRootSig.Term();
}

void SampleApp::OnRender()
{
	// テンポラルジッタ関連
	Matrix viewProjNoJitter;
	Matrix viewProjWithJitter;
	Matrix projWithJitter;
	{
		m_TemporalAASampleIndex++;
		if (m_TemporalAASampleIndex >= TEMPORAL_AA_SAMPLES)
		{
			m_TemporalAASampleIndex = 0;
		}

		constexpr float fovY = DirectX::XMConvertToRadians(CAMERA_FOV_Y_DEGREE);
		float aspect = static_cast<float>(m_Width) / static_cast<float>(m_Height);

		const Matrix& view = m_Camera.GetView();
		Matrix proj = Matrix::CreatePerspectiveFieldOfView(fovY, aspect, CAMERA_NEAR, CAMERA_FAR);
		viewProjNoJitter = view * proj; // 行ベクトル形式の順序で乗算するのがXMMatrixMultiply()

		// UEのTAAのジッタを参考にしている
		projWithJitter = proj;
		projWithJitter.m[2][0] += (Halton(m_TemporalAASampleIndex + 1, 2) - 0.5f) * 2.0f / m_Width;
		projWithJitter.m[2][1] += (Halton(m_TemporalAASampleIndex + 1, 3) - 0.5f) * 2.0f / m_Height;

		viewProjWithJitter = view * projWithJitter; // 行ベクトル形式の順序で乗算するのがXMMatrixMultiply()
	}

	// ディレクショナルライト方向（の逆方向ベクトル）の更新
	Vector3 lightForward;
	{
		//m_RotateAngle += 0.01f;
		const Matrix& matrix = Matrix::CreateRotationY(m_RotateAngle);
		lightForward = Vector3::TransformNormal(Vector3(-1.0f, -10.0f, -1.0f), matrix);
		lightForward.Normalize();
	}

	ID3D12GraphicsCommandList* pCmd = m_CommandList.Reset();

	ID3D12DescriptorHeap* const pHeaps[] = {
		m_pPool[POOL_TYPE_RES]->GetHeap()
	};

	pCmd->SetDescriptorHeaps(1, pHeaps);
	
	DrawDirectionalLightShadowMap(pCmd, lightForward);

	if (ENABLE_TEMPORAL_AA)
	{
		DrawScene(pCmd, lightForward, viewProjWithJitter);
	}
	else
	{
		DrawScene(pCmd, lightForward, viewProjNoJitter);
	}

	DrawSSAOSetup(pCmd);

	DrawSSAO(pCmd, projWithJitter);

	DrawAmbientLight(pCmd);

	DrawCameraVelocity(pCmd, viewProjNoJitter);

	const ColorTarget& TemporalAA_SrcTarget = m_TemporalAA_Target[m_FrameIndex];
	const ColorTarget& TemporalAA_DstTarget = m_TemporalAA_Target[(m_FrameIndex + 1) % FRAME_COUNT]; // FRAME_COUNT=2前提だとm_FrameIndex ^ 1でも可能

	DrawTemporalAA(pCmd, viewProjNoJitter, TemporalAA_SrcTarget, TemporalAA_DstTarget);

	DrawMotionBlur(pCmd, TemporalAA_DstTarget);

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

#if DEBUG_VIEW_SSAO
	DebugDrawSSAO(pCmd);
#endif

	pCmd->Close();

	ID3D12CommandList* pLists[] = {pCmd};
	m_pQueue->ExecuteCommandLists(1, pLists);

	Present(1);

	m_PrevViewProjNoJitter = viewProjNoJitter;
}

void SampleApp::DrawDirectionalLightShadowMap(ID3D12GraphicsCommandList* pCmdList, const Vector3& lightForward)
{
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

	pCmdList->SetGraphicsRootSignature(m_SceneRootSig.GetPtr());
	pCmdList->SetGraphicsRootDescriptorTable(0, m_DirLightShadowMapTransformCB[m_FrameIndex].GetHandleGPU());
	pCmdList->SetGraphicsRootDescriptorTable(1, m_MeshCB.GetHandleGPU());

	pCmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	// Opaqueマテリアルのメッシュの描画
	pCmdList->SetPipelineState(m_pSceneDepthOpaquePSO.Get());
	DrawMesh(pCmdList, ALPHA_MODE::ALPHA_MODE_OPAQUE);

	// Mask, DoubleSidedマテリアルのメッシュの描画
	pCmdList->SetPipelineState(m_pSceneDepthMaskPSO.Get());
	DrawMesh(pCmdList, ALPHA_MODE::ALPHA_MODE_MASK);

	DirectX::TransitionResource(pCmdList, m_DirLightShadowMapTarget.GetResource(), D3D12_RESOURCE_STATE_DEPTH_WRITE, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
}

void SampleApp::DrawSpotLightShadowMap(ID3D12GraphicsCommandList* pCmdList, uint32_t spotLightIdx)
{
	pCmdList->SetGraphicsRootSignature(m_SceneRootSig.GetPtr());
	pCmdList->SetGraphicsRootDescriptorTable(0, m_SpotLightShadowMapTransformCB[spotLightIdx].GetHandleGPU());
	pCmdList->SetGraphicsRootDescriptorTable(1, m_MeshCB.GetHandleGPU());

	pCmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	// Opaqueマテリアルのメッシュの描画
	pCmdList->SetPipelineState(m_pSceneDepthOpaquePSO.Get());
	DrawMesh(pCmdList, ALPHA_MODE::ALPHA_MODE_OPAQUE);

	// Mask, DoubleSidedマテリアルのメッシュの描画
	pCmdList->SetPipelineState(m_pSceneDepthMaskPSO.Get());
	DrawMesh(pCmdList, ALPHA_MODE::ALPHA_MODE_MASK);
}

void SampleApp::DrawScene(ID3D12GraphicsCommandList* pCmdList, const DirectX::SimpleMath::Vector3& lightForward, const Matrix& viewProj)
{
	ScopedTimer scopedTimer(pCmdList, L"BasePass");

	// 変換行列用の定数バッファの更新
	{
		CbTransform* ptr = m_TransformCB[m_FrameIndex].GetPtr<CbTransform>();
		ptr->ViewProj = viewProj;

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
		ptr->ModelToSpotLight1ShadowMap = m_SpotLightShadowMapTransformCB[0].GetPtr<CbTransform>()->ViewProj * toShadowMap; // 行ベクトル形式の順序で乗算するのがXMMatrixMultiply()
		ptr->ModelToSpotLight2ShadowMap = m_SpotLightShadowMapTransformCB[1].GetPtr<CbTransform>()->ViewProj * toShadowMap; // 行ベクトル形式の順序で乗算するのがXMMatrixMultiply()
		ptr->ModelToSpotLight3ShadowMap = m_SpotLightShadowMapTransformCB[2].GetPtr<CbTransform>()->ViewProj * toShadowMap; // 行ベクトル形式の順序で乗算するのがXMMatrixMultiply()
	}

	// カメラバッファの更新
	{
		CbCamera* ptr = m_CameraCB[m_FrameIndex].GetPtr<CbCamera>();
		ptr->CameraPosition = m_Camera.GetPosition();
	}

	// ライトバッファの更新
	{
		CbDirectionalLight* ptr = m_DirectionalLightCB[m_FrameIndex].GetPtr<CbDirectionalLight>();
		ptr->LightColor = Vector3(1.0f, 1.0f, 1.0f); // 白色光
		ptr->LightForward = lightForward;
		ptr->LightIntensity = 10.0f;
		ptr->ShadowMapTexelSize = 1.0f / DIRECTIONAL_LIGHT_SHADOW_MAP_SIZE;
	}

	DirectX::TransitionResource(pCmdList, m_SceneColorTarget.GetResource(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
	DirectX::TransitionResource(pCmdList, m_SceneNormalTarget.GetResource(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
	DirectX::TransitionResource(pCmdList, m_SceneDepthTarget.GetResource(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_DEPTH_WRITE);

	D3D12_CPU_DESCRIPTOR_HANDLE rtvs[2] = { m_SceneColorTarget.GetHandleRTV()->HandleCPU, m_SceneNormalTarget.GetHandleRTV()->HandleCPU };
	const DescriptorHandle* handleDSV = m_SceneDepthTarget.GetHandleDSV();

	pCmdList->OMSetRenderTargets(2, rtvs, FALSE, &handleDSV->HandleCPU);

	m_SceneColorTarget.ClearView(pCmdList);
	m_SceneNormalTarget.ClearView(pCmdList);
	m_SceneDepthTarget.ClearView(pCmdList);

	pCmdList->RSSetViewports(1, &m_Viewport);
	pCmdList->RSSetScissorRects(1, &m_Scissor);

	//TODO:DrawDirectionalLightShadowMapと重複してるがとりあえず
	pCmdList->SetGraphicsRootSignature(m_SceneRootSig.GetPtr());
	pCmdList->SetGraphicsRootDescriptorTable(0, m_TransformCB[m_FrameIndex].GetHandleGPU());
	pCmdList->SetGraphicsRootDescriptorTable(1, m_MeshCB.GetHandleGPU());
	pCmdList->SetGraphicsRootDescriptorTable(2, m_CameraCB[m_FrameIndex].GetHandleGPU());
	pCmdList->SetGraphicsRootDescriptorTable(4, m_DirectionalLightCB[m_FrameIndex].GetHandleGPU());
	for (uint32_t i = 0u; i < NUM_POINT_LIGHTS; i++)
	{
		pCmdList->SetGraphicsRootDescriptorTable(5 + i, m_PointLightCB[i].GetHandleGPU());
	}

	for (uint32_t i = 0u; i < NUM_SPOT_LIGHTS; i++)
	{
		pCmdList->SetGraphicsRootDescriptorTable(9 + i, m_SpotLightCB[i].GetHandleGPU());
	}

	pCmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	// Opaqueマテリアルのメッシュの描画
	pCmdList->SetPipelineState(m_pSceneOpaquePSO.Get());
	DrawMesh(pCmdList, ALPHA_MODE::ALPHA_MODE_OPAQUE);

	// Mask, DoubleSidedマテリアルのメッシュの描画
	pCmdList->SetPipelineState(m_pSceneMaskPSO.Get());
	DrawMesh(pCmdList, ALPHA_MODE::ALPHA_MODE_MASK);

	DirectX::TransitionResource(pCmdList, m_SceneColorTarget.GetResource(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
	DirectX::TransitionResource(pCmdList, m_SceneNormalTarget.GetResource(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
	DirectX::TransitionResource(pCmdList, m_SceneDepthTarget.GetResource(), D3D12_RESOURCE_STATE_DEPTH_WRITE, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
}

void SampleApp::DrawMesh(ID3D12GraphicsCommandList* pCmdList, ALPHA_MODE AlphaMode)
{
	for (size_t i = 0; i < m_pMesh.size(); i++)
	{
		// TODO:Materialはとりあえず最初は一種類しか作らない。テクスチャの差し替えで使いまわす
		uint32_t materialId = m_pMesh[i]->GetMaterialId();

		if (AlphaMode == ALPHA_MODE::ALPHA_MODE_OPAQUE && m_Material.GetDoubleSided(materialId))
		{
			continue;
		}
		else if (AlphaMode == ALPHA_MODE::ALPHA_MODE_MASK && !m_Material.GetDoubleSided(materialId))
		{
			continue;
		}

		pCmdList->SetGraphicsRootDescriptorTable(3, m_Material.GetBufferHandle(materialId));
		pCmdList->SetGraphicsRootDescriptorTable(12, m_Material.GetTextureHandle(materialId, Material::TEXTURE_USAGE_BASE_COLOR));
		pCmdList->SetGraphicsRootDescriptorTable(13, m_Material.GetTextureHandle(materialId, Material::TEXTURE_USAGE_METALLIC_ROUGHNESS));
		pCmdList->SetGraphicsRootDescriptorTable(14, m_Material.GetTextureHandle(materialId, Material::TEXTURE_USAGE_NORMAL));
		pCmdList->SetGraphicsRootDescriptorTable(15, m_DirLightShadowMapTarget.GetHandleSRV()->HandleGPU);
		for (uint32_t i = 0u; i < NUM_SPOT_LIGHTS; i++)
		{
			pCmdList->SetGraphicsRootDescriptorTable(16 + i, m_SpotLightShadowMapTarget[i].GetHandleSRV()->HandleGPU);
		}

		m_pMesh[i]->Draw(pCmdList);
	}
}

void SampleApp::DrawSSAOSetup(ID3D12GraphicsCommandList* pCmdList)
{
	ScopedTimer scopedTimer(pCmdList, L"SSAOSetup");

	DirectX::TransitionResource(pCmdList, m_SSAOSetup_Target.GetResource(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);

	const DescriptorHandle* handleRTV = m_SSAOSetup_Target.GetHandleRTV();
	pCmdList->OMSetRenderTargets(1, &handleRTV->HandleCPU, FALSE, nullptr);

	m_SSAOSetup_Target.ClearView(pCmdList);

	pCmdList->SetGraphicsRootSignature(m_SSAOSetup_RootSig.GetPtr());
	pCmdList->SetGraphicsRootDescriptorTable(0, m_SSAOSetupCB.GetHandleGPU());
	pCmdList->SetGraphicsRootDescriptorTable(1, m_SceneDepthTarget.GetHandleSRV()->HandleGPU);
	pCmdList->SetGraphicsRootDescriptorTable(2, m_SceneNormalTarget.GetHandleSRV()->HandleGPU);
	pCmdList->SetPipelineState(m_pSSAOSetup_PSO.Get());

	D3D12_VIEWPORT halfResViewport = m_Viewport;
	halfResViewport.Width = (FLOAT)m_SSAOSetup_Target.GetDesc().Width;
	halfResViewport.Height = (FLOAT)m_SSAOSetup_Target.GetDesc().Height;
	pCmdList->RSSetViewports(1, &halfResViewport);

	D3D12_RECT halfResScissor = m_Scissor;
	halfResScissor.right = (LONG)m_SSAOSetup_Target.GetDesc().Width;
	halfResScissor.bottom = (LONG)m_SSAOSetup_Target.GetDesc().Height;
	pCmdList->RSSetScissorRects(1, &halfResScissor);
	
	pCmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	const D3D12_VERTEX_BUFFER_VIEW& VBV = m_QuadVB.GetView();
	pCmdList->IASetVertexBuffers(0, 1, &VBV);

	pCmdList->DrawInstanced(3, 1, 0, 0);

	DirectX::TransitionResource(pCmdList, m_SSAOSetup_Target.GetResource(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
}

//TODO:SSパスは処理を共通化したい
void SampleApp::DrawSSAO(ID3D12GraphicsCommandList* pCmdList, const DirectX::SimpleMath::Matrix& projWithJitter)
{
	ScopedTimer scopedTimer(pCmdList, L"SSAO");

	{
		CbSSAO* ptr = m_SSAO_CB[m_FrameIndex].GetPtr<CbSSAO>();
		// UE5は%8しているが0-10までループするのでそのままで扱っている。またUE5はRandomationSize.Widthだけで割ってるがy側はHeightで割るのが自然なのでそうしている
		ptr->TemporalOffset = (float)m_TemporalAASampleIndex * Vector2(2.48f, 7.52f) / ptr->RandomationSize;
		ptr->ViewMatrix = m_Camera.GetView();
		ptr->InvProjMatrix = projWithJitter.Invert();
	}

	DirectX::TransitionResource(pCmdList, m_SSAO_Target.GetResource(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);

	const DescriptorHandle* handleRTV = m_SSAO_Target.GetHandleRTV();
	pCmdList->OMSetRenderTargets(1, &handleRTV->HandleCPU, FALSE, nullptr);

	m_SSAO_Target.ClearView(pCmdList);

	pCmdList->SetGraphicsRootSignature(m_SSAO_RootSig.GetPtr());
	pCmdList->SetGraphicsRootDescriptorTable(0, m_SSAO_CB[m_FrameIndex].GetHandleGPU());
	pCmdList->SetGraphicsRootDescriptorTable(1, m_SceneDepthTarget.GetHandleSRV()->HandleGPU);
	pCmdList->SetGraphicsRootDescriptorTable(2, m_SceneNormalTarget.GetHandleSRV()->HandleGPU);
	pCmdList->SetGraphicsRootDescriptorTable(3, m_SSAO_RandomizationTarget.GetHandleSRV()->HandleGPU);
	pCmdList->SetPipelineState(m_pSSAO_PSO.Get());

	pCmdList->RSSetViewports(1, &m_Viewport);
	pCmdList->RSSetScissorRects(1, &m_Scissor);
	
	pCmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	const D3D12_VERTEX_BUFFER_VIEW& VBV = m_QuadVB.GetView();
	pCmdList->IASetVertexBuffers(0, 1, &VBV);

	pCmdList->DrawInstanced(3, 1, 0, 0);

	DirectX::TransitionResource(pCmdList, m_SSAO_Target.GetResource(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
}

void SampleApp::DrawAmbientLight(ID3D12GraphicsCommandList* pCmdList)
{
	ScopedTimer scopedTimer(pCmdList, L"AmbientLight");

	DirectX::TransitionResource(pCmdList, m_AmbientLightTarget.GetResource(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);

	const DescriptorHandle* handleRTV = m_AmbientLightTarget.GetHandleRTV();
	pCmdList->OMSetRenderTargets(1, &handleRTV->HandleCPU, FALSE, nullptr);

	m_AmbientLightTarget.ClearView(pCmdList);

	pCmdList->SetGraphicsRootSignature(m_AmbientLightRootSig.GetPtr());
	pCmdList->SetGraphicsRootDescriptorTable(0, m_SceneColorTarget.GetHandleSRV()->HandleGPU);
	pCmdList->SetGraphicsRootDescriptorTable(1, m_SSAO_Target.GetHandleSRV()->HandleGPU);
	pCmdList->SetPipelineState(m_pAmbientLightPSO.Get());

	pCmdList->RSSetViewports(1, &m_Viewport);
	pCmdList->RSSetScissorRects(1, &m_Scissor);
	
	pCmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	const D3D12_VERTEX_BUFFER_VIEW& VBV = m_QuadVB.GetView();
	pCmdList->IASetVertexBuffers(0, 1, &VBV);

	pCmdList->DrawInstanced(3, 1, 0, 0);

	DirectX::TransitionResource(pCmdList, m_AmbientLightTarget.GetResource(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
}

void SampleApp::DrawCameraVelocity(ID3D12GraphicsCommandList* pCmdList, const DirectX::SimpleMath::Matrix& viewProjNoJitter)
{
	ScopedTimer scopedTimer(pCmdList, L"CameraVelocity");

	{
		CbCameraVelocity* ptr = m_CameraVelocityCB[m_FrameIndex].GetPtr<CbCameraVelocity>();
		ptr->ClipToPrevClip = viewProjNoJitter.Invert() * m_PrevViewProjNoJitter;
	}

	DirectX::TransitionResource(pCmdList, m_CameraVelocityTarget.GetResource(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);

	const DescriptorHandle* handleRTV = m_CameraVelocityTarget.GetHandleRTV();
	pCmdList->OMSetRenderTargets(1, &handleRTV->HandleCPU, FALSE, nullptr);

	m_CameraVelocityTarget.ClearView(pCmdList);

	pCmdList->SetGraphicsRootSignature(m_CameraVelocityRootSig.GetPtr());
	pCmdList->SetGraphicsRootDescriptorTable(0, m_CameraVelocityCB[m_FrameIndex].GetHandleGPU());
	pCmdList->SetGraphicsRootDescriptorTable(1, m_AmbientLightTarget.GetHandleSRV()->HandleGPU);
	pCmdList->SetGraphicsRootDescriptorTable(2, m_SceneDepthTarget.GetHandleSRV()->HandleGPU);
	pCmdList->SetPipelineState(m_pCameraVelocityPSO.Get());

	pCmdList->RSSetViewports(1, &m_Viewport);
	pCmdList->RSSetScissorRects(1, &m_Scissor);
	
	pCmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	const D3D12_VERTEX_BUFFER_VIEW& VBV = m_QuadVB.GetView();
	pCmdList->IASetVertexBuffers(0, 1, &VBV);

	pCmdList->DrawInstanced(3, 1, 0, 0);

	DirectX::TransitionResource(pCmdList, m_CameraVelocityTarget.GetResource(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
}

void SampleApp::DrawTemporalAA(ID3D12GraphicsCommandList* pCmdList, const DirectX::SimpleMath::Matrix& viewProjNoJitter, const ColorTarget& SrcColor, const ColorTarget& DstColor)
{
	ScopedTimer scopedTimer(pCmdList, L"TemporalAA");

	{
		CbTemporalAA* ptr = m_TemporalAA_CB[m_FrameIndex].GetPtr<CbTemporalAA>();
		ptr->ClipToPrevClip = viewProjNoJitter.Invert() * m_PrevViewProjNoJitter;
	}

	DirectX::TransitionResource(pCmdList, m_SceneDepthTarget.GetResource(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	DirectX::TransitionResource(pCmdList, m_AmbientLightTarget.GetResource(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	DirectX::TransitionResource(pCmdList, SrcColor.GetResource(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	DirectX::TransitionResource(pCmdList, DstColor.GetResource(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

	pCmdList->SetComputeRootSignature(m_TemporalAA_RootSig.GetPtr());
	pCmdList->SetPipelineState(m_pTemporalAA_PSO.Get());
	pCmdList->SetComputeRootDescriptorTable(0, m_TemporalAA_CB[m_FrameIndex].GetHandleGPU());
	pCmdList->SetComputeRootDescriptorTable(1, m_SceneDepthTarget.GetHandleSRV()->HandleGPU);
	pCmdList->SetComputeRootDescriptorTable(2, m_AmbientLightTarget.GetHandleSRV()->HandleGPU);
	pCmdList->SetComputeRootDescriptorTable(3, SrcColor.GetHandleSRV()->HandleGPU);
	pCmdList->SetComputeRootDescriptorTable(4, DstColor.GetHandleUAV()->HandleGPU);

	// シェーダ側と合わせている
	const size_t GROUP_SIZE_X = 8;
	const size_t GROUP_SIZE_Y = 8;

	// グループ数は切り上げ
	UINT NumGroupX = (m_Width + GROUP_SIZE_X - 1) / GROUP_SIZE_X;
	UINT NumGroupY = (m_Height + GROUP_SIZE_Y - 1) / GROUP_SIZE_Y;
	UINT NumGroupZ = 1;
	pCmdList->Dispatch(NumGroupX, NumGroupY, NumGroupZ);

	DirectX::TransitionResource(pCmdList, m_SceneDepthTarget.GetResource(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
	DirectX::TransitionResource(pCmdList, m_AmbientLightTarget.GetResource(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
	DirectX::TransitionResource(pCmdList, SrcColor.GetResource(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
	DirectX::TransitionResource(pCmdList, DstColor.GetResource(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
}

void SampleApp::DrawMotionBlur(ID3D12GraphicsCommandList* pCmdList, const ColorTarget& InputColor)
{
	ScopedTimer scopedTimer(pCmdList, L"MotionBlur");

	DirectX::TransitionResource(pCmdList, m_MotionBlurTarget.GetResource(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);

	const DescriptorHandle* handleRTV = m_MotionBlurTarget.GetHandleRTV();
	pCmdList->OMSetRenderTargets(1, &handleRTV->HandleCPU, FALSE, nullptr);

	m_MotionBlurTarget.ClearView(pCmdList);

	pCmdList->SetGraphicsRootSignature(m_MotionBlurRootSig.GetPtr());
	pCmdList->SetGraphicsRootDescriptorTable(0, m_MotionBlurCB.GetHandleGPU());
	pCmdList->SetGraphicsRootDescriptorTable(1, InputColor.GetHandleSRV()->HandleGPU);
	pCmdList->SetGraphicsRootDescriptorTable(2, m_CameraVelocityTarget.GetHandleSRV()->HandleGPU);
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
	halfResViewport.Width = (FLOAT)m_BloomSetupTarget->GetDesc().Width;
	halfResViewport.Height = (FLOAT)m_BloomSetupTarget->GetDesc().Height;
	pCmdList->RSSetViewports(1, &halfResViewport);

	D3D12_RECT halfResScissor = m_Scissor;
	halfResScissor.right = (LONG)m_BloomSetupTarget->GetDesc().Width;
	halfResScissor.bottom = (LONG)m_BloomSetupTarget->GetDesc().Height;
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
	}

	DirectX::TransitionResource(pCmdList, m_ColorTarget[m_FrameIndex].GetResource(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);

	const DescriptorHandle* handleRTV = m_ColorTarget[m_FrameIndex].GetHandleRTV();
	pCmdList->OMSetRenderTargets(1, &handleRTV->HandleCPU, FALSE, nullptr);

	m_ColorTarget[m_FrameIndex].ClearView(pCmdList);

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

	DirectX::TransitionResource(pCmdList, m_ColorTarget[m_FrameIndex].GetResource(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
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

void SampleApp::DebugDrawSSAO(ID3D12GraphicsCommandList* pCmdList)
{
	ScopedTimer scopedTimer(pCmdList, L"DebugSSAO");

	// R8_UNORMとR10G10B10A2_UNORMではCopyResourceでは非対応でエラーが出るのでシェーダでコピーする

	//DirectX::TransitionResource(pCmd, m_SSAO_Target.GetResource(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_SOURCE);
	//DirectX::TransitionResource(pCmd, m_ColorTarget[m_FrameIndex].GetResource(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_COPY_DEST);
	//pCmd->CopyResource(m_ColorTarget[m_FrameIndex].GetResource(), m_SSAO_Target.GetResource());
	//DirectX::TransitionResource(pCmd, m_SSAO_Target.GetResource(), D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
	//DirectX::TransitionResource(pCmd, m_ColorTarget[m_FrameIndex].GetResource(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PRESENT);

	DirectX::TransitionResource(pCmdList, m_ColorTarget[m_FrameIndex].GetResource(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);

	const DescriptorHandle* handleRTV = m_ColorTarget[m_FrameIndex].GetHandleRTV();
	pCmdList->OMSetRenderTargets(1, &handleRTV->HandleCPU, FALSE, nullptr);

	m_ColorTarget[m_FrameIndex].ClearView(pCmdList);


	pCmdList->SetGraphicsRootSignature(m_DebugRenderTargetRootSig.GetPtr());
	pCmdList->SetPipelineState(m_pDebugRenderTargetPSO.Get());
	//pCmdList->SetGraphicsRootDescriptorTable(0, m_SceneNormalTarget.GetHandleSRV()->HandleGPU);
	pCmdList->SetGraphicsRootDescriptorTable(0, m_SSAO_Target.GetHandleSRV()->HandleGPU);

	pCmdList->RSSetViewports(1, &m_Viewport);
	pCmdList->RSSetScissorRects(1, &m_Scissor);
	
	pCmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	const D3D12_VERTEX_BUFFER_VIEW& VBV = m_QuadVB.GetView();
	pCmdList->IASetVertexBuffers(0, 1, &VBV);

	pCmdList->DrawInstanced(3, 1, 0, 0);

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

void SampleApp::OnMsgProc(HWND hWnd, UINT msg, WPARAM wp, LPARAM lp)
{
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
				case 'N':
					m_TonemapType = TONEMAP_NONE;
					break;
				case 'R':
					m_TonemapType = TONEMAP_REINHARD;
					break;
				case 'G':
					m_TonemapType = TONEMAP_GT;
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
}
