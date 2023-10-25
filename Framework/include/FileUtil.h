#pragma once

#include <string>
#include <Shlwapi.h>

#pragma comment(lib, "shlwapi.lib")

//-----------------------------------------------------------------------------
//! @brief      �t�@�C���p�X���������܂�.
//!
//! @param[in]      filePath        ��������t�@�C�X�p�X.
//! @param[out]     result          �������ʂ̊i�[��.
//! @retval true    �t�@�C���𔭌�.
//! @retval false   �t�@�C����������Ȃ�����.
//! @memo �������[���͈ȉ��̒ʂ�.
//!      .\
//!      ..\
//!      ..\..\
//!      .\res\
//!      %EXE_DIR%\
//!      %EXE_DIR%\..\
//!      %EXE_DIR%\..\..\
//!      %EXE_DIR%\res\
//-----------------------------------------------------------------------------
bool SearchFilePathW(const wchar_t* filename, std::wstring& result);

//-----------------------------------------------------------------------------
//! @brief      �t�@�C���p�X���������܂�.
//!
//! @param[in]      filePath        ��������t�@�C�X�p�X.
//! @param[out]     result          �������ʂ̊i�[��.
//! @retval true    �t�@�C���𔭌�.
//! @retval false   �t�@�C����������Ȃ�����.
//! @memo �������[���͈ȉ��̒ʂ�.
//!      .\
//!      ..\
//!      ..\..\
//!      .\res\
//!      %EXE_DIR%\
//!      %EXE_DIR%\..\
//!      %EXE_DIR%\..\..\
//!      %EXE_DIR%\res\
//-----------------------------------------------------------------------------
bool SearchFilePathA(const char* filename, std::string& result);

//-----------------------------------------------------------------------------
//! @brief      �f�B���N�g�������擾���܂�.
//!
//! @param[in]      filePath        �t�@�C���p�X.
//! @return     �w�肳�ꂽ�t�@�C���p�X����f�B���N�g�����𔲂��o���܂�.
//-----------------------------------------------------------------------------
std::string GetDirectoryPathA(const char* path);

//-----------------------------------------------------------------------------
//! @brief      �f�B���N�g�������擾���܂�.
//!
//! @param[in]      filePath        �t�@�C���p�X.
//! @return     �w�肳�ꂽ�t�@�C���p�X����f�B���N�g�����𔲂��o���܂�.
//-----------------------------------------------------------------------------
std::wstring GetDirectoryPathW(const wchar_t* path);

#if defined(UNICODE) || defined(_UNICODE)
inline bool SearchFilePath(const wchar_t* filename, std::wstring& result)
{
	return SearchFilePathW(filename, result);
}

inline std::wstring GetDirectoryPath(const wchar_t* path)
{
	return GetDirectoryPathW(path);
}
#else
inline bool SearchFilePath(const char* filename, std::string& result)
{
	return SearchFilePathA(filename, result);
}

inline std::string GetDirectoryPath(const char* path)
{
	return GetDirectoryPathA(path);
}
#endif
