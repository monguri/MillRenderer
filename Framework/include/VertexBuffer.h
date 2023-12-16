#pragma once

#include <d3d12.h>
#include "ComPtr.h"

class VertexBuffer
{
public:
	VertexBuffer();
	~VertexBuffer();

	bool Init
	(
		ID3D12Device* pDevice,
		size_t size,
		size_t stride,
		const void* pInitData = nullptr
	);

	template<typename T>
	bool Init
	(
		ID3D12Device* pDevice,
		size_t count,
		const T* pInitData = nullptr
	)
	{
		return Init(pDevice, sizeof(T) * count, sizeof(T), pInitData);
	}

	void Term();

	void* Map() const;
	void Unmap() const;

	template<typename T>
	T* Map() const
	{
		return reinterpret_cast<T*>(Map());
	}

	D3D12_VERTEX_BUFFER_VIEW GetView() const;

private:
	ComPtr<ID3D12Resource> m_pVB;
	D3D12_VERTEX_BUFFER_VIEW m_View;

	VertexBuffer(const VertexBuffer&) = delete;
	void operator=(const VertexBuffer&) = delete;
};
