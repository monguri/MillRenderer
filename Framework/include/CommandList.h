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
		uint32_t count // The number to create allocator. Set 2 if want double buffer.
	);

	void Term();

	ID3D12GraphicsCommandList6* Reset();

private:
	ComPtr<ID3D12GraphicsCommandList6> m_pCmdList;
	std::vector<ComPtr<ID3D12CommandAllocator>> m_pAllocators;
	// allocator index
	uint32_t m_Index;

	CommandList(const CommandList&) = delete;
	void operator=(const CommandList&) = delete;
};
