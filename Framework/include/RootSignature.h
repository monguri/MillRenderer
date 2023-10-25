#pragma once

#include <stdint.h>
#include <d3d12.h>
#include <vector>
#include "ComPtr.h"

// D3D12_SHADER_VISIBILITY‚Æ“¯‚¶’l‚É‚È‚é‚æ‚¤‚É‚µ‚Ä‚ ‚é
enum ShaderStage
{
	ALL = 0,
	VS = 1,
	HS = 2,
	DS = 3,
	GS = 4,
	PS = 5,
};

enum SamplerState
{
	PointWrap,
	PointClamp,
	LinearWrap,
	LinearClamp,
	AnisotropicWrap,
	AnisotropicClamp,
};

class RootSignature
{
public:
	class Desc
	{
	public:
		Desc();
		~Desc();
		Desc& Begin(int count);
		Desc& SetCBV(ShaderStage stage, int index, uint32_t reg);
		Desc& SetSRV(ShaderStage stage, int index, uint32_t reg);
		Desc& SetUAV(ShaderStage stage, int index, uint32_t reg);
		Desc& SetSmp(ShaderStage stage, int index, uint32_t reg);
		Desc& AddStaticSmp(ShaderStage stage, uint32_t reg, SamplerState state);
		Desc& AllowIL();
		Desc& AllowSO();
		Desc& End();
		const D3D12_ROOT_SIGNATURE_DESC* GetDesc() const;

	private:
		std::vector<D3D12_DESCRIPTOR_RANGE> m_Ranges;
		std::vector<D3D12_STATIC_SAMPLER_DESC> m_Samplers;
		std::vector<D3D12_ROOT_PARAMETER> m_Params;
		D3D12_ROOT_SIGNATURE_DESC m_Desc;
		bool m_DenyStage[5];
		uint32_t m_Flags;

		void CheckStage(ShaderStage stage);
		void SetParam(ShaderStage stage, int index, uint32_t reg, D3D12_DESCRIPTOR_RANGE_TYPE type);
	};

	RootSignature();
	~RootSignature();

	bool Init(ID3D12Device* pDevice, const D3D12_ROOT_SIGNATURE_DESC* pDesc);
	void Term();
	ID3D12RootSignature* GetPtr() const;

private:
	ComPtr<ID3D12RootSignature> m_RootSignature;
};

