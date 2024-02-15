#include "stdafx.h"

#ifdef _WIN64
#define __movsp __movsq
#else
#define __movsp __movsd
#endif

#ifdef _X86_

#pragma warning(disable: 4483) // Allow use of __identifier

#define __imp_VirtualAlloc __identifier("_imp__VirtualAlloc@16")

#endif

EXTERN_C_START 

extern IMAGE_DOS_HEADER __ImageBase;
extern PVOID __imp_VirtualAlloc;

EXTERN_C_END

void RemapSelfInternal(PVOID ImageBase, PVOID TempBase, ULONG SizeOfImage, PVOID VirtualAlloc)
{
  if (UnmapViewOfFile(ImageBase))
  {
    if (ImageBase == reinterpret_cast<PVOID (WINAPI * )( PVOID , SIZE_T , DWORD , DWORD  )>(VirtualAlloc)
      (ImageBase, SizeOfImage, MEM_COMMIT|MEM_RESERVE, PAGE_EXECUTE_READWRITE))
    {
      __movsp((ULONG_PTR*)ImageBase, (ULONG_PTR*)TempBase, SizeOfImage / sizeof(ULONG_PTR));
      return ;
    }

    __debugbreak();
  }
}

void RemapSelf()
{
  if (PIMAGE_NT_HEADERS pinth = RtlImageNtHeader(&__ImageBase))
  {
    ULONG SizeOfImage = pinth->OptionalHeader.SizeOfImage;

    if (PVOID TempBase = VirtualAlloc(0, SizeOfImage, MEM_COMMIT, PAGE_EXECUTE_READWRITE))
    {
      memcpy(TempBase, &__ImageBase, SizeOfImage);

      PVOID Cookie;

      if (0 <= LdrLockLoaderLock(0, 0, &Cookie))
      {
        reinterpret_cast<void (*) (PVOID , PVOID , ULONG , PVOID)>
          (RtlOffsetToPointer(TempBase, RtlPointerToOffset(&__ImageBase, RemapSelfInternal)))
          (&__ImageBase, TempBase, SizeOfImage, __imp_VirtualAlloc);

        LdrUnlockLoaderLock(0, Cookie);
      }

      VirtualFree(TempBase, 0, MEM_RELEASE);
    }
  }
}

void ShowErrorBox(HRESULT hr, PCWSTR pzCaption, UINT uType)
{
  WCHAR msg[0x100];

  ULONG dwFlags = FORMAT_MESSAGE_IGNORE_INSERTS|FORMAT_MESSAGE_FROM_SYSTEM;
  HMODULE hmod = 0;

  if ((hr & FACILITY_NT_BIT) || (0 > hr && HRESULT_FACILITY(hr) == FACILITY_NULL))
  {
    hr &= ~FACILITY_NT_BIT;
__nt:
    dwFlags = FORMAT_MESSAGE_FROM_HMODULE | FORMAT_MESSAGE_IGNORE_INSERTS;
    hmod = GetModuleHandle(L"ntdll");
  }
  
  if (FormatMessageW(dwFlags, hmod, hr, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), msg, _countof(msg), 0))
  {
    MessageBoxW(0, msg, pzCaption, uType);
  }
  else if (FORMAT_MESSAGE_FROM_SYSTEM & dwFlags)
  {
    goto __nt;
  }
}

HRESULT WINAPI DllRegisterServer()
{
  NTSTATUS status = STATUS_NO_MEMORY;

  OBJECT_ATTRIBUTES oa = { sizeof(oa), 0, 0, OBJ_CASE_INSENSITIVE };

  SIZE_T cb;
  if (oa.ObjectName = (PUNICODE_STRING)LocalAlloc(LMEM_FIXED, cb = 0x10000))
  {
    if (0 > (status = ZwQueryVirtualMemory(NtCurrentProcess(), &__ImageBase, 
      MemoryMappedFilenameInformation, oa.ObjectName, cb, &cb)))
    {
      ShowErrorBox(HRESULT_FROM_NT(status), L"MemoryMappedFilenameInformation", MB_ICONHAND);
    }
    else
    {
      LONG f = 0;
      
      static const PCWSTR sz[] = { L"#2 try delete", L"#1 try delete" };
      ULONG n = _countof(sz);

      do 
      {
        status = ZwDeleteFile(&oa);
        
        ShowErrorBox(status ? HRESULT_FROM_NT(status) : S_OK, sz[--n], status ? MB_ICONWARNING : MB_ICONINFORMATION);
        
        if (!_bittestandset(&f, 0))
        {
          RemapSelf();
        }

      } while (n);
    }

    LocalFree(oa.ObjectName);
  }

  return RtlNtStatusToDosError(status);
}