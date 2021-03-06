#include <system/process_memory.hpp>

#include <misc/exceptions.hpp>
#include <misc/native.hpp>
#include <system/process.hpp>

namespace resurgence
{
    namespace system
    {
        process_memory::process_memory(process* proc)
            : _process(proc)
        {
        }
        process_memory::process_memory()
            : _process(nullptr)
        {

        }
        uint8_t* process_memory::allocate(size_t size, uint32_t allocation, uint32_t protection)
        {
            uint8_t* address;
            native::allocate_memory(_process->get_handle().get(), (PVOID*)&address, &size, allocation, protection);
            return address;
        }
        NTSTATUS process_memory::allocate_ex(uint8_t** address, size_t size, uint32_t allocation, uint32_t protection)
        {
            return native::allocate_memory(_process->get_handle().get(), (PVOID*)address, &size, allocation, protection);
        }
        NTSTATUS process_memory::protect(const uint8_t* address, size_t size, uint32_t protection, uint32_t* oldProtection /*= nullptr*/)
        {
            uint32_t old;
            auto ret = native::protect_memory(_process->get_handle().get(), (PVOID*)&address, &size, protection, &old);
            if(oldProtection)
                *oldProtection = old;
            return ret;
        }
        NTSTATUS process_memory::free(const uint8_t* address, size_t size, uint32_t freeType)
        {
            return native::free_memory(_process->get_handle().get(), (PVOID*)&address, size, freeType);
        }
        NTSTATUS process_memory::read_bytes(const uint8_t* address, uint8_t* buffer, size_t size)
        {
            return native::read_memory(_process->get_handle().get(), (void*)address, buffer, size);
        }
        NTSTATUS process_memory::write_bytes(const uint8_t* address, uint8_t* buffer, size_t size)
        {
            return native::write_memory(_process->get_handle().get(), (void*)address, buffer, size);
        }
    }
}