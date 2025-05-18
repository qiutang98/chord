#pragma once

#include <utils/utils.h>
#include <utils/noncopyable.h>
#include <utils/allocator.h>

namespace chord 
{
	template<class T, class SizeT = int64>
	class MPMCQueue : NonCopyable
	{
	private:
		struct Node
		{
            TRACE_OP_NEW_AND_DELETE;

			T data;
			std::atomic<SizeT> seq;
		};
		alignas(kCpuCachelineSize) std::atomic<SizeT> m_headSeq;
        alignas(kCpuCachelineSize) std::atomic<SizeT> m_tailSeq;

        const SizeT m_size;
        const SizeT m_mask;
        std::unique_ptr<Node[]> m_buffer;

	public:
		MPMCQueue(SizeT size)
			: m_size(size)
			, m_mask(size - 1)
			, m_headSeq(0)
			, m_tailSeq(0)
		{
			assert(chord::isPOT(m_size));

			m_buffer = std::make_unique<Node[]>(m_size);
			for (SizeT i = 0; i < m_size; ++i)
			{
				m_buffer[i].seq.store(i, std::memory_order_relaxed);
			}

			std::atomic_thread_fence(std::memory_order_release);
		}

        bool enqueue(const T& data)
        {
            SizeT headSeq = m_headSeq.load(std::memory_order_relaxed);

            while (true)
            {
                Node* node = &m_buffer[headSeq & m_mask];

                SizeT ndoeSeq = node->seq.load(std::memory_order_acquire);
                intptr_t dif = (intptr_t)ndoeSeq - (intptr_t)headSeq;

                if (dif == 0) CHORD_LIKELY
                {
                    if (m_headSeq.compare_exchange_weak(headSeq, headSeq + 1, std::memory_order_relaxed)) CHORD_LIKELY
                    {
                        node->data = data;
                        node->seq.store(headSeq + 1, std::memory_order_release);
                        return true;
                    }
                }
                else if (dif < 0) CHORD_UNLIKELY
                {
                    return false;
                }
                else CHORD_UNLIKELY
                {
                    headSeq = m_headSeq.load(std::memory_order_relaxed);
                }
            }

            return false;
        }

        bool dequeue(T& data)
        {
            SizeT tailSeq = m_tailSeq.load(std::memory_order_relaxed);

            while(true)
            {
                Node* node = &m_buffer[tailSeq & m_mask];
                SizeT ndoeSeq = node->seq.load(std::memory_order_acquire);
                intptr_t dif = (intptr_t)ndoeSeq - (intptr_t)(tailSeq + 1);

                if (dif == 0) CHORD_LIKELY
                {
                    if (m_tailSeq.compare_exchange_weak(tailSeq, tailSeq + 1, std::memory_order_relaxed)) CHORD_LIKELY
                    {
                        data = node->data;
                        node->seq.store(tailSeq + m_mask + 1, std::memory_order_release);
                        return true;
                    }
                }
                else if (dif < 0) CHORD_UNLIKELY
                {
                    return false;
                }
                else CHORD_UNLIKELY
                {
                    tailSeq = m_tailSeq.load(std::memory_order_relaxed);
                }
            }

            return false;
        }
	};
} 