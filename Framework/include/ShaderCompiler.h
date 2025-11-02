#pragma once

#include "ComPtr.h"
#include <vector>
#include <dxcapi.h>

class ShaderCompiler
{
public:
	virtual ~ShaderCompiler();
	bool Init();
	void Term();
	bool Compile(const wchar_t* filePath, std::vector<const wchar_t*>& args, ComPtr<struct IDxcBlob>& outBlob);

private:
	ComPtr<struct IDxcUtils> m_pUtils;
	ComPtr<struct IDxcCompiler3> m_pCompiler;
};
