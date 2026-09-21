// Root Engine / Crash Handler

#include "Basement/Structures/Platform.h"
#include "Basement/HAL/PlatformDebug.h"

#include <windows.h>
#include <dbghelp.h>
#include <cstdio>

#pragma comment(lib, "dbghelp.lib")

inline uint32 PID;
inline uintptr_t RemoteContextAddr;
inline HANDLE hGameProcess;

void PrintRemoteCallstack(HANDLE hProcess, DWORD ThreadId, const void* RemoteExceptionPointersAddress)
{
    printf("\n=== Callstack ===\n");
	
    SymInitialize(hProcess, NULL, TRUE);
    SymSetOptions(SYMOPT_LOAD_LINES | SYMOPT_DEFERRED_LOADS);

    const HANDLE hThread = OpenThread(THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, ThreadId);
    if(!hThread)
    {
        printf("[Error] Failed to open target thread! Code: %lu\n", GetLastError());
        return;
    }
	
    EXCEPTION_POINTERS RemotePtrs;
    if(!ReadProcessMemory(hProcess, RemoteExceptionPointersAddress, &RemotePtrs, sizeof(RemotePtrs), nullptr))
    {
        printf("[Error] Failed to read remote exception pointers!\n");
        CloseHandle(hThread);
        return;
    }
	
    CONTEXT ThreadContext;
    if(!ReadProcessMemory(hProcess, RemotePtrs.ContextRecord, &ThreadContext, sizeof(ThreadContext), nullptr))
    {
        printf("[Error] Failed to read remote thread context!\n");
        CloseHandle(hThread);
        return;
    }
	
    STACKFRAME64 StackFrame = {};
    const DWORD MachineType = IMAGE_FILE_MACHINE_AMD64;
	
    StackFrame.AddrPC.Offset    = ThreadContext.Rip;
    StackFrame.AddrPC.Mode      = AddrModeFlat;
    StackFrame.AddrFrame.Offset = ThreadContext.Rbp;
    StackFrame.AddrFrame.Mode   = AddrModeFlat;
    StackFrame.AddrStack.Offset = ThreadContext.Rsp;
    StackFrame.AddrStack.Mode   = AddrModeFlat;
	
    char SymbolBuffer[sizeof(SYMBOL_INFO) + MAX_SYM_NAME * sizeof(char)];
    SYMBOL_INFO* pSymbol = (SYMBOL_INFO*)SymbolBuffer;
    pSymbol->MaxNameLen = MAX_SYM_NAME;
    pSymbol->SizeOfStruct = sizeof(SYMBOL_INFO);
	
    IMAGEHLP_LINE64 LineInfo = {};
    LineInfo.SizeOfStruct = sizeof(IMAGEHLP_LINE64);
	
    int32 FrameIndex = 0;
    while(StackWalk64(MachineType, hProcess, hThread, &StackFrame, &ThreadContext, NULL, SymFunctionTableAccess64, SymGetModuleBase64, NULL))
    {
        if(StackFrame.AddrPC.Offset == 0) break;
    	
    	DWORD Displacement = 0;
    	const bool bGotLine = SymGetLineFromAddr64(hProcess, StackFrame.AddrPC.Offset, &Displacement, &LineInfo);
    	
        printf("[%d] 0x%llX -> %s [%s:%lu]",
        	FrameIndex++,
        	StackFrame.AddrPC.Offset,
        	SymFromAddr(hProcess, StackFrame.AddrPC.Offset, 0, pSymbol) ? pSymbol->Name : "[UnknownFunction]",
        	bGotLine ? LineInfo.FileName : "?",
        	bGotLine ? LineInfo.LineNumber : 0
        );
        
        printf("\n");
    }
	
    CloseHandle(hThread);
    SymCleanup(hProcess);
}

void HandleCrash()
{
    printf("\n=== Crash Info ===\n");
	hGameProcess = OpenProcess(PROCESS_VM_READ | PROCESS_QUERY_INFORMATION | PROCESS_TERMINATE, FALSE, PID);
	if(!hGameProcess)
	{
		printf("[Error] Failed to open game process! Code: %lu\n\n", GetLastError());
		return;
	}
	
	FCrashHandlerContext LocalContext;
	SIZE_T BytesRead = 0;
	
	if(!ReadProcessMemory(hGameProcess, reinterpret_cast<LPCVOID>(RemoteContextAddr), &LocalContext, sizeof(LocalContext), &BytesRead))
	{
		printf("[Error] Failed to read context from game memory! Code: %lu\n\n", GetLastError());
		return;
	}
	
	printf("[Info] Crashed Thread ID: %u\n", LocalContext.ThreadId);
	printf("[Info] Exception Pointers Remote Address: 0x%p\n", LocalContext.ExceptionPointers);
	
	printf("\n=== Crash Message ===\n");
	if (LocalContext.CrashMessage != nullptr)
	{
		if(char CrashMessage[256]; ReadProcessMemory(hGameProcess, LocalContext.CrashMessage, CrashMessage, sizeof(CrashMessage) - 1, nullptr))
			printf("[Info] [Crash Message]: %s\n", CrashMessage);
		else
			printf("[Error] [Crash Message]: [Failed to read message from remote memory]\n");
	}
	else
		printf("[Info] [Crash Message]: No custom message provided\n");
	
	printf("\n=== Minidump ===\n");
	
	const HANDLE hDumpFile = CreateFileA("RootEngineCrashDump.dmp", GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	if(hDumpFile == INVALID_HANDLE_VALUE)
	{
		printf("[Error] [Minidump] Failed to create file dump! Code: %lu\n", GetLastError());
		return;
	}
	
	MINIDUMP_EXCEPTION_INFORMATION DumpInfo;
	DumpInfo.ThreadId = LocalContext.ThreadId;
	DumpInfo.ExceptionPointers = static_cast<EXCEPTION_POINTERS*>(LocalContext.ExceptionPointers);
	DumpInfo.ClientPointers = TRUE;
	
	printf("[Info] [Minidump] Writing minidump to 'RootEngineCrashDump.dmp'...\n");
	
	if(MiniDumpWriteDump(hGameProcess, PID, hDumpFile, MiniDumpNormal, &DumpInfo, NULL, NULL))
		printf("[Info] [Minidump] Minidump generated successfully!\n");
	else
		printf("[Error] [Minidump] Failed to write minidump! Code: %lu\n", GetLastError());
	
	PrintRemoteCallstack(hGameProcess, LocalContext.ThreadId, LocalContext.ExceptionPointers);
	
	CloseHandle(hDumpFile);
}

uintptr_t DecodeArgv(const char* InArg)
{
	uintptr_t Result = 0;
	for(int32 i = 0; i < 16; ++i) { Result <<= 4; Result |= InArg[i] - 'A'; }
	return Result;
}

#define EXAMPLE 1

int I = 2;

void Example()
{
	--I;
	I = 1 / I;
}

// RootEngineCrashHandler
int main(const int argc, char* argv[])
{
#if EXAMPLE
	FPlatformDebug::Initialize();
	
	FPlatformDebug::Printf("=== Root Engine Crash Handler Tester ===\n\n");
	
	FPlatformDebug::Printf("Press ENTER to simulate crash...\n");
	getchar();
	FPlatformDebug::Printf("Crash started!\n");
	
	// FPlatformDebug::Crash("Array index out of bounds!");
	Example();
	Example();
	Example();
#else
	printf("=== Root Engine Crash Handler ===\n\n");
	
	if(argc < 1 || argv[0] == nullptr)
	{
		printf("[Error] Not enough arguments passed!\n");
		Sleep(3000);
		return 1;
	}
	
	PID = 0; RemoteContextAddr = 0;
	
	for(int32 i =  0; i < 16; ++i) { PID               <<= 4; PID               |= argv[0][i] - 'A'; }
	for(int32 i = 16; i < 32; ++i) { RemoteContextAddr <<= 4; RemoteContextAddr |= argv[0][i] - 'A'; }
	
	printf("[Info] Target Game PID: %u\n", PID);
	printf("[Info] Remote Context Address: 0x%p\n", reinterpret_cast<void*>(RemoteContextAddr));
	
	HandleCrash();
	
	printf("\nPress ENTER to close\n");
	getchar();
	
	TerminateProcess(hGameProcess, 1);
	CloseHandle(hGameProcess);
#endif
	
	return 0;
}
