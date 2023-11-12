#include "SampleApp.h"
#include <DirectXMath.h>
#include <CommonStates.h>
#include <DirectXHelpers.h>
#include "FileUtil.h"
#include "Logger.h"
#include "ResMesh.h"
#include "Mesh.h"
#include "InlineUtil.h"
#include "RootSignature.h"

using namespace DirectX::SimpleMath;

namespace
{
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

	struct alignas(256) CbTonemap
	{
		int Type;
		int ColorSpace;
		float BaseLuminance;
		float MaxLuminance;
	};

	struct alignas(256) CbMesh
	{
		Matrix World;
	};

	struct alignas(256) CbTransform
	{
		Matrix ViewProj;
		Matrix ToShadowMap;
	};

	struct alignas(256) CbLight
	{
		Vector3 LightColor;
		float LightIntensity;
		Vector3 LightForward;
	};

	struct alignas(256) CbCamera
	{
		Vector3 CameraPosition;
	};

	struct alignas(256) CbMaterial
	{
		Vector3 BaseColorFactor;
		float MetallicFactor;
		float RoughnessFactor;
		float AlphaCutoff;
	};

	UINT16 inline GetChromaticityCoord(double value)
	{
		return UINT16(value * 50000);
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

	// ライトバッファの設定
	{
		for (uint32_t i = 0u; i < FrameCount; i++)
		{
			if (!m_LightCB[i].Init(m_pDevice.Get(), m_pPool[POOL_TYPE_RES], sizeof(CbLight)))
			{
				ELOG("Error : ConstantBuffer::Init() Failed.");
				return false;
			}
		}
	}

	// カメラバッファの設定
	{
		for (uint32_t i = 0u; i < FrameCount; i++)
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
		if (!m_ShadowMapTarget.Init
		(
			m_pDevice.Get(),
			m_pPool[POOL_TYPE_DSV],
			m_pPool[POOL_TYPE_RES], // シャドウマップなのでSRVも作る
			2048, // TODO:ModelViewerを参考にした
			2048, // TODO:ModelViewerを参考にした
			DXGI_FORMAT_D16_UNORM, // TODO:ModelViewerを参考にした
			1.0f,
			0
		))
		{
			ELOG("Error : DepthTarget::Init() Failed.");
			return false;
		}

		{
			m_ShadowMapViewport.TopLeftX = 0;
			m_ShadowMapViewport.TopLeftY = 0;
			m_ShadowMapViewport.Width = static_cast<float>(m_ShadowMapTarget.GetDesc().Width);
			m_ShadowMapViewport.Height = static_cast<float>(m_ShadowMapTarget.GetDesc().Height);
			m_ShadowMapViewport.MinDepth = 0.0f;
			m_ShadowMapViewport.MaxDepth = 1.0f;
		}

		// TODO:ModelViewerだと内部で以下の処理がある
		//// Prevent drawing to the boundary pixels so that we don't have to worry about shadows stretching
		//m_Scissor.left = 1;
		//m_Scissor.top = 1;
		//m_Scissor.right = (LONG)Width - 2;
		//m_Scissor.bottom = (LONG)Height - 2;
		{
			m_ShadowMapScissor.left = 0;
			m_ShadowMapScissor.right = (LONG)m_ShadowMapTarget.GetDesc().Width;
			m_ShadowMapScissor.top = 0;
			m_ShadowMapScissor.bottom = (LONG)m_ShadowMapTarget.GetDesc().Height;
		}
	}

	// シーン用カラーターゲットの生成
	{
		float clearColor[4] = {0.2f, 0.2f, 0.2f, 1.0f};

		if (!m_SceneColorTarget.Init
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

	// シーン用デプスターゲットの生成
	{
		if (!m_SceneDepthTarget.Init
		(
			m_pDevice.Get(),
			m_pPool[POOL_TYPE_DSV],
			nullptr,
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

    // シーン用ルートシグニチャの生成。デプスだけ描画するパスにも使用される
	{
		RootSignature::Desc desc;
		desc.Begin(9)
			.SetCBV(ShaderStage::VS, 0, 0)
			.SetCBV(ShaderStage::VS, 1, 1)
			.SetCBV(ShaderStage::PS, 2, 1)
			.SetCBV(ShaderStage::PS, 3, 2)
			.SetCBV(ShaderStage::PS, 4, 3)
			.SetSRV(ShaderStage::PS, 5, 0)
			.SetSRV(ShaderStage::PS, 6, 1)
			.SetSRV(ShaderStage::PS, 7, 2)
			.SetSRV(ShaderStage::PS, 8, 3)
			.AddStaticSmp(ShaderStage::PS, 0, SamplerState::LinearWrap)
			.AddStaticSmp(ShaderStage::PS, 1, SamplerState::LinearWrap)
			.AddStaticSmp(ShaderStage::PS, 2, SamplerState::LinearWrap)
			//.AddStaticCmpSmp(ShaderStage::PS, 3, SamplerState::PointClamp) //TODO: SponzaRenderに合わせた
			.AddStaticSmp(ShaderStage::PS, 3, SamplerState::PointClamp)
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
		desc.DSVFormat = m_ShadowMapTarget.GetDSVDesc().Format;
		desc.SampleDesc.Count = 1;
		desc.SampleDesc.Quality = 0;

		//// TODO:SponzaRendererの数字を何も考えずに使っている
		//desc.RasterizerState.SlopeScaledDepthBias = -1.5f;
		//desc.RasterizerState.DepthBias = -100;

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
		desc.NumRenderTargets = 1;
		desc.RTVFormats[0] = m_ColorTarget[0].GetRTVDesc().Format;
		desc.DSVFormat = m_DepthTarget.GetDSVDesc().Format;
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

    // トーンマップ用ルートシグニチャの生成
	{
		RootSignature::Desc desc;
		desc.Begin(2)
			.SetCBV(ShaderStage::PS, 0, 0)
			.SetSRV(ShaderStage::PS, 1, 0)
			.AddStaticSmp(ShaderStage::PS, 0, SamplerState::LinearWrap)
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

		D3D12_GRAPHICS_PIPELINE_STATE_DESC desc = {};
		desc.InputLayout.pInputElementDescs = elements;
		desc.InputLayout.NumElements = 2;
		desc.pRootSignature = m_TonemapRootSig.GetPtr();
		desc.VS.pShaderBytecode = pVSBlob->GetBufferPointer();
		desc.VS.BytecodeLength = pVSBlob->GetBufferSize();
		desc.PS.pShaderBytecode = pPSBlob->GetBufferPointer();
		desc.PS.BytecodeLength = pPSBlob->GetBufferSize();
		desc.RasterizerState = DirectX::CommonStates::CullCounterClockwise;
		desc.BlendState = DirectX::CommonStates::Opaque;
		desc.DepthStencilState = DirectX::CommonStates::DepthNone;
		desc.SampleMask = UINT_MAX;
		desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
		desc.NumRenderTargets = 1;
		desc.RTVFormats[0] = m_ColorTarget[0].GetRTVDesc().Format;
		desc.SampleDesc.Count = 1;
		desc.SampleDesc.Quality = 0;

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

	// トーンマップ用頂点バッファの生成
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
		ptr[0].px = -1.0f; ptr[0].py = 1.0f; ptr[0].tx = 0.0f; ptr[0].ty = -1.0f;
		ptr[1].px = 3.0f; ptr[1].py = 1.0f; ptr[1].tx = 2.0f; ptr[1].ty = -1.0f;
		ptr[2].px = -1.0f; ptr[2].py = -3.0f; ptr[2].tx = 0.0f; ptr[2].ty = 1.0f;
		m_QuadVB.Unmap();
	}

	// トーンマップ用定数バッファの作成
	for (uint32_t i = 0; i < FrameCount; i++)
	{
		if (!m_TonemapCB[i].Init(m_pDevice.Get(), m_pPool[POOL_TYPE_RES], sizeof(CbTonemap)))
		{
			ELOG("Error : ConstantBuffer::Init() Failed.");
			return false;
		}
	}

	m_RotateAngle = DirectX::XMConvertToRadians(-60.0f);

	// シャドウマップとシーンの変換行列用の定数バッファの作成
	{
		// モデルのサイズから目分量で決めている
		float zNear = 0.0f;
		float zFar = 40.0f;
		float widthHeight = 40.0f;

		const Matrix& matrix = Matrix::CreateRotationY(m_RotateAngle);
		Vector3 lightForward = Vector3::TransformNormal(Vector3(1.0f, 1.0f, 1.0f), matrix);
		lightForward.Normalize();

		for (uint32_t i = 0u; i < FrameCount; i++)
		{
			if (!m_ShadowMapTransformCB[i].Init(m_pDevice.Get(), m_pPool[POOL_TYPE_RES], sizeof(CbTransform)))
			{
				ELOG("Error : ConstantBuffer::Init() Failed.");
				return false;
			}

			CbTransform* ptr = m_ShadowMapTransformCB[m_FrameIndex].GetPtr<CbTransform>();

			const Matrix& view = Matrix::CreateLookAt(Vector3::Zero + lightForward * (zFar - zNear) * 0.5f, Vector3::Zero, Vector3::UnitY);
			const Matrix& proj = Matrix::CreateOrthographic(widthHeight, widthHeight, zNear, zFar);
			ptr->ViewProj = view * proj; // 行ベクトル形式の順序で乗算するのがXMMatrixMultiply()
		}

		for (uint32_t i = 0u; i < FrameCount; i++)
		{
			if (!m_TransformCB[i].Init(m_pDevice.Get(), m_pPool[POOL_TYPE_RES], sizeof(CbTransform)))
			{
				ELOG("Error : ConstantBuffer::Init() Failed.");
				return false;
			}

			constexpr float fovY = DirectX::XMConvertToRadians(37.5f);
			float aspect = static_cast<float>(m_Width) / static_cast<float>(m_Height);

			const Matrix& view = m_Camera.GetView();
			const Matrix& proj = Matrix::CreatePerspectiveFieldOfView(fovY, aspect, 0.1f, 100.0f);
			CbTransform* ptr = m_TransformCB[m_FrameIndex].GetPtr<CbTransform>();
			ptr->ViewProj = view * proj; // 行ベクトル形式の順序で乗算するのがXMMatrixMultiply()

			// プロジェクション座標の[-w/2,w/2]*[-h/2,h/2]*[zNear,zFar]をシャドウマップ用座標[0,1]*[1,0]*[0,1]に変換する
			const Matrix& toShadowMap = Matrix::CreateTranslation(0.0f, 0.0f, -zNear) * Matrix::CreateScale(1.0f / widthHeight, -1.0f / widthHeight, 1.0f / (zFar - zNear)) * Matrix::CreateTranslation(0.5f, 0.5f, -zNear);
			ptr->ToShadowMap = toShadowMap;
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

	return true;
}

void SampleApp::OnTerm()
{
	m_QuadVB.Term();

	for (uint32_t i = 0; i < FrameCount; i++)
	{
		m_TonemapCB[i].Term();
		m_LightCB[i].Term();
		m_CameraCB[i].Term();
		m_ShadowMapTransformCB[i].Term();
		m_TransformCB[i].Term();
	}

	m_MeshCB.Term();

	for (size_t i = 0; i < m_pMesh.size(); i++)
	{
		SafeTerm(m_pMesh[i]);
	}
	m_pMesh.clear();
	m_pMesh.shrink_to_fit();

	m_Material.Term();

	m_ShadowMapTarget.Term();
	m_SceneColorTarget.Term();
	m_SceneDepthTarget.Term();

	m_pSceneOpaquePSO.Reset();
	m_pSceneMaskPSO.Reset();
	m_pSceneDepthOpaquePSO.Reset();
	m_pSceneDepthMaskPSO.Reset();

	m_SceneRootSig.Term();

	m_pTonemapPSO.Reset();
	m_TonemapRootSig.Term();
}

void SampleApp::OnRender()
{
	// ディレクショナルライト方向（の逆方向ベクトル）の更新
	//m_RotateAngle += 0.01f;
	const Matrix& matrix = Matrix::CreateRotationY(m_RotateAngle);
	Vector3 lightForward = Vector3::TransformNormal(Vector3(1.0f, 1.0f, 1.0f), matrix);
	lightForward.Normalize();

	ID3D12GraphicsCommandList* pCmd = m_CommandList.Reset();

	ID3D12DescriptorHeap* const pHeaps[] = {
		m_pPool[POOL_TYPE_RES]->GetHeap()
	};

	pCmd->SetDescriptorHeaps(1, pHeaps);
	
	// シャドウマップ描画パス
	{
		DirectX::TransitionResource(pCmd, m_ShadowMapTarget.GetResource(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_DEPTH_WRITE);

		const DescriptorHandle* handleDSV = m_ShadowMapTarget.GetHandleDSV();

		pCmd->OMSetRenderTargets(0, nullptr, FALSE, &handleDSV->HandleCPU);

		m_ShadowMapTarget.ClearView(pCmd);

		pCmd->RSSetViewports(1, &m_ShadowMapViewport);
		pCmd->RSSetScissorRects(1, &m_ShadowMapScissor);

		DrawShadowMap(pCmd, lightForward);

		DirectX::TransitionResource(pCmd, m_ShadowMapTarget.GetResource(), D3D12_RESOURCE_STATE_DEPTH_WRITE, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
	}

	// シーンをレンダーターゲットに描画するパス
	{
		DirectX::TransitionResource(pCmd, m_SceneColorTarget.GetResource(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
		DirectX::TransitionResource(pCmd, m_SceneDepthTarget.GetResource(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_DEPTH_WRITE);

		const DescriptorHandle* handleRTV = m_SceneColorTarget.GetHandleRTV();
		const DescriptorHandle* handleDSV = m_SceneDepthTarget.GetHandleDSV();

		pCmd->OMSetRenderTargets(1, &handleRTV->HandleCPU, FALSE, &handleDSV->HandleCPU);

		m_SceneColorTarget.ClearView(pCmd);
		m_SceneDepthTarget.ClearView(pCmd);

		pCmd->RSSetViewports(1, &m_Viewport);
		pCmd->RSSetScissorRects(1, &m_Scissor);

		DrawScene(pCmd, lightForward);

		DirectX::TransitionResource(pCmd, m_SceneColorTarget.GetResource(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
		DirectX::TransitionResource(pCmd, m_SceneDepthTarget.GetResource(), D3D12_RESOURCE_STATE_DEPTH_WRITE, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
	}

	// トーンマップを適用してフレームバッファに描画するパス
	{
		DirectX::TransitionResource(pCmd, m_ColorTarget[m_FrameIndex].GetResource(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);

		const DescriptorHandle* handleRTV = m_ColorTarget[m_FrameIndex].GetHandleRTV();
		pCmd->OMSetRenderTargets(1, &handleRTV->HandleCPU, FALSE, nullptr);

		m_ColorTarget[m_FrameIndex].ClearView(pCmd);

		DrawTonemap(pCmd);

		DirectX::TransitionResource(pCmd, m_ColorTarget[m_FrameIndex].GetResource(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
	}

	pCmd->Close();

	ID3D12CommandList* pLists[] = {pCmd};
	m_pQueue->ExecuteCommandLists(1, pLists);

	Present(1);
}

void SampleApp::DrawShadowMap(ID3D12GraphicsCommandList* pCmdList, const Vector3& lightForward)
{
	// 変換行列用の定数バッファの更新
	{
		float width = static_cast<float>(m_ShadowMapTarget.GetDesc().Width);
		float height = static_cast<float>(m_ShadowMapTarget.GetDesc().Height);
		// モデルのサイズから目分量で決めている
		float zNear = 0.0f;
		float zFar = 40.0f;
		float widthHeight = 40.0f;

		CbTransform* ptr = m_ShadowMapTransformCB[m_FrameIndex].GetPtr<CbTransform>();

		const Matrix& view = Matrix::CreateLookAt(Vector3::Zero + lightForward * (zFar - zNear) * 0.5f, Vector3::Zero, Vector3::UnitY);
		const Matrix& proj = Matrix::CreateOrthographic(widthHeight, widthHeight, zNear, zFar);
		ptr->ViewProj = view * proj; // 行ベクトル形式の順序で乗算するのがXMMatrixMultiply()
	}

	pCmdList->SetGraphicsRootSignature(m_SceneRootSig.GetPtr());
	pCmdList->SetGraphicsRootDescriptorTable(0, m_ShadowMapTransformCB[m_FrameIndex].GetHandleGPU());
	pCmdList->SetGraphicsRootDescriptorTable(1, m_MeshCB.GetHandleGPU());

	pCmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST); // 本のサンプルでは漏れている

	// Opaqueマテリアルのメッシュの描画
	pCmdList->SetPipelineState(m_pSceneDepthOpaquePSO.Get());
	DrawMesh(pCmdList, ALPHA_MODE::ALPHA_MODE_OPAQUE);

	// Mask, DoubleSidedマテリアルのメッシュの描画
	pCmdList->SetPipelineState(m_pSceneDepthMaskPSO.Get());
	DrawMesh(pCmdList, ALPHA_MODE::ALPHA_MODE_MASK);
}

void SampleApp::DrawScene(ID3D12GraphicsCommandList* pCmdList, const DirectX::SimpleMath::Vector3& lightForward)
{
	// 変換行列用の定数バッファの更新
	{
		constexpr float fovY = DirectX::XMConvertToRadians(37.5f);
		float aspect = static_cast<float>(m_Width) / static_cast<float>(m_Height);

		const Matrix& view = m_Camera.GetView();
		const Matrix& proj = Matrix::CreatePerspectiveFieldOfView(fovY, aspect, 0.1f, 100.0f);
		CbTransform* ptr = m_TransformCB[m_FrameIndex].GetPtr<CbTransform>();
		ptr->ViewProj = view * proj; // 行ベクトル形式の順序で乗算するのがXMMatrixMultiply()

		float zNear = 0.0f;
		float zFar = 40.0f;
		float widthHeight = 40.0f;

		// プロジェクション座標の[-w/2,w/2]*[-h/2,h/2]*[zNear,zFar]をシャドウマップ用座標[-1,1]*[-1,1]*[0,1]に変換する
		const Matrix& toShadowMap = Matrix::CreateTranslation(0.0f, 0.0f, -zNear) * Matrix::CreateScale(1.0f / widthHeight, -1.0f / widthHeight, 1.0f / (zFar - zNear)) * Matrix::CreateTranslation(0.5f, 0.5f, -zNear);
		ptr->ToShadowMap = toShadowMap;
	}

	// カメラバッファの更新
	{
		CbCamera* ptr = m_CameraCB[m_FrameIndex].GetPtr<CbCamera>();
		ptr->CameraPosition = m_Camera.GetPosition();
	}

	// ライトバッファの更新
	{
		CbLight* ptr = m_LightCB[m_FrameIndex].GetPtr<CbLight>();
		ptr->LightColor = Vector3(1.0f, 1.0f, 1.0f);
		ptr->LightForward = lightForward;
		ptr->LightIntensity = 5.0f;
	}

	//TODO:DrawShadowMapと重複してるがとりあえず
	pCmdList->SetGraphicsRootSignature(m_SceneRootSig.GetPtr());
	pCmdList->SetGraphicsRootDescriptorTable(0, m_TransformCB[m_FrameIndex].GetHandleGPU());
	pCmdList->SetGraphicsRootDescriptorTable(1, m_MeshCB.GetHandleGPU());
	pCmdList->SetGraphicsRootDescriptorTable(2, m_LightCB[m_FrameIndex].GetHandleGPU());
	pCmdList->SetGraphicsRootDescriptorTable(3, m_CameraCB[m_FrameIndex].GetHandleGPU());

	//TODO:DrawShadowMapと重複してるがとりあえず
	pCmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST); // 本のサンプルでは漏れている

	// Opaqueマテリアルのメッシュの描画
	pCmdList->SetPipelineState(m_pSceneOpaquePSO.Get());
	DrawMesh(pCmdList, ALPHA_MODE::ALPHA_MODE_OPAQUE);

	// Mask, DoubleSidedマテリアルのメッシュの描画
	pCmdList->SetPipelineState(m_pSceneMaskPSO.Get());
	DrawMesh(pCmdList, ALPHA_MODE::ALPHA_MODE_MASK);
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

		pCmdList->SetGraphicsRootDescriptorTable(4, m_Material.GetBufferHandle(materialId));
		pCmdList->SetGraphicsRootDescriptorTable(5, m_Material.GetTextureHandle(materialId, Material::TEXTURE_USAGE_BASE_COLOR));
		pCmdList->SetGraphicsRootDescriptorTable(6, m_Material.GetTextureHandle(materialId, Material::TEXTURE_USAGE_METALLIC_ROUGHNESS));
		pCmdList->SetGraphicsRootDescriptorTable(7, m_Material.GetTextureHandle(materialId, Material::TEXTURE_USAGE_NORMAL));
		pCmdList->SetGraphicsRootDescriptorTable(8, m_ShadowMapTarget.GetHandleSRV()->HandleGPU);

		m_pMesh[i]->Draw(pCmdList);
	}
}

void SampleApp::DrawTonemap(ID3D12GraphicsCommandList* pCmdList)
{
	{
		CbTonemap* ptr = m_TonemapCB[m_FrameIndex].GetPtr<CbTonemap>();
		ptr->Type = m_TonemapType;
		ptr->ColorSpace = m_ColorSpace;
		ptr->BaseLuminance = m_BaseLuminance;
		ptr->MaxLuminance = m_MaxLuminance;
	}

	pCmdList->SetGraphicsRootSignature(m_TonemapRootSig.GetPtr());
	pCmdList->SetGraphicsRootDescriptorTable(0, m_TonemapCB[m_FrameIndex].GetHandleGPU());
	pCmdList->SetGraphicsRootDescriptorTable(1, m_SceneColorTarget.GetHandleSRV()->HandleGPU);
	pCmdList->SetPipelineState(m_pTonemapPSO.Get());

	pCmdList->RSSetViewports(1, &m_Viewport);
	pCmdList->RSSetScissorRects(1, &m_Scissor);
	
	pCmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	const D3D12_VERTEX_BUFFER_VIEW& VBV = m_QuadVB.GetView();
	pCmdList->IASetVertexBuffers(0, 1, &VBV);
	pCmdList->DrawInstanced(3, 1, 0, 0);
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
