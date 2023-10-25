#include "Logger.h"
#include <Windows.h>
#include <cstdarg>
#include <cstdio>

void OutputLog(const char* format, ...)
{
	char msg[2048];
	memset(msg, '\0', sizeof(msg));
	va_list arg;

	va_start(arg, format);
	vsprintf_s(msg, format, arg);
	va_end(arg);

	// コンソールに出力
	printf_s("%s", msg);

	// VisualStudioの出力ウィンドウに出力
	OutputDebugStringA(msg);
}
