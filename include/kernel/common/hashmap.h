#ifndef HASHMAP_H
#define HASHMAP_H
#include "doublyLinkedList.h"
#include "pair.h"
#include "hash/MurmurHash3.h"
#include "print.h"

namespace Common {
    template<typename T, typename U>
    class Hashmap
    {
    public:
        Hashmap() {
            m_bucketNum = 1;
            m_maxLoadFactor = 1;
            m_totalElements = 0;
            m_bucketArray = new DoublyLinkedList<Pair<T, U>>[m_bucketNum];
        }
        ~Hashmap() { delete[] m_bucketArray; }

        Hashmap(const Hashmap& other) : m_bucketNum(other.m_bucketNum), m_totalElements(other.m_totalElements), m_maxLoadFactor(other.m_maxLoadFactor)
        {
            m_bucketArray = new DoublyLinkedList<Pair<T, U>>[m_bucketNum];
            for (uint64_t i = 0; i < m_bucketNum; i++) { m_bucketArray[i] = other.m_bucketArray[i]; }
        }

        Hashmap(Hashmap&& other) : Hashmap() { swap(other); }

        Hashmap& operator=(Hashmap other)
        {
            swap(other);
            return *this;
        }

        void swap(Hashmap& other)
        {
            auto buckets = m_bucketArray;
            m_bucketArray = other.m_bucketArray;
            other.m_bucketArray = buckets;
            auto bucketNum = m_bucketNum;
            m_bucketNum = other.m_bucketNum;
            other.m_bucketNum = bucketNum;
            auto totalElements = m_totalElements;
            m_totalElements = other.m_totalElements;
            other.m_totalElements = totalElements;
            auto maxLoadFactor = m_maxLoadFactor;
            m_maxLoadFactor = other.m_maxLoadFactor;
            other.m_maxLoadFactor = maxLoadFactor;
        }

        uint64_t bucket_index(T key)
        {
            uint64_t hashedIntegers[2];
            // This is an open source hash.
            MurmurHash3_x64_128(&key, sizeof(T), 3, hashedIntegers);
            return hashedIntegers[0] % m_bucketNum;
        }

        DoublyLinkedListIterator<Pair<T, U>> find(T key)
        {
            auto it = m_bucketArray[bucket_index(key)].first();
            // Search through the bucket to find the key.
            for (it = m_bucketArray[bucket_index(key)].first(); !it.is_end(); ++it)
            {
                if (it->first == key) { return it; }
            }
            return it;
        }

        void check_expand()
        {
            // If we have a high load factor (avg. bucket size), then increase the number of buckets.
            if (m_totalElements/m_bucketNum <= m_maxLoadFactor) { return; }
            m_bucketNum *= 2;
            auto newBuckets = new DoublyLinkedList<Pair<T, U>>[m_bucketNum];
            // Rehash each element and populate the new buckets.
            for (int i = 0; i < m_bucketNum/2; i++)
            {
                for (auto it = m_bucketArray[i].first(); !it.is_end(); ++it)
                {
                    newBuckets[bucket_index(it->first)].append(*it);
                }
            }
            delete[] m_bucketArray;
            m_bucketArray = newBuckets;
        }

        void check_shrink()
        {
            // Check if the number of buckets can be shrunk, but always keep at least one bucket.
            if (m_bucketNum == 1) { return; }
            if (4*m_totalElements/m_bucketNum >= 1 && m_totalElements != 0) { return; }
            m_bucketNum /= 2;
            auto newBuckets = new DoublyLinkedList<Pair<T, U>>[m_bucketNum];
            for (int i = 0; i < m_bucketNum*2; i++)
            {
                for (auto it = m_bucketArray[i].first(); !it.is_end(); ++it)
                {
                    newBuckets[bucket_index(it->first)].append(*it);
                }
            }
            delete[] m_bucketArray;
            m_bucketArray = newBuckets;
        }

        DoublyLinkedListIterator<Pair<T,U>> insert(const T& key, const U& value)
        {
            auto it = find(key);
            if (!it.is_end()) { it->last = value; return it; }
            // Add to the relevant bucket.
            m_bucketArray[bucket_index(key)].append(Pair(key, value));
            m_totalElements++;
            check_expand();
            return find(key);
        }

        void remove(const T& key)
        {
            // Find the key and remove it.
            auto it = find(key);
            if (!it.is_end()) {
                m_bucketArray[bucket_index(key)].remove(it);
                m_totalElements--;
            }
            check_shrink();
        }
        uint64_t m_bucketNum;
    private:
        DoublyLinkedList<Pair<T, U>>* m_bucketArray;
        uint64_t m_totalElements;
        uint64_t m_maxLoadFactor;
        uint64_t m_hash;
    };
}
#endif
