#pragma once
// claudius/include/readline_wrapper.h — Line editing abstraction (readline / libedit)

#include <string>
#include <vector>
#include <functional>

namespace claudius {

// Completion provider: given current buffer and token being completed,
// return a list of candidate strings.
using CompletionProvider =
    std::function<std::vector<std::string>(
        const std::string& buffer,
        const std::string& token)>;

class ReadlineWrapper {
public:
    ReadlineWrapper();
    ~ReadlineWrapper();

    // Read one line with the given prompt.
    // prompt may contain ANSI escapes wrapped in \001/\002 markers for readline.
    // Returns false on EOF (Ctrl-D).
    bool read_line(const std::string& prompt, std::string& out);

    // Register a tab-completion provider.
    void set_completion_provider(CompletionProvider provider);

    // Load history from file (silently ignored if file doesn't exist).
    // Also stores the path for auto-save on destruction.
    void load_history(const std::string& path);

    // Save history to file.
    void save_history(const std::string& path);

    // Max history entries kept in memory and file.
    void set_max_history(int n);

private:
    std::string history_path_;
    int         max_history_ = 1000;
};

} // namespace claudius
