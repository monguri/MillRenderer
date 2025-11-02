#include "ShaderCompiler.h"
#include "Logger.h"

ShaderCompiler::~ShaderCompiler()
{
	Term();
}

bool ShaderCompiler::Init()
{
	HRESULT hr = DxcCreateInstance(CLSID_DxcUtils, IID_PPV_ARGS(m_pUtils.GetAddressOf()));
	if (FAILED(hr))
	{
		ELOG("Error : DxcCreateInstance Failed.");
		return false;
	}

	hr = DxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(m_pCompiler.GetAddressOf()));
	if (FAILED(hr))
	{
		ELOG("Error : DxcCreateInstance Failed.");
		return false;
	}

	return true;
}

void ShaderCompiler::Term()
{
	m_pUtils.Reset();
	m_pCompiler.Reset();
}

bool ShaderCompiler::Compile(const wchar_t* filePath, std::vector<const wchar_t*>& args, ComPtr<IDxcBlob>& outBlob)
{
	ComPtr<IDxcBlobEncoding> pSourceBlob;
	HRESULT hr = m_pUtils->LoadFile(
		filePath,
		nullptr,
		pSourceBlob.GetAddressOf()
	);
	if (FAILED(hr))
	{
		ELOG("Error : IDxcUtils::LoadFile() Failed.");
		return false;
	}

	DxcBuffer sourceBuffer = {};
	sourceBuffer.Ptr = pSourceBlob->GetBufferPointer();
	sourceBuffer.Size = pSourceBlob->GetBufferSize();
	sourceBuffer.Encoding = DXC_CP_UTF8;
	
	ComPtr<IDxcResult> pResult;
	hr = m_pCompiler->Compile(
		&sourceBuffer,
		args.data(),
		static_cast<UINT32>(args.size()),
		nullptr,
		IID_PPV_ARGS(pResult.GetAddressOf())
	);
	if (FAILED(hr))
	{
		ELOG("Error : IDxcCompiler3::Compile() Failed.");
		return false;
	}

	ComPtr<IDxcBlobUtf8> pErrors;
	hr = pResult->GetOutput(
		DXC_OUT_ERRORS,
		IID_PPV_ARGS(pErrors.GetAddressOf()),
		nullptr
	);
	if (FAILED(hr))
	{
		ELOG("Error : IDxcResult::GetOutput() Failed.");
		return false;
	}

	return true;
}
