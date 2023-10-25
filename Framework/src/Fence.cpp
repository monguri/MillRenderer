#include "Fence.h"

Fence::Fence()
: m_pFence(nullptr)
, m_Event(nullptr)
, m_Counter(0)
{
}

Fence::~Fence()
{
	Term();
}

bool Fence::Init(ID3D12Device* pDevice)
{
	if (pDevice == nullptr)
	{
		return false;
	}

	m_Event = CreateEventEx(nullptr, FALSE, FALSE, EVENT_ALL_ACCESS);
	if (m_Event == nullptr)
	{
		return false;
	}

	HRESULT hr = pDevice->CreateFence(
		0,
		D3D12_FENCE_FLAG_NONE,
		IID_PPV_ARGS(m_pFence.GetAddressOf())
	);
	if (FAILED(hr))
	{
		return false;
	}

	m_Counter = 1;

	return true;
}

void Fence::Term()
{
	if (m_Event != nullptr)
	{
		CloseHandle(m_Event);
		m_Event = nullptr;
	}

	m_pFence.Reset();

	m_Counter = 0;
}

void Fence::Wait(ID3D12CommandQueue* pQueue, UINT timeout)
{
	if (pQueue == nullptr)
	{
		return;
	}

	const UINT fenceValue = m_Counter;
	HRESULT hr = pQueue->Signal(m_pFence.Get(), fenceValue);
	if (FAILED(hr))
	{
		return;
	}

	m_Counter++;

	if (m_pFence->GetCompletedValue() < fenceValue)
	{
		hr = m_pFence->SetEventOnCompletion(fenceValue, m_Event);
		if (FAILED(hr))
		{
			return;
		}

		if (WAIT_OBJECT_0 != WaitForSingleObjectEx(m_Event, timeout, FALSE))
		{
			return;
		}
	}
}

void Fence::Sync(ID3D12CommandQueue* pQueue)
{
	if (pQueue == nullptr)
	{
		return;
	}

	const UINT fenceValue = m_Counter;
	HRESULT hr = pQueue->Signal(m_pFence.Get(), fenceValue);
	if (FAILED(hr))
	{
		return;
	}

	hr = m_pFence->SetEventOnCompletion(fenceValue, m_Event);
	if (FAILED(hr))
	{
		return;
	}

	if (WAIT_OBJECT_0 != WaitForSingleObjectEx(m_Event, INFINITE, FALSE))
	{
		return;
	}

	m_Counter++;
}
