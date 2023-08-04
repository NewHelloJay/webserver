#include "heap_timer.h"
#include <iostream>

void HeapTimer::siftup(size_t i) {
    if(i < 0 || i >= m_heap.size()){
        return;
    }
    size_t j = (i - 1) / 2;
    while(j >= 0) {
        if(m_heap[j] < m_heap[i]) { break; }
        SwapNode(i, j);
        i = j;
        j = (i - 1) / 2;
    }
}

void HeapTimer::SwapNode(size_t i, size_t j) {
    if(i < 0 || i >= m_heap.size()){
        return;
    }
    if(j < 0 || j >= m_heap.size()){
        return;
    }
    std::swap(m_heap[i], m_heap[j]);
    m_ref[m_heap[i].id] = i;
    m_ref[m_heap[j].id] = j;
} 

bool HeapTimer::siftdown(size_t index, size_t n) {
    assert(index >= 0 && index < m_heap.size());
    assert(n >= 0 && n <= m_heap.size());
    size_t i = index;
    size_t j = i * 2 + 1;
    while(j < n) {
        if(j + 1 < n && m_heap[j + 1] < m_heap[j]){
            j++;
        }
        if(m_heap[i] < m_heap[j]){
            break;
        }
        SwapNode(i, j);
        i = j;
        j = i * 2 + 1;
    }
    return i > index;
}

void HeapTimer::add(int id, int timeout,const TimeoutCallBack& cb) {
    if(id < 0){
        return;
    }
    size_t i;
    if(m_ref.count(id) == 0) {
        /* 新节点：堆尾插入，调整堆 */
        i = m_heap.size();
        m_ref[id] = i;
        m_heap.push_back({id, Clock::now() + MS(timeout), cb});
        siftup(i);
    } 
    else {
        /* 已有结点：调整堆 */
        i = m_ref[id];
        m_heap[i].expires = Clock::now() + MS(timeout);
        m_heap[i].cb = cb;
        if(!siftdown(i, m_heap.size())) {
            siftup(i);
        }
    }
}

void HeapTimer::doWork(int id) {
    /* 删除指定id结点，并触发回调函数 */
    if(m_heap.empty() || m_ref.count(id) == 0) {
        return;
    }
    size_t i = m_ref[id];
    TimerNode node = m_heap[i];
    node.cb();
    del(i);
}

void HeapTimer::del(size_t index) {
    /* 删除指定位置的结点 */
    if(m_heap.empty() || index < 0 || index >= m_heap.size()){
        return;
    }
    /* 将要删除的结点换到队尾，然后调整堆 */
    size_t i = index;
    size_t n = m_heap.size() - 1;
    assert(i <= n);
    if(i < n) {
        SwapNode(i, n);
        if(!siftdown(i, n)) {
            // 不是向下调整就是向上调整
            siftup(i);
        }
    }
    /* 队尾元素删除 */
    m_ref.erase(m_heap.back().id);
    m_heap.pop_back();
}

void HeapTimer::adjust(int id, int timeout) {
    // 重置结点中的expires变量
    if(m_heap.empty() || m_ref.count(id) == 0){
        return;
    }
    m_heap[m_ref[id]].expires = Clock::now() + MS(timeout);;
    siftdown(m_ref[id], m_heap.size());
}

void HeapTimer::tick() {
    /* 清除超时结点 */
    if(m_heap.empty()) {
        return;
    }
    while(!m_heap.empty()) {
        TimerNode node = m_heap.front();
        if(std::chrono::duration_cast<MS>(node.expires - Clock::now()).count() > 0) { 
            break; 
        }
        node.cb();
        pop();
        std::cout << "超时结点fd: " << node.id << std::endl;
    }
}

void HeapTimer::pop() {
    if(m_heap.empty()){
        return;
    }
    del(0);
}

void HeapTimer::clear() {
    m_ref.clear();
    m_heap.clear();
}

int HeapTimer::GetNextTick() {
    tick();
    size_t res = -1;
    if(!m_heap.empty()) {
        res = std::chrono::duration_cast<MS>(m_heap.front().expires - Clock::now()).count();
        if(res < 0) { res = 0; }
    }
    return res;
}