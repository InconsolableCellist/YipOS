#include "AudioCapture.hpp"
#include <algorithm>
#include <cstring>

namespace YipOS {

// --- AudioRingBuffer ---

AudioRingBuffer::AudioRingBuffer(size_t capacity)
    : buffer_(capacity, 0.0f) {}

void AudioRingBuffer::Write(const float* data, size_t count) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (size_t i = 0; i < count; i++) {
        buffer_[write_pos_] = data[i];
        write_pos_ = (write_pos_ + 1) % buffer_.size();
    }
    available_ = std::min(available_ + count, buffer_.size());
}

size_t AudioRingBuffer::Read(float* out, size_t max_count) {
    std::lock_guard<std::mutex> lock(mutex_);
    size_t to_read = std::min(max_count, available_);
    for (size_t i = 0; i < to_read; i++) {
        out[i] = buffer_[read_pos_];
        read_pos_ = (read_pos_ + 1) % buffer_.size();
    }
    available_ -= to_read;
    return to_read;
}

size_t AudioRingBuffer::Available() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return available_;
}

void AudioRingBuffer::Clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    write_pos_ = 0;
    read_pos_ = 0;
    available_ = 0;
}

} // namespace YipOS
