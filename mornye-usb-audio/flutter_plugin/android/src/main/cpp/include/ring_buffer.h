#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <atomic>
#include <algorithm>

/**
 * Lock-free Single-Producer Single-Consumer (SPSC) Circular Ring Buffer
 * Designed for low-latency, glitch-free PCM streaming directly into USB Isochronous URBs.
 */
class LockFreeAudioRingBuffer {
public:
    explicit LockFreeAudioRingBuffer(size_t capacityBytes)
        : m_capacity(capacityBytes),
          m_head(0),
          m_tail(0) {
        // Allocate buffer (page-aligned if possible)
        m_buffer = static_cast<uint8_t*>(malloc(capacityBytes));
        if (!m_buffer) {
            m_capacity = 0;
        }
    }

    ~LockFreeAudioRingBuffer() {
        if (m_buffer) {
            free(m_buffer);
            m_buffer = nullptr;
        }
    }

    // Number of bytes currently available to read
    size_t availableToRead() const {
        size_t head = m_head.load(std::memory_order_acquire);
        size_t tail = m_tail.load(std::memory_order_relaxed);
        if (head >= tail) {
            return head - tail;
        }
        return m_capacity - (tail - head);
    }

    // Number of bytes currently available to write
    size_t availableToWrite() const {
        return (m_capacity - 1) - availableToRead();
    }

    // Write PCM data into ring buffer (Called by Audio Decoder Thread)
    size_t write(const uint8_t* data, size_t bytesToWrite) {
        if (!m_buffer || bytesToWrite == 0) return 0;

        size_t head = m_head.load(std::memory_order_relaxed);
        size_t tail = m_tail.load(std::memory_order_acquire);

        size_t freeBytes = (head >= tail) ? (m_capacity - 1 - (head - tail)) : (tail - head - 1);
        size_t toWrite = std::min(bytesToWrite, freeBytes);

        if (toWrite == 0) return 0;

        size_t firstChunk = std::min(toWrite, m_capacity - head);
        memcpy(m_buffer + head, data, firstChunk);

        size_t secondChunk = toWrite - firstChunk;
        if (secondChunk > 0) {
            memcpy(m_buffer, data + firstChunk, secondChunk);
        }

        size_t newHead = (head + toWrite) % m_capacity;
        m_head.store(newHead, std::memory_order_release);

        return toWrite;
    }

    // Read PCM data from ring buffer into USB URB packet (Called by USB Streamer Thread)
    // If underflow occurs, fills remainder with silence (0x00)
    size_t readWithSilencePadding(uint8_t* destination, size_t bytesRequested) {
        if (!m_buffer || bytesRequested == 0) return 0;

        size_t head = m_head.load(std::memory_order_acquire);
        size_t tail = m_tail.load(std::memory_order_relaxed);

        size_t available = (head >= tail) ? (head - tail) : (m_capacity - (tail - head));
        size_t toRead = std::min(bytesRequested, available);

        if (toRead > 0) {
            size_t firstChunk = std::min(toRead, m_capacity - tail);
            memcpy(destination, m_buffer + tail, firstChunk);

            size_t secondChunk = toRead - firstChunk;
            if (secondChunk > 0) {
                memcpy(destination + firstChunk, m_buffer, secondChunk);
            }

            size_t newTail = (tail + toRead) % m_capacity;
            m_tail.store(newTail, std::memory_order_release);
        }

        // Pad remainder with silence to avoid click/pop artifacts during momentary underrun
        if (toRead < bytesRequested) {
            memset(destination + toRead, 0, bytesRequested - toRead);
        }

        return toRead;
    }

    void reset() {
        m_head.store(0, std::memory_order_relaxed);
        m_tail.store(0, std::memory_order_relaxed);
    }

private:
    size_t m_capacity;
    uint8_t* m_buffer;
    alignas(64) std::atomic<size_t> m_head;
    alignas(64) std::atomic<size_t> m_tail;
};
