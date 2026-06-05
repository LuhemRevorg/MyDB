#include "storage/disk_manager.h"
#include <cassert>
#include <cstring>
#include <stdexcept>

DiskManager::DiskManager(const std::string& db_file) : file_name_{db_file} {
    db_io_.open(file_name_, std::ios::binary | std::ios::in | std::ios::out);

    if (!db_io_.is_open()) {
        // File doesn't exist yet — create it, then reopen for read/write.
        db_io_.open(file_name_, std::ios::binary | std::ios::trunc | std::ios::out);
        db_io_.close();
        db_io_.open(file_name_, std::ios::binary | std::ios::in | std::ios::out);
        if (!db_io_.is_open())
            throw std::runtime_error("Cannot open database file: " + db_file);
    }

    db_io_.seekg(0, std::ios::end);
    num_pages_ = static_cast<int>(db_io_.tellg()) / PAGE_SIZE;
}

DiskManager::~DiskManager() {
    if (db_io_.is_open()) {
        db_io_.flush();
        db_io_.close();
    }
}

void DiskManager::ReadPage(page_id_t page_id, char* page_data) {
    std::scoped_lock lock(io_latch_);
    assert(page_id >= 0 && page_id < num_pages_);
    db_io_.seekg(static_cast<std::streamoff>(page_id) * PAGE_SIZE);
    if (!db_io_.read(page_data, PAGE_SIZE)) {
        auto bytes_read = db_io_.gcount();
        memset(page_data + bytes_read, 0, PAGE_SIZE - bytes_read);
        db_io_.clear();
    }
}

void DiskManager::WritePage(page_id_t page_id, const char* page_data) {
    std::scoped_lock lock(io_latch_);
    assert(page_id >= 0);
    db_io_.seekp(static_cast<std::streamoff>(page_id) * PAGE_SIZE);
    db_io_.write(page_data, PAGE_SIZE);
    if (db_io_.bad())
        throw std::runtime_error("I/O error writing page " + std::to_string(page_id));
    db_io_.flush();
}

page_id_t DiskManager::AllocatePage() {
    std::scoped_lock lock(io_latch_);
    const page_id_t pid = num_pages_++;
    db_io_.seekp(static_cast<std::streamoff>(pid) * PAGE_SIZE);
    char empty[PAGE_SIZE]{};
    db_io_.write(empty, PAGE_SIZE);
    db_io_.flush();
    return pid;
}
