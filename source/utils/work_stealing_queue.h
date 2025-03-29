#pragma once 

#include <utils/utils.h>

namespace chord 
{
    // "Correct and Efficient Work-Stealing for Weak Memory Models"
    // https://www.di.ens.fr/~zappa/readings/ppopp13.pdf.
    template<class WorkType, uint32 Capacity>
    class WorkStealingQueue
    {
        static_assert(std::is_trivially_constructible_v<WorkType>);
        static_assert(std::is_trivially_destructible_v<WorkType>);

        static_assert(Capacity >= 2 && Capacity < std::numeric_limits<int32>::max());
        static_assert(!std::has_single_bit(Capacity), "Capacity should set power of two.");

    private:
        const std::thread::id m_threadId;

        // 
        WorkType m_work[Capacity];

        // 
        alignas(kCpuCachelineSize) std::atomic<int32> m_top { 0 }; // stealing by any thread.
        alignas(kCpuCachelineSize) std::atomic<int32> m_bottom { 0 }; // push & pop in queue thread.

    private:
        WorkType get(uint32 index) const 
        { 
            assert(index < Capacity); 
            return m_work[index]; 
        }

        void set(uint32 index, WorkType work) 
        { 
            assert(index < Capacity); 
            m_work[index] = work; 
        }

    public:
        explicit WorkStealingQueue(const std::thread::id& queueThreadId)
            : m_threadId(queueThreadId)
        {

        }

        void push(WorkType work)
        {
            assert(std::this_thread::get_id() == m_threadId); // 

            int32 bottom = m_bottom.load(std::memory_order_relaxed);
            set(bottom, work);

            // Memory order require when write.
            m_bottom.store(bottom + 1, std::memory_order_seq_cst);
        }

        WorkType pop()
        {
            assert(std::this_thread::get_id() == m_threadId); // 

            int32 bottom = m_bottom.fetch_sub(1, std::memory_order_seq_cst) - 1;
            assert(bottom >= -1); // when queue is empty, pop will return -1.
            {
                // Case A: May stole by other thread in steal(). See Case B.
            }
            int32 top = m_top.load(std::memory_order_seq_cst);
            if (top < bottom) CHORD_LIKELY
            {
                // Pop success.
                return get(bottom);
            }

            WorkType work { };
            if (top == bottom)
            {
                if (m_top.compare_exchange_strong(top, top + 1, std::memory_order_seq_cst,std::memory_order_relaxed)) 
                {
                    // Current thread pop success.
                    work = get(bottom);
                    top++; //m_top already +1 so top ++ to make bottom step.
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
        WorkType steal() // call from any thread.
        {
            while (true) 
            {
                int32 top = m_top.load(std::memory_order_seq_cst);
                int32 bottom = m_bottom.load(std::memory_order_seq_cst);

                // Queue empty.
                if (top >= bottom) 
                {
                    return { };
                }

                // Case B:
                if (m_top.compare_exchange_strong(top, top + 1, std::memory_order_seq_cst, std::memory_order_relaxed)) 
                {
                    // Success stole top.
                    return get(top);
                }
                else
                {
                    // Current top was stole by other thread, so retry.
                }
            }
        }
    };
}