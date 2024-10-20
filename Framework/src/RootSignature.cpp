#include "RootSignature.h"
#include "Logger.h"
#include "d3dcompiler.h"

RootSignature::Desc::Desc()
: m_Desc()
, m_Flags(0)
{
	for (size_t i = 0; i < 5; i++)
	{
		m_DenyStage[i] = true;
	}
}

RootSignature::Desc::~Desc()
{
	m_Ranges.clear();
	m_Samplers.clear();
	m_Params.clear();
}

RootSignature::Desc& RootSignature::Desc::Begin()
{
	m_Flags = 0;

	for (size_t i = 0; i < 5; i++)
	{
		m_DenyStage[i] = true;
	}

	memset(&m_Desc, 0, sizeof(m_Desc));

	m_Samplers.clear();

	m_Ranges.clear();
	m_Params.clear();

	return *this;
}

void RootSignature::Desc::CheckStage(ShaderStage stage)
{
	int index = int(stage - 1);
	if (0 <= index && index < 5)
	{
		m_DenyStage[index] = false;
	}
}

void RootSignature::Desc::SetParam(ShaderStage stage, int rootParamIdx, uint32_t reg, D3D12_DESCRIPTOR_RANGE_TYPE type)
{
	if (rootParamIdx >= m_Params.size())
	{
		return;
	}

	m_Ranges[rootParamIdx].RangeType = type;
	m_Ranges[rootParamIdx].NumDescriptors = 1;
	m_Ranges[rootParamIdx].BaseShaderRegister = reg;
	m_Ranges[rootParamIdx].RegisterSpace = 0;
	m_Ranges[rootParamIdx].OffsetInDescriptorsFromTableStart = 0;

	// CBV,SRV,UAV,Samplerの場合もD3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLEのタイプの書き方で書ける。
	// ID3D12CommandList::SetGraphicsRootConstantBufferViewでなくID3D12CommandList::SetGraphicsRootDescriptorTableを使えば
	m_Params[rootParamIdx].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	m_Params[rootParamIdx].DescriptorTable.NumDescriptorRanges = 1;
	m_Params[rootParamIdx].ShaderVisibility = D3D12_SHADER_VISIBILITY(stage);

	CheckStage(stage);
}

RootSignature::Desc& RootSignature::Desc::SetCBV(ShaderStage stage, int rootParamIdx, uint32_t reg)
{
	m_Ranges.push_back(D3D12_DESCRIPTOR_RANGE());
	m_Params.push_back(D3D12_ROOT_PARAMETER());
	SetParam(stage, rootParamIdx, reg, D3D12_DESCRIPTOR_RANGE_TYPE_CBV);
	return *this;
}

RootSignature::Desc& RootSignature::Desc::SetSRV(ShaderStage stage, int rootParamIdx, uint32_t reg)
{
	m_Ranges.push_back(D3D12_DESCRIPTOR_RANGE());
	m_Params.push_back(D3D12_ROOT_PARAMETER());
	SetParam(stage, rootParamIdx, reg, D3D12_DESCRIPTOR_RANGE_TYPE_SRV);
	return *this;
}

RootSignature::Desc& RootSignature::Desc::SetUAV(ShaderStage stage, int rootParamIdx, uint32_t reg)
{
	m_Ranges.push_back(D3D12_DESCRIPTOR_RANGE());
	m_Params.push_back(D3D12_ROOT_PARAMETER());
	SetParam(stage, rootParamIdx, reg, D3D12_DESCRIPTOR_RANGE_TYPE_UAV);
	return *this;
}

RootSignature::Desc& RootSignature::Desc::SetSmp(ShaderStage stage, int rootParamIdx, uint32_t reg)
{
	m_Ranges.push_back(D3D12_DESCRIPTOR_RANGE());
	m_Params.push_back(D3D12_ROOT_PARAMETER());
	SetParam(stage, rootParamIdx, reg, D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER);
	return *this;
}

RootSignature::Desc& RootSignature::Desc::AddStaticSmp(ShaderStage stage, uint32_t reg, SamplerState state, float MipLODBias)
{
	D3D12_STATIC_SAMPLER_DESC desc = {};
	desc.MipLODBias = MipLODBias;
	desc.MaxAnisotropy = 1;
	desc.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
	desc.BorderColor = D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK;
	desc.MinLOD = 0.0f;
	desc.MaxLOD = D3D12_FLOAT32_MAX;
	desc.ShaderRegister = reg;
	desc.RegisterSpace = 0;
	desc.ShaderVisibility = D3D12_SHADER_VISIBILITY(stage);
	CheckStage(stage);

	switch (state)
	{
		case PointWrap:
			desc.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
			desc.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
			desc.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
			desc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
			break;
		case PointClamp:
			desc.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
			desc.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
			desc.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
			desc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
			break;
		case PointBorder:
			desc.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
			desc.AddressU = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
			desc.AddressV = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
			desc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
			break;
		case LinearWrap:
			desc.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
			desc.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
			desc.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
			desc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
			break;
		case LinearClamp:
			desc.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
			desc.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
			desc.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
			desc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
			break;
		case LinearBorder:
			desc.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
			desc.AddressU = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
			desc.AddressV = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
			desc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
			break;
		case MinMagLinearMipPointWrap:
			desc.Filter = D3D12_FILTER_MIN_MAG_LINEAR_MIP_POINT;
			desc.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
			desc.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
			desc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
			break;
		case MinMagLinearMipPointClamp:
			desc.Filter = D3D12_FILTER_MIN_MAG_LINEAR_MIP_POINT;
			desc.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
			desc.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
			desc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
			break;
		case MinMagLinearMipPointBorder:
			desc.Filter = D3D12_FILTER_MIN_MAG_LINEAR_MIP_POINT;
			desc.AddressU = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
			desc.AddressV = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
			desc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
			break;
		case AnisotropicWrap:
			desc.Filter = D3D12_FILTER_ANISOTROPIC;
			desc.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
			desc.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
			desc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
			desc.MaxAnisotropy = D3D12_MAX_MAXANISOTROPY;
			break;
		case AnisotropicClamp:
			desc.Filter = D3D12_FILTER_ANISOTROPIC;
			desc.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
			desc.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
			desc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
			desc.MaxAnisotropy = D3D12_MAX_MAXANISOTROPY;
			break;
		case AnisotropicBorder:
			desc.Filter = D3D12_FILTER_ANISOTROPIC;
			desc.AddressU = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
			desc.AddressV = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
			desc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
			desc.MaxAnisotropy = D3D12_MAX_MAXANISOTROPY;
			break;
		default:
			ELOG("Error : RootSignature::Desc::AddStaticSmp Failed. Invalid SamplerState = %d.", state);
			break;
	}

	m_Samplers.push_back(desc);

	return *this;
}

RootSignature::Desc& RootSignature::Desc::AddStaticCmpSmp(ShaderStage stage, uint32_t reg, SamplerState state)
{
	D3D12_STATIC_SAMPLER_DESC desc = {};
	desc.MipLODBias = D3D12_DEFAULT_MIP_LOD_BIAS;
	desc.MaxAnisotropy = 1;
	desc.ComparisonFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL; // DirectX::CommonStates::DepthDefaultの仕様に合わせる
	desc.BorderColor = D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE; // zFarを1にしているので、zFarで初期化する。
	desc.MinLOD = 0.0f;
	desc.MaxLOD = D3D12_FLOAT32_MAX;
	desc.ShaderRegister = reg;
	desc.RegisterSpace = 0;
	desc.ShaderVisibility = D3D12_SHADER_VISIBILITY(stage);
	CheckStage(stage);

	switch (state)
	{
		case PointWrap:
			desc.Filter = D3D12_FILTER_COMPARISON_MIN_MAG_MIP_POINT;
			desc.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
			desc.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
			desc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
			break;
		case PointClamp:
			desc.Filter = D3D12_FILTER_COMPARISON_MIN_MAG_MIP_POINT;
			desc.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
			desc.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
			desc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
			break;
		case LinearWrap:
			desc.Filter = D3D12_FILTER_COMPARISON_MIN_MAG_MIP_LINEAR;
			desc.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
			desc.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
			desc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
			break;
		case LinearClamp:
			desc.Filter = D3D12_FILTER_COMPARISON_MIN_MAG_MIP_LINEAR;
			desc.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
			desc.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
			desc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
			break;
		case MinMagLinearMipPointWrap:
			desc.Filter = D3D12_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT;
			desc.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
			desc.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
			desc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
			break;
		case MinMagLinearMipPointClamp:
			desc.Filter = D3D12_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT;
			desc.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
			desc.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
			desc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
			break;
		case AnisotropicWrap:
			desc.Filter = D3D12_FILTER_COMPARISON_ANISOTROPIC;
			desc.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
			desc.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
			desc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
			desc.MaxAnisotropy = D3D12_MAX_MAXANISOTROPY;
			break;
		case AnisotropicClamp:
			desc.Filter = D3D12_FILTER_COMPARISON_ANISOTROPIC;
			desc.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
			desc.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
			desc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
			desc.MaxAnisotropy = D3D12_MAX_MAXANISOTROPY;
			break;
		default:
			ELOG("Error : RootSignature::Desc::AddStaticCmpSmp Failed. Invalid SamplerState = %d.", state);
			break;
	}

	m_Samplers.push_back(desc);

	return *this;
}

RootSignature::Desc& RootSignature::Desc::AllowIL()
{
	m_Flags |= D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
	return *this;
}

RootSignature::Desc& RootSignature::Desc::AllowSO()
{
	m_Flags |= D3D12_ROOT_SIGNATURE_FLAG_ALLOW_STREAM_OUTPUT;
	return *this;
}

RootSignature::Desc& RootSignature::Desc::End()
{
	if (m_DenyStage[0])
	{
		m_Flags |= D3D12_ROOT_SIGNATURE_FLAG_DENY_VERTEX_SHADER_ROOT_ACCESS;
	}

	if (m_DenyStage[1])
	{
		m_Flags |= D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS;
	}

	if (m_DenyStage[2])
	{
		m_Flags |= D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS;
	}

	if (m_DenyStage[3])
	{
		m_Flags |= D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS;
	}

	if (m_DenyStage[4])
	{
		m_Flags |= D3D12_ROOT_SIGNATURE_FLAG_DENY_PIXEL_SHADER_ROOT_ACCESS;
	}

	// push_back()によってm_Ranges[rootParamIdx]のアドレスが変わるのでアドレスが確定してから代入する
	for (int rootParamIdx = 0; rootParamIdx < m_Params.size(); rootParamIdx++)
	{
		m_Params[rootParamIdx].DescriptorTable.pDescriptorRanges = &m_Ranges[rootParamIdx];
	}

	m_Desc.NumParameters = UINT(m_Params.size());
	m_Desc.pParameters = m_Params.data();
	m_Desc.NumStaticSamplers = UINT(m_Samplers.size());
	m_Desc.pStaticSamplers = m_Samplers.data();
	m_Desc.Flags = D3D12_ROOT_SIGNATURE_FLAGS(m_Flags);

	return *this;
}

const D3D12_ROOT_SIGNATURE_DESC* RootSignature::Desc::GetDesc() const
{
	return &m_Desc;
}

RootSignature::RootSignature()
{
}

RootSignature::~RootSignature()
{
	Term();
}

bool RootSignature::Init(ID3D12Device* pDevice, const D3D12_ROOT_SIGNATURE_DESC* pDesc)
{
	ComPtr<ID3DBlob> pBlob;
	ComPtr<ID3DBlob> pErrorBlob;

	HRESULT hr = D3D12SerializeRootSignature(
		pDesc,
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
		IID_PPV_ARGS(m_RootSignature.GetAddressOf())
	);
	if (FAILED(hr))
	{
		ELOG("Error : Root Signature Create Failed. retcode = 0x%x", hr);
		return false;
	}

	return true;
}

bool RootSignature::Init(ID3D12Device* pDevice, ComPtr<ID3DBlob> pRootSignatureBlob)
{
	ComPtr<ID3DBlob> pRSBlob;
	HRESULT hr = D3DGetBlobPart(pRootSignatureBlob->GetBufferPointer(), pRootSignatureBlob->GetBufferSize(), D3D_BLOB_ROOT_SIGNATURE, 0, &pRSBlob);
	if (FAILED(hr))
	{
		ELOG("Error : D3DGetBlobPart Failed. retcode = 0x%x", hr);
		return false;
	}

	hr = pDevice->CreateRootSignature(0, pRSBlob->GetBufferPointer(), pRSBlob->GetBufferSize(), IID_PPV_ARGS(m_RootSignature.GetAddressOf()));
	if (FAILED(hr))
	{
		ELOG("Error : Root Signature Create Failed. retcode = 0x%x", hr);
		return false;
	}

	return true;
}

void RootSignature::Term()
{
	m_RootSignature.Reset();
}

ID3D12RootSignature* RootSignature::GetPtr() const
{
	return m_RootSignature.Get();
}
