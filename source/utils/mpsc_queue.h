#pragma once

#include <utils/utils.h>
#include <utils/noncopyable.h>
#include <utils/allocator.h>

namespace chord
{
    template<typename T>
    struct MPSCQueueNode
    {
        T data{ }; // Temp store data, use move semantic to dequeue/enqueue.
        std::atomic<MPSCQueueNode*> next = nullptr;
    };

    // Heap allocator.
    template<typename T>
    using MPSCQueueHeapAllocator = HeapAllocator<MPSCQueueNode<T>>;

    // Cache object allocator, scatter in memory.
    template<typename T, int64 kMaxCacheObject = std::numeric_limits<int64>::max()>
    using MPSCQueueFreeListAllocator = FreeListAllocator<MPSCQueueNode<T>, kMaxCacheObject>;

    // Larger object and frequency reused one can use pool allocator.
    template<typename T, size_t kPageSize>
    using MPSCQueuePoolAllocator = FreeListArenaAllocator<MPSCQueueNode<T>, kPageSize>;
    
    // http://www.1024cores.net/home/lock-free-algorithms/queues/non-intrusive-mpsc-node-based-queue
	template<class T, class Allocator>
	class MPSCQueue final : NonCopyable
	{
	private:
        using Node = MPSCQueueNode<T>;
        static_assert(sizeof(Node) == sizeof(Allocator::ValueType));

        alignas(kCpuCachelineSize) std::atomic<Node*> m_enqueuePos;
        alignas(kCpuCachelineSize) Node* m_dequeuePos;
        alignas(kCpuCachelineSize) std::atomic<int64> m_nodeCount{ 0 };
        alignas(kCpuCachelineSize) std::unique_ptr<Allocator> m_allocator;

        void freeNode(Node* node)
        {
            node->~Node();
            m_allocator->free(node);
        }

        Node* allocateNode()
        {
            return new (m_allocator->allocate()) Node;
        }

	public:
		MPSCQueue()  // Consumer thread.
        {
            m_allocator = std::make_unique<Allocator>();

            //
            m_dequeuePos = allocateNode();
            m_dequeuePos->next.store(nullptr, std::memory_order_relaxed);

            // 
            m_enqueuePos.store(m_dequeuePos, std::memory_order_relaxed);
        }

        ~MPSCQueue()
        {
            T temp;
            while (dequeue(temp)) { }

            assert(m_enqueuePos.load(std::memory_order_relaxed) == m_dequeuePos);
            freeNode(m_dequeuePos);
        }

        void enqueue(T&& input) // Producer threads.
        {
            // Multi thread may modify m_enqueuePos, so we need to use acq_rel semantic.
            m_nodeCount ++;
            Node* node = allocateNode();

            node->data = std::move(input); // Move semantic.
            node->next.store(nullptr, std::memory_order_release);

			// memory_order_seq_cst ensure node data flush before exchange.
			auto* prev = m_enqueuePos.exchange(node, std::memory_order_seq_cst);
                
            // Update store.
            prev->next.store(node, std::memory_order_release);
        }

        bool isEmpty() const
        {
            return m_nodeCount == 0;
        }

		// Get dequeue node data but not dequeue it.
        bool getDequeue(T& output) const
        {
            Node* dequeueNode = m_dequeuePos;
            Node* next = dequeueNode->next.load(std::memory_order_acquire);
            if (next == nullptr)
            {
                // No update yet or empty.
                return false;
            }

            output = next->data;
            return true;
        }

        bool dequeue(T& output) // Consumer thread.
        {
            Node* dequeueNode = m_dequeuePos;
            Node* next = dequeueNode->next.load(std::memory_order_acquire);
            if (next == nullptr)
            {
                // No update yet or empty.
                return false;
            }

            m_nodeCount --;

            // Move semantic.
            output = std::move(next->data);

			// dequeue node update for next time dequeue.
            m_dequeuePos = next;

            // Clean this.
            freeNode(dequeueNode);

            // 
            return true;
        }
	};
}