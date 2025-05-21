#pragma once

#include <utils/allocator.h>

namespace chord
{
	struct alignas(kCpuCachelineSize) MiniTask : NonCopyable
	{
		template<typename RetType, typename... Args>
		using TaskFunction = RetType(*)(void* storage, std::tuple<Args...>&& args);

		void* anyFuncPtr;
		void* storage[7]; // 

		template<typename Lambda>
		static inline MiniTask* allocate(Lambda function)
		{
			static_assert(sizeof(Lambda) <= sizeof(MiniTask::storage),
				"Don't pass over 54 char capture function input.");

			using LambdaArgs = function_traits<Lambda>::args_tuple;
			using RetType    = function_traits<Lambda>::return_type;

			MiniTask* miniTask = new MiniTask;
			miniTask->anyFuncPtr = [](void* storage, LambdaArgs&& args)
			{
				Lambda* object = static_cast<Lambda*>(storage);
				if constexpr (std::is_void_v<RetType>)
				{
					std::apply(std::bind_front(&Lambda::operator(), *object), args);
					object->~Lambda();
				}
				else
				{
					RetType result = std::apply(std::bind_front(&Lambda::operator(), *object), args);
					object->~Lambda();
					return std::move(result);
				}
			};

			new (miniTask->storage) Lambda(std::move(function));
			return miniTask;
		}

		template<typename RetType, typename... Args>
		RetType execute(Args&&... args)
		{
			auto function = reinterpret_cast<TaskFunction<RetType, Args...>>(anyFuncPtr);
			if constexpr (std::is_void_v<RetType>)
			{
				function(storage, std::tuple<Args...>(std::forward<Args>(args)...));
			}
			else
			{
				RetType result = function(storage, std::tuple<Args...>(std::forward<Args>(args)...));
				return std::move(result);
			}
		}

		void free()
		{
			delete this;
		}

		// 
		void* operator new(size_t size);
		void  operator delete(void* rawMemory);
	};
	static_assert(sizeof(MiniTask) == kCpuCachelineSize);
}