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
		Matrix View;
		Matrix Proj;
	};

	struct alignas(256) CbLight
	{
		float TextureSize;
		float MipCount;
		float LightIntensity;
		float Padding0;
		Vector3 LightDirection; // TODO:使ってない
	};

	struct alignas(256) CbCamera
	{
		Vector3 CameraPosition;
	};

	struct alignas(256) CbMaterial
	{
		Vector3 BaseColor;
		float Alpha;
		float Roughness;
		float Metallic;
	};

	UINT16 inline GetChromaticityCoord(double value)
	{
		return UINT16(value * 50000);
	}

	void SetTextureSet
	(
		const std::wstring& base_path,
		Material& material,
		DirectX::ResourceUploadBatch& batch
	)
	{
		const std::wstring& pathBC = base_path + L"_bc.dds";
		const std::wstring& pathM = base_path + L"_m.dds";
		const std::wstring& pathR = base_path + L"_r.dds";
		const std::wstring& pathN = base_path + L"_n.dds";

		material.SetTexture(0, Material::TEXTURE_USAGE_BASE_COLOR, pathBC, batch);
		material.SetTexture(0, Material::TEXTURE_USAGE_METALLIC, pathM, batch);
		material.SetTexture(0, Material::TEXTURE_USAGE_ROUGHNESS, pathR, batch);
		material.SetTexture(0, Material::TEXTURE_USAGE_NORMAL, pathN, batch);
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
		if (!SearchFilePath(L"res/matball/matball.obj", path))
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

		for (size_t j = 0; j < 16; j++)
		{
			if (!m_Material[j].Init(m_pDevice.Get(), m_pPool[POOL_TYPE_RES], sizeof(CbMaterial), resMaterial.size())) // ContantBufferはこの時点では作らない。テクスチャはダミー。
			{
				ELOG("Error : Material Initialize Failed.");
				return false;

			}
		}
		DirectX::ResourceUploadBatch batch(m_pDevice.Get());

		batch.Begin();

		SetTextureSet(L"../res/texture/wood", m_Material[0], batch);
        SetTextureSet(L"../res/texture/camouflage", m_Material[1], batch);
        SetTextureSet(L"../res/texture/dirt", m_Material[2], batch);
        SetTextureSet(L"../res/texture/fabric", m_Material[3], batch);
        SetTextureSet(L"../res/texture/leathertte", m_Material[4], batch);
        SetTextureSet(L"../res/texture/machinery", m_Material[5], batch);
        SetTextureSet(L"../res/texture/marble", m_Material[6], batch);
        SetTextureSet(L"../res/texture/plastic", m_Material[7], batch);
        SetTextureSet(L"../res/texture/rubber", m_Material[8], batch);
        SetTextureSet(L"../res/texture/rust", m_Material[9], batch);
        SetTextureSet(L"../res/texture/bronze", m_Material[10], batch);
        SetTextureSet(L"../res/texture/steel", m_Material[11], batch);
        SetTextureSet(L"../res/texture/iron", m_Material[12], batch);
        SetTextureSet(L"../res/texture/alminum", m_Material[13], batch);
        SetTextureSet(L"../res/texture/copper", m_Material[14], batch);
        SetTextureSet(L"../res/texture/gold", m_Material[15], batch);

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

    // シーン用ルートシグニチャの生成
	{
		RootSignature::Desc desc;
		desc.Begin(11)
			.SetCBV(ShaderStage::VS, 0, 0)
			.SetCBV(ShaderStage::VS, 1, 1)
			.SetCBV(ShaderStage::PS, 2, 1)
			.SetCBV(ShaderStage::PS, 3, 2)
			.SetSRV(ShaderStage::PS, 4, 0)
			.SetSRV(ShaderStage::PS, 5, 1)
			.SetSRV(ShaderStage::PS, 6, 2)
			.SetSRV(ShaderStage::PS, 7, 3)
			.SetSRV(ShaderStage::PS, 8, 4)
			.SetSRV(ShaderStage::PS, 9, 5)
			.SetSRV(ShaderStage::PS, 10, 6)
			.AddStaticSmp(ShaderStage::PS, 0, SamplerState::LinearWrap)
			.AddStaticSmp(ShaderStage::PS, 1, SamplerState::LinearWrap)
			.AddStaticSmp(ShaderStage::PS, 2, SamplerState::LinearWrap)
			.AddStaticSmp(ShaderStage::PS, 3, SamplerState::LinearWrap)
			.AddStaticSmp(ShaderStage::PS, 4, SamplerState::LinearWrap)
			.AddStaticSmp(ShaderStage::PS, 5, SamplerState::LinearWrap)
			.AddStaticSmp(ShaderStage::PS, 6, SamplerState::LinearWrap)
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
		std::wstring vsPath;
		std::wstring psPath;

		if (!SearchFilePath(L"BasicVS.cso", vsPath))
		{
			ELOG("Error : Vertex Shader Not Found");
			return false;
		}

		if (!SearchFilePath(L"BasicPS.cso", psPath))
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

		D3D12_GRAPHICS_PIPELINE_STATE_DESC desc = {};
		desc.InputLayout = MeshVertex::InputLayout;
		desc.pRootSignature = m_SceneRootSig.GetPtr();
		desc.VS.pShaderBytecode = pVSBlob->GetBufferPointer();
		desc.VS.BytecodeLength = pVSBlob->GetBufferSize();
		desc.PS.pShaderBytecode = pPSBlob->GetBufferPointer();
		desc.PS.BytecodeLength = pPSBlob->GetBufferSize();
		desc.RasterizerState = DirectX::CommonStates::CullNone;
		desc.BlendState = DirectX::CommonStates::Opaque;
		desc.DepthStencilState = DirectX::CommonStates::DepthDefault;
		desc.SampleMask = UINT_MAX;
		desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
		desc.NumRenderTargets = 1;
		desc.RTVFormats[0] = m_ColorTarget[0].GetRTVDesc().Format;
		desc.DSVFormat = m_DepthTarget.GetDSVDesc().Format;
		desc.SampleDesc.Count = 1;
		desc.SampleDesc.Quality = 0;

		hr = m_pDevice->CreateGraphicsPipelineState(
			&desc,
			IID_PPV_ARGS(m_pScenePSO.GetAddressOf())
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
		desc.RasterizerState = DirectX::CommonStates::CullNone;
		desc.BlendState = DirectX::CommonStates::Opaque;
		desc.DepthStencilState = DirectX::CommonStates::DepthDefault;
		desc.SampleMask = UINT_MAX;
		desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
		desc.NumRenderTargets = 1;
		desc.RTVFormats[0] = m_ColorTarget[0].GetRTVDesc().Format;
		desc.DSVFormat = m_DepthTarget.GetDSVDesc().Format;
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

	// 変換行列用の定数バッファの作成
	{
		for (uint32_t i = 0u; i < FrameCount; i++)
		{
			if (!m_TransformCB[i].Init(m_pDevice.Get(), m_pPool[POOL_TYPE_RES], sizeof(CbTransform)))
			{
				ELOG("Error : ConstantBuffer::Init() Failed.");
				return false;
			}

			const Vector3& eyePos = Vector3(0.0f, 0.0f, 1.0f);
			const Vector3& targetPos = Vector3::Zero;
			const Vector3& upward = Vector3::UnitY;

			constexpr float fovY = DirectX::XMConvertToRadians(37.5f);
			float aspect = static_cast<float>(m_Width) / static_cast<float>(m_Height);

			CbTransform* ptr = m_TransformCB[i].GetPtr<CbTransform>();
			ptr->View = Matrix::CreateLookAt(eyePos, targetPos, upward);
			ptr->Proj = Matrix::CreatePerspectiveFieldOfView(fovY, aspect, 0.1f, 1000.0f);
		}
	}

	// メッシュ用バッファの作成
	{
		for (uint32_t i = 0u; i < FrameCount * 16; i++)
		{
			if (!m_MeshCB[i].Init(m_pDevice.Get(), m_pPool[POOL_TYPE_RES], sizeof(CbMesh)))
			{
				ELOG("Error : ConstantBuffer::Init() Failed.");
				return false;
			}

			CbMesh* ptr = m_MeshCB[i].GetPtr<CbMesh>();
			ptr->World = Matrix::Identity;
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

	// テクスチャロード
	{
		DirectX::ResourceUploadBatch batch(m_pDevice.Get());

		batch.Begin();

		{
			std::wstring sphereMapPath;
			if (!SearchFilePathW(L"../res/texture/hdr014.dds", sphereMapPath))
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

	// スフィアマップコンバーター初期化
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

	// ベイク処理を実行
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

		m_Fence.Sync(m_pQueue.Get());
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
		m_TransformCB[i].Term();
	}

	for (uint32_t i = 0; i < FrameCount * 16; i++)
	{
		m_MeshCB[i].Term();
	}

	for (size_t i = 0; i < m_pMesh.size(); i++)
	{
		SafeTerm(m_pMesh[i]);
	}
	m_pMesh.clear();
	m_pMesh.shrink_to_fit();

	for (size_t i = 0; i < 16; i++)
	{
		m_Material[i].Term();
	}

	m_SceneColorTarget.Term();
	m_SceneDepthTarget.Term();

	m_pScenePSO.Reset();
	m_SceneRootSig.Term();

	m_pTonemapPSO.Reset();
	m_TonemapRootSig.Term();

	m_IBLBaker.Term();
	m_SphereMapConverter.Term();
	m_SphereMap.Term();
	m_SkyBox.Term();
}

void SampleApp::OnRender()
{
	// カメラ更新
	{
		constexpr float fovY = DirectX::XMConvertToRadians(37.5f);
		float aspect = static_cast<float>(m_Width) / static_cast<float>(m_Height);

		m_View = m_Camera.GetView();
		m_Proj = Matrix::CreatePerspectiveFieldOfView(fovY, aspect, 0.1f, 1000.0f);
	}

	ID3D12GraphicsCommandList* pCmd = m_CommandList.Reset();

	ID3D12DescriptorHeap* const pHeaps[] = {
		m_pPool[POOL_TYPE_RES]->GetHeap()
	};

	pCmd->SetDescriptorHeaps(1, pHeaps);

	// シーンをレンダーターゲットに描画するパス
	{
		DirectX::TransitionResource(pCmd, m_SceneColorTarget.GetResource(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);

		const DescriptorHandle* handleRTV = m_SceneColorTarget.GetHandleRTV();
		const DescriptorHandle* handleDSV = m_SceneDepthTarget.GetHandleDSV();

		pCmd->OMSetRenderTargets(1, &handleRTV->HandleCPU, FALSE, &handleDSV->HandleCPU);

		m_SceneColorTarget.ClearView(pCmd);
		m_SceneDepthTarget.ClearView(pCmd);

		pCmd->RSSetViewports(1, &m_Viewport);
		pCmd->RSSetScissorRects(1, &m_Scissor);

		m_SkyBox.Draw(pCmd, m_SphereMapConverter.GetHandleGPU(), m_View, m_Proj, 100.0f);

		DrawScene(pCmd);

		DirectX::TransitionResource(pCmd, m_SceneColorTarget.GetResource(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
	}

	// トーンマップを適用してフレームバッファに描画するパス
	{
		DirectX::TransitionResource(pCmd, m_ColorTarget[m_FrameIndex].GetResource(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);

		const DescriptorHandle* handleRTV = m_ColorTarget[m_FrameIndex].GetHandleRTV();
		const DescriptorHandle* handleDSV = m_DepthTarget.GetHandleDSV();

		pCmd->OMSetRenderTargets(1, &handleRTV->HandleCPU, FALSE, &handleDSV->HandleCPU);

		m_ColorTarget[m_FrameIndex].ClearView(pCmd);
		m_DepthTarget.ClearView(pCmd);

		DrawTonemap(pCmd);

		DirectX::TransitionResource(pCmd, m_ColorTarget[m_FrameIndex].GetResource(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
	}

	pCmd->Close();

	ID3D12CommandList* pLists[] = {pCmd};
	m_pQueue->ExecuteCommandLists(1, pLists);

	Present(1);
}

void SampleApp::DrawScene(ID3D12GraphicsCommandList* pCmdList)
{
	// ライトバッファの更新
	{
		CbLight* ptr = m_LightCB[m_FrameIndex].GetPtr<CbLight>();
		ptr->TextureSize = m_IBLBaker.LDTextureSize; // TODO:DFGTextureSizeはLDTextureSizeの2倍あるのにいいのか？
		ptr->MipCount = m_IBLBaker.MipCount;
		ptr->LightDirection = Vector3(0.0f, -1.0f, 0.0f); // TODO:使ってない
		ptr->LightIntensity = 1.0f;
	}

	// カメラバッファの更新
	{
		CbCamera* ptr = m_CameraCB[m_FrameIndex].GetPtr<CbCamera>();
		ptr->CameraPosition = m_Camera.GetPosition();
	}

	// 変換行列用の定数バッファの更新
	{
		CbTransform* ptr = m_TransformCB[m_FrameIndex].GetPtr<CbTransform>();
		ptr->View = m_View;
		ptr->Proj = m_Proj;
	}

	// メッシュのワールド行列の更新
	{
		float space = 0.75f;

		for (size_t i = 0; i < 16; i++)
		{
			float x = -space * 1.5f + (i % 4) * space;
			float z = -space * 1.5f + (i / 4) * space;

			CbMesh* ptr = m_MeshCB[i + m_FrameIndex * 16].GetPtr<CbMesh>();
			ptr->World = Matrix::CreateTranslation(Vector3(x, 0.0f, z));
		}
	}

	pCmdList->SetGraphicsRootSignature(m_SceneRootSig.GetPtr());
	pCmdList->SetGraphicsRootDescriptorTable(0, m_TransformCB[m_FrameIndex].GetHandleGPU());
	pCmdList->SetGraphicsRootDescriptorTable(2, m_LightCB[m_FrameIndex].GetHandleGPU());
	pCmdList->SetGraphicsRootDescriptorTable(3, m_CameraCB[m_FrameIndex].GetHandleGPU());
	pCmdList->SetGraphicsRootDescriptorTable(4, m_IBLBaker.GetHandleGPU_DFG());
	pCmdList->SetGraphicsRootDescriptorTable(5, m_IBLBaker.GetHandleGPU_DiffuseLD());
	pCmdList->SetGraphicsRootDescriptorTable(6, m_IBLBaker.GetHandleGPU_SpecularLD());
	pCmdList->SetPipelineState(m_pScenePSO.Get());

	pCmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST); // 本のサンプルでは漏れている

	for (size_t i = 0; i < 16; i++)
	{
		pCmdList->SetGraphicsRootDescriptorTable(1, m_MeshCB[i + m_FrameIndex * 16].GetHandleGPU());
		DrawMesh(pCmdList, (int)i);
	}
}

void SampleApp::DrawMesh(ID3D12GraphicsCommandList* pCmdList, int material_index)
{
	for (size_t i = 0; i < m_pMesh.size(); i++)
	{
		uint32_t id = m_pMesh[i]->GetMaterialId();

		const Material& mat = m_Material[material_index];

		pCmdList->SetGraphicsRootDescriptorTable(7, mat.GetTextureHandle(id, Material::TEXTURE_USAGE_BASE_COLOR));
		pCmdList->SetGraphicsRootDescriptorTable(8, mat.GetTextureHandle(id, Material::TEXTURE_USAGE_METALLIC));
		pCmdList->SetGraphicsRootDescriptorTable(9, mat.GetTextureHandle(id, Material::TEXTURE_USAGE_ROUGHNESS));
		pCmdList->SetGraphicsRootDescriptorTable(10, mat.GetTextureHandle(id, Material::TEXTURE_USAGE_NORMAL));

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
