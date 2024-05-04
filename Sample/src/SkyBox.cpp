#include "SkyBox.h"
#include "Logger.h"
#include "DescriptorPool.h"
#include <CommonStates.h>

using namespace DirectX::SimpleMath;

namespace
{
#include "../res/Compiled/SkyBoxVS.inc"
#include "../res/Compiled/SkyBoxPS.inc"

	struct alignas(256) CbSkyBox
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

bool SkyBox::Init
(
	ID3D12Device* pDevice,
	class DescriptorPool* pPoolRes,
	DXGI_FORMAT colorFormat,
	DXGI_FORMAT depthFormat
)
{
	if (pDevice == nullptr || pPoolRes == nullptr)
	{
		ELOG("Error : Invalid Argument.");
		return false;
	}

	m_pPoolRes = pPoolRes;
	m_pPoolRes->AddRef();

	// ルートシグニチャの生成
	//TODO: RootSignatureクラスがあるのにわざわざ作る
	{
		D3D12_DESCRIPTOR_RANGE range[2] = {};

		range[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_CBV;
		range[0].NumDescriptors = 1;
		range[0].BaseShaderRegister = 0;
		range[0].RegisterSpace = 0;
		range[0].OffsetInDescriptorsFromTableStart = 0;

		range[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
		range[1].NumDescriptors = 1;
		range[1].BaseShaderRegister = 0;
		range[1].RegisterSpace = 0;
		range[1].OffsetInDescriptorsFromTableStart = 0;

		D3D12_STATIC_SAMPLER_DESC sampler = {};
		sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
		sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
		sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
		sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
		sampler.MipLODBias = D3D12_DEFAULT_MIP_LOD_BIAS;
		sampler.MaxAnisotropy = 1;
		sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
		sampler.BorderColor = D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK;
		sampler.MinLOD = -D3D12_FLOAT32_MAX;
		sampler.MaxLOD = D3D12_FLOAT32_MAX;
		sampler.ShaderRegister = 0;
		sampler.RegisterSpace = 0;
		sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

		D3D12_ROOT_PARAMETER param[2] = {};

		param[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
		param[0].DescriptorTable.NumDescriptorRanges = 1;
		param[0].DescriptorTable.pDescriptorRanges = &range[0];
		param[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;

		param[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
		param[1].DescriptorTable.NumDescriptorRanges = 1;
		param[1].DescriptorTable.pDescriptorRanges = &range[1];
		param[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

		D3D12_ROOT_SIGNATURE_DESC desc = {};
		desc.NumParameters = 2;
		desc.NumStaticSamplers = 1;
		desc.pParameters = param;
		desc.pStaticSamplers = &sampler;
		desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT | D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS
			| D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS | D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS;

		ComPtr<ID3DBlob> pBlob;
		ComPtr<ID3DBlob> pErrorBlob;

		HRESULT hr = D3D12SerializeRootSignature(
			&desc,
			D3D_ROOT_SIGNATURE_VERSION_1,
			pBlob.GetAddressOf(),
			pErrorBlob.GetAddressOf()
		);
		if (FAILED(hr))
		{
			ELOG("Error : D3D12SerializeRootSignature Failed. retcode = 0x%x", hr);
			return false;
		}

		hr = pDevice->CreateRootSignature(
			0,
			pBlob->GetBufferPointer(),
			pBlob->GetBufferSize(),
			IID_PPV_ARGS(m_pRootSig.GetAddressOf())
		);
		if (FAILED(hr))
		{
			ELOG("Error : Root Signature Create Failed. retcode = 0x%x", hr);
			return false;
		}
	}

	// パイプラインステートの生成
	{
		const D3D12_INPUT_ELEMENT_DESC elements[] = {
			{"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
		};

		D3D12_GRAPHICS_PIPELINE_STATE_DESC desc = {};
		desc.pRootSignature = m_pRootSig.Get();
		desc.VS.pShaderBytecode = SkyBoxVS;
		desc.VS.BytecodeLength = sizeof(SkyBoxVS);
		desc.PS.pShaderBytecode = SkyBoxPS;
		desc.PS.BytecodeLength = sizeof(SkyBoxPS);
		desc.BlendState = DirectX::CommonStates::Opaque;
		desc.SampleMask = UINT_MAX;
		desc.RasterizerState = DirectX::CommonStates::CullNone;
		desc.DepthStencilState = DirectX::CommonStates::DepthRead;
		desc.InputLayout.pInputElementDescs = elements;
		desc.InputLayout.NumElements = 1;
		desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
		desc.NumRenderTargets = 1;
		desc.RTVFormats[0] = colorFormat;
		desc.DSVFormat = depthFormat;
		desc.SampleDesc.Count = 1;
		desc.SampleDesc.Quality = 0;
		desc.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;

		HRESULT hr = pDevice->CreateGraphicsPipelineState(
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

	// 定数バッファの生成
	{
		for (size_t i = 0; i < 2; i++)
		{
			if (!m_CB[i].Init(pDevice, pPoolRes, sizeof(CbSkyBox)))
			{
				ELOG("Error : ConstantBuffer::Init() Failed.");
				return false;
			}
		}
	}

	m_Index = 0;

	return true;
}

void SkyBox::Term()
{
	for (size_t i = 0; i < 2; i++)
	{
		m_CB[i].Term();
	}

	m_VB.Term();

	m_pPSO.Reset();
	m_pRootSig.Reset();

	if (m_pPoolRes != nullptr)
	{
		m_pPoolRes->Release();
		m_pPoolRes = nullptr;
	}
}

void SkyBox::Draw
(
	ID3D12GraphicsCommandList* pCmd,
	D3D12_GPU_DESCRIPTOR_HANDLE handleCubeMap,
	const DirectX::SimpleMath::Matrix& viewMatrix,
	const DirectX::SimpleMath::Matrix& projMatrix,
	float boxSize
)
{
	// 定数バッファの更新
	{
		CbSkyBox* ptr = m_CB[m_Index].GetPtr<CbSkyBox>();
		const Matrix& invViewMat = viewMatrix.Invert();
		const Vector3& cameraWorldPos = Vector3(invViewMat._41, invViewMat._42, invViewMat._43);
		ptr->World = Matrix::CreateScale(boxSize) * Matrix::CreateTranslation(cameraWorldPos);
		ptr->View = viewMatrix;
		ptr->Proj = projMatrix;
	}

	const D3D12_VERTEX_BUFFER_VIEW& vbv = m_VB.GetView();
	pCmd->SetGraphicsRootSignature(m_pRootSig.Get());
	pCmd->SetGraphicsRootDescriptorTable(0, m_CB[m_Index].GetHandleGPU());
	pCmd->SetGraphicsRootDescriptorTable(1, handleCubeMap);
	pCmd->SetPipelineState(m_pPSO.Get());
	pCmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	pCmd->IASetIndexBuffer(nullptr);
	pCmd->IASetVertexBuffers(0, 1, &vbv);
	pCmd->DrawInstanced(36, 1, 0, 0); // TODO:36がマジックナンバー

	m_Index = (m_Index + 1) % 2;
}