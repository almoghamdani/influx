#include <kernel/tty/tty.h>

#include <kernel/algorithm.h>
#include <kernel/console/console.h>
#include <kernel/kernel.h>
#include <kernel/memory/utils.h>
#include <kernel/threading/lock_guard.h>
#include <kernel/threading/unique_lock.h>

influx::tty::tty::tty(bool active) : _active(active), _canonical(true) {}

influx::tty::tty &influx::tty::tty::operator=(const influx::tty::tty &other) {
    _canonical = other._canonical;
    _stdin_buffer = other._stdin_buffer;
    _stdout_buffer = other._stdout_buffer;

    return *this;
}

void influx::tty::tty::activate() {
    // Try to lock both stdin mutex and stdout mutex
    if (kernel::scheduler() != nullptr && kernel::scheduler()->started()) {
        while (true) {
            if (_stdin_mutex.try_lock()) {
                if (_stdout_mutex.try_lock()) {
                    break;
                } else {
                    _stdin_mutex.unlock();
                }
            }
        }
    }

    // Set the tty as active
    _active = true;

    // If there is a console
    if (console::get_console() != nullptr) {
        // Clear the current console
        console::get_console()->clear();

        // Write the stdout history
        for (const auto &str : _stdout_buffer) {
            console::get_console()->print(str);
        }
    }

    // Unlock mutexes
    if (kernel::scheduler() != nullptr && kernel::scheduler()->started()) {
        _stdin_mutex.unlock();
        _stdout_mutex.unlock();
    }
}

void influx::tty::tty::deactivate() {
    // Try to lock both stdin mutex and stdout mutex
    while (true) {
        if (_stdin_mutex.try_lock()) {
            if (_stdout_mutex.try_lock()) {
                break;
            } else {
                _stdin_mutex.unlock();
            }
        }
    }

    // Set the tty as not active
    _active = false;

    // Unlock mutexes
    _stdin_mutex.unlock();
    _stdout_mutex.unlock();
}

uint64_t influx::tty::tty::stdin_read(char *buf, size_t count) {
    threading::unique_lock lk(_stdin_mutex);

    uint64_t amount = 0;
    char *newline_char = nullptr;

    // If line buffered
    if (_canonical) {
        // Search for newline character
        newline_char = algorithm::find(_stdin_buffer.begin(), _stdin_buffer.end(), '\n');

        // If the new line wasn't found
        if (newline_char == _stdin_buffer.end()) {
            _stdin_cv.wait(lk);
        }

        // Try to find again the newline character
        newline_char = algorithm::find(_stdin_buffer.begin(), _stdin_buffer.end(), '\n');
        if (newline_char == _stdin_buffer.end()) {
            return 0;
        }

        // Copy the string
        amount = algorithm::min<uint64_t>(count, newline_char - _stdin_buffer.begin() + 1);
        memory::utils::memcpy(buf, _stdin_buffer.begin(), amount);

        // Resize the vector
        _stdin_buffer = structures::vector<char>(
            _stdin_buffer.begin() + amount, _stdin_buffer.end());

        // If there is more in the buffer, notify another
        if (!_stdin_buffer.empty()) {
            _stdin_cv.notify_one();
        }
    } else {
        // If the buffer is empty, wait for new input
        if (_stdin_buffer.empty()) {
            // Wait for new input
            _stdin_cv.wait(lk);
        }

        // Copy the string
        amount = algorithm::min<uint64_t>(count, _stdin_buffer.size());
        memory::utils::memcpy(buf, _stdin_buffer.begin(), amount);

        // Resize the vector
        _stdin_buffer =
            structures::vector<char>(_stdin_buffer.begin() + amount, _stdin_buffer.end());

        // If there is more in the buffer, notify another
        if (!_stdin_buffer.empty()) {
            _stdin_cv.notify_one();
        }
    }

    return amount;
}

void influx::tty::tty::stdout_write(influx::structures::string &str) {
    threading::lock_guard lk(_stdout_mutex);

    // Save the string in the buffer
    _stdout_buffer.push_back(str);

    // Print the string
    if (_active) {
        console::get_console()->print(str);
    }
}

void influx::tty::tty::input_thread() {
    char key;
    static bool ctrl = false, alt = false, left_shift = false, right_shift = false;

    while (true) {
        threading::unique_lock lk(_raw_input_mutex);

        // For each key event
        for (const auto &key_evt : _raw_input_buffer) {
            // Handle CTRL, ALT and SHIFT (PRESS/RELEASE)
            if (key_evt.code == key_code::CTRL) {
                ctrl = !key_evt.released;
            } else if (key_evt.code == key_code::ALT) {
                alt = !key_evt.released;
            } else if (key_evt.code == key_code::RIGHT_SHIFT) {
                right_shift = !key_evt.released;
            } else if (key_evt.code == key_code::LEFT_SHIFT) {
                left_shift = !key_evt.released;
            } else if (key_evt.released && ctrl && alt && key_evt.code >= key_code::F1 && key_evt.code <= key_code::F12) {
                // Change active TTY
                kernel::tty_manager()->set_active_tty((uint64_t)key_evt.code - (uint64_t)key_code::F1 + 1);
            }
            else if (!key_evt.released) {  // Key pressed
                key = (right_shift || left_shift) ? shifted_qwertz[key_evt.raw_key]
                                                  : qwertz[key_evt.raw_key];

                // Insert to stdin buffer
                {
                    threading::lock_guard stdin_lk(_stdin_mutex);

                    if (key && (!_canonical || key != '\b')) {
                        _stdin_buffer.push_back(key);
                    } else if (_canonical && key == '\b' && !_stdin_buffer.empty()) {
                        _stdin_buffer.pop_back();
                    }
                }

                // If the key isn't invalid, print it
                if (key && key != '\b') {
                    structures::string str(&key, 1);
                    stdout_write(str);
                }

                // If the terminal is canonical and the key was a new line
                if (_canonical && key_evt.code == key_code::ENTER) {
                    _stdin_cv.notify_one();
                } else if (!_canonical && key) {
                    _stdin_cv.notify_one();
                }
            }
        }

        // Clear input buffer
        _raw_input_buffer.clear();

        // Wait for new input
        _raw_input_cv.wait(lk);
    }
}