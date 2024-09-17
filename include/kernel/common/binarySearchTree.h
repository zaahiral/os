#ifndef BINARYSEARCHTREE_H
#define BINARYSEARCHTREE_H
#include "types.h"
#include "memory/memory.h"
#include "print.h"

namespace Common {

    /*
        This implementation of a Binary Search Tree does not rely on malloc/free as it is used to manage the heap
        As a result, the default constructor should NOT be used elsewhere, as it is a fixed size BST.
    */

    template <typename T>
    class BinarySearchTree
    {
    private:
        struct TreeNode
        {
            TreeNode* left = nullptr;
            TreeNode* right = nullptr;
            TreeNode* parent = nullptr;
            T value;
        };
    public:
        BinarySearchTree(void* address, uint64_t maxSize, bool (*lessThanPredicate)(T, T));
        BinarySearchTree(BinarySearchTree const&) = delete;
        BinarySearchTree& operator=(BinarySearchTree const&) = delete;
        void insert_element(T item);
        void remove_element(T item);
        // Get smallest item greater than the given item.
        T* get_successor(T item);
        uint64_t get_element_count();
        uint64_t get_size();
        uint64_t get_size_of_one_node();
    private:
        TreeNode* m_startingAddress;
        TreeNode* m_root;
        uint64_t m_elementCount;
        uint64_t m_maxSize;
        bool (*m_lessThanPredicate)(T, T);
        void remove_node(TreeNode* node);
        void substitute_nodes(TreeNode* before, TreeNode* after);
        TreeNode* successor_node(TreeNode* node);
        TreeNode* find_node_from_element(T item, TreeNode* node);
        TreeNode* minimum_node(TreeNode* node);
        TreeNode* get_root_node();
    };

    template <typename T>
    BinarySearchTree<T>::BinarySearchTree(void* address, uint64_t maxSize, bool (*lessThanPredicate)(T, T))
    {
        m_startingAddress = (TreeNode*)address;
        m_root = m_startingAddress;
        m_elementCount = 0;
        m_maxSize = 0;
        m_lessThanPredicate = lessThanPredicate;
    }

    template <typename T>
    typename BinarySearchTree<T>::TreeNode* BinarySearchTree<T>::get_root_node()
    {
        if (m_elementCount == 0) { return nullptr; }
        TreeNode* root = m_root;

        // Verify that this is the root node.
        while (root->parent)
        {
            root = root->parent;
        }
        return root;
    }

    template <typename T>
    T* BinarySearchTree<T>::get_successor(T item)
    {
        // Insert the item, so that we can use the successor of its node, and then remove it again.
        insert_element(item);
        TreeNode* node = m_startingAddress + m_elementCount - 1;
        TreeNode* succ = successor_node(node);
        remove_element(item);
        if (!succ) { return nullptr; }
        return &succ->value;
    }

    template <typename T>
    void BinarySearchTree<T>::insert_element(T item)
    {
        // Nodes are contiguous in memory, so the new node is placed after the last node.
        TreeNode* ptr = m_startingAddress + m_elementCount;
        *ptr = TreeNode();
        ptr->value = item;

        // Traverse down the tree to find the parent of the new node.
        TreeNode* node = get_root_node();
        TreeNode* trailingPointer = nullptr;
        while (node)
        {
            trailingPointer = node;
            if (m_lessThanPredicate(item, node->value))
                node = node->left;
            else
                node = node->right;
        }
        m_elementCount++;
        ptr->parent = trailingPointer;
        if (!trailingPointer)
        {
            m_root = ptr;
            return;
        }
        if (m_lessThanPredicate(item, trailingPointer->value))
        {
            trailingPointer->left = ptr;
            return;
        }
        trailingPointer->right = ptr;
    }

    template <typename T>
    typename BinarySearchTree<T>::TreeNode* BinarySearchTree<T>::minimum_node(TreeNode* node)
    {
        // Recursively find the minimum node in the tree.
        if (node->left) { return minimum_node(node->left); }
        return node;
    }

    template <typename T>
    typename BinarySearchTree<T>::TreeNode* BinarySearchTree<T>::successor_node(TreeNode* node)
    {
        // If the node has a right branch, the minimum node of the subtree will be the smallest larger element.
        if (node->right) { return minimum_node(node->right); }

        // Otherwise, keep traversing up until the node is a left child.
        TreeNode* parent = node->parent;
        while (parent && node == parent->right)
        {
            node = parent;
            parent = node->parent;
        }
        return parent;
    }

    template <typename T>
    typename BinarySearchTree<T>::TreeNode* BinarySearchTree<T>::find_node_from_element(T item, TreeNode* node)
    {
        // Recursively find the node using the ordered subtree property of the binary search tree.
        if (!node || item == node->value)
            return node;
        if (m_lessThanPredicate(item, node->value))
            return find_node_from_element(item, node->left);
        else
            return find_node_from_element(item, node->right);
        return node;
    }

    template <typename T>
    void BinarySearchTree<T>::remove_element(T item)
    {
        TreeNode* node = find_node_from_element(item, get_root_node());
        remove_node(node);

        // As nodes are not dynamically allocated, but are instead contiguous in memory, we must maintain this property.
        // If the removed node was not at the end, we move the last node in to "fill the gap".
        if (node != m_startingAddress + m_elementCount)
        {
            TreeNode* last = m_startingAddress + m_elementCount;
            if (last->parent)
            {
                if (last == last->parent->left) { last->parent->left = node; }
                if (last == last->parent->right) { last->parent->right = node; }
            } else {
                m_root = node;
            }
            if (last->left) { last->left->parent = node; }
            if (last->right) { last->right->parent = node; }
            *node = m_startingAddress[m_elementCount];
        }
    }

    template <typename T>
    void BinarySearchTree<T>::remove_node(TreeNode* node)
    {
        m_elementCount--;
        if (m_elementCount==0) { return; }
        // If we do not have a left or right child, we can simply move the other subtree in to replace the removed node
        if (!node->left) { substitute_nodes(node, node->right); return; }
        if (!node->right) { substitute_nodes(node, node->left); return; }

        // We have two remaining cases where the successor (S) displaces the removed node (N):
        // (i) if S is N's right child, S displaces N.
        // (ii) if S instead is only in N's right subtree, we first replace S with its right child, then it displaces N.

        TreeNode* succ = successor_node(node);
        if (succ->parent != node)
        {
            substitute_nodes(succ, succ->right);
            succ->right = node->right;
            succ->right->parent = succ;
        }
        substitute_nodes(node, succ);
        succ->left = node->left;
        succ->left->parent = succ;
    }

    template <typename T>
    void BinarySearchTree<T>::substitute_nodes(TreeNode* before, TreeNode* after)
    {
        if (!before->parent)
            m_root = after;
        else if (before == before->parent->left)
            before->parent->left = after;
        else
            before->parent->right = after; 
        if (after)
            after->parent = before->parent;
    }

    template <typename T>
    uint64_t BinarySearchTree<T>::get_size() { return m_elementCount * sizeof(TreeNode); }

    template <typename T>
    uint64_t BinarySearchTree<T>::get_element_count() { return m_elementCount; }

    template <typename T>
    uint64_t BinarySearchTree<T>::get_size_of_one_node() { return sizeof(TreeNode); }
}

#endif
