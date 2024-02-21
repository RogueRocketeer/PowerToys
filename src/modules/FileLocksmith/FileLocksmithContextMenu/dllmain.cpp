// dllmain.cpp : Defines the entry point for the DLL application.
#include "pch.h"

#include <common/utils/process_path.h>
#include <common/utils/resources.h>
#include <common/utils/elevation.h>

#include "FileLocksmithLib/Settings.h"
#include "FileLocksmithLib/Trace.h"

#include <atlstr.h>
#include <atlfile.h>
#include <Shlwapi.h>
#include <shobjidl_core.h>
#include <string>
#include <thread>
#include <wrl/module.h>

#include "Generated Files/resource.h"

#define BUFSIZE 4096 * 4

using namespace Microsoft::WRL;

HINSTANCE g_hInst = 0;

BOOL APIENTRY DllMain( HMODULE hModule,
                       DWORD  ul_reason_for_call,
                       LPVOID lpReserved
                     )
{
    switch (ul_reason_for_call)
    {
    case DLL_PROCESS_ATTACH:
        g_hInst = hModule;
        Trace::RegisterProvider();
        break;
    case DLL_PROCESS_DETACH:
        Trace::UnregisterProvider();
        break;
    }
    return TRUE;
}

class __declspec(uuid("AAF1E27D-4976-49C2-8895-AAFA743C0A7E")) FileLocksmithContextMenuCommand final : public RuntimeClass<RuntimeClassFlags<ClassicCom>, IExplorerCommand, IObjectWithSite>
{
public:
    virtual const wchar_t* Title() { return L"File Locksmith"; }
    virtual const EXPCMDFLAGS Flags() { return ECF_DEFAULT; }
    virtual const EXPCMDSTATE State(_In_opt_ IShellItemArray* selection) { return ECS_ENABLED; }

    // IExplorerCommand
    IFACEMETHODIMP GetTitle(_In_opt_ IShellItemArray* items, _Outptr_result_nullonfailure_ PWSTR* name)
    {
        return SHStrDup(context_menu_caption.c_str(), name);
    }

    IFACEMETHODIMP GetIcon(_In_opt_ IShellItemArray*, _Outptr_result_nullonfailure_ PWSTR* icon)
    {
        std::wstring iconResourcePath = get_module_folderpath(g_hInst);
        iconResourcePath += L"\\Assets\\FileLocksmith\\";
        iconResourcePath += L"FileLocksmith.ico";
        return SHStrDup(iconResourcePath.c_str(), icon);
    }

    IFACEMETHODIMP GetToolTip(_In_opt_ IShellItemArray*, _Outptr_result_nullonfailure_ PWSTR* infoTip)
    {
        *infoTip = nullptr;
        return E_NOTIMPL;
    }

    IFACEMETHODIMP GetCanonicalName(_Out_ GUID* guidCommandName)
    {
        *guidCommandName = __uuidof(this);
        return S_OK;
    }

    IFACEMETHODIMP GetState(_In_opt_ IShellItemArray* selection, _In_ BOOL okToBeSlow, _Out_ EXPCMDSTATE* cmdState)
    {
        *cmdState = ECS_ENABLED;

        if (!FileLocksmithSettingsInstance().GetEnabled())
        {
            *cmdState = ECS_HIDDEN;
        }

        if (FileLocksmithSettingsInstance().GetShowInExtendedContextMenu())
        {
            *cmdState = ECS_HIDDEN;
        }

        return S_OK;
    }

    IFACEMETHODIMP Invoke(_In_opt_ IShellItemArray* selection, _In_opt_ IBindCtx*) noexcept
    {
        Trace::Invoked();

        if (selection == nullptr)
        {
            return S_OK;
        }

        std::wstring pipe_name(L"\\\\.\\pipe\\powertoys_filelocksmithinput_");
        UUID temp_uuid;
        wchar_t* uuid_chars = nullptr;
        if (UuidCreate(&temp_uuid) == RPC_S_UUID_NO_ADDRESS)
        {
            auto val = get_last_error_message(GetLastError());
            Logger::warn(L"UuidCreate can not create guid. {}", val.has_value() ? val.value() : L"");
        }
        else if (UuidToString(&temp_uuid, reinterpret_cast<RPC_WSTR*>(&uuid_chars)) != RPC_S_OK)
        {
            auto val = get_last_error_message(GetLastError());
            Logger::warn(L"UuidToString can not convert to string. {}", val.has_value() ? val.value() : L"");
        }

        if (uuid_chars != nullptr)
        {
            pipe_name += std::wstring(uuid_chars);
            RpcStringFree(reinterpret_cast<RPC_WSTR*>(&uuid_chars));
            uuid_chars = nullptr;
        }
        create_pipe_thread = std::thread(&FileLocksmithContextMenuCommand::StartNamedPipeServerAndSendData, this, pipe_name);


        std::wstring path = get_module_folderpath(g_hInst);
        path = path + L"\\PowerToys.FileLocksmithUI.exe";

        HRESULT result;

        if (!RunNonElevatedEx(path.c_str(), L"", get_module_folderpath(g_hInst)))
        {
            result = E_FAIL;
            Trace::InvokedRet(result);
            return result;
        }

        if (hPipe != INVALID_HANDLE_VALUE)
        {
            CAtlFile writePipe(hPipe);

            DWORD fileCount = 0;
            // Gets the list of files currently selected using the IShellItemArray
            selection->GetCount(&fileCount);
            // Iterate over the list of files
            for (DWORD i = 0; i < fileCount; i++)
            {
                IShellItem* shellItem;
                selection->GetItemAt(i, &shellItem);
                LPWSTR itemName;
                // Retrieves the entire file system path of the file from its shell item
                shellItem->GetDisplayName(SIGDN_FILESYSPATH, &itemName);
                CString fileName(itemName);
                // File name can't contain '?'
                fileName.Append(_T("?"));
                // Write the file path into the input stream for image resizer
                writePipe.Write(fileName, fileName.GetLength() * sizeof(TCHAR));
            }
            writePipe.Close();
        }

        Trace::InvokedRet(S_OK);
        return S_OK;
    }

    IFACEMETHODIMP GetFlags(_Out_ EXPCMDFLAGS* flags)
    {
        *flags = Flags();
        return S_OK;
    }
    IFACEMETHODIMP EnumSubCommands(_COM_Outptr_ IEnumExplorerCommand** enumCommands)
    {
        *enumCommands = nullptr;
        return E_NOTIMPL;
    }

    // IObjectWithSite
    IFACEMETHODIMP SetSite(_In_ IUnknown* site) noexcept
    {
        m_site = site;
        return S_OK;
    }
    IFACEMETHODIMP GetSite(_In_ REFIID riid, _COM_Outptr_ void** site) noexcept { return m_site.CopyTo(riid, site); }

protected:
    ComPtr<IUnknown> m_site;

private:
    HRESULT StartNamedPipeServerAndSendData(std::wstring pipe_name)
    {
        hPipe = CreateNamedPipe(
            pipe_name.c_str(),
            PIPE_ACCESS_DUPLEX |
                WRITE_DAC,
            PIPE_TYPE_MESSAGE |
                PIPE_READMODE_MESSAGE |
                PIPE_WAIT,
            PIPE_UNLIMITED_INSTANCES,
            BUFSIZE,
            BUFSIZE,
            0,
            NULL);

        if (hPipe == NULL || hPipe == INVALID_HANDLE_VALUE)
        {
            return E_FAIL;
        }

        // This call blocks until a client process connects to the pipe
        BOOL connected = ConnectNamedPipe(hPipe, NULL);
        if (!connected)
        {
            if (GetLastError() == ERROR_PIPE_CONNECTED)
            {
                return S_OK;
            }
            else
            {
                CloseHandle(hPipe);
            }
            return E_FAIL;
        }

        return S_OK;
    }

    std::wstring context_menu_caption = GET_RESOURCE_STRING_FALLBACK(IDS_FILE_LOCKSMITH_CONTEXT_MENU_ENTRY, L"Unlock with File Locksmith");
    std::thread create_pipe_thread;
    HANDLE hPipe = INVALID_HANDLE_VALUE;
};

CoCreatableClass(FileLocksmithContextMenuCommand)
CoCreatableClassWrlCreatorMapInclude(FileLocksmithContextMenuCommand)


STDAPI DllGetActivationFactory(_In_ HSTRING activatableClassId, _COM_Outptr_ IActivationFactory** factory)
{
    return Module<ModuleType::InProc>::GetModule().GetActivationFactory(activatableClassId, factory);
}

STDAPI DllCanUnloadNow()
{
    return Module<InProc>::GetModule().GetObjectCount() == 0 ? S_OK : S_FALSE;
}

STDAPI DllGetClassObject(_In_ REFCLSID rclsid, _In_ REFIID riid, _COM_Outptr_ void** instance)
{
    return Module<InProc>::GetModule().GetClassObject(rclsid, riid, instance);
}