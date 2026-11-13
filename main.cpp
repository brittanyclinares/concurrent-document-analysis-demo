#include <cctype>
#include <condition_variable>
#include <iostream>
#include <map>
#include <mutex>
#include <queue>
#include <sstream>
#include <string>
#include <thread>
#include <utility>
#include <vector>

class LineQueue {
public:
    void push(std::string line) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            lines_.push(std::move(line));
        }
        ready_.notify_one();
    }

    bool pop(std::string& line) {
        std::unique_lock<std::mutex> lock(mutex_);
        ready_.wait(lock, [this] { return closed_ || !lines_.empty(); });
        if (lines_.empty()) {
            return false;
        }
        line = std::move(lines_.front());
        lines_.pop();
        return true;
    }

    void close() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            closed_ = true;
        }
        ready_.notify_all();
    }

private:
    std::mutex mutex_;
    std::condition_variable ready_;
    std::queue<std::string> lines_;
    bool closed_ = false;
};

using Counts = std::map<std::string, std::size_t>;

void count_words(const std::string& line, Counts& counts) {
    std::string word;
    for (unsigned char character : line) {
        if (std::isalnum(character)) {
            word += static_cast<char>(std::tolower(character));
        } else if (!word.empty()) {
            ++counts[word];
            word.clear();
        }
    }
    if (!word.empty()) {
        ++counts[word];
    }
}

Counts analyze(std::istream& input, std::size_t worker_count) {
    LineQueue queue;
    std::vector<Counts> partial(worker_count);
    std::vector<std::thread> workers;
    workers.reserve(worker_count);
    try {
        for (auto& counts : partial) {
            workers.emplace_back([&queue, &counts] {
                std::string line;
                while (queue.pop(line)) {
                    count_words(line, counts);
                }
            });
        }
        std::string line;
        while (std::getline(input, line)) {
            queue.push(std::move(line));
        }
    } catch (...) {
        queue.close();
        for (auto& worker : workers) {
            worker.join();
        }
        throw;
    }
    queue.close();
    for (auto& worker : workers) {
        worker.join();
    }

    Counts result;
    for (const auto& counts : partial) {
        for (const auto& [word, count] : counts) {
            result[word] += count;
        }
    }
    return result;
}

int main(int argc, char* argv[]) {
    if (argc == 2 && std::string(argv[1]) == "--check") {
        const Counts expected{{"and", 1}, {"control", 1}, {"signal", 3}};
        std::istringstream serial("Signal, SIGNAL!\nSignal and control.\n");
        std::istringstream parallel(serial.str());
        std::istringstream empty;
        if (analyze(serial, 1) != expected || analyze(parallel, 4) != expected || !analyze(empty, 4).empty()) {
            std::cerr << "Word count check failed.\n";
            return 1;
        }
        std::cout << "Word count checks passed.\n";
        return 0;
    }
    if (argc != 1) {
        std::cerr << "Usage: analyzer [--check]\n";
        return 2;
    }
    for (const auto& [word, count] : analyze(std::cin, 4)) {
        std::cout << word << '\t' << count << '\n';
    }
}
