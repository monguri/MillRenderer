#pragma once

#include <string>
#include <d3d12.h>
#include <pix3.h>

class ScopedTimer
{
public:
	ScopedTimer(ID3D12GraphicsCommandList* pCmdList, const std::wstring& name)
	{
		m_pCmdList = pCmdList;
		::PIXBeginEvent(pCmdList, 0, name.c_str());
	}

	~ScopedTimer()
	{
		if (m_pCmdList != nullptr)
		{
			::PIXEndEvent(m_pCmdList);
		}
	}

private:
	ID3D12GraphicsCommandList* m_pCmdList = nullptr;
};
