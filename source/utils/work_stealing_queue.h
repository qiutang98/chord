#pragma once 

#include <utils/utils.h>

namespace chord
{
    // "Correct and Efficient Work-Stealing for Weak Memory Models"
    // https://www.di.ens.fr/~zappa/readings/ppopp13.pdf.
    template<class WorkType>
    class WorkStealingQueue final : NonCopyable
    {
    private:
        const int64 m_mask;
        const int64 m_capacity;
        std::unique_ptr<WorkType[]> m_works;

        void set(int64 index, WorkType work)
        {
            m_works[index & m_mask] = work;
        }

		WorkType get(int64 index)
		{
			return m_works[index & m_mask];
		}

        // 
        alignas(kCpuCachelineSize) std::atomic<int64> m_top{ 0 }; // stealing by any thread.
        alignas(kCpuCachelineSize) std::atomic<int64> m_bottom{ 0 }; // push & pop in queue thread.

    public:
        explicit WorkStealingQueue(int64 capacity)
            : m_mask(capacity - 1)
            , m_capacity(capacity)
        {
            assert(chord::isPOT(capacity));
            m_works = std::make_unique<WorkType[]>(capacity);
        }

        void push(WorkType work)
        {
            int64 bottom = m_bottom.load(std::memory_order_relaxed);

            // Overflow checking.
            int64 top = m_top.load(std::memory_order_relaxed);
            assert(bottom - top < m_capacity);

            //
            set(bottom, work);

            // memory_order_seq_cst ensure bottom store work already.
            m_bottom.store(bottom + 1, std::memory_order_seq_cst);
        }

        std::optional<WorkType> pop()
        {
            int64 bottom = m_bottom.fetch_sub(1, std::memory_order_seq_cst) - 1;

            assert(bottom >= -1); // when queue is empty, pop will return -1.
            {
                // Case A: May stole by other thread in steal(). See Case B.
            }
            int64 top = m_top.load(std::memory_order_seq_cst);
            if (top < bottom)
            {
                // Pop success.
                return get(bottom);
            }

            std::optional<WorkType> work;
            if (top == bottom)
            {
                if (m_top.compare_exchange_strong(top, top + 1, 
                    std::memory_order_seq_cst, 
                    std::memory_order_relaxed))
                {
                    work = get(bottom);
                    top++; // m_top already +1 so top ++ to make bottom step.
                }
                else
                {
                    // m_top(m_bottom) was stole by other thread in steal() .
                }
            }
            else
            {
                // Case A: May stole by other thread in steal(), top already plus one.
                assert(top - bottom == 1); // See Case B.
            }

            // Update top to m_bottom.
            m_bottom.store(top, std::memory_order_seq_cst);

            //
            return work;
        }

        // 
        std::optional<WorkType> steal() // call from any thread.
        {
            while (true)
            {
                int64 top = m_top.load(std::memory_order_seq_cst);
                int64 bottom = m_bottom.load(std::memory_order_seq_cst);

                // Queue empty.
                if (top >= bottom)
                {
                    return std::nullopt;
                }

                // Case B:
                if (m_top.compare_exchange_strong(top, top + 1, 
                    std::memory_order_seq_cst, 
                    std::memory_order_relaxed))
                {
                    // Success stole top.
                    return get(top);
                }

                // Current top was stole by other thread, so retry.
            }
        }
    };
}