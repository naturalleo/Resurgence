#include <misc/native.hpp>
#include <misc/safe_handle.hpp>
#include <misc/exceptions.hpp>
#include <system/process.hpp>

#include <algorithm>
#include <Shlwapi.h>

#pragma comment(lib, "Shlwapi.lib")

namespace resurgence
{
    namespace native
    {
        ///</summary>
        /// Gets the message associated with a status value.
        ///</summary>
        ///<param name="status"> The status code. </param>
        ///<returns> 
        /// A string containing the message.
        ///</returns>
        std::wstring get_status_message(NTSTATUS status)
        {
            HMODULE ntdll = GetModuleHandle(L"ntdll.dll");

            wchar_t buffer[MAX_PATH] = {0};

            FormatMessageW(
                FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_FROM_HMODULE,
                ntdll, status,
                MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                buffer, 260, nullptr);

            return std::wstring(buffer);
        }

        ///</summary>
        /// Gets the required size needed for a NtQuerySystemInformation call.
        ///</summary>
        ///<param name="status"> The information class. </param>
        ///<returns> 
        /// The required buffer size.
        ///</returns>
        size_t query_required_size(SYSTEM_INFORMATION_CLASS information)
        {
            ULONG cb;

            NTSTATUS status = NtQuerySystemInformation(information, nullptr, 0, (PULONG)&cb);
            if(status != STATUS_INFO_LENGTH_MISMATCH)
                return 0;

            return cb;
        }

        ///<summary>
        /// Gets the required size needed for a NtQueryInformationProcess call.
        ///</summary>
        ///<param name="status"> The information class. </param>
        ///<returns> 
        /// The required buffer size.
        ///</returns>
        size_t query_required_size(PROCESSINFOCLASS information)
        {
            switch(information) {
                case ProcessBasicInformation:
                    return sizeof(PROCESS_BASIC_INFORMATION);
                case ProcessQuotaLimits:
                    return sizeof(QUOTA_LIMITS_EX);
                case ProcessIoCounters:
                    return sizeof(IO_COUNTERS);
                case ProcessVmCounters:
                    return sizeof(VM_COUNTERS);
                case ProcessTimes:
                    return sizeof(KERNEL_USER_TIMES);
                case ProcessPriorityClass:
                    return sizeof(PROCESS_PRIORITY_CLASS);
                case ProcessHandleCount:
                    return sizeof(ULONG);
                case ProcessSessionInformation:
                    return sizeof(PROCESS_SESSION_INFORMATION);
                case ProcessWow64Information:
                    return sizeof(ULONG_PTR);
                case ProcessImageFileName:
                    return sizeof(UNICODE_STRING) + MAX_PATH * sizeof(wchar_t);
                case ProcessImageFileNameWin32:
                    return sizeof(UNICODE_STRING) + MAX_PATH * sizeof(wchar_t);
                case ProcessExecuteFlags:
                    return sizeof(ULONG);
                case ProcessImageInformation:
                    return sizeof(SECTION_IMAGE_INFORMATION);
                default:
                    throw;
            }
        }

        ///<summary>
        /// Gets the required size needed for a NtQueryObject call.
        ///</summary>
        ///<param name="status"> The information class. </param>
        ///<returns> 
        /// The required buffer size.
        ///</returns>
        size_t query_required_size(OBJECT_INFORMATION_CLASS information)
        {
            switch(information) {
                case ObjectBasicInformation:
                    return sizeof(OBJECT_BASIC_INFORMATION);
                case ObjectNameInformation:
                    return PAGE_SIZE;       // Can be lower
                case ObjectTypeInformation:
                    return PAGE_SIZE;       // Can be lower
                default:
                    throw;
            }
        }

        ///<summary>
        /// Query system information.
        ///</summary>
        ///<param name="information"> The information class. </param>
        ///<returns> 
        /// A buffer with the requested information or nullptr on failure.
        ///</returns>
        ///<remarks>
        /// The returned buffer, if not null, must be freed with free_local_buffer.
        ///</remarks>
        uint8_t* query_system_information(SYSTEM_INFORMATION_CLASS information)
        {
            uint8_t*        buffer = nullptr;
            NTSTATUS        status = STATUS_SUCCESS;
            size_t          cb = query_required_size(information);

            allocate_local_buffer(&buffer, cb);

            if(!buffer) return nullptr;

            do {
                status = NtQuerySystemInformation(information, buffer, (ULONG)cb, (PULONG)&cb);
                if(NT_SUCCESS(status)) {
                    return buffer;
                } else {
                    if(status == STATUS_INFO_LENGTH_MISMATCH) {
                        if(buffer != nullptr)
                            free_local_buffer(buffer);
                        allocate_local_buffer(&buffer, cb);
                        continue;
                    }
                    return nullptr;
                }
            } while(true);
        }

        ///<summary>
        /// Query process information.
        ///</summary>
        ///<param name="information"> The information class. </param>
        ///<returns> 
        /// A buffer with the requested information or nullptr on failure.
        ///</returns>
        ///<remarks>
        /// The returned buffer, if not null, must be freed with free_local_buffer.
        ///</remarks>
        uint8_t* query_process_information(HANDLE handle, PROCESSINFOCLASS information)
        {
            uint8_t*    buffer = nullptr;
            NTSTATUS    status = STATUS_SUCCESS;
            size_t      cb = query_required_size(information);

            allocate_local_buffer(&buffer, cb);

            if(!buffer) return nullptr;

            status = NtQueryInformationProcess(handle, information, buffer, (ULONG)cb, (PULONG)&cb);
            if(NT_SUCCESS(status)) {
                return buffer;
            } else {
                if(buffer != nullptr)
                    free_local_buffer(buffer);
                return nullptr;
            }
        }

        ///<summary>
        /// Query object information.
        ///</summary>
        ///<param name="information"> The information class. </param>
        ///<returns> 
        /// A buffer with the requested information or nullptr on failure.
        ///</returns>
        ///<remarks>
        /// The returned buffer, if not null, must be freed with free_local_buffer.
        ///</remarks>
        uint8_t* query_object_information(HANDLE handle, OBJECT_INFORMATION_CLASS information)
        {
            uint8_t*    buffer = nullptr;
            NTSTATUS    status = STATUS_SUCCESS;
            size_t      cb = query_required_size(information);

            allocate_local_buffer(&buffer, cb);

            if(!buffer) return nullptr;

            status = NtQueryObject(handle, information, buffer, (ULONG)cb, nullptr);
            if(NT_SUCCESS(status)) {
                return buffer;
            } else {
                return nullptr;
            }
        }

        ///<summary>
        /// Enumerates system modules (drivers).
        ///</summary>
        ///<param name="callback"> 
        /// The callback that will be called for each module. 
        /// Enumeration stops if the callback returns STATUS_SUCCESS. 
        ///</param>
        ///<returns> 
        /// The status code.
        ///</returns>
        NTSTATUS enumerate_system_modules(system_module_enumeration_callback callback)
        {
            if(!callback) return STATUS_INVALID_PARAMETER_1;

            uint8_t*    buffer = nullptr;
            NTSTATUS    status = STATUS_SUCCESS;

            buffer = query_system_information(SystemModuleInformation);
            if(buffer) {
                auto pSysModules = (PRTL_PROCESS_MODULES)buffer;
                for(ULONG i = 0; i < pSysModules->NumberOfModules; i++) {
                    status = callback(&pSysModules->Modules[i]);
                    if(NT_SUCCESS(status))
                        break;
                }
                free_local_buffer(buffer);
            }
            return status;
        }

        ///<summary>
        /// Enumerates system objects.
        ///</summary>
        ///<param name="root"> 
        /// The root folder for the enumeration.
        ///</param>
        ///<param name="callback"> 
        /// The callback that will be called for each objects. 
        /// Enumeration stops if the callback returns STATUS_SUCCESS. 
        ///</param>
        ///<returns> 
        /// The status code.
        ///</returns>
        NTSTATUS enumerate_system_objects(const std::wstring& root, object_enumeration_callback callback)
        {
            if(root.empty()) return STATUS_INVALID_PARAMETER_1;
            if(!callback)    return STATUS_INVALID_PARAMETER_2;

            OBJECT_ATTRIBUTES   objAttr;
            UNICODE_STRING      usDirectoryName;
            NTSTATUS            status;
            HANDLE              hDirectory;
            ULONG               uEnumCtx = 0;
            size_t              uBufferSize = 0x100;
            POBJECT_DIRECTORY_INFORMATION   pObjBuffer = nullptr;

            RtlSecureZeroMemory(&usDirectoryName, sizeof(usDirectoryName));
            RtlInitUnicodeString(&usDirectoryName, std::data(root));
            InitializeObjectAttributes(&objAttr, &usDirectoryName, OBJ_CASE_INSENSITIVE, nullptr, nullptr);

            status = NtOpenDirectoryObject(&hDirectory, DIRECTORY_QUERY, &objAttr);

            if(!NT_SUCCESS(status) || !hDirectory) {
                return status;
            }

            pObjBuffer = (POBJECT_DIRECTORY_INFORMATION)RtlAllocateHeap(NtCurrentPeb()->ProcessHeap, 0, uBufferSize);

            do {
                status = NtQueryDirectoryObject(hDirectory, pObjBuffer, (ULONG)uBufferSize, TRUE, FALSE, &uEnumCtx, (PULONG)&uBufferSize);
                if(!NT_SUCCESS(status)) {
                    if(status == STATUS_BUFFER_TOO_SMALL || status == STATUS_INFO_LENGTH_MISMATCH) {
                        if(pObjBuffer != nullptr)
                            RtlFreeHeap(NtCurrentPeb()->ProcessHeap, 0, pObjBuffer);
                        uBufferSize = uBufferSize * 2;
                        pObjBuffer = (POBJECT_DIRECTORY_INFORMATION)RtlAllocateHeap(NtCurrentPeb()->ProcessHeap, 0, uBufferSize);
                        continue;
                    }
                    break;
                }

                if(!pObjBuffer) break;

                status = callback(pObjBuffer);
                if(NT_SUCCESS(status))
                    break;
            } while(true);

            if(pObjBuffer != nullptr)
                RtlFreeHeap(NtCurrentPeb()->ProcessHeap, 0, pObjBuffer);

            NtClose(hDirectory);
            return status;
        }

        ///<summary>
        /// Enumerates running processes.
        ///</summary>
        ///<param name="callback"> 
        /// The callback that will be called for each process. 
        /// Enumeration stops if the callback returns STATUS_SUCCESS. 
        ///</param>
        ///<returns> 
        /// The status code.
        ///</returns>
        NTSTATUS enumerate_processes(process_enumeration_callback callback)
        {
            if(!callback) return STATUS_INVALID_PARAMETER_1;

            uint8_t*   buffer = nullptr;
            NTSTATUS   status = STATUS_SUCCESS;

            buffer = query_system_information(SystemExtendedProcessInformation);
            if(buffer) {
                auto pProcessEntry = (PSYSTEM_PROCESS_INFORMATION)buffer;
                while(pProcessEntry->NextEntryDelta) {
                    status = callback((PSYSTEM_PROCESS_INFORMATION)pProcessEntry);
                    if(NT_SUCCESS(status))
                        break;
                    pProcessEntry = (PSYSTEM_PROCESS_INFORMATION)((PUCHAR)pProcessEntry + pProcessEntry->NextEntryDelta);
                }
                free_local_buffer(buffer);
            }
            return status;
        }

        ///<summary>
        /// Enumerates process' threads.
        ///</summary>
        ///<param name="process"> 
        /// Handle to the process. Must have read and query information access. 
        ///</param>
        ///<param name="callback"> 
        /// The callback that will be called for each thread. 
        /// Enumeration stops if the callback returns STATUS_SUCCESS. 
        ///</param>
        ///<returns> 
        /// The status code.
        ///</returns>
        NTSTATUS enumerate_process_threads(uint32_t pid, thread_enumeration_callback callback)
        {
            if(!callback) return STATUS_INVALID_PARAMETER_2;
            
            return enumerate_processes([=](PSYSTEM_PROCESS_INFORMATION entry) {
                if(pid == reinterpret_cast<uint32_t>(entry->UniqueProcessId)) {
                    for(auto i = 0ul; i < entry->ThreadCount; i++) {
                        auto status = callback((PSYSTEM_EXTENDED_THREAD_INFORMATION)&entry->Threads[i]);
                        if(NT_SUCCESS(status))
                            break;
                    }
                    return STATUS_SUCCESS;
                }
                return STATUS_NOT_FOUND;
            });
        }

        ///<summary>
        /// Enumerates process' x64 modules.
        ///</summary>
        ///<param name="process"> 
        /// Handle to the process. Must have read and query information access. 
        ///</param>
        ///<param name="callback"> 
        /// The callback that will be called for each module. 
        /// Enumeration stops if the callback returns STATUS_SUCCESS. 
        ///</param>
        ///<returns> 
        /// The status code.
        ///</returns>
        NTSTATUS enumerate_process_modules(HANDLE process, module_enumeration_callback callback)
        {
            PPEB_LDR_DATA           ldr;
            PEB_LDR_DATA            ldrData;
            PLIST_ENTRY             startLink;
            PLIST_ENTRY             currentLink;
            LDR_DATA_TABLE_ENTRY    currentEntry;

            auto basic_info = (PPROCESS_BASIC_INFORMATION)query_process_information(process, ProcessBasicInformation);

            if(!basic_info)
                return STATUS_UNSUCCESSFUL;

            //
            // PEB will be invalid when trying to access a x64 process from WOW64
            // 
            if(basic_info->PebBaseAddress == 0) {
                free_local_buffer(basic_info);
                return STATUS_ACCESS_DENIED;
            }

            NTSTATUS status = read_memory(
                process,
                PTR_ADD(basic_info->PebBaseAddress, FIELD_OFFSET(PEB, Ldr)),
                &ldr,
                sizeof(ldr)
            );

            if(!NT_SUCCESS(status)) {
                free_local_buffer(basic_info);
                return status;
            }

            status = read_memory(process, ldr, &ldrData, sizeof(ldrData));
            if(!NT_SUCCESS(status)) {
                free_local_buffer(basic_info);
                return status;
            }

            if(!ldrData.Initialized) return STATUS_UNSUCCESSFUL;

            startLink = (PLIST_ENTRY)PTR_ADD(ldr, FIELD_OFFSET(PEB_LDR_DATA, InLoadOrderModuleList));
            currentLink = ldrData.InLoadOrderModuleList.Flink;

            while(currentLink != startLink) {
                PVOID addressOfEntry = CONTAINING_RECORD(currentLink, LDR_DATA_TABLE_ENTRY, InLoadOrderLinks);

                status = read_memory(
                    process,
                    addressOfEntry,
                    &currentEntry,
                    sizeof(LDR_DATA_TABLE_ENTRY)
                );

                if(!NT_SUCCESS(status)) {
                    free_local_buffer(basic_info);
                    return status;
                }

                if(currentEntry.DllBase != 0) {
                    status = callback(&currentEntry);

                    if(NT_SUCCESS(status)) {
                        free_local_buffer(basic_info);
                        return status;
                    }
                }
                currentLink = currentEntry.InLoadOrderLinks.Flink;
            }
            free_local_buffer(basic_info);
            return STATUS_SUCCESS;
        }

        ///<summary>
        /// Enumerates process' x86 modules.
        ///</summary>
        ///<param name="process"> 
        /// Handle to the process. Must have read and query information access. 
        ///</param>
        ///<param name="callback"> 
        /// The callback that will be called for each module. 
        /// Enumeration stops if the callback returns STATUS_SUCCESS. 
        ///</param>
        ///<returns> 
        /// The status code.
        ///</returns>
        NTSTATUS enumerate_process_modules32(HANDLE process, module_enumeration_callback32 callback)
        {
            auto basic_info = (PULONG_PTR)query_process_information(process, ProcessWow64Information);

            ULONG                   ldr;
            PEB_LDR_DATA32          ldrData;
            ULONG                   startLink;
            ULONG                   currentLink;
            LDR_DATA_TABLE_ENTRY32  currentEntry;
            ULONG_PTR               wow64Peb = *basic_info;

            NTSTATUS status = read_memory(
                process,
                PTR_ADD(wow64Peb, FIELD_OFFSET(PEB32, Ldr)),
                &ldr,
                sizeof(ldr)
            );

            if(!NT_SUCCESS(status)) {
                free_local_buffer(basic_info);
                return status;
            }   

            status = read_memory(process, (PVOID)ldr, &ldrData, sizeof(ldrData));
            if(!NT_SUCCESS(status)) {
                free_local_buffer(basic_info);
                return status;
            }

            if(!ldrData.Initialized) return STATUS_UNSUCCESSFUL;

            startLink = (ULONG)PTR_ADD((ULONG_PTR)ldr, FIELD_OFFSET(PEB_LDR_DATA32, InLoadOrderModuleList));
            currentLink = (ULONG)ldrData.InLoadOrderModuleList.Flink;

            while(currentLink != startLink) {
                PVOID addressOfEntry = CONTAINING_RECORD((ULONG_PTR)currentLink, LDR_DATA_TABLE_ENTRY32, InLoadOrderLinks);

                status = read_memory(
                    process,
                    addressOfEntry,
                    &currentEntry,
                    sizeof(LDR_DATA_TABLE_ENTRY32)
                );

                if(!NT_SUCCESS(status)) {
                    free_local_buffer(basic_info);
                    return status;
                }

                if(currentEntry.DllBase != 0) {
                    status = callback(&currentEntry);

                    if(NT_SUCCESS(status)) {
                        free_local_buffer(basic_info);
                        return status;
                    }
                }

                currentLink = (ULONG)currentEntry.InLoadOrderLinks.Flink;
            }
            free_local_buffer(basic_info);
            return STATUS_SUCCESS;
        }

        ///<summary>
        /// Checks if a object exists on the system.
        ///</summary>
        ///<param name="root">   The folder to check. </param>
        ///<param name="object"> The object name. </param>
        ///<param name="found">  Pointer to a variable that will hold the result. </param>
        ///<returns> 
        /// The status code.
        ///</returns>
        NTSTATUS object_exists(const std::wstring& root, const std::wstring& object, bool* found)
        {
            UNICODE_STRING  uName;

            RtlInitUnicodeString(&uName, std::data(object));

            NTSTATUS status = enumerate_system_objects(root, [&](POBJECT_DIRECTORY_INFORMATION entry) -> NTSTATUS {
                if(RtlEqualUnicodeString(&uName, &entry->Name, TRUE)) {
                    *found = true;
                    return STATUS_SUCCESS;
                }
                return STATUS_NOT_FOUND;
            });
            return status;
        }

        ///<summary>
        /// Queries information about a system module.
        ///</summary>
        ///<param name="module"> The module name. </param>
        ///<param name="info">   The returned information. </param>
        ///<returns> 
        /// The status code.
        ///</returns>
        NTSTATUS get_system_module_info(const std::string& module, PRTL_PROCESS_MODULE_INFORMATION moduleInfo)
        {
            if(!moduleInfo) return STATUS_INVALID_PARAMETER;

            NTSTATUS status = enumerate_system_modules([&](PRTL_PROCESS_MODULE_INFORMATION info) -> NTSTATUS {
                if(!strcmp(std::data(module), (char*)(info->FullPathName + info->OffsetToFileName))) {
                    RtlCopyMemory(moduleInfo, info, sizeof(RTL_PROCESS_MODULE_INFORMATION));
                    return STATUS_SUCCESS;
                }
                return STATUS_NOT_FOUND;
            });
            return status;
        }

        ///<summary>
        /// Opens an existing file.
        ///</summary>
        ///<param name="path">        The file path. </param>
        ///<param name="accessFlags"> The desired access. </param>
        ///<param name="handle">      The returned handle. </param>
        ///<returns> 
        /// The status code.
        ///</returns>
        NTSTATUS open_file(const std::wstring& path, uint32_t accessFlags, PHANDLE handle)
        {
            NTSTATUS            status;
            OBJECT_ATTRIBUTES   objAttr;
            UNICODE_STRING      usFilePath;
            IO_STATUS_BLOCK     ioStatus;

            if(!RtlDosPathNameToNtPathName_U(
                std::data(path),
                &usFilePath,
                NULL,
                NULL))
                return STATUS_OBJECT_NAME_NOT_FOUND;

            InitializeObjectAttributes(&objAttr, &usFilePath, OBJ_CASE_INSENSITIVE, NULL, NULL);

            status = NtCreateFile(
                handle,
                accessFlags,
                &objAttr, 
                &ioStatus, 
                0, 
                FILE_ATTRIBUTE_NORMAL, 
                FILE_SHARE_READ, 
                FILE_OPEN,
                FILE_SYNCHRONOUS_IO_NONALERT, 
                nullptr, 
                0);

            return status;
        }

        ///<summary>
        /// Writes to a file.
        ///</summary>
        ///<param name="path">   The file path. </param>
        ///<param name="buffer"> The buffer to write. </param>
        ///<param name="length"> The size of the buffer. </param>
        ///<returns> 
        /// The status code.
        ///</returns>
        NTSTATUS write_file(const std::wstring& path, uint8_t* buffer, size_t length)
        {
            HANDLE          handle;
            NTSTATUS        status;
            IO_STATUS_BLOCK ioStatus;

            status = open_file(std::data(path), GENERIC_WRITE, &handle);

            if(NT_SUCCESS(status)) {
                status = NtWriteFile(handle, NULL, NULL, NULL, &ioStatus, buffer, static_cast<uint32_t>(length), NULL, NULL);

                NtClose(handle);
            }
            return status;
        }

        NTSTATUS get_file_size(HANDLE handle, size_t* size, LARGE_INTEGER* li)
        {
            NTSTATUS                    status;
            FILE_STANDARD_INFORMATION   standardInfo;
            IO_STATUS_BLOCK             isb;

            status = NtQueryInformationFile(
                handle,
                &isb,
                &standardInfo,
                sizeof(FILE_STANDARD_INFORMATION),
                FileStandardInformation
            );

            if(!NT_SUCCESS(status))
                return status;

            if(size)
                *size = static_cast<size_t>(standardInfo.EndOfFile.QuadPart);
            if(li)
                *li = standardInfo.EndOfFile;

            return status;
        }

        ///<summary>
        /// Copy a file.
        ///</summary>
        ///<param name="oldPath"> The old path. </param>
        ///<param name="newPath"> The new path. </param>
        ///<returns> 
        /// The status code.
        ///</returns>
        NTSTATUS copy_file(const std::wstring& oldPath, const std::wstring& newPath)
        {
            if(!::CopyFileW(std::data(oldPath), std::data(newPath), FALSE))
                return get_last_ntstatus();
            return STATUS_SUCCESS;
        }

        ///<summary>
        /// Translates a relative path to full path.
        ///</summary>
        ///<param name="path"> The path. </param>
        ///<returns> 
        /// The full path.
        ///</returns>
        std::wstring get_full_path(const std::wstring& path)
        {
            wchar_t fullpath[MAX_PATH];

            if(!GetFullPathNameW(std::data(path), MAX_PATH, const_cast<wchar_t*>(std::data(fullpath)), nullptr))
                return L"";

            return fullpath;
        }

        ///<summary>
        /// Translates a NT path to DOS path.
        ///</summary>
        ///<param name="path"> The NT path (i.e "\SystemRoot\system32\ntoskrnl.exe"). </param>
        ///<returns> 
        /// The DOS path (i.e "C:\Windows\system32\ntoskrnl.exe").
        ///</returns>
        std::wstring get_dos_path(const std::wstring& path)
        {
            auto startsWith = [](const std::wstring& str1, const std::wstring& str2, bool ignoreCasing) {
                if(ignoreCasing) {
                    std::wstring copy1 = str1, copy2 = str2;
                    std::transform(copy1.begin(), copy1.end(), copy1.begin(), ::tolower);
                    std::transform(copy2.begin(), copy2.end(), copy2.begin(), ::tolower);
                    return copy1.compare(0, copy2.size(), copy2) == 0;
                } else {
                    return str1.compare(0, str2.size(), str2) == 0;
                }
            };

            std::wstring dosPath = path;

            auto idx = dosPath.find(L"\\??\\");
            if(startsWith(dosPath, L"\\??\\", false)) {
                return dosPath.substr(idx + 4);
            } else if(startsWith(dosPath, L"\\SystemRoot", true)) {
                return std::wstring(USER_SHARED_DATA->NtSystemRoot) + dosPath.substr(11);
            } else if(startsWith(dosPath, L"system32\\", true)) {
                return std::wstring(USER_SHARED_DATA->NtSystemRoot) + L"\\system32" + dosPath.substr(8);
            } else if(startsWith(dosPath, L"\\Device", true)) {
                std::vector<std::wstring> drives;
                query_mounted_drives(drives);
                for(auto& drive : drives) {
                    std::wstring sym;
                    get_symbolic_link_from_drive(drive, sym);
                    if(startsWith(dosPath, sym, false)) {
                        dosPath.replace(0, sym.size(), drive);
                        break;
                    }
                }
            }
            return dosPath;
        }

        ///<summary>
        /// Query mounted devices (i.e C:\, D:\ etc).
        ///</summary>
        ///<param name="letters"> A vector containing the mounted driver letters. </param>
        ///<returns> 
        /// The status code.
        ///</returns>
        NTSTATUS query_mounted_drives(std::vector<std::wstring>& letters)
        {
            // Required size:
            // 26 letters * 2 * sizeof(WCHAR) = 104
            // C:\

            wchar_t     buffer[MAX_PATH] = {0};
            uint32_t    length;

            letters.reserve(MAX_PATH);

            if(!!(length = GetLogicalDriveStrings(MAX_PATH, buffer))) {
                for(wchar_t* current = buffer; current < &buffer[length]; ) {
                    letters.push_back(std::wstring(current, 2));
                    current += 4;
                }
                return STATUS_SUCCESS;
            } else {
                return get_last_ntstatus();
            }
        }

        ///<summary>
        /// Gets the symbolic device link from a driver letter.
        ///</summary>
        ///<param name="drive">      The drive letter. </param>
        ///<param name="deviceLink"> The returned device link. </param>
        ///<returns> 
        /// The status code.
        ///</returns>
        NTSTATUS get_symbolic_link_from_drive(const std::wstring& drive, std::wstring& deviceLink)
        {
            HANDLE              linkHandle;
            OBJECT_ATTRIBUTES   oa;
            UNICODE_STRING      deviceName;
            UNICODE_STRING      devicePrefix;

            wchar_t deviceNameBuffer[] = L"\\??\\ :";

            deviceNameBuffer[4] = drive[0];

            deviceName.Buffer = deviceNameBuffer;
            deviceName.Length = 6 * sizeof(wchar_t);
            deviceName.MaximumLength = 7 * sizeof(wchar_t);

            InitializeObjectAttributes(
                &oa,
                &deviceName,
                OBJ_CASE_INSENSITIVE,
                NULL,
                NULL
            );

            devicePrefix.Length = MAX_PATH * sizeof(WCHAR);
            devicePrefix.MaximumLength = MAX_PATH * sizeof(WCHAR);
            devicePrefix.Buffer = nullptr;
            allocate_local_buffer(&devicePrefix.Buffer, MAX_PATH * sizeof(WCHAR));
            
            ZeroMemory(devicePrefix.Buffer, MAX_PATH * sizeof(WCHAR));
            //devicePrefix.Buffer = (PWSTR)RtlAllocateHeap(NtCurrentPeb()->ProcessHeap, 0, MAX_PATH * sizeof(WCHAR));

            auto status = NtOpenSymbolicLinkObject(&linkHandle, SYMBOLIC_LINK_QUERY, &oa);
            if(NT_SUCCESS(status)) {
                status = NtQuerySymbolicLinkObject(linkHandle, &devicePrefix, NULL);
                if(NT_SUCCESS(status)) {
                    deviceLink = std::wstring(devicePrefix.Buffer, devicePrefix.Length / sizeof(wchar_t));
                }
                NtClose(linkHandle);
            }
            free_local_buffer(devicePrefix.Buffer);
            return status;
        }

        ///<summary>
        /// Adds a service to the database.
        ///</summary>
        ///<param name="manager">    Handle to the service database. </param>
        ///<param name="driverName"> The driver name. </param>
        ///<param name="driverPath"> The driver path. </param>
        ///<returns> 
        /// The status code.
        ///</returns>
        NTSTATUS create_service(SC_HANDLE manager, const std::wstring& driverName, const std::wstring& driverPath)
        {
            SC_HANDLE schService;

            schService = CreateServiceW(manager,
                std::data(driverName),
                std::data(driverName),
                SERVICE_ALL_ACCESS,
                SERVICE_KERNEL_DRIVER,
                SERVICE_DEMAND_START,
                SERVICE_ERROR_NORMAL,
                std::data(driverPath),
                nullptr,
                nullptr,
                nullptr,
                nullptr,
                nullptr
            );
            if(!schService) {
                return get_last_ntstatus();
            }

            CloseServiceHandle(schService);
            return STATUS_SUCCESS;
        }

        ///<summary>
        /// Starts a driver service.
        ///</summary>
        ///<param name="manager">    Handle to the service database. </param>
        ///<param name="driverName"> The driver name. </param>
        ///<returns> 
        /// The status code.
        ///</returns>
        NTSTATUS start_driver(SC_HANDLE manager, const std::wstring& driverName)
        {
            SC_HANDLE  schService;

            schService = OpenService(manager,
                std::data(driverName),
                SERVICE_ALL_ACCESS
            );
            if(!schService)
                return get_last_ntstatus();

            BOOL success = (StartService(schService, 0, nullptr) || GetLastError() == ERROR_SERVICE_ALREADY_RUNNING);

            CloseServiceHandle(schService);

            return success ? STATUS_SUCCESS : get_last_ntstatus();
        }

        ///<summary>
        /// Stops a driver service.
        ///</summary>
        ///<param name="manager">    Handle to the service database. </param>
        ///<param name="driverName"> The driver name. </param>
        ///<returns> 
        /// The status code.
        ///</returns>
        NTSTATUS stop_driver(SC_HANDLE manager, const std::wstring& driverName)
        {
            INT             iRetryCount;
            SC_HANDLE       schService;
            SERVICE_STATUS  serviceStatus;

            schService = OpenService(manager, std::data(driverName), SERVICE_ALL_ACCESS);
            if(!schService) {
                return get_last_ntstatus();
            }

            iRetryCount = 5;
            do {
                if(ControlService(schService, SERVICE_CONTROL_STOP, &serviceStatus))
                    break;

                if(GetLastError() != ERROR_DEPENDENT_SERVICES_RUNNING)
                    break;

                Sleep(1000);
                iRetryCount--;
            } while(iRetryCount);

            CloseServiceHandle(schService);

            if(iRetryCount == 0)
                return get_last_ntstatus();
            return STATUS_SUCCESS;
        }

        ///<summary>
        /// Gets a driver device handle.
        ///</summary>
        ///<param name="driver">       The driver name. </param>
        ///<param name="deviceHandle"> The device handle. </param>
        ///<returns> 
        /// The status code.
        ///</returns>
        NTSTATUS get_driver_device(const std::wstring& driver, PHANDLE deviceHandle)
        {
            wchar_t szDeviceName[MAX_PATH];
            HANDLE  hDevice;

            if(driver.empty() || !deviceHandle) return STATUS_INVALID_PARAMETER;

            RtlSecureZeroMemory(szDeviceName, sizeof(szDeviceName));
            wsprintf(szDeviceName, L"\\\\.\\%ws", std::data(driver));

            hDevice = CreateFile(szDeviceName,
                GENERIC_READ | GENERIC_WRITE,
                0,
                nullptr,
                OPEN_EXISTING,
                FILE_ATTRIBUTE_NORMAL,
                nullptr
            );
            if(hDevice == INVALID_HANDLE_VALUE)
                return get_last_ntstatus();

            *deviceHandle = hDevice;

            return STATUS_SUCCESS;
        }

        ///<summary>
        /// Removes a service from the database.
        ///</summary>
        ///<param name="manager">    Handle to the service database. </param>
        ///<param name="driverName"> The driver name. </param>
        ///<returns> 
        /// The status code.
        ///</returns>
        NTSTATUS delete_service(SC_HANDLE manager, const std::wstring& driverName)
        {
            SC_HANDLE  schService;
            schService = OpenService(manager,
                std::data(driverName),
                DELETE
            );

            if(!schService)
                return get_last_ntstatus();

            BOOL success = DeleteService(schService);

            CloseServiceHandle(schService);

            return success ? STATUS_SUCCESS : get_last_ntstatus();
        }

        ///<summary>
        /// Loads a driver.
        ///</summary>
        ///<param name="driverName">   The driver name. </param>
        ///<param name="driverPath">   The driver path. </param>
        ///<param name="deviceHandle"> The returned device handle. </param>
        ///<returns> 
        /// The status code.
        ///</returns>
        NTSTATUS load_driver(const std::wstring& driverName, const std::wstring& driverPath, PHANDLE deviceHandle)
        {
            SC_HANDLE	  schSCManager;
            NTSTATUS    status;

            if(driverName.empty())
                return STATUS_INVALID_PARAMETER_1;
            if(driverPath.empty())
                return STATUS_INVALID_PARAMETER_2;
            if(!deviceHandle)
                return STATUS_INVALID_PARAMETER_3;


            schSCManager = OpenSCManager(nullptr, nullptr, SC_MANAGER_ALL_ACCESS);
            if(schSCManager) {
                delete_service(schSCManager, driverName);
                status = create_service(schSCManager, driverName, driverPath);
                if(!NT_SUCCESS(status))

                {
                    CloseServiceHandle(schSCManager);
                    return status;
                }
                status = start_driver(schSCManager, driverName);
                if(!NT_SUCCESS(status)) {
                    CloseServiceHandle(schSCManager);
                    return status;
                }

                status = get_driver_device(driverName, deviceHandle);
                if(!NT_SUCCESS(status)) {
                    CloseServiceHandle(schSCManager);
                    return status;
                }
            }
            CloseServiceHandle(schSCManager);
            return status;
        }

        ///<summary>
        /// Unloads a driver.
        ///</summary>
        ///<param name="driverName">   The driver name. </param>
        ///<returns> 
        /// The status code.
        ///</returns>
        NTSTATUS unload_driver(const std::wstring& driverName)
        {
            SC_HANDLE	  schSCManager;
            NTSTATUS  status;

            if(driverName.empty()) return STATUS_INVALID_PARAMETER;

            schSCManager = OpenSCManager(nullptr,
                nullptr,
                SC_MANAGER_ALL_ACCESS
            );
            if(schSCManager) {
                status = stop_driver(schSCManager, driverName);
                if(NT_SUCCESS(status))
                    status = delete_service(schSCManager, driverName);
                CloseServiceHandle(schSCManager);
            }
            return status;
        }

        ///<summary>
        /// Allocates virtual memory.
        ///</summary>
        ///<param name="process">    The target process. </param>
        ///<param name="start">      The allocation start address. </param>
        ///<param name="size">       The allocation size. </param>
        ///<param name="allocation"> The allocation flags. </param>
        ///<param name="protection"> The memory protection flags. </param>
        ///<returns> 
        /// The status code.
        ///</returns>
        NTSTATUS allocate_memory(HANDLE process, PVOID* start, size_t* size, uint32_t allocation, uint32_t protection)
        {
            return NtAllocateVirtualMemory(process, start, 0, (PSIZE_T)size, allocation, protection);
        }

        ///<summary>
        /// Change virtual memory protection.
        ///</summary>
        ///<param name="process">       The target process. </param>
        ///<param name="start">         The start address. </param>
        ///<param name="size">          The region size. </param>
        ///<param name="protection">    The new protection flags. </param>
        ///<param name="oldProtection"> The old protection flags. </param>
        ///<returns> 
        /// The status code.
        ///</returns>
        NTSTATUS protect_memory(HANDLE process, PVOID* start, size_t* size, uint32_t protection, uint32_t* oldProtection)
        {
            auto status = NtProtectVirtualMemory(process, start, (PSIZE_T)size, protection, (PULONG)oldProtection);
            return status;
        }

        ///<summary>
        /// Frees virtual memory.
        ///</summary>
        ///<param name="process"> The target process. </param>
        ///<param name="start">   The start address. </param>
        ///<param name="size">    The region size. </param>
        ///<param name="free">    The free flag. </param>
        ///<returns> 
        /// The status code.
        ///</returns>
        NTSTATUS free_memory(HANDLE process, PVOID* start, size_t size, uint32_t free)
        {
            return NtFreeVirtualMemory(process, start, (PSIZE_T)&size, free);
        }

        ///<summary>
        /// Read virtual memory.
        ///</summary>
        ///<param name="process"> The target process. </param>
        ///<param name="address"> The start address. </param>
        ///<param name="buffer">  The buffer. </param>
        ///<param name="size">    The buffer size. </param>
        ///<returns> 
        /// The status code.
        ///</returns>
        NTSTATUS read_memory(HANDLE process, LPVOID address, LPVOID buffer, size_t size)
        {
            if(process == GetCurrentProcess()) {
                memcpy((PVOID)buffer, (PVOID)address, size);
                return STATUS_SUCCESS;
            } else {
                return NtReadVirtualMemory(process, address, buffer, size, nullptr);
            }
        }

        ///<summary>
        /// Write virtual memory.
        ///</summary>
        ///<param name="process"> The target process. </param>
        ///<param name="address"> The start address. </param>
        ///<param name="buffer">  The buffer. </param>
        ///<param name="size">    The buffer size. </param>
        ///<returns> 
        /// The status code.
        ///</returns>
        NTSTATUS write_memory(HANDLE process, LPVOID address, LPVOID buffer, size_t size)
        {
            if(process == GetCurrentProcess()) {
                memcpy(const_cast<void*>(address), buffer, size);
                return STATUS_SUCCESS;
            } else {
                auto status = NtWriteVirtualMemory(process, address, buffer, size, nullptr);
                return status;
            }
        }

        ///<summary>
        /// Opens a process.
        ///</summary>
        ///<param name="handle"> The returned handle. </param>
        ///<param name="pid">    The process id. </param>
        ///<param name="access"> The desired access flags. </param>
        ///<returns> 
        /// The status code.
        ///</returns>
        NTSTATUS open_process(PHANDLE handle, uint32_t pid, uint32_t access)
        {
            OBJECT_ATTRIBUTES objAttr;

            InitializeObjectAttributes(&objAttr, NULL, NULL, NULL, NULL);
            CLIENT_ID cid;
            cid.UniqueProcess = reinterpret_cast<HANDLE>(pid);
            cid.UniqueThread = 0;

            return NtOpenProcess(handle, access, &objAttr, &cid);
        }

        ///<summary>
        /// Checks if the process is running under WOW64.
        ///</summary>
        ///<param name="process"> The target process. </param>
        ///<param name="pebAddress"> The address of the x86 PEB. </param>
        ///<returns> 
        /// True if the process is a wow64 process.
        ///</returns>
        bool process_is_wow64(HANDLE process, PPEB32* pebAddress /*= nullptr*/)
        {
            PULONG_PTR buffer = (PULONG_PTR)query_process_information(process, ProcessWow64Information);
            bool iswow64 = *buffer != 0;
            if(pebAddress) *pebAddress = (PPEB32)*buffer;
            free_local_buffer(buffer);
            return iswow64;
        }

        ///<summary>
        /// Creates a thread.
        ///</summary>
        ///<param name="process">        The target process. </param>
        ///<param name="startAddress">   The thread start address. </param>
        ///<param name="startParameter"> The thread start parameter. </param>
        ///<param name="wait">           Blocks and wait for the thread to finish running. </param>
        ///<returns> 
        /// On success the thread exit status if wait is true. 0 if wait is false.
        /// On failure the status code.
        ///</returns>
        ULONG create_thread(HANDLE process, LPVOID startAddress, LPVOID startParameter, bool wait)
        {
            HANDLE thread = CreateRemoteThread(process, NULL, 0, (LPTHREAD_START_ROUTINE)startAddress, startParameter, 0, NULL);

            if(!thread) return get_last_ntstatus();

            if(!wait) return STATUS_SUCCESS;

            if(WaitForSingleObject(thread, INFINITE) == WAIT_OBJECT_0) {
                DWORD exitCode = 0;
                GetExitCodeThread(thread, &exitCode);
                return exitCode;
            } else {
                return STATUS_SUCCESS;
            }
        }

        ///<summary>
        /// Terminate a process.
        ///</summary>
        ///<param name="process">  The target process. </param>
        ///<param name="exitCode"> The exit code. </param>
        void terminate_process(HANDLE process, uint32_t exitCode)
        {
            NtTerminateProcess(process, exitCode);
        }

        ///<summary>
        /// Maps an image to the current process. This does not call the entry point.
        ///</summary>
        ///<param name="process"> The target image path. </param>
        ///<returns> 
        /// The image base.
        ///</returns>
        NTSTATUS load_mapped_image(const std::wstring& path, mapped_image& image)
        {
            NTSTATUS        status;
            HANDLE          fileHandle;
            LARGE_INTEGER   fileSize;
            HANDLE          sectionHandle;

            status = open_file(
                path, 
                FILE_EXECUTE | FILE_READ_ATTRIBUTES | FILE_READ_DATA | SYNCHRONIZE, 
                &fileHandle);

            sectionHandle = nullptr;
            if(NT_SUCCESS(status)) {
                status = get_file_size(fileHandle, nullptr, &fileSize);
                if(NT_SUCCESS(status)) {
                    status = NtCreateSection(
                        &sectionHandle,
                        SECTION_ALL_ACCESS,
                        NULL,
                        &fileSize,
                        PAGE_EXECUTE_READ,
                        SEC_COMMIT,
                        fileHandle);

                    if(NT_SUCCESS(status)) {
                        image.view_base = 0;
                        image.view_size = static_cast<size_t>(fileSize.QuadPart);

                        status = NtMapViewOfSection(
                            sectionHandle,
                            NtCurrentProcess(),
                            (PVOID*)&image.view_base,
                            0,
                            0,
                            NULL,
                            (SIZE_T*)&image.view_size,
                            ViewShare,
                            0,
                            PAGE_EXECUTE_READ);

                        if(NT_SUCCESS(status)) {
                            auto nthdrs = reinterpret_cast<PIMAGE_NT_HEADERS>(image.view_base + image.dos_hdr->e_lfanew);
                            if(nthdrs->Signature != IMAGE_NT_SIGNATURE) {
                                status = STATUS_INVALID_IMAGE_FORMAT;
                            } else {
                                if(nthdrs->OptionalHeader.Magic == IMAGE_NT_OPTIONAL_HDR32_MAGIC) {
                                    image.nt_hdrs32 = reinterpret_cast<PIMAGE_NT_HEADERS32>(nthdrs);
                                    image.nt_hdrs64 = nullptr;
                                } else {
                                    image.nt_hdrs32 = nullptr;
                                    image.nt_hdrs64 = reinterpret_cast<PIMAGE_NT_HEADERS64>(nthdrs);
                                }
                                image.section_count = nthdrs->FileHeader.NumberOfSections;
                                image.sections = (IMAGE_SECTION_HEADER*)((uintptr_t)&nthdrs->OptionalHeader + nthdrs->FileHeader.SizeOfOptionalHeader);
                            }
                        }
                        NtClose(sectionHandle);
                    }
                }
                NtClose(fileHandle);
            }
            return status;
        }

        ///<summary>
        /// Unloads a previously mapped image
        ///</summary>
        ///<param name="process"> The image base. </param>
        void unload_mapped_image(const mapped_image& image)
        {
            NtUnmapViewOfSection(NtCurrentProcess(), (PVOID)image.view_base);
        }

        PIMAGE_SECTION_HEADER mapped_image_rva_to_section(const mapped_image& image, ULONG rva)
        {
            ULONG i;

            for(i = 0; i < image.section_count; i++) {
                if((rva >= image.sections[i].VirtualAddress) &&
                   (rva < image.sections[i].VirtualAddress + image.sections[i].SizeOfRawData)) {
                    return &image.sections[i];
                }
            }

            return NULL;
        }

        uintptr_t mapped_image_rva_to_va(const mapped_image& image, ULONG rva)
        {
            PIMAGE_SECTION_HEADER section;

            section = mapped_image_rva_to_section(image, rva);

            if(!section)
                return NULL;

            return (uintptr_t)(image.view_base + (rva - section->VirtualAddress) + section->PointerToRawData);
        }
    }
}
