#pragma once

namespace chord
{
    class NonCopyable
    {
    protected:
        NonCopyable() = default;
        ~NonCopyable() = default;

    private:
        NonCopyable(const NonCopyable&) = delete;
        const NonCopyable& operator=(const NonCopyable&) = delete;
    }; 
}