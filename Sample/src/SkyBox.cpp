#include "SkyBox.h"
#include "Logger.h"
#include "DescriptorPool.h"
#include "FileUtil.h"
#include "ColorTarget.h"
#include <d3dcompiler.h>
#include <CommonStates.h>

using namespace DirectX::SimpleMath;

namespace
{
	static constexpr float KM_TO_M = 1000.0f;

	struct alignas(256) CbSkyBox
	{
		Matrix WVP;
		Matrix InvVRotP;
		Matrix SkyViewLutReferential;
		Vector3 AtmosphereLightDirection;
		float ViewHeight;
		Vector3 AtmosphereLightLuminance;
		int SkyViewLutWidth;
		int SkyViewLutHeight;
		float BottomRadiusKm;
		float TopRadiusKm;
		float Padding[1];
	};

	struct alignas(256) CbEnvironmentCubeMap
	{
		Matrix World;
		Matrix View;
		Matrix Proj;
	};
}

SkyBox::SkyBox()
: m_pPoolRes(nullptr)
, m_Index(0)
{
}

SkyBox::~SkyBox()
{
	Term();
}


bool SkyBox::Init(
	ID3D12Device* pDevice,
	class DescriptorPool* pPoolRes,
	DXGI_FORMAT colorFormat,
	DXGI_FORMAT normalFormat,
	DXGI_FORMAT metallicRoughnessFormat,
	DXGI_FORMAT depthFormat,
	const wchar_t* vsFileName,
	const wchar_t* psFileName
)
{
	if (pDevice == nullptr || pPoolRes == nullptr)
	{
		ELOG("Error : Invalid Argument.");
		return false;
	}

	m_pPoolRes = pPoolRes;
	m_pPoolRes->AddRef();

	// パイプラインステートの生成
	{
		std::wstring vsPath;
		if (!SearchFilePath(vsFileName, vsPath))
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
		if (!SearchFilePath(psFileName, psPath))
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

		if (!m_pRootSig.Init(pDevice, pRSBlob))
		{
			ELOG("Error : RootSignature::Init() Failed.");
			return false;
		}

		const D3D12_INPUT_ELEMENT_DESC elements[] = {
			{"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
		};

		D3D12_GRAPHICS_PIPELINE_STATE_DESC desc = {};
		desc.pRootSignature = m_pRootSig.GetPtr();
		desc.VS.pShaderBytecode = pVSBlob->GetBufferPointer();
		desc.VS.BytecodeLength = pVSBlob->GetBufferSize();
		desc.PS.pShaderBytecode = pPSBlob->GetBufferPointer();
		desc.PS.BytecodeLength = pPSBlob->GetBufferSize();
		desc.BlendState = DirectX::CommonStates::Opaque;
		desc.SampleMask = UINT_MAX;
		desc.RasterizerState = DirectX::CommonStates::CullNone;
		desc.DepthStencilState = DirectX::CommonStates::DepthDefault;
		desc.InputLayout.pInputElementDescs = elements;
		desc.InputLayout.NumElements = 1;
		desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
		desc.NumRenderTargets = 3;
		desc.RTVFormats[0] = colorFormat;
		desc.RTVFormats[1] = normalFormat;
		desc.RTVFormats[2] = metallicRoughnessFormat;
		desc.DSVFormat = depthFormat;
		desc.SampleDesc.Count = 1;
		desc.SampleDesc.Quality = 0;
		desc.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;

		hr = pDevice->CreateGraphicsPipelineState(
			&desc,
			IID_PPV_ARGS(m_pPSO.GetAddressOf())
		);
		if (FAILED(hr))
		{
			ELOG("Error : ID3D12Device::CreateGraphicsPipelineState Failed. retcode = 0x%x", hr);
			return false;
		}
	}

	// 頂点バッファとインデックスバッファの生成
	{
		Vector3 vertices[] =
		{
            Vector3(-1.0f,  1.0f, -1.0f),
            Vector3(-1.0f, -1.0f, -1.0f),
            Vector3( 1.0f, -1.0f, -1.0f),

            Vector3(-1.0f,  1.0f, -1.0f),
            Vector3( 1.0f, -1.0f, -1.0f),
            Vector3( 1.0f,  1.0f, -1.0f),

            Vector3(1.0f,  1.0f, -1.0f),
            Vector3(1.0f, -1.0f, -1.0f),
            Vector3(1.0f, -1.0f,  1.0f),

            Vector3(1.0f,  1.0f, -1.0f),
            Vector3(1.0f, -1.0f,  1.0f),
            Vector3(1.0f,  1.0f,  1.0f),

            Vector3( 1.0f,  1.0f, 1.0f),
            Vector3( 1.0f, -1.0f, 1.0f),
            Vector3(-1.0f, -1.0f, 1.0f),

            Vector3( 1.0f,  1.0f, 1.0f),
            Vector3(-1.0f, -1.0f, 1.0f),
            Vector3(-1.0f,  1.0f, 1.0f),

            Vector3(-1.0f,  1.0f,  1.0f),
            Vector3(-1.0f, -1.0f,  1.0f),
            Vector3(-1.0f, -1.0f, -1.0f),

            Vector3(-1.0f,  1.0f,  1.0f),
            Vector3(-1.0f, -1.0f, -1.0f),
            Vector3(-1.0f,  1.0f, -1.0f),

            Vector3(-1.0f, 1.0f,  1.0f),
            Vector3(-1.0f, 1.0f, -1.0f),
            Vector3( 1.0f, 1.0f, -1.0f),

            Vector3(-1.0f, 1.0f,  1.0f),
            Vector3( 1.0f, 1.0f, -1.0f),
            Vector3( 1.0f, 1.0f,  1.0f),

            Vector3(-1.0f, -1.0f, -1.0f),
            Vector3(-1.0f, -1.0f,  1.0f),
            Vector3( 1.0f, -1.0f,  1.0f),

            Vector3(-1.0f, -1.0f, -1.0f),
            Vector3( 1.0f, -1.0f,  1.0f),
            Vector3( 1.0f, -1.0f, -1.0f),
		};

		uint32_t vertexCount = uint32_t(sizeof(vertices) / sizeof(vertices[0]));
		if (!m_VB.Init<Vector3>(pDevice, vertexCount, vertices))
		{
			ELOG("Error : VertexBuffer::Init() Failed.");
			return false;
		}
	}

	m_Index = 0;

	return true;
}

bool SkyBox::InitSkyAtmosphere
(
	ID3D12Device* pDevice,
	class DescriptorPool* pPoolRes,
	DXGI_FORMAT colorFormat,
	DXGI_FORMAT normalFormat,
	DXGI_FORMAT metallicRoughnessFormat,
	DXGI_FORMAT depthFormat,
	uint32_t skyViewLutWidth,
	uint32_t skyViewLutHeight,
	float planetBottomRadiusKm,
	float planetTopRadiusKm
)
{
	// 定数バッファの生成
	{
		for (size_t i = 0; i < 2; i++)
		{
			if (!m_CB[i].Init(pDevice, pPoolRes, sizeof(CbSkyBox)))
			{
				ELOG("Error : ConstantBuffer::Init() Failed.");
				return false;
			}

			CbSkyBox* ptr = m_CB[i].GetPtr<CbSkyBox>();
			ptr->SkyViewLutWidth = skyViewLutWidth;
			ptr->SkyViewLutHeight = skyViewLutHeight;
			ptr->BottomRadiusKm = planetBottomRadiusKm;
			ptr->TopRadiusKm = planetTopRadiusKm;
		}
	}

	return Init(
		pDevice,
		pPoolRes,
		colorFormat,
		normalFormat,
		metallicRoughnessFormat,
		depthFormat,
		L"SkyBoxVS.cso",
		L"SkyBoxPS.cso"
	);
}

bool SkyBox::InitEnvironmentCubeMap
(
	ID3D12Device* pDevice,
	class DescriptorPool* pPoolRes,
	DXGI_FORMAT colorFormat,
	DXGI_FORMAT normalFormat,
	DXGI_FORMAT metallicRoughnessFormat,
	DXGI_FORMAT depthFormat
)
{
	// 定数バッファの生成
	{
		for (size_t i = 0; i < 2; i++)
		{
			if (!m_CB[i].Init(pDevice, pPoolRes, sizeof(CbEnvironmentCubeMap)))
			{
				ELOG("Error : ConstantBuffer::Init() Failed.");
				return false;
			}
		}
	}

	return Init(
		pDevice,
		pPoolRes,
		colorFormat,
		normalFormat,
		metallicRoughnessFormat,
		depthFormat,
		L"EnvironmentCubeMapVS.cso",
		L"EnvironmentCubeMapPS.cso"
	);
}

void SkyBox::Term()
{
	for (size_t i = 0; i < 2; i++)
	{
		m_CB[i].Term();
	}

	m_VB.Term();

	m_pPSO.Reset();
	m_pRootSig.Term();

	if (m_pPoolRes != nullptr)
	{
		m_pPoolRes->Release();
		m_pPoolRes = nullptr;
	}
}

void SkyBox::Draw(ID3D12GraphicsCommandList* pCmd, D3D12_GPU_DESCRIPTOR_HANDLE texHandle)
{
	const D3D12_VERTEX_BUFFER_VIEW& vbv = m_VB.GetView();
	pCmd->SetGraphicsRootSignature(m_pRootSig.GetPtr());
	pCmd->SetGraphicsRootDescriptorTable(0, m_CB[m_Index].GetHandleGPU());
	pCmd->SetGraphicsRootDescriptorTable(1, texHandle);
	pCmd->SetGraphicsRootDescriptorTable(2, texHandle);
	pCmd->SetPipelineState(m_pPSO.Get());
	pCmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	pCmd->IASetIndexBuffer(nullptr);
	pCmd->IASetVertexBuffers(0, 1, &vbv);
	pCmd->DrawInstanced(36, 1, 0, 0); // TODO:36がマジックナンバー

	m_Index = (m_Index + 1) % 2;
}

void SkyBox::DrawSkyAtmosphere
(
	ID3D12GraphicsCommandList* pCmd,
	const class ColorTarget& SkyViewLUT_Target,
	const class ColorTarget& SkyTransmittanceLUT_Target,
	const Matrix& viewMatrix,
	const Matrix& projMatrix,
	const Matrix& viewRotProjMatrix,
	float boxSize,
	const Matrix& skyViewLutReferential,
	float planetBottomRadiusKm,
	const Vector3& atmosphereLightDirection,
	const Vector3& atmosphereLightLuminance
)
{
	// 定数バッファの更新
	{
		CbSkyBox* ptr = m_CB[m_Index].GetPtr<CbSkyBox>();
		const Matrix& invViewMat = viewMatrix.Invert();
		const Vector3& cameraWorldPos = Vector3(invViewMat._41, invViewMat._42, invViewMat._43);
		const Vector3& planetCenter = Vector3(0.0f, -planetBottomRadiusKm * KM_TO_M, 0.0f);
		float viewHeight = Vector3::Distance(cameraWorldPos, planetCenter);

		ptr->WVP = Matrix::CreateScale(boxSize) * Matrix::CreateTranslation(cameraWorldPos) * viewMatrix * projMatrix;
		ptr->InvVRotP = viewRotProjMatrix.Invert();
		ptr->SkyViewLutReferential = skyViewLutReferential;
		ptr->AtmosphereLightDirection = atmosphereLightDirection;
		ptr->ViewHeight = viewHeight;
		ptr->AtmosphereLightLuminance = atmosphereLightLuminance;
	}

	Draw(pCmd, inputTex.GetHandleSRV()->HandleGPU);
}

void SkyBox::DrawEnvironmentCubeMap
(
	ID3D12GraphicsCommandList* pCmd,
	D3D12_GPU_DESCRIPTOR_HANDLE cubeMapHandle,
	const struct Matrix& viewMatrix,
	const struct Matrix& projMatrix,
	float boxSize
)
{
	// 定数バッファの更新
	{
		CbEnvironmentCubeMap* ptr = m_CB[m_Index].GetPtr<CbEnvironmentCubeMap>();
		const Matrix& invViewMat = viewMatrix.Invert();
		const Vector3& cameraWorldPos = Vector3(invViewMat._41, invViewMat._42, invViewMat._43);
		ptr->World = Matrix::CreateScale(boxSize) * Matrix::CreateTranslation(cameraWorldPos);
		ptr->View = viewMatrix;
		ptr->Proj = projMatrix;
	}

	Draw(pCmd, cubeMapHandle);
}

