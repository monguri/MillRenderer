#pragma once

#include <d3d12.h>
#include "ComPtr.h"
#include <cstdint>

class IndexBuffer
{
public:
	IndexBuffer();
	~IndexBuffer();

	bool Init
	(
		ID3D12Device* pDevice,
		size_t count,
		const uint32_t* pInitData = nullptr
	);

	void Term();

	uint32_t* Map() const;
	void Unmap() const;

	D3D12_INDEX_BUFFER_VIEW GetView() const;
	size_t GetCount() const;

private:
	ComPtr<ID3D12Resource> m_pIB;
	D3D12_INDEX_BUFFER_VIEW m_View;
	size_t m_Count;

	IndexBuffer(const IndexBuffer&) = delete;
	void operator=(const IndexBuffer&) = delete;
};
