#include "SphereMapConverter.h"
#include "Logger.h"
#include "DescriptorPool.h"
#include <CommonStates.h>
#include <SimpleMath.h>
#include <DirectXHelpers.h>

using namespace DirectX::SimpleMath;

namespace
{
#include "../res/Compiled/SphereToCubeVS.inc"
#include "../res/Compiled/SphereToCubePS.inc"

	struct alignas(256) CbTransform
	{
		Matrix World;
		Matrix View;
		Matrix Proj;
	};
}

SphereMapConverter::SphereMapConverter()
: m_pPoolRes(nullptr)
, m_pPoolRTV(nullptr)
, m_pCubeSRV(nullptr)
{
}

SphereMapConverter::~SphereMapConverter()
{
	Term();
}

bool SphereMapConverter::Init
(
	ID3D12Device* pDevice,
	DescriptorPool* pPoolRTV,
	DescriptorPool* pPoolRes,
	const D3D12_RESOURCE_DESC& sphereMapDesc,
	int mapSize /* = -1 */
)
{
	if (pDevice == nullptr || pPoolRTV == nullptr || pPoolRes == nullptr)
	{
		ELOG("Eror : Invalid Argument.");
		return false;
	}

	m_pPoolRTV = pPoolRTV;
	m_pPoolRTV->AddRef();

	m_pPoolRes = pPoolRes;
	m_pPoolRes->AddRef();

	// ���[�g�V�O�j�`���̐���
	//TODO: RootSignature�N���X������̂ɂ킴�킴���
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
		// TODO:�Ȃ�ANISOTROPIC?
		sampler.Filter = D3D12_FILTER_ANISOTROPIC;
		sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
		sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
		sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
		sampler.MipLODBias = 0.0f;
		sampler.MaxAnisotropy = D3D12_MAX_MAXANISOTROPY;
		sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
		sampler.BorderColor = D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK;
		sampler.MinLOD = 0.0f;
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

	// �p�C�v���C���X�e�[�g�̐���
	{
		const D3D12_INPUT_ELEMENT_DESC elements[] = {
			{"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
			{"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0}
		};

		D3D12_GRAPHICS_PIPELINE_STATE_DESC desc = {};
		desc.pRootSignature = m_pRootSig.Get();
		desc.VS.pShaderBytecode = SphereToCubeVS;
		desc.VS.BytecodeLength = sizeof(SphereToCubeVS);
		desc.PS.pShaderBytecode = SphereToCubePS;
		desc.PS.BytecodeLength = sizeof(SphereToCubePS);
		desc.BlendState = DirectX::CommonStates::Opaque;
		desc.SampleMask = UINT_MAX;
		desc.RasterizerState = DirectX::CommonStates::CullNone;
		desc.DepthStencilState = DirectX::CommonStates::DepthReverseZ;
		desc.InputLayout.pInputElementDescs = elements;
		desc.InputLayout.NumElements = 2;
		desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
		desc.NumRenderTargets = 1;
		desc.RTVFormats[0] = DXGI_FORMAT_R32G32B32A32_FLOAT;
		desc.DSVFormat = DXGI_FORMAT_UNKNOWN;
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

	// �e�N�X�`���̐���
	// TODO:������Ȃ���ColorTarget�N���X���g��Ȃ�
	{
		uint32_t tempSize = (mapSize == -1) ? uint32_t(sphereMapDesc.Width / 4) : uint32_t(mapSize);

		uint32_t currSize = 1;
		uint32_t currMipLevels = 1;

		uint32_t prevSize = 0;

		uint32_t size = 0;
		uint32_t mipLevels = 0;

		for (;;)
		{
			if (prevSize < tempSize && tempSize <= currSize)
			{
				size = currSize;
				mipLevels = currMipLevels;
				break;
			}

			prevSize = currSize;
			currSize <<= 1;
			currMipLevels++;
		}

		m_MipCount = mipLevels;

		D3D12_RESOURCE_DESC desc = {};
		desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
		desc.Width = size;
		desc.Height = UINT(size);
		desc.DepthOrArraySize = 6;
		desc.MipLevels = m_MipCount;
		desc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
		desc.SampleDesc.Count = 1;
		desc.SampleDesc.Quality = 0;
		desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
		desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

		D3D12_HEAP_PROPERTIES prop = {};
		prop.Type = D3D12_HEAP_TYPE_DEFAULT;
		prop.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
		prop.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;

		D3D12_CLEAR_VALUE clearValue = {};
		clearValue.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
		clearValue.Color[0] = 0.0f;
		clearValue.Color[1] = 0.0f;
		clearValue.Color[2] = 0.0f;
		clearValue.Color[3] = 1.0f;

		HRESULT hr = pDevice->CreateCommittedResource
		(
			&prop,
			D3D12_HEAP_FLAG_NONE,
			&desc,
			D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
			&clearValue,
			IID_PPV_ARGS(m_pCubeTex.GetAddressOf())
		);
		if (FAILED(hr))
		{
			ELOG("Eror : ID3D12Device::CreateCommittedResource() Failed. retcode = 0x%x", hr);
			return false;
		}
	}

	// �V�F�[�_���\�[�X�r���[�̐���
	{
		m_pCubeSRV = m_pPoolRes->AllocHandle();
		if (m_pCubeSRV == nullptr)
		{
			ELOG("Eror : DescriptorPool::AllocHandle() Failed.");
			return false;
		}

		D3D12_SHADER_RESOURCE_VIEW_DESC  viewDesc = {};
		viewDesc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
		viewDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
		viewDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		viewDesc.TextureCube.MipLevels = m_MipCount;
		viewDesc.TextureCube.MostDetailedMip = 0;
		viewDesc.TextureCube.ResourceMinLODClamp = 0.0f;

		pDevice->CreateShaderResourceView(m_pCubeTex.Get(), &viewDesc, m_pCubeSRV->HandleCPU);
	}

	// �����_�[�^�[�Q�b�g�̐���
	{
		m_pCubeRTV.resize(m_MipCount * 6);

		uint32_t idx = 0;
		for (UINT i = 0; i < 6; i++)
		{
			for (UINT m = 0; m < m_MipCount; m++)
			{
				DescriptorHandle* pHandle = m_pPoolRTV->AllocHandle();
				if (pHandle == nullptr)
				{
					ELOG("Eror : DescriptorPool::AllocHandle() Failed.");
					return false;
				}

				m_pCubeRTV[idx] = pHandle;

				D3D12_RENDER_TARGET_VIEW_DESC desc = {};
				desc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
				desc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2DARRAY;
				desc.Texture2DArray.ArraySize = 1;
				desc.Texture2DArray.FirstArraySlice = i;
				desc.Texture2DArray.MipSlice = m;
				desc.Texture2DArray.PlaneSlice = 0;

				pDevice->CreateRenderTargetView(
					m_pCubeTex.Get(),
					&desc,
					pHandle->HandleCPU
				);

				idx++;
			}
		}
	}

	// �ϊ��o�b�t�@�̐���
	{
		Vector3 target[6] = 
		{
			Vector3(1.0f, 0.0f, 0.0f),
			Vector3(-1.0f, 0.0f, 0.0f),

			Vector3(0.0f, 1.0f, 0.0f),
			Vector3(0.0f, -1.0f, 0.0f),

			Vector3(0.0f, 0.0f, -1.0f),
			Vector3(0.0f, 0.0f, 1.0f),
		};

		Vector3 upward[6] = 
		{
			Vector3(0.0f, 1.0f, 0.0f),
			Vector3(0.0f, 1.0f, 0.0f),

			Vector3(0.0f, 0.0f, 1.0f),
			Vector3(0.0f, 0.0f, -1.0f),

			Vector3(0.0f, 1.0f, 0.0f),
			Vector3(0.0f, 1.0f, 0.0f),
		};

		for (size_t i = 0; i < 6; i++)
		{
			if (!m_TransformCB[i].Init(pDevice, pPoolRes, sizeof(CbTransform)))
			{
				ELOG("Error : ConstantBuffer::Init() Failed.");
				return false;
			}

			CbTransform* ptr = m_TransformCB[i].GetPtr<CbTransform>();
			ptr->World = Matrix::Identity;
			ptr->View = Matrix::CreateLookAt(Vector3::Zero, target[i], upward[i]);
			ptr->Proj = Matrix::CreatePerspectiveFieldOfView(DirectX::XM_PIDIV2, 1.0f, 0.1f, 1000.0f);
		}
	}

	// ���_�o�b�t�@�ƃC���f�b�N�X�o�b�t�@�̐���
	{
		struct Vertex
		{
			Vector3 Position;
			Vector2 TexCoord;
		};

		std::vector<Vertex> vertices;
		std::vector<uint32_t> indices;

		uint32_t tessellation = 20u;

		uint32_t verticalSegments = tessellation * 2;
		uint32_t horizontalSegments = tessellation * 2;

		float radius = 10.0f;

		for (size_t i = 0; i <= verticalSegments; i++)
		{
			float v = 1 - (float)i / verticalSegments;

			float latitude = (i * DirectX::XM_PI / verticalSegments) - DirectX::XM_PIDIV2;
			float dy, dxz;

			dy = sinf(latitude);
			dxz = cosf(latitude);

			for (size_t j = 0; j <= horizontalSegments; j++)
			{
				// u=0��1�Œ��_���d�����Ă���
				float u = (float)j / horizontalSegments;

				float longitude = j * DirectX::XM_2PI / horizontalSegments;
				float dx, dz;

				dx = sinf(longitude);
				dz = cosf(longitude);

				dx *= dxz;
				dz *= dxz;

				const Vector3& normal = Vector3(dx, dy, dz);
				const Vector2& uv = Vector2(u, v);

				Vertex vert;
				vert.Position = normal * radius;
				vert.TexCoord = uv;

				vertices.push_back(vert);
			}
		}

		uint32_t stride = horizontalSegments + 1;
		for (uint32_t i = 0u; i < verticalSegments; i++)
		{
			for (uint32_t j = 0u; j <= horizontalSegments; j++)
			{
				uint32_t nextI = i + 1;
				uint32_t nextJ = (j + 1) % stride;

				indices.push_back(i * stride + j);
				indices.push_back(nextI * stride + j);
				indices.push_back(i * stride + nextJ);

				indices.push_back(i * stride + nextJ);
				indices.push_back(nextI * stride + j);
				indices.push_back(nextI * stride + nextJ);
			}
		}

		if (!m_VB.Init<Vertex>(pDevice, vertices.size(), vertices.data()))
		{
			ELOG("Error : VertexBuffer::Init() Failed.");
			return false;
		}

		if (!m_IB.Init(pDevice, indices.size(), indices.data()))
		{
			ELOG("Error : IndexBuffer::Init() Failed.");
			return false;
		}
	}

	return true;
}

void SphereMapConverter::Term()
{
	m_VB.Term();
	m_IB.Term();
	m_pRootSig.Reset();
	m_pPSO.Reset();

	for (size_t i = 0; i < 6; i++)
	{
		m_TransformCB[i].Term();
	}

	if (m_pPoolRes != nullptr)
	{
		if (m_pCubeSRV != nullptr)
		{
			m_pPoolRes->FreeHandle(m_pCubeSRV);
			m_pCubeSRV = nullptr;
		}

		m_pPoolRes->Release();
		m_pPoolRes = nullptr;
	}

	if (m_pPoolRTV != nullptr)
	{
		for (size_t i = 0; i < m_pCubeRTV.size(); i++)
		{
			if (m_pCubeRTV[i] != nullptr)
			{
				m_pPoolRTV->FreeHandle(m_pCubeRTV[i]);
				m_pCubeRTV[i] = nullptr;
			}
		}

		m_pCubeRTV.clear();
		m_pCubeRTV.shrink_to_fit();

		m_pPoolRTV->Release();
		m_pPoolRTV = nullptr;
	}

	m_pCubeTex.Reset();
}

void SphereMapConverter::DrawToCube(ID3D12GraphicsCommandList* pCmdList, D3D12_GPU_DESCRIPTOR_HANDLE sphereMapHandle)
{
	DirectX::TransitionResource(pCmdList, m_pCubeTex.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);

	size_t idx = 0;
	const D3D12_RESOURCE_DESC& desc = m_pCubeTex->GetDesc();
	for (size_t i = 0; i < 6; i++)
	{
		uint32_t w = uint32_t(desc.Width);
		UINT h = desc.Height;

		for (uint32_t m = 0u; m < m_MipCount; m++)
		{

			D3D12_CPU_DESCRIPTOR_HANDLE handleRTV = m_pCubeRTV[idx]->HandleCPU;
			pCmdList->OMSetRenderTargets(1, &handleRTV, FALSE, nullptr);

			pCmdList->SetGraphicsRootSignature(m_pRootSig.Get());
			pCmdList->SetGraphicsRootDescriptorTable(0, m_TransformCB[i].GetHandleGPU());
			pCmdList->SetGraphicsRootDescriptorTable(1, sphereMapHandle);
			pCmdList->SetPipelineState(m_pPSO.Get());

			D3D12_VIEWPORT viewport = {};
			viewport.TopLeftX = 0;
			viewport.TopLeftY = 0;
			viewport.Width = float(w);
			viewport.Height = float(h);
			viewport.MinDepth = 0.0f;
			viewport.MaxDepth = 1.0f;
			pCmdList->RSSetViewports(1, &viewport);

			D3D12_RECT scissor = {};
			scissor.left = 0;
			scissor.right = w;
			scissor.top = 0;
			scissor.bottom = h;
			pCmdList->RSSetScissorRects(1, &scissor);

			DrawSphere(pCmdList);

			w >>= 1;
			h >>= 1;

			if (w < 1)
			{
				w = 1;
			}

			if (h < 1)
			{
				h = 1;
			}

			idx++;
		}
	}

	DirectX::TransitionResource(pCmdList, m_pCubeTex.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
}

void SphereMapConverter::DrawSphere(ID3D12GraphicsCommandList* pCmdList)
{
	const D3D12_VERTEX_BUFFER_VIEW& VBV = m_VB.GetView();
	const D3D12_INDEX_BUFFER_VIEW& IBV = m_IB.GetView();

	pCmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	pCmdList->IASetVertexBuffers(0, 1, &VBV);
	pCmdList->IASetIndexBuffer(&IBV);
	pCmdList->DrawIndexedInstanced(UINT(m_IB.GetCount()), 1, 0, 0, 0);
}

D3D12_RESOURCE_DESC SphereMapConverter::GetCubeMapDesc() const
{
	if (m_pCubeTex == nullptr)
	{
		return D3D12_RESOURCE_DESC();
	}

	return m_pCubeTex->GetDesc();
}

D3D12_GPU_DESCRIPTOR_HANDLE SphereMapConverter::GetHandleGPU() const
{
	if (m_pCubeSRV == nullptr)
	{
		return D3D12_GPU_DESCRIPTOR_HANDLE();
	}

	return m_pCubeSRV->HandleGPU;
}
