#pragma once

#include "ComPtr.h"
#include <vector>


class ShaderCompiler
{
public:
	virtual ~ShaderCompiler();
	bool Init();
	void Term();
	bool Compile(const wchar_t* filePath, std::vector<const wchar_t*>& args, ComPtr<class IDxcBlob>& outBlob);

private:
	ComPtr<class IDxcUtils> m_pUtils;
	ComPtr<class IDxcCompiler3> m_pCompiler;
};
