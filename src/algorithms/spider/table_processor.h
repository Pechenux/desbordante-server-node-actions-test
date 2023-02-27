#pragma once

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <malloc.h>
#include <optional>
#include <queue>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <sys/mman.h>
#include <thread>
#include <unistd.h>
#include <vector>

#include <boost/range/iterator_range.hpp>
#include <boost/tokenizer.hpp>

#if defined(__GLIBC__) && defined(__GLIBC_MINOR__)
#define GLIBC_VERSION (__GLIBC__ * 1000 + __GLIBC_MINOR__)
#else
#define GLIBC_VERSION 0
#endif

static std::size_t PAGE_SIZE = sysconf(_SC_PAGE_SIZE);

namespace fs = std::filesystem;

using SetCharPtr = std::set<char*, std::function<bool(char*, char*)>>;
using SetStringView = std::set<std::string_view>;
using SetUInt = std::set<unsigned int, std::function<bool(unsigned int, unsigned int)>>;
using PairOffset = std::pair<unsigned int, unsigned int>;
using SetPair = std::set<PairOffset, std::function<bool(PairOffset, PairOffset)>>;
using VectorCharPtr = std::vector<char*>;
using VectorStringView = std::vector<std::string_view>;
using VectorUInt = std::vector<unsigned int>;
using VectorPair = std::vector<PairOffset>;

class BaseTableProcessor {
public:
    virtual void Execute() = 0;
    virtual std::size_t GetHeaderSize() const = 0;
    virtual std::vector<std::string> const& GetHeader() const = 0;
    virtual std::vector<std::string> GetMaxValues() const = 0;
    virtual ~BaseTableProcessor() = default;
};

template <typename T>
class TableProcessor : public BaseTableProcessor {
private:
    std::vector<std::string> header_;
    std::size_t header_size_ = 0;
    using ColVec = std::vector<T>;
    int fd_;
    uintmax_t file_size_;
    char separator;
    bool has_header;

    char* cur_chunk_data_begin_;
    char* cur_chunk_data_end_;
    std::size_t swap_count = 0;

    // char */uint-specific
    std::string delimiters;

    // SET-specific
    std::size_t memory_limit_check_frequency;

    // VECTOR-specific
    std::size_t threads_count_;
    std::size_t file_count_;
    std::size_t rows_limit_ = 0;

    std::size_t data_m_limit_;
    std::size_t m_limit_;
    std::size_t chunks_n_;
    std::size_t current_chunk_ = 0;
    std::size_t chunk_size_;

    std::size_t attribute_offset;

    std::vector<std::string> max_values;
    ColVec columns_;
    char escape_symbol_ = '\\';
    char quote_ = '\"';

public:
    TableProcessor(fs::path const& file_path, char separator, bool has_header,
                   std::size_t memory_limit, std::size_t mem_check_frequency,
                   std::size_t threads_count, std::size_t attribute_offset)
        : fd_{open(file_path.c_str(), O_RDONLY)},
          file_size_{fs::file_size(file_path)},
          separator(separator),
          has_header(has_header),
          delimiters(std::string{"\n"} + separator),
          memory_limit_check_frequency(mem_check_frequency),
          threads_count_(threads_count),
          m_limit_(memory_limit),
          attribute_offset(attribute_offset) {
        if (fd_ == -1) {
            throw std::runtime_error("Failed to open file.");
        }
        std::tie(chunks_n_, chunk_size_) = GetChunksInfo(file_size_, memory_limit);
    }

    void Execute() override {
        data_m_limit_ = m_limit_ - chunk_size_;

        std::cout << "ChunkSize: " << (chunk_size_) <<'\n';

        for (; current_chunk_ != chunks_n_; ++current_chunk_) {
            if (chunks_n_ != 1) {
                std::cout << "Chunk: " << current_chunk_ << '\n';
            }

            auto offset = current_chunk_ * chunk_size_;
            auto length = chunk_size_ + PAGE_SIZE;
            if (current_chunk_ == chunks_n_ - 1) {
                length = file_size_ - offset;
            }
            auto data = mmap(nullptr, length, PROT_READ, MAP_PRIVATE, fd_, (off_t)offset);
            if (data == MAP_FAILED) {
                close(fd_);
                throw std::runtime_error("Failed to mmap file.");
            }
            SetChunkBorders(data, length);

            if (current_chunk_ == 0) {
                InitHeader();
                max_values = std::vector<std::string>(header_size_,
                                                      std::numeric_limits<std::string>::min());
                ReserveColumns();
            }
            CountRowsAndReserve();
            ProcessColumns();
            munmap(data, length);
        }
        if (chunks_n_ != 1) {
            std::cout << "Merge chunks\n";
            auto merge_time = std::chrono::system_clock::now();

            MergeChunks();

            auto inserting_time = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::system_clock::now() - merge_time);
            std::cout << "MergeChunks: " << inserting_time.count() << std::endl;
        }
    }

    void To(std::size_t value, std::string const& to = "mb") {
        if (to == "mb") {
            std::cout << (value >> 20);
        } else if (to == "kb") {
            std::cout << (value >> 10);
        } else if (to == "gb") {
            std::cout << (value >> 30);
        } else {
            std::cout << "unexpected ";
        }
        std::cout << " " << to << std::endl;
    }

    void SetChunkBorders(void* data, std::size_t length) {
        cur_chunk_data_begin_ = FindNearestLineStart(static_cast<char*>(data));
        cur_chunk_data_end_ = FindChunkEnd(static_cast<char*>(data) + length);
    }

    void CountRowsAndReserve() {
        if constexpr (std::is_same_v<VectorCharPtr, T> || std::is_same_v<T, VectorStringView> ||
                      std::is_same_v<VectorUInt, T> || std::is_same_v<VectorPair, T>) {
            auto count_time = std::chrono::system_clock::now();
            file_count_ = std::count(cur_chunk_data_begin_, cur_chunk_data_end_ + 1, '\n');

            auto counting_time = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::system_clock::now() - count_time);

            std::cout << "Counting time: " << counting_time.count() << std::endl;
            std::size_t row_memory = header_size_ * sizeof(typename T::value_type);
            std::size_t needed_memory = file_count_ * row_memory;

            if (needed_memory > data_m_limit_) {
                std::size_t times = std::ceil((double)needed_memory / (double)data_m_limit_);
                rows_limit_ = std::ceil((double)file_count_ / (double)times);
                std::cout << (needed_memory >> 20) << " > " << (data_m_limit_ >> 20) << " ["
                          << times << "]\n";
            } else {
                rows_limit_ = file_count_;
            }

            for (auto& col : columns_) {
                col.reserve(rows_limit_);
            }
        }
    }

    char* FindNearestLineStart(char* data) {
        if (current_chunk_ == 0) {
            return data;
        }
        while (*(data++) != '\n') {
            ;
        }
        return data;
    }

    char* FindChunkEnd(char* data) {
        if (current_chunk_ == chunks_n_ - 1) {
            return data;
        }
        data--;
        while (*(data) != '\n') {
            --data;
        }
        return data;
    }

    static std::pair<std::size_t, std::size_t> GetChunksInfo(std::size_t const& file_size,
                                                             std::size_t const& m_limit) {
        double available_memory;
        if constexpr (std::is_same_v<T, VectorStringView> || std::is_same_v<T, VectorCharPtr> ||
                      std::is_same_v<T, VectorUInt> || std::is_same_v<VectorPair, T>) {
            available_memory = (double)m_limit / 2;
        } else {
            available_memory = (double)m_limit / 3 * 2;
        }
        if constexpr (std::is_same_v<T, SetPair> || std::is_same_v<T, VectorPair>) {
            available_memory = std::min((double)4290000000, available_memory);
        }
        std::size_t n_chunks = std::ceil((double)file_size / available_memory);
        auto chunk_size = (file_size / n_chunks) & ~(PAGE_SIZE - 1);
        if (n_chunks != 1) {
            std::cout << "SPLIT DATASET IN " << n_chunks << " CHUNKS" << std::endl;
        }
        return {n_chunks, chunk_size};
    }

    void ReserveColumns() {
        if constexpr (std::is_same_v<T, VectorStringView> || std::is_same_v<T, SetStringView> ||
                      std::is_same_v<T, VectorCharPtr> || std::is_same_v<T, VectorUInt> ||
                      std::is_same_v<VectorPair, T>) {
            columns_.assign(header_size_, {});
        } else if constexpr (std::is_same_v<T, SetCharPtr>) {
            auto cmp = [delimiters = delimiters](char* str1, char* str2) {
                std::string_view s1(str1, strcspn(str1, delimiters.c_str()));
                std::string_view s2(str2, strcspn(str2, delimiters.c_str()));
                return s1 < s2;
            };
            columns_ = std::vector<SetCharPtr>(header_size_, SetCharPtr{cmp});
        } else if constexpr (std::is_same_v<T, SetUInt>) {
            auto cmp = [&begin = cur_chunk_data_begin_, delimiters = delimiters](
                               unsigned int str1_off, unsigned int str2_off) {
                auto str1_ptr = begin + str1_off;
                auto str2_ptr = begin + str2_off;
                std::string_view s1(str1_ptr, strcspn(str1_ptr, delimiters.c_str()));
                std::string_view s2(str2_ptr, strcspn(str2_ptr, delimiters.c_str()));
                return s1 < s2;
            };
            columns_ = std::vector<T>(header_size_, T{cmp});
        } else if constexpr (std::is_same_v<T, SetPair>) {
            auto cmp = [&begin=cur_chunk_data_begin_](PairOffset str1_off, PairOffset str2_off) {
                std::string_view view1(begin + str1_off.first, str1_off.second);
                std::string_view view2(begin + str2_off.first, str2_off.second);
                return view1 < view2;
            };
            columns_ = std::vector<T>(header_size_, T{cmp});
        } else {
            throw std::runtime_error("err");
        }
    }

    void InitHeader() {
        char const* pos = cur_chunk_data_begin_;
        while (*pos != '\0' && *pos != '\n') {
            char const* next_pos = pos;
            while (*next_pos != '\0' && *next_pos != separator && *next_pos != '\n') {
                next_pos++;
            }
            if (has_header) {
                header_.emplace_back(pos, next_pos - pos);
            } else {
                header_.emplace_back(std::to_string(header_size_));
            }
            header_size_++;
            pos = next_pos + (*next_pos == separator);
        }
        if (has_header) {
            cur_chunk_data_begin_ = (char*)pos;
        }
    }

    static fs::path GenerateDirectory(std::size_t swap_count, std::size_t chunk_count,
                                      std::size_t chunks_n) {
        fs::path dir = fs::current_path();
        if (chunks_n != 1) {
            dir /= "temp" + std::to_string(chunk_count);
        } else {
            dir /= "temp";
        }
        if (swap_count != 0) {
            if (!fs::exists(dir) && fs::create_directory(dir)) {
                //                std::cout << "Directory created: " << dir << std::endl;
            }
            dir /= "swap" + std::to_string(swap_count);
        }
        if (!fs::exists(dir) && fs::create_directory(dir)) {
            //            std::cout << "Directory created: " << dir << std::endl;
        }
        return dir;
    }

    static fs::path GeneratePath(std::size_t index, std::size_t swap_count = 0,
                                 std::size_t chunk_count = 0, std::size_t chunks_n = 1) {
        return GenerateDirectory(swap_count, chunk_count, chunks_n) / std::to_string(index);
    }

    void PrintCurrentSizeInfo(std::string const& context = "") {
        if (chunks_n_ != 1) {
            return;
        }
        if (!context.empty()) {
            std::cout << context << std::endl;
        }
        if (columns_.size() != header_size_) {
            throw std::logic_error("column size is not equal to header_size");
        }
        for (std::size_t i = 0; i != header_size_; ++i) {
            std::cout << header_[i] << "-" << columns_[i].size() << " | ";
        }
        std::cout << std::endl;
    }

    void WriteAllColumns(bool is_final) {
        Sort();

        auto write_time = std::chrono::system_clock::now();

        fs::path path;
        if (!is_final || swap_count != 0) {
            swap_count++;
        }
        for (std::size_t i = 0; i != columns_.size(); ++i) {
            WriteColumn(i,
                        GeneratePath(i + attribute_offset, swap_count, current_chunk_, chunks_n_),
                        is_final);
        }
        std::cout << "Memory: " << (GetCurrentMemory() >> 20) << std::endl;

        for (auto& values : columns_) {
            values.clear();
        }
        if (is_final && swap_count != 0) {
            std::cout << "Merge files\n";
            for (std::size_t i = 0; i != header_size_; ++i) {
                MergeAndRemoveDuplicates(i);
            }
            for (std::size_t i = 0; i != header_size_; ++i) {
                if (fs::remove_all(GenerateDirectory(i + 1, current_chunk_, chunks_n_)) <= 0) {
                    std::cout << "Error deleting directory: "
                              << GenerateDirectory(0, current_chunk_, chunks_n_) << std::endl;
                }
            }
        }
        auto writing_time = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now() - write_time);
        std::cout << "Writing: " << writing_time.count() << std::endl;
    }

    void MergeAndRemoveDuplicates(std::vector<std::ifstream> inputFiles, std::ofstream outputFile,
                                  std::size_t attr_id) {
        using ColumnElement = std::pair<std::string, std::size_t>;
        auto cmp = [](ColumnElement const& lhs, ColumnElement const& rhs) {
            return lhs.first > rhs.first;
        };
        std::priority_queue<ColumnElement, std::vector<ColumnElement>, decltype(cmp)> queue{cmp};
        for (std::size_t i = 0; i < inputFiles.size(); ++i) {
            std::string value;
            if (std::getline(inputFiles[i], value)) {
                queue.push({value, i});
            }
        }

        // Merge the files and remove duplicates
        std::string prev;
        while (!queue.empty()) {
            auto [value, fileIndex] = queue.top();
            queue.pop();

            if (value != prev) {
                outputFile << value << std::endl;
                prev = value;
            }

            if (std::getline(inputFiles[fileIndex], value)) {
                queue.push({value, fileIndex});
            }
        }
        max_values[attr_id] = prev;
    }

    void MergeAndRemoveDuplicates(std::size_t attr_id) {
        // Open all the input files
        std::vector<std::ifstream> inputFiles;
        for (std::size_t i = 0; i < swap_count; ++i) {
            inputFiles.emplace_back(
                    GeneratePath(attr_id + attribute_offset, i + 1, current_chunk_, chunks_n_));
        }
        // Open the output file
        std::ofstream outputFile(
                GeneratePath(attr_id + attribute_offset, 0, current_chunk_, chunks_n_));
        MergeAndRemoveDuplicates(std::move(inputFiles), std::move(outputFile), attr_id);
    }

    void MergeChunks() {
        for (std::size_t i = 0; i != header_size_; ++i) {
            MergeChunk(i);
        }
        for (std::size_t i = 0; i != chunks_n_; ++i) {
            if (fs::remove_all(GenerateDirectory(0, i, chunks_n_)) <= 0) {
                std::cout << "Error deleting directory: " << GenerateDirectory(0, i, chunks_n_)
                          << std::endl;
            }
        }
    }

    void MergeChunk(std::size_t attr_id) {
        std::vector<std::ifstream> inputFiles;
        for (std::size_t i = 0; i < chunks_n_; ++i) {
            inputFiles.emplace_back(GeneratePath(attr_id + attribute_offset, 0, i, chunks_n_));
        }
        std::ofstream outputFile(GeneratePath(attr_id + attribute_offset));
        MergeAndRemoveDuplicates(std::move(inputFiles), std::move(outputFile), attr_id);
    }

    void WriteColumn(std::size_t i, std::filesystem::path const& path, bool is_final) {
        std::ofstream out{path};
        if (!out.is_open()) {
            throw std::runtime_error("cannot open");
        }
        auto& values = columns_[i];
        for (auto const& value : values) {
            if constexpr (std::is_same_v<SetStringView, T> || std::is_same_v<VectorStringView, T>) {
                out << value << std::endl;
            } else if constexpr (std::is_same_v<VectorUInt, T> || std::is_same_v<SetUInt, T>) {
                auto value_ptr = cur_chunk_data_begin_ + value;
                out << std::string_view{value_ptr, strcspn(value_ptr, delimiters.c_str())}
                    << std::endl;
            } else if constexpr (std::is_same_v<VectorPair, T> || std::is_same_v<SetPair, T>) {
                auto& [begin, size] = value;
                auto val_ptr = cur_chunk_data_begin_ + begin;
                out << std::string_view{val_ptr, size} << std::endl;
            } else {
                out << std::string_view{value, strcspn(value, delimiters.c_str())} << std::endl;
            }
        }

        if (is_final) {
            auto getValue = [this, i]() {
                if constexpr (std::is_same_v<SetStringView, T> || std::is_same_v<SetUInt, T> ||
                              std::is_same_v<SetCharPtr, T> || std::is_same_v<SetPair, T>) {
                    return *(--columns_[i].end());
                } else {
                    return columns_[i].back();
                }
            };

            if (columns_[i].empty()) {
                max_values[i] = "";
            } else if constexpr (std::is_same_v<SetStringView, T> || std::is_same_v<VectorStringView, T>) {
                max_values[i] = std::string{getValue()};
            } else if constexpr (std::is_same_v<VectorUInt, T> || std::is_same_v<SetUInt, T>) {
                auto value_ptr = cur_chunk_data_begin_ + getValue();
                max_values[i] = std::string{
                        std::string_view{value_ptr, strcspn(value_ptr, delimiters.c_str())}};
            } else if constexpr (std::is_same_v<SetPair, T> || std::is_same_v<VectorPair, T>) {
                auto [begin, size] = getValue();
                auto val_ptr = cur_chunk_data_begin_ + begin;
                max_values[i] = std::string{std::string_view{val_ptr, size}};
            } else {
                auto value = getValue();
                max_values[i] =
                        std::string{std::string_view{value, strcspn(value, delimiters.c_str())}};
            }
        }
    }


    void ProcessColumns() {
        auto insert_time = std::chrono::system_clock::now();

        std::size_t length = cur_chunk_data_end_ - cur_chunk_data_begin_;
        auto line_begin = cur_chunk_data_begin_;
        auto next_pos = line_begin;
        auto line_end = (char*)memchr(line_begin, '\n', length);
        using escaped_list_t = boost::escaped_list_separator<char>;
        escaped_list_t escaped_list(escape_symbol_, separator, quote_);
        std::size_t cur_index;

        std::size_t counter = 0;
        while (line_end != nullptr && line_end < cur_chunk_data_end_) {
            if constexpr (std::is_same_v<VectorStringView, T> || std::is_same_v<VectorCharPtr, T> ||
                          std::is_same_v<VectorUInt, T> || std::is_same_v<VectorPair, T>) {
                counter++;
                if (file_count_ != rows_limit_ && counter == rows_limit_) {
                    counter = 0;
                    std::cout << "SWAP " << swap_count << std::endl;
                    WriteAllColumns(false);
                    std::cout << "SWAPPED" << std::endl;
                }
            } else {
                if (++counter == memory_limit_check_frequency) {
                    counter = 0;
                    if (IsMemoryLimitReached()) {
                        WriteAllColumns(false);
                    }
                }
            }
            boost::tokenizer<escaped_list_t, char const*> tokens(line_begin, line_end,
                                                                 escaped_list);
            for (std::string const& value : tokens) {
                if (!value.empty()) {
                    bool is_quoted = (*next_pos == '\"' && *(next_pos + value.size() + 1) == '\"');
                    if (is_quoted) {
                        next_pos++;
                    }
                    if constexpr (std::is_same_v<T, VectorStringView>) {
                        columns_[cur_index].emplace_back(next_pos, value.size());
                    } else if constexpr (std::is_same_v<T, SetStringView>) {
                        columns_[cur_index].insert(std::string_view(next_pos, value.size()));
                    } else if constexpr (std::is_same_v<T, SetCharPtr>) {
                        columns_[cur_index].insert(next_pos);
                    } else if constexpr (std::is_same_v<T, SetUInt>) {
                        columns_[cur_index].insert(next_pos - cur_chunk_data_begin_);
                    } else if constexpr (std::is_same_v<T, VectorCharPtr>) {
                        columns_[cur_index].emplace_back(next_pos);
                    } else if constexpr (std::is_same_v<T, VectorUInt>) {
                        columns_[cur_index].emplace_back(next_pos - cur_chunk_data_begin_);
                    } else if constexpr (std::is_same_v<T, VectorPair>) {
                        columns_[cur_index].emplace_back(next_pos - cur_chunk_data_begin_,
                                                         value.size());
                    } else if constexpr (std::is_same_v<T, SetPair>) {
                        columns_[cur_index].emplace(next_pos - cur_chunk_data_begin_, value.size());
                    } else {
                        throw std::runtime_error("wht3");
                    }
                    if (is_quoted) {
                        next_pos++;
                    }
                    next_pos += value.size();
                }
                next_pos++;
                cur_index++;
            }
            cur_index = 0;

            line_begin = line_end + 1;
            next_pos = line_begin;
            length = cur_chunk_data_end_ - line_begin;
            line_end = (char*)memchr(line_begin, '\n', length);
        }
        auto inserting_time = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now() - insert_time);
        std::cout << "Inserting: " << inserting_time.count() << std::endl;
        WriteAllColumns(true);
    }
    void Sort() {
        if constexpr (std::is_same_v<T, VectorStringView> || std::is_same_v<T, VectorCharPtr> ||
                      std::is_same_v<T, VectorUInt> || std::is_same_v<T, VectorPair>) {
            auto sort_time = std::chrono::system_clock::now();

            std::vector<std::thread> threads;
            for (std::size_t j = 0; j < header_size_; ++j) {
                threads.emplace_back(std::thread{[this, j]() {
                    if constexpr (std::is_same_v<T, VectorStringView>) {
                        std::sort(columns_[j].begin(), columns_[j].end());
                        columns_[j].erase(unique(columns_[j].begin(), columns_[j].end()),
                                          columns_[j].end());
                    } else if constexpr (std::is_same_v<T, VectorCharPtr>) {
                        auto compare_char_ptr = [delimiters = delimiters](char* str1, char* str2) {
                            std::string_view s1(str1, strcspn(str1, delimiters.c_str()));
                            std::string_view s2(str2, strcspn(str2, delimiters.c_str()));
                            return s1 < s2;
                        };

                        auto is_equal_char_ptr = [delimiters = delimiters](char* str1, char* str2) {
                            std::string_view s1(str1, strcspn(str1, delimiters.c_str()));
                            std::string_view s2(str2, strcspn(str2, delimiters.c_str()));
                            return s1 == s2;
                        };
                        std::sort(columns_[j].begin(), columns_[j].end(), compare_char_ptr);
                        columns_[j].erase(
                                unique(columns_[j].begin(), columns_[j].end(), is_equal_char_ptr),
                                columns_[j].end());
                    } else if constexpr (std::is_same_v<T, VectorUInt>) {
                        std::sort(columns_[j].begin(), columns_[j].end(),
                                  [begin = cur_chunk_data_begin_, delimiters = delimiters](
                                          unsigned int str1_off, unsigned int str2_off) {
                                      auto str1_ptr = begin + str1_off;
                                      auto str2_ptr = begin + str2_off;
                                      std::string_view s1(str1_ptr,
                                                          strcspn(str1_ptr, delimiters.c_str()));
                                      std::string_view s2(str2_ptr,
                                                          strcspn(str2_ptr, delimiters.c_str()));
                                      return s1 < s2;
                                  });
                        columns_[j].erase(
                                unique(columns_[j].begin(), columns_[j].end(),
                                       [begin = cur_chunk_data_begin_, delimiters = delimiters](
                                               unsigned int str1_off, unsigned int str2_off) {
                                           auto str1_ptr = begin + str1_off;
                                           auto str2_ptr = begin + str2_off;
                                           std::string_view s1(
                                                   str1_ptr, strcspn(str1_ptr, delimiters.c_str()));
                                           std::string_view s2(
                                                   str2_ptr, strcspn(str2_ptr, delimiters.c_str()));
                                           return s1 == s2;
                                       }),
                                columns_[j].end());
                    } else if (std::is_same_v<T, VectorPair>) {
                        std::sort(
                                columns_[j].begin(), columns_[j].end(),
                                [begin = cur_chunk_data_begin_](PairOffset str1_off,
                                                                PairOffset str2_off) {
                                    std::string_view view1(begin + str1_off.first, str1_off.second);
                                    std::string_view view2(begin + str2_off.first, str2_off.second);
                                    return view1 < view2;
                                });
                        columns_[j].erase(unique(columns_[j].begin(), columns_[j].end(),
                                                 [begin = cur_chunk_data_begin_](
                                                         PairOffset str1_off, PairOffset str2_off) {
                                                     std::string_view view1(begin + str1_off.first,
                                                                            str1_off.second);
                                                     std::string_view view2(begin + str2_off.first,
                                                                            str2_off.second);
                                                     return view1 == view2;
                                                 }),
                                          columns_[j].end());
                    } else {
                        throw std::runtime_error("hz");
                    }
                }});
                if ((j != 0 && j % threads_count_ == 0) || j == header_size_ - 1) {
                    for (auto& th : threads) {
                        th.join();
                    }
                    threads.clear();
                }
            }
            auto sorting_time = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::system_clock::now() - sort_time);
            std::cout << "Sorting: " << sorting_time.count() << std::endl;
        }
    }

    auto GetCurrentMemory() const {
        if constexpr (std::is_same_v<SetCharPtr, T> || std::is_same_v<SetStringView, T> ||
                      std::is_same_v<SetUInt, T> || std::is_same_v<SetPair, T>) {
#if GLIBC_VERSION >= 2033
            return mallinfo2().uordblks;
#else
            double magic_number = 200.0 / 167;
            std::size_t rough_estimation = sizeof(T);
            for (auto const& column : columns_) {
                rough_estimation +=
                        column.size() * sizeof(std::_Rb_tree_node<typename T::key_type>);
            }
            return (std::size_t)(magic_number * (double)rough_estimation);
#endif
        } else {
            std::size_t row_memory = header_size_ * sizeof(typename T::value_type);
            return row_memory * rows_limit_;
        }
    }

    bool IsMemoryLimitReached() {
        return GetCurrentMemory() > data_m_limit_;
    }

    ~TableProcessor() override {
        close(fd_);
    }

    std::size_t GetHeaderSize() const override {
        return header_size_;
    }
    std::vector<std::string> const& GetHeader() const override {
        return header_;
    }
    std::vector<std::string> GetMaxValues() const override {
        return max_values;
    }
};