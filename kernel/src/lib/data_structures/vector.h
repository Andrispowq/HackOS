#ifndef VECTOR_H
#define VECTOR_H

#include "lib/memory.h"
#include "lib/string.h"

#include "memory/heap.h"

#define VECTOR_DEFAULT_SIZE 2

template<typename T>
class vector
{
public:
    vector()
    {
        ptr = new T[VECTOR_DEFAULT_SIZE];
        _size = 0;
        capacity = VECTOR_DEFAULT_SIZE;
    }

    ~vector()
    {
        delete[] ptr;
    }

    size_t size() const { return _size; }

    T& operator[](size_t index)
    {
        if(index < _size)
        {
            return ptr[index];
        }
    }

    T operator[](size_t index) const
    {
        if(index < _size)
        {
            return ptr[index];
        }

        return T();
    }

    void push_back(const T& element)
    {
        if(_size == capacity)
        {
            resize(capacity * VECTOR_DEFAULT_SIZE);
        }

        ptr[_size++] = element;
    }

    void erase(size_t index)
    {
        if(index >= _size)
        {
            return;
        }

        //Shift elements back by one
        memcpy(&ptr[index], &ptr[index + 1], (_size - index - 1));
    }

    void clear()
    {
        _size = 0;
    }

    void resize(size_t new_capacity)
    {
        capacity = new_capacity;
        T* new_ptr = new T[capacity];
        memcpy(new_ptr, ptr, _size * sizeof(T));
        delete[] ptr;
        ptr = new_ptr;
    }

    vector(const vector<T>& other)
    {
        this->capacity = other.capacity;
        this->_size = other._size;
        this->ptr = new T[capacity];
        memcpy(ptr, other.ptr, _size * sizeof(T));
    }

    void operator=(const vector<T>& other)
    {
        this->capacity = other.capacity;
        this->_size = other._size;
        delete[] ptr;
        this->ptr = new T[capacity];
        memcpy(ptr, other.ptr, _size * sizeof(T));
    }

private:
    size_t capacity;
    size_t _size;
    T* ptr;
};

#endif