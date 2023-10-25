#pragma once

#include <d3d12.h>
#include "ComPtr.h"
#include <vector>
#include <cstdint>

class CommandList
{
public:
	CommandList();
	~CommandList();

	bool Init
	(
		ID3D12Device* pDevice,
		D3D12_COMMAND_LIST_TYPE type,
		uint32_t count
	);

	void Term();

	ID3D12GraphicsCommandList* Reset();

private:
	ComPtr<ID3D12GraphicsCommandList> m_pCmdList;
	std::vector<ComPtr<ID3D12CommandAllocator>> m_pAllocators;
	uint32_t m_Index;

	CommandList(const CommandList&) = delete;
	void operator=(const CommandList&) = delete;
};
