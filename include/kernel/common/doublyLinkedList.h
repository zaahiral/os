#ifndef DOUBLYLINKEDLIST_H
#define DOUBLYLINKEDLIST_H

#include "memory/kheap.h"
#include "utilities.h"
#include "x86_64.h"
#include "process/spinlock.h"

namespace Common {
    template<typename T>
    class DoublyLinkedListIterator;

    template<typename T>
    class DoublyLinkedList
    {
        friend class DoublyLinkedListIterator<T>;
    private:
        struct ListNode
        {
            ListNode* next = nullptr;
            ListNode* prev = nullptr;
            T value;
            ListNode(const T& value) : value(value) {}
            ListNode(T&& value) : value(move(value)) {}
        };
        typename DoublyLinkedList<T>::ListNode* m_head;
        typename DoublyLinkedList<T>::ListNode* m_tail;
        uint64_t m_size;
        Spinlock m_spinlock;
    public:
        uint64_t size() { return m_size; }
        void clear()
        {
            // Delete all nodes by iterating through them.
            m_spinlock.acquire();
            for (auto* node = m_head; node;)
            {
                auto* next = node->next;
                delete node;
                node = next;
            }
            m_head = nullptr;
            m_tail = nullptr;
            m_size = 0;
            m_spinlock.release();
        }

        DoublyLinkedListIterator<T> append(T&& value)
        {
            m_spinlock.acquire();
            auto node = new ListNode(move(value));
            m_size++;
            // The list is empty, set the new node as the head
            if (!m_head)
            {
                m_head = node;
                m_tail = node;
                m_spinlock.release();
                return DoublyLinkedListIterator<T>(node);
            }
            m_tail->next = node;
            node->prev = m_tail;
            m_tail = node;
            m_spinlock.release();
            return DoublyLinkedListIterator<T>(node);
        }

        DoublyLinkedListIterator<T> append(const T& value)
        {
            m_spinlock.acquire();
            auto node = new ListNode(value);
            m_size++;
            if (!m_head)
            {
                m_head = node;
                m_tail = node;
                m_spinlock.release();
                return DoublyLinkedListIterator<T>(node);
            }
            m_tail->next = node;
            node->prev = m_tail;
            m_tail = node;
            m_spinlock.release();
            return DoublyLinkedListIterator<T>(node);
        }

        DoublyLinkedListIterator<T> insert(T&& value, DoublyLinkedListIterator<T> it)
        {
            m_spinlock.acquire();
            // Check that the iterator is valid.
            if (it.is_end() || !it.m_node) {  m_spinlock.release(); return append(value);}
            auto node = new ListNode(move(value));
            m_size++;
            if (!m_head)
            {
                m_head = node;
                m_tail = node;
                m_spinlock.release();
                return DoublyLinkedListIterator<T>(node);
            }
            auto next = it.m_node->next;
            it.m_node->next = node;
            node->prev = it.m_node;
            node->next = next;
            // If we inserted after the tail, the new node becomes the tail.
            if (next) { next->prev = node; }
            else { m_tail = node; }
            m_spinlock.release();
        }

        DoublyLinkedListIterator<T> insert(const T& value, DoublyLinkedListIterator<T> it)
        {
            m_spinlock.acquire();
            if (it.is_end() || !it.m_node) { m_spinlock.release(); return append(value);}
            auto node = new ListNode(value);
            m_size++;
            if (!m_head)
            {
                m_head = node;
                m_tail = node;
                m_spinlock.release(); 
                return DoublyLinkedListIterator<T>(node);
            }
            auto next = it.m_node->next;
            it.m_node->next = node;
            node->prev = it.m_node;
            node->next = next;
            // If we inserted after the tail, the new node becomes the tail.
            if (next) { next->prev = node; }
            else { m_tail = node; }
            m_spinlock.release(); 
            return DoublyLinkedListIterator<T>(node);
        }

        DoublyLinkedListIterator<T> insert_before(T&& value, DoublyLinkedListIterator<T> it)
        {
            m_spinlock.acquire();
            if (it.is_end() || !it.m_node) { m_spinlock.release(); return append(value);}
            auto node = new ListNode(move(value));
            m_size++;
            if (!m_head)
            {
                m_head = node;
                m_tail = node;
                m_spinlock.release(); 
                return DoublyLinkedListIterator<T>(node);
            }
            auto prev = it.m_node->prev;
            it.m_node->prev = node;
            node->prev = prev;
            node->next = it.m_node;
            // If we inserted before the head, the new node becomes the head.
            if (prev) { prev->next = node; }
            else { m_head = node; }
            m_spinlock.release(); 
            return DoublyLinkedListIterator<T>(node);
        }

        DoublyLinkedListIterator<T> insert_before(const T& value, DoublyLinkedListIterator<T> it)
        {
            m_spinlock.acquire();
            if (it.is_end() || !it.m_node) { m_spinlock.release(); return append(value);}
            auto node = new ListNode(value);
            m_size++;
            if (!m_head)
            {
                m_head = node;
                m_tail = node;
                m_spinlock.release(); 
                return DoublyLinkedListIterator<T>(node);
            }
            auto prev = it.m_node->prev;
            it.m_node->prev = node;
            node->prev = prev;
            node->next = it.m_node;
            // If we inserted before the head, the new node becomes the head.
            if (prev) { prev->next = node; }
            else { m_head = node; }
            m_spinlock.release(); 
            return DoublyLinkedListIterator<T>(node);
        }

        DoublyLinkedListIterator<T> remove(DoublyLinkedListIterator<T> it)
        {
            m_spinlock.acquire();
            m_size--;
            auto* node = it.m_node;
            auto ret = DoublyLinkedListIterator<T>(node->next);
            // Remove the node and change the node before and after to point to each other.
            if (node->prev)
            {
                node->prev->next = node->next;
            }
            else
            {
                m_head = node->next;
            }

            if (node->next)
            {
                node->next->prev = node->prev;
            }
            else
            {
                m_tail = node->prev;
            }
            delete node;
            m_spinlock.release(); 
            return ret;
        }
        DoublyLinkedList() : m_head(nullptr), m_tail(nullptr), m_size(0) {}
        ~DoublyLinkedList() { clear(); }
         
        DoublyLinkedList& operator=(DoublyLinkedList const &rhs)
        {
            auto cpy(rhs);
            cpy.swap(*this);
            return *this;
        }

        DoublyLinkedList(DoublyLinkedList&& rhs) : m_head(nullptr), m_tail(nullptr), m_size(0) {
            rhs.swap(*this);
        }

        DoublyLinkedList& operator=(DoublyLinkedList&& other)
        {
            this->swap(other);
            return *this;
        }

        void swap(DoublyLinkedList& with)
        {
            auto* tempNode = with.m_head;
            with.m_head = m_head;
            m_head = tempNode;
            tempNode = with.m_tail;
            with.m_tail = m_tail;
            m_tail = tempNode;
            uint64_t tempSize = m_size;
            m_size = with.m_size;
            with.m_size = tempSize;
        }

        DoublyLinkedList(DoublyLinkedList const& rhs) : m_head(nullptr), m_tail(nullptr), m_size(0)
        {
            for (auto* node = rhs.m_head; node;)
            {
                append(node->value);
                node = node->next;
            } 
        }

        DoublyLinkedListIterator<T> first()
        {
            return DoublyLinkedListIterator<T>(m_head);
        }

        DoublyLinkedListIterator<T> last()
        {
            return DoublyLinkedListIterator<T>(m_tail);
        }
    };
    
    template<typename T>
    class DoublyLinkedListIterator
    {
        friend class DoublyLinkedList<T>;
        public:
        DoublyLinkedListIterator& operator++()
        {
            m_node = m_node->next;
            return *this;
        }
        T& operator*() { return m_node->value; }
        T* operator->() { return &m_node->value; }
        bool is_end() { return !m_node; }
        DoublyLinkedListIterator(typename DoublyLinkedList<T>::ListNode* node);
        private:
        typename DoublyLinkedList<T>::ListNode* m_node;
    };
    template <typename T>
    DoublyLinkedListIterator<T>::DoublyLinkedListIterator(typename DoublyLinkedList<T>::ListNode* node) : m_node(node) {}
}

#endif
